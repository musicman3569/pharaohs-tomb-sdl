#include "platform.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamepad.h"
#include "platform_sdl.h"
#include "render.h"
#include "settings.h"
#include "timer.h"

isr_keys_t isr_keys;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID audio;

static int shot_no;

/* The mouse (menu.navigation) */
static int mouse_x = -1, mouse_y = -1;
static bool menu_clicks, click_pending;
static int click_x, click_y;
static Uint32 mouse_moved_at; /* fullscreen hides the pointer when it isn't being used */
static bool quitting;
static int level_step;

/* BIOS keyboard buffer: what Crt ReadKey sees. Extended keys are stored as 0, scancode. */
static int keybuf[64];
static int key_head, key_tail;

static char status_text[200], message_text[100];
static bool title_dirty = true;

static Uint64 perf_freq, last_counter, last_present;
static double pending_ticks;

static SDL_atomic_t speaker_hz;

/* --- audio: PC speaker square wave --------------------------------------------------------- */

/* Short queued tones (plat_sound_blip). Written by the game thread under the audio lock. */
typedef struct {
    int hz;
    int samples; /* remaining samples of this entry */
} blip_t;
static blip_t blips[2048];
static int blip_head, blip_tail;
static int audio_rate = 44100;

/* --record-audio: keep every sample played, written as a WAV file on shutdown. */
static const char *record_path;
static Sint16 *record_buf;
static size_t record_len, record_cap;

static void record_samples(const Sint16 *s, int n)
{
    if (record_len + (size_t)n > record_cap) {
        size_t cap = record_cap ? record_cap * 2 : 1 << 20;
        while (cap < record_len + (size_t)n)
            cap *= 2;
        Sint16 *grown = SDL_realloc(record_buf, cap * sizeof *grown);
        if (!grown)
            return;
        record_buf = grown;
        record_cap = cap;
    }
    SDL_memcpy(record_buf + record_len, s, (size_t)n * sizeof *s);
    record_len += (size_t)n;
}

void plat_record_audio(const char *path)
{
    record_path = path;
}

static void write_recording(void)
{
    if (!record_path || !record_buf)
        return;
    FILE *f = fopen(record_path, "wb");
    if (!f)
        return;
    const Uint32 data = (Uint32)(record_len * 2), rate = (Uint32)audio_rate;
    const Uint8 hdr[44] = {
        'R', 'I', 'F', 'F', (Uint8)(data + 36), (Uint8)((data + 36) >> 8), (Uint8)((data + 36) >> 16),
        (Uint8)((data + 36) >> 24), 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ', 16, 0, 0, 0, 1, 0, 1, 0,
        (Uint8)rate, (Uint8)(rate >> 8), (Uint8)(rate >> 16), (Uint8)(rate >> 24),
        (Uint8)(rate * 2), (Uint8)((rate * 2) >> 8), (Uint8)((rate * 2) >> 16), (Uint8)((rate * 2) >> 24),
        2, 0, 16, 0, 'd', 'a', 't', 'a', (Uint8)data, (Uint8)(data >> 8), (Uint8)(data >> 16),
        (Uint8)(data >> 24),
    };
    fwrite(hdr, 1, sizeof hdr, f);
    fwrite(record_buf, 2, record_len, f); /* little-endian hosts only, fine for a test aid */
    fclose(f);
    printf("recorded %s (%.1f s)\n", record_path, (double)record_len / rate);
}

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    static double phase;
    (void)userdata;
    Sint16 *out = (Sint16 *)stream;
    const int n = len / (int)sizeof(Sint16);
    const int steady = SDL_AtomicGet(&speaker_hz);
    for (int i = 0; i < n; i++) {
        int hz = steady;
        if (blip_head != blip_tail) {
            blip_t *b = &blips[blip_head];
            hz = b->hz;
            if (--b->samples <= 0)
                blip_head = (blip_head + 1) % (int)(sizeof blips / sizeof blips[0]);
        }
        if (hz <= 0) {
            out[i] = 0;
            continue;
        }
        phase += (double)hz / audio_rate;
        if (phase >= 1.0)
            phase -= (int)phase;
        out[i] = phase < 0.5 ? 2500 : -2500;
    }
    if (record_path)
        record_samples(out, n);
}

