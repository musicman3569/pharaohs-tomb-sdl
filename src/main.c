/* Pharaoh's Tomb port: SDL2 front end.
 *
 * For now this is a level viewer that exercises the pieces the game logic will sit on: the
 * CGA framebuffer, Graph3 drawing, the 18.2 Hz timer, and assets read from the original EXE.
 *
 *   ptomb [PTOMBn.EXE] [--level N] [--palette 0|1] [--bright] [--screenshot out.png]
 *
 * Keys: Left/Right or PgUp/PgDn change level, F1 palette 0/1, F2 intensity, F3 aspect
 * correction, F12 screenshot, Esc quit. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamedata.h"
#include "level.h"
#include "timer.h"

typedef struct {
    const char *exe_path;
    int level;
    int palette;     /* 0 by default: the project's preferred look; 1 is the game's own */
    int bright;
    const char *screenshot;
} options_t;

static void usage(void)
{
    fprintf(stderr,
            "usage: ptomb [PTOMBn.EXE] [--level N] [--palette 0|1] [--bright] [--screenshot out.png]\n");
}

static int parse_args(int argc, char **argv, options_t *o)
{
    o->exe_path = "PTOMB1.EXE";
    o->level = 1;
    o->palette = 0;
    o->bright = 0;
    o->screenshot = NULL;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            o->palette = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(a, "--bright"))
            o->bright = 1;
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (a[0] == '-') {
            usage();
            return -1;
        } else
            o->exe_path = a;
    }
    return 0;
}

static void fail(const char *msg)
{
    fprintf(stderr, "ptomb: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pharaoh's Tomb", msg, NULL);
}

/* Fit the 320x200 picture into the window, optionally stretched to 4:3 like a CRT. */
static SDL_Rect fit_rect(int win_w, int win_h, int aspect_43)
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

int main(int argc, char **argv)
{
    options_t opt;
    if (parse_args(argc, argv, &opt) != 0)
        return 2;

    static gamedata_t game;
    char err[256];
    if (gamedata_load(&game, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    if (opt.level < 1 || opt.level > game.num_levels)
        opt.level = 1;

    static level_t lv;
    level_start(&lv, &game, opt.level);

    if (opt.screenshot) {
        int rc = cga_write_png(opt.screenshot, cga_palette(opt.palette, opt.bright), 2);
        if (rc != 0)
            fprintf(stderr, "ptomb: can't write %s\n", opt.screenshot);
        gamedata_free(&game);
        return rc != 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fail(SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_Window *win = SDL_CreateWindow("Pharaoh's Tomb", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = win ? SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC) : NULL;
    SDL_Texture *tex = ren ? SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                               CGA_W, CGA_H) : NULL;
    if (!tex) {
        fail(SDL_GetError());
        SDL_Quit();
        return 1;
    }

    int aspect_43 = 1, running = 1, shot_no = 0;
    const Uint64 freq = SDL_GetPerformanceFrequency();
    const double tick_seconds = 1.0 / TIMER_HZ;
    Uint64 last = SDL_GetPerformanceCounter();
    double pending = 0.0;
    uint32_t title_tick = (uint32_t)-1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT)
                running = 0;
            else if (ev.type == SDL_KEYDOWN) {
                int new_level = lv.level;
                switch (ev.key.keysym.sym) {
                case SDLK_ESCAPE: running = 0; break;
                case SDLK_LEFT: case SDLK_PAGEUP: new_level--; break;
                case SDLK_RIGHT: case SDLK_PAGEDOWN: new_level++; break;
                case SDLK_F1: opt.palette ^= 1; break;
                case SDLK_F2: opt.bright ^= 1; break;
                case SDLK_F3: aspect_43 ^= 1; break;
                case SDLK_F12: {
                    char name[64];
                    snprintf(name, sizeof name, "ptomb_shot%02d.png", shot_no++);
                    if (cga_write_png(name, cga_palette(opt.palette, opt.bright), 2) == 0)
                        printf("saved %s\n", name);
                    break;
                }
                default: break;
                }
                if (new_level < 1)
                    new_level = game.num_levels;
                if (new_level > game.num_levels)
                    new_level = 1;
                if (new_level != lv.level)
                    level_start(&lv, &game, new_level);
            }
        }

        /* Run the timer "interrupt" at 18.2065 Hz of real time. */
        const Uint64 now = SDL_GetPerformanceCounter();
        pending += (double)(now - last) / (double)freq;
        last = now;
        if (pending > 1.0)
            pending = 1.0; /* don't fast-forward after a stall */
        while (pending >= tick_seconds) {
            pending -= tick_seconds;
            timer_isr();
        }

        if (tick_total / 18 != title_tick) {
            title_tick = tick_total / 18;
            char title[128];
            snprintf(title, sizeof title, "Pharaoh's Tomb - level %d/%d - palette %d%s - %u ticks",
                     lv.level, game.num_levels, opt.palette, opt.bright ? " bright" : "", tick_total);
            SDL_SetWindowTitle(win, title);
        }

        void *pixels;
        int pitch;
        if (SDL_LockTexture(tex, NULL, &pixels, &pitch) == 0) {
            cga_to_argb(pixels, pitch / 4, cga_palette(opt.palette, opt.bright));
            SDL_UnlockTexture(tex);
        }
        int ow, oh;
        SDL_GetRendererOutputSize(ren, &ow, &oh);
        const SDL_Rect dst = fit_rect(ow, oh, aspect_43);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, &dst);
        SDL_RenderPresent(ren);
    }

    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    gamedata_free(&game);
    return 0;
}
