#include "platform.h"

#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "platform_sdl.h"
#include "timer.h"

isr_keys_t isr_keys;

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *tex;
static SDL_AudioDeviceID audio;

static int palette, bright, aspect_43 = 1, shot_no;
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
    const double ar = aspect_43 ? 4.0 / 3.0 : 320.0 / 200.0;
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

static void present(void)
{
    void *pixels;
    int pitch;
    if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
        cga_to_argb(pixels, pitch / 4, cga_palette(palette, bright));
        SDL_UnlockTexture(tex);
    }
    int ow, oh;
    SDL_GetRendererOutputSize(ren, &ow, &oh);
    const SDL_Rect dst = fit_rect(ow, oh);
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_RenderPresent(ren);

    if (title_dirty) {
        char title[400];
        snprintf(title, sizeof title, "Pharaoh's Tomb - %s%s%s", message_text[0] ? message_text : "",
                 message_text[0] ? " - " : "", status_text);
        SDL_SetWindowTitle(win, title);
        title_dirty = false;
    }
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

static void push_extended(int scancode)
{
    push_key(0);
    push_key(scancode);
}

/* Map an SDL key press to what the BIOS would put in its buffer. */
static void key_down(const SDL_KeyboardEvent *k)
{
    const SDL_Keycode sym = k->keysym.sym;
    const bool shift = (k->keysym.mod & KMOD_SHIFT) != 0;

    switch (sym) {
    /* front-end keys, not seen by the game */
    case SDLK_F1: palette ^= 1; return;
    case SDLK_F2: bright ^= 1; return;
    case SDLK_F3: aspect_43 ^= 1; return;
    case SDLK_F12: {
        char name[64];
        snprintf(name, sizeof name, "ptomb_shot%02d.png", shot_no++);
        if (cga_write_png(name, cga_palette(palette, bright), 2) == 0)
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
 *   wait N | down KEY | up KEY | tap KEY | shot FILE.png | quit
 * KEY is left, right, up, down, space, esc, enter, or a single character. */
static char script_buf[4096];
static char *script_pos;
static uint32_t script_wait_until;
static const char *script_tap_pending;

static SDL_Keycode key_by_name(const char *name)
{
    static const struct { const char *name; SDL_Keycode sym; } names[] = {
        {"left", SDLK_LEFT}, {"right", SDLK_RIGHT}, {"up", SDLK_UP}, {"down", SDLK_DOWN},
        {"space", SDLK_SPACE}, {"esc", SDLK_ESCAPE}, {"enter", SDLK_RETURN}, {"pgdn", SDLK_PAGEDOWN},
        {"pgup", SDLK_PAGEUP},
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        if (!strcmp(name, names[i].name))
            return names[i].sym;
    return strlen(name) == 1 ? (SDL_Keycode)(unsigned char)name[0] : SDLK_UNKNOWN;
}

static void script_key(const char *name, bool down)
{
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
        sscanf(script_pos, " %15s %255s", cmd, arg);
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
            if (cga_write_png(arg, cga_palette(palette, bright), 2) == 0)
                printf("[tick %u] saved %s  %s\n", tick_total, arg, status_text);
        } else if (!strcmp(cmd, "quit"))
            quitting = true;
    }
}

/* --- main pump ----------------------------------------------------------------------------- */

void plat_poll(void)
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

    if ((double)(now - last_present) / (double)perf_freq >= 1.0 / 60.0) {
        last_present = now;
        present();
    }
}

bool plat_quitting(void)
{
    return quitting;
}

bool plat_key_pressed(void)
{
    plat_poll();
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
        SDL_Delay(left_ms > 1 ? (Uint32)(left_ms - 1) : 0);
        plat_poll();
        now = SDL_GetPerformanceCounter();
    }
}

/* --- set-up -------------------------------------------------------------------------------- */

int plat_init(int pal, int bri, char *err, size_t errlen)
{
    palette = pal;
    bright = bri;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        snprintf(err, errlen, "%s", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow("Pharaoh's Tomb", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 960,
                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
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