static void queue_blip(int hz, int us)
{
    const int samples = (int)((long long)us * audio_rate / 1000000);
    if (samples <= 0)
        return;
    const int next = (blip_tail + 1) % (int)(sizeof blips / sizeof blips[0]);
    if (next == blip_head)
        return; /* queue full: drop */
    blips[blip_tail].hz = hz;
    blips[blip_tail].samples = samples;
    blip_tail = next;
}

void plat_sound_blip(int hz, int on_us, int off_us)
{
    if (!audio)
        return;
    SDL_LockAudioDevice(audio);
    queue_blip(hz > 18 ? hz : 0, on_us); /* Crt Sound ignores hz <= 18 */
    queue_blip(0, off_us);
    SDL_UnlockAudioDevice(audio);
}

void plat_sound(int hz)
{
    /* Crt Sound ignores frequencies the PIT divisor can't represent (hz <= 18). */
    if (hz > 18)
        SDL_AtomicSet(&speaker_hz, hz);
}

void plat_nosound(void)
{
    SDL_AtomicSet(&speaker_hz, 0);
}

/* --- display ------------------------------------------------------------------------------- */

static SDL_Rect fit_rect(int win_w, int win_h)
{
    const double ar = settings.aspect_43 ? 4.0 / 3.0 : 320.0 / 200.0;
    SDL_Rect r;
    r.w = win_w;
    r.h = (int)(win_w / ar + 0.5);
    if (r.h > win_h) {
        r.h = win_h;
        r.w = (int)(win_h * ar + 0.5);
    }
    r.x = (win_w - r.w) / 2;
    r.y = (win_h - r.h) / 2;
    return r;
}

/* display.smooth / display.sprites: pictures of moving things as textures, made on first use
 * and rebuilt when the colours change. */
static struct {
    const uint8_t *pic;
    SDL_Texture *tex;
} pic_cache[256];
static int pic_cache_n;
static unsigned pic_cache_style;

static SDL_Texture *pic_texture(const uint8_t *pic)
{
    if (pic_cache_style != render_style()) {
        for (int i = 0; i < pic_cache_n; i++)
            SDL_DestroyTexture(pic_cache[i].tex);
        pic_cache_n = 0;
        pic_cache_style = render_style();
    }
    for (int i = 0; i < pic_cache_n; i++)
        if (pic_cache[i].pic == pic)
            return pic_cache[i].tex;
    if (pic_cache_n == (int)(sizeof pic_cache / sizeof pic_cache[0]))
        return NULL;
    int w, h;
    const uint32_t *px = render_pic_argb(pic, &w, &h);
    SDL_Texture *t = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t)
        return NULL;
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    pic_cache[pic_cache_n].pic = pic;
    pic_cache[pic_cache_n++].tex = t;
    return t;
}

static double seconds_now(void)
{
    return (double)SDL_GetPerformanceCounter() / (double)perf_freq;
}

static bool display_layer(void)
{
    return settings.smooth || render_custom();
}

/* The display layer's background, at the custom art's scale (render.h). */
static SDL_Texture *bg_tex;
static int bg_tex_scale;

static void present(void)
{
    /* display.smooth times its frames itself (plat_pace draws between game passes), so a
     * present mustn't block until vsync. On Windows the compositor still keeps it tear-free in
     * a window and in (desktop) fullscreen. */
    static int vsync = 1;
    if (vsync != !settings.smooth) {
        vsync = !settings.smooth;
        SDL_RenderSetVSync(ren, vsync);
    }
    const render_out_t *f = NULL;
    SDL_Texture *shown = tex;
    if (display_layer()) {
        f = render_frame(seconds_now());
        if (f->bg && (!bg_tex || bg_tex_scale != f->scale)) {
            if (bg_tex)
                SDL_DestroyTexture(bg_tex);
            bg_tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                       CGA_W * f->scale, CGA_H * f->scale);
            bg_tex_scale = f->scale;
            if (bg_tex)
                SDL_UpdateTexture(bg_tex, NULL, f->bg, CGA_W * f->scale * 4);
        } else if (f->bg && f->bg_changed) {
            SDL_UpdateTexture(bg_tex, NULL, f->bg, CGA_W * f->scale * 4);
        }
        if (bg_tex)
            shown = bg_tex;
    } else {
        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
            cga_to_argb(pixels, pitch / 4, cga_palette(settings.palette, settings.bright));
            SDL_UnlockTexture(tex);
        }
    }
    int ow, oh;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    const SDL_Rect dst = fit_rect(ow, oh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, shown, NULL, &dst);
    const float sx = (float)dst.w / CGA_W, sy = (float)dst.h / CGA_H;
    for (int i = 0; f && shown == bg_tex && i < f->n_items; i++) { /* moving things, at fractional positions */
        const render_item_t *it = &f->items[i];
        const SDL_FRect r = {(float)dst.x + it->x * sx, (float)dst.y + it->y * sy, it->w * sx, it->h * sy};
        if (it->kind == ITEM_PIC) {
            SDL_Texture *t = pic_texture(it->pic);
            if (t)
                SDL_RenderCopyF(ren, t, NULL, &r);
        } else if (it->kind == ITEM_LINE) {
            const uint32_t c = render_line_argb(it->color);
            SDL_SetRenderDrawColor(ren, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, 255);
            SDL_RenderFillRectF(ren, &r);
        } else { /* ITEM_STRIP: one background row stretched (a pole) */
            const SDL_Rect src = {it->sx * f->scale, it->sy * f->scale + f->scale - 1, it->sw * f->scale, 1};
            SDL_RenderCopyF(ren, bg_tex, &src, &r);
        }
    }
    SDL_RenderPresent(ren);

    if (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)
        SDL_ShowCursor(SDL_GetTicks() - mouse_moved_at < 2000 ? SDL_ENABLE : SDL_DISABLE);
    else
        SDL_ShowCursor(SDL_ENABLE);

    if (title_dirty) {
        char title[400];
        snprintf(title, sizeof title, "Pharaoh's Tomb - %s%s%s", message_text[0] ? message_text : "",
                 message_text[0] ? " - " : "", status_text);
        SDL_SetWindowTitle(win, title);
        title_dirty = false;
    }
}

/* A screenshot of what the window shows: at twice 320x200, or the custom art's scale. Without
 * the display layer that's the framebuffer as before. */
static int save_shot(const char *path)
{
    if (!display_layer())
        return cga_write_png(path, cga_palette(settings.palette, settings.bright), 2);
    int w, h;
    uint32_t *px = render_compose(seconds_now(), &w, &h);
    const int rc = px ? png_write_argb(path, px, w, h) : -1;
    free(px);
    return rc;
}

void plat_status(const char *text)
{
    if (strcmp(status_text, text) != 0) {
        snprintf(status_text, sizeof status_text, "%s", text);
        title_dirty = true;
    }
}

void plat_message(const char *text)
{
    snprintf(message_text, sizeof message_text, "%s", text);
    title_dirty = true;
}

/* --- input --------------------------------------------------------------------------------- */

static void push_key(int c)
{
    const int next = (key_tail + 1) % (int)(sizeof keybuf / sizeof keybuf[0]);
    if (next != key_head) { /* a full BIOS buffer just beeps and drops the key */
        keybuf[key_tail] = c;
        key_tail = next;
    }
}

void plat_push_key(int c)
{
    push_key(c);
}

static void push_extended(int scancode)
{
    push_key(0);
    push_key(scancode);
}

static void toggle_fullscreen(void)
{
    settings.fullscreen = !settings.fullscreen; /* for this run; ptomb.ini isn't changed */
    SDL_SetWindowFullscreen(win, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

/* A window position to 320x200 screen pixels, through the same fit as present(). */
static void window_to_screen(int wx, int wy, int *x, int *y)
{
    int ww, wh, ow, oh;
    SDL_GetWindowSize(win, &ww, &wh);
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    const SDL_Rect r = fit_rect(ow, oh);
    const double px = (double)wx * ow / (ww ? ww : 1), py = (double)wy * oh / (wh ? wh : 1);
    *x = (int)((px - r.x) * CGA_W / r.w);
    *y = (int)((py - r.y) * CGA_H / r.h);
    if (px < r.x || py < r.y || *x >= CGA_W || *y >= CGA_H)
        *x = *y = -1;
}

/* A left click at (mouse_x, mouse_y). */
static void mouse_click(void)
{
    if (!settings.navigation)
        return;
    if (menu_clicks) {
        click_pending = true;
        click_x = mouse_x;
        click_y = mouse_y;
    } else if (pad_get_mode() == PAD_MENU) {
        push_key(13);
    }
}

static void mouse_event(const SDL_Event *ev)
{
    if (ev->type == SDL_MOUSEMOTION) {
        window_to_screen(ev->motion.x, ev->motion.y, &mouse_x, &mouse_y);
        mouse_moved_at = SDL_GetTicks();
    } else if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        window_to_screen(ev->button.x, ev->button.y, &mouse_x, &mouse_y);
        mouse_moved_at = SDL_GetTicks();
        mouse_click();
    }
}

void plat_mouse_pos(int *x, int *y)
{
    *x = mouse_x;
    *y = mouse_y;
}

void plat_menu_clicks(bool on)
{
    menu_clicks = on;
    click_pending = false;
}

bool plat_take_click(int *x, int *y)
{
    if (!click_pending)
        return false;
    click_pending = false;
    *x = click_x;
    *y = click_y;
    return true;
}

/* Map an SDL key press to what the BIOS would put in its buffer. */
static void key_down(const SDL_KeyboardEvent *k)
{
    const SDL_Keycode sym = k->keysym.sym;
    const bool shift = (k->keysym.mod & KMOD_SHIFT) != 0;

    if ((sym == SDLK_RETURN && (k->keysym.mod & KMOD_ALT)) || sym == SDLK_F11) {
        if (!k->repeat)
            toggle_fullscreen();
        return;
    }

    switch (sym) {
    /* front-end keys, not seen by the game */
    case SDLK_F1: settings.palette ^= 1; return; /* for this run; ptomb.ini isn't changed */
    case SDLK_F2: settings.bright ^= 1; return;
    case SDLK_F3: settings.aspect_43 ^= 1; return;
    case SDLK_F4: settings.sprites ^= 1; return;
    case SDLK_F5: settings.smooth ^= 1; return;
    case SDLK_F12: {
        char name[64];
        snprintf(name, sizeof name, "ptomb_shot%02d.png", shot_no++);
        if (save_shot(name) == 0)
            printf("saved %s\n", name);
        return;
    }
    case SDLK_PAGEUP: level_step = -1; return;
    case SDLK_PAGEDOWN: level_step = 1; return;

    /* arrows: the keyboard ISR tracks Left/Right; the BIOS queues all four as extended keys */
    case SDLK_LEFT:
        if (!k->repeat)
            isr_keys.left = true;
        push_extended(0x4B);
        return;
    case SDLK_RIGHT:
        if (!k->repeat)
            isr_keys.right = true;
        push_extended(0x4D);
        return;
    case SDLK_UP: push_extended(0x48); return;
    case SDLK_DOWN: push_extended(0x50); return;

    case SDLK_ESCAPE: push_key(27); return;
    case SDLK_RETURN: case SDLK_KP_ENTER: push_key(13); return;
    case SDLK_BACKSPACE: push_key(8); return;
    case SDLK_KP_PLUS: push_key('+'); return;
    default: break;
    }

    if (sym >= 'a' && sym <= 'z') {
        push_key(shift ? sym - 32 : sym);
    } else if (sym >= 0x20 && sym < 0x7F) {
        static const char plain[] = "1234567890-=[];',./`\\";
        static const char shifted[] = "!@#$%^&*()_+{}:\"<>?~|";
        const char *p = strchr(plain, (int)sym);
        push_key(shift && p ? shifted[p - plain] : (int)sym);
    }
}

static void key_up(const SDL_KeyboardEvent *k)
{
    if (k->keysym.sym == SDLK_LEFT)
        isr_keys.left = false;
    else if (k->keysym.sym == SDLK_RIGHT)
        isr_keys.right = false;
}

shift_keys_t plat_shift_keys(void)
{
    const SDL_Keymod m = SDL_GetModState();
    shift_keys_t s;
    s.ctrl = (m & KMOD_CTRL) != 0;
    s.alt = (m & KMOD_ALT) != 0;
    s.lshift = (m & KMOD_LSHIFT) != 0;
    s.rshift = (m & KMOD_RSHIFT) != 0;
    s.scroll_lock = (m & KMOD_SCROLL) != 0;
    return s;
}

/* --- scripted input (--script) ------------------------------------------------------------- */

/* Commands separated by ';', timed in timer ticks (18.2 per second):
 *   wait N | down KEY | up KEY | tap KEY | shot FILE.png | view FILE.png | mouse X Y | click X Y | quit
 * (shot saves the framebuffer, view what the window shows)
 * (mouse and click are in 320x200 screen pixels)
 * KEY is left, right, up, down, space, esc, enter, pgup, pgdn, f11, or a single character, or a control on a
 * virtual game controller: pad_a, pad_b, pad_x, pad_start, pad_back, pad_left, ... (gamepad.h). */
static char script_buf[4096];
static bool (*script_handler)(const char *cmd, int a, int b);

void plat_script_handler(bool (*fn)(const char *cmd, int a, int b))
{
    script_handler = fn;
}

static char *script_pos;
static uint32_t script_wait_until;
static const char *script_tap_pending;

static SDL_Keycode key_by_name(const char *name)
{
    static const struct { const char *name; SDL_Keycode sym; } names[] = {
        {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"up", SDLK_UP}, {"down", SDLK_DOWN},
        {"space", SDLK_SPACE}, {"esc", SDLK_ESCAPE}, {"enter", SDLK_RETURN}, {"pgdn", SDLK_PAGEDOWN},
        {"pgup", SDLK_PAGEUP}, {"f11", SDLK_F11},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i].name))
            return names[i].sym;
    return strlen(name) == 1 ? (SDL_Keycode)(unsigned char)name[0] : SDLK_UNKNOWN;
}

static void script_key(const char *name, bool down)
{
    if (pad_script(name, down))
        return;
    SDL_KeyboardEvent k;
    SDL_zero(k);
    k.keysym.sym = key_by_name(name);
    if (down)
        key_down(&k);
    else
        key_up(&k);
}

void plat_set_script(const char *script)
{
    snprintf(script_buf, sizeof script_buf, "%s", script);
    script_pos = script_buf;
    script_wait_until = 0;
}

static void run_script(void)
{
    static char tap_key[32];
    if (script_tap_pending && tick_total >= script_wait_until) {
        script_key(script_tap_pending, false);
        script_tap_pending = NULL;
    }
    while (script_pos && !script_tap_pending && tick_total >= script_wait_until) {
        char *end = strchr(script_pos, ';');
        if (end)
            *end = '\0';
        char cmd[16] = "", arg[256] = "";
        int mx = -1, my = -1;
        sscanf(script_pos, " %15s %255s", cmd, arg);
        sscanf(script_pos, " %*s %d %d", &mx, &my);
        script_pos = end ? end + 1 : NULL;

        if (!strcmp(cmd, "wait"))
            script_wait_until = tick_total + (uint32_t)atoi(arg);
        else if (!strcmp(cmd, "down"))
            script_key(arg, true);
        else if (!strcmp(cmd, "up"))
            script_key(arg, false);
        else if (!strcmp(cmd, "tap")) {
            snprintf(tap_key, sizeof tap_key, "%s", arg);
            script_key(tap_key, true);
            script_tap_pending = tap_key;
            script_wait_until = tick_total + 1;
        } else if (!strcmp(cmd, "shot")) {
            if (cga_write_png(arg, cga_palette(settings.palette, settings.bright), 2) == 0)
                printf("[tick %u] saved %s  %s\n", tick_total, arg, status_text);
        } else if (!strcmp(cmd, "view")) { /* what the window shows (display.smooth / sprites) */
            if (save_shot(arg) == 0)
                printf("[tick %u] saved %s  %s\n", tick_total, arg, status_text);
        } else if (!strcmp(cmd, "mouse") || !strcmp(cmd, "click")) {
            mouse_x = mx;
            mouse_y = my;
            if (cmd[0] == 'c')
                mouse_click();
        } else if (!strcmp(cmd, "quit")) {
            quitting = true;
        } else if (cmd[0] && !(script_handler && script_handler(cmd, mx, my))) {
            fprintf(stderr, "ptomb: unknown script command '%s'\n", cmd);
        }
    }
}

/* --- main pump ----------------------------------------------------------------------------- */

static int refresh_hz(void)
{
    SDL_DisplayMode m;
    if (SDL_GetWindowDisplayMode(win, &m) == 0 && m.refresh_rate >= 30)
        return m.refresh_rate > 360 ? 360 : m.refresh_rate;
    return 60;
}

/* may_present: whether a frame may be drawn now if one is due. With display.smooth, frames come
 * at the monitor's rate and the wait for vsync can take most of one. GameTick polls in the middle
 * of a pass (KeyPressed), and a wait there would let timer ticks land between the pass's
 * counter checks, so the game would play differently. There it only draws if frames have
 * stopped coming from anywhere else (a menu loop that does nothing but poll). */
static void poll(bool may_present)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        if (ev.type == SDL_QUIT)
            quitting = true;
        else if (ev.type == SDL_KEYDOWN)
            key_down(&ev.key);
        else if (ev.type == SDL_KEYUP)
            key_up(&ev.key);
        else if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            isr_keys.left = isr_keys.right = false; /* don't leave a key "held" */
        else if (ev.type == SDL_MOUSEMOTION || ev.type == SDL_MOUSEBUTTONDOWN)
            mouse_event(&ev);
        else
            pad_event(&ev);
    }

    /* The timer interrupt, at 18.2065 Hz of real time. */
    const Uint64 now = SDL_GetPerformanceCounter();
    pending_ticks += (double)(now - last_counter) / (double)perf_freq * TIMER_HZ;
    last_counter = now;
    if (pending_ticks > 18.0)
        pending_ticks = 18.0; /* after a stall, don't fast-forward more than a second */
    while (pending_ticks >= 1.0) {
        pending_ticks -= 1.0;
        timer_isr();
    }
    if (script_pos || script_tap_pending)
        run_script();

    /* 60 frames a second, or with smooth movement the monitor's own rate (vsync paces it) */
    const double since = (double)(now - last_present) / (double)perf_freq;
    if (settings.smooth ? (may_present && since >= 0.97 / refresh_hz()) || since >= 1.0 / 30.0
                        : since >= 1.0 / 60.0) {
        last_present = now;
        present();
    }
}

bool plat_quitting(void)
{
    return quitting;
}

void plat_poll(void)
{
    poll(true);
}

bool plat_key_pressed(void)
{
    poll(false);
    return key_head != key_tail;
}

int plat_read_key(void)
{
    while (key_head == key_tail) {
        if (quitting)
            return 27;
        plat_poll();
        SDL_Delay(1);
    }
    const int c = keybuf[key_head];
    key_head = (key_head + 1) % (int)(sizeof keybuf / sizeof keybuf[0]);
    return c;
}

void plat_delay(int ticks)
{
    tick_counter[TICK_7F74] = 0;
    while (tick_counter[TICK_7F74] < ticks && !quitting) {
        plat_poll();
        SDL_Delay(1);
    }
}

void plat_crt_delay(int ms)
{
    const Uint64 end = SDL_GetPerformanceCounter() + (Uint64)ms * perf_freq / 1000;
    while (SDL_GetPerformanceCounter() < end && !quitting) {
        plat_poll();
        SDL_Delay(1);
    }
}

void plat_pace(int hz)
{
    static Uint64 next;
    const Uint64 period = perf_freq / (Uint64)hz;
    Uint64 now = SDL_GetPerformanceCounter();
    if (next == 0 || now > next + perf_freq / 4)
        next = now; /* first call, or far behind (a long animation): don't catch up */
    next += period;
    while (now < next && !quitting) {
        const Uint64 left_ms = (next - now) * 1000 / perf_freq;
        Uint32 sleep_ms = left_ms > 1 ? (Uint32)(left_ms - 1) : 0;
        if (settings.smooth && sleep_ms > 1)
            sleep_ms = 1; /* wake for every frame at the monitor's rate, not just each pass */
        SDL_Delay(sleep_ms);
        /* With display.smooth, draw a frame only if it's done before the next pass is due: a
         * late pass would see the timer differently and play differently. */
        now = SDL_GetPerformanceCounter();
        poll(!settings.smooth ||
             (now < next && (double)(next - now) / (double)perf_freq > 0.003));
        now = SDL_GetPerformanceCounter();
    }
}

/* --- set-up -------------------------------------------------------------------------------- */

int plat_init(char *err, size_t errlen)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        snprintf(err, errlen, "%s", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow("Pharaoh's Tomb", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI |
                               (settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0));
    ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, CGA_W, CGA_H) : NULL;
    if (!tex) {
        snprintf(err, errlen, "%s", SDL_GetError());
        SDL_Quit();
        return -1;
    }

    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    want.userdata = NULL;
    audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audio) {
        audio_rate = have.freq;
        SDL_PauseAudioDevice(audio, 0);
    } /* no audio device: carry on silently */

    pad_init();

    perf_freq = SDL_GetPerformanceFrequency();
    last_counter = last_present = SDL_GetPerformanceCounter();
    return 0;
}

void plat_shutdown(void)
{
    if (audio)
        SDL_CloseAudioDevice(audio);
    write_recording();
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
}

int plat_take_level_step(void)
{
    const int s = level_step;
    level_step = 0;
    return s;
}

void plat_present_now(void)
{
    present();
}
