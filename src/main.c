/* Pharaoh's Tomb port: entry point and game loop.
 *
 *   ptomb [PTOMBn.EXE] [--level N] [--palette 0|1] [--bright] [--keys 1|2|3] [--cheat]
 *         [--screenshot out.png] [--script "wait 40; down right; wait 10; up right; shot a.png; quit"]
 *
 * In game: Left/Right walk, Space jump, Z X C V B N M , . / F throw a spear, P pause,
 * E sound on/off, Q or Esc quit (then Y). Port extras: PgUp/PgDn change level, F1 palette,
 * F2 intensity, F3 aspect correction, F12 screenshot. There's no menu yet: quitting or
 * losing starts a new game. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "game.h"
#include "gamedata.h"
#include "level.h"
#include "platform.h"
#include "platform_sdl.h"

/* GameTick runs flat out on the original hardware; anything speed-sensitive is paced by the
 * 18.2 Hz timer counters instead. Running it at a fixed 70 Hz keeps those counters from being
 * missed while keeping per-call randomness (gem glints) independent of the monitor. */
#define GAME_TICK_HZ 70

typedef struct {
    const char *exe_path;
    int level;
    int palette; /* 0 by default: the project's preferred look; 1 is the game's own */
    int bright;
    int keys;    /* PTOMB.CFG layout for the alternate keys */
    int cheat;
    const char *screenshot;
    const char *script;
} options_t;

static int parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->exe_path = "PTOMB1.EXE";
    o->level = 1;
    o->keys = '3';
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--level") && i + 1 < argc)
            o->level = atoi(argv[++i]);
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            o->palette = atoi(argv[++i]) ? 1 : 0;
        else if (!strcmp(a, "--bright"))
            o->bright = 1;
        else if (!strcmp(a, "--keys") && i + 1 < argc)
            o->keys = argv[++i][0];
        else if (!strcmp(a, "--cheat"))
            o->cheat = 1;
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc)
            o->script = argv[++i];
        else if (a[0] == '-') {
            fprintf(stderr, "usage: ptomb [PTOMBn.EXE] [--level N] [--palette 0|1] [--bright] "
                            "[--keys 1|2|3] [--cheat] [--screenshot out.png]\n");
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

static void new_game(const gamedata_t *data, const options_t *opt, int level)
{
    G.key_layout = opt->keys;
    game_new(data, level);
    G.cheat = opt->cheat != 0; /* the menu's Scroll Lock + '+' cheat */
}

int main(int argc, char **argv)
{
    options_t opt;
    if (parse_args(argc, argv, &opt) != 0)
        return 2;

    static gamedata_t data;
    char err[256];
    if (gamedata_load(&data, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    if (opt.level < 1 || opt.level > data.num_levels)
        opt.level = 1;

    if (opt.screenshot) {
        /* Render one level to a file without opening a window. */
        static level_t lv;
        level_start(&lv, &data, opt.level);
        const int rc = cga_write_png(opt.screenshot, cga_palette(opt.palette, opt.bright), 2);
        if (rc != 0)
            fprintf(stderr, "ptomb: can't write %s\n", opt.screenshot);
        gamedata_free(&data);
        return rc != 0;
    }

    if (plat_init(opt.palette, opt.bright, err, sizeof err) != 0) {
        fail(err);
        gamedata_free(&data);
        return 1;
    }

    if (opt.script)
        plat_set_script(opt.script);
    new_game(&data, &opt, opt.level);

    const Uint64 freq = SDL_GetPerformanceFrequency();
    const Uint64 period = freq / GAME_TICK_HZ;
    Uint64 next = SDL_GetPerformanceCounter();

    while (!plat_quitting()) {
        if (G.quit)
            new_game(&data, &opt, opt.level);

        const int step = plat_take_level_step();
        if (step) {
            int l = G.level + step;
            if (l < 1)
                l = data.num_levels;
            if (l > data.num_levels)
                l = 1;
            game_start_level(l);
        }

        game_tick();
        plat_poll();

        next += period;
        Uint64 now = SDL_GetPerformanceCounter();
        if (now > next + freq / 4)
            next = now; /* fell far behind (a long animation): don't try to catch up */
        while (now < next && !plat_quitting()) {
            const Uint64 left_ms = (next - now) * 1000 / freq;
            SDL_Delay(left_ms > 1 ? (Uint32)(left_ms - 1) : 0);
            plat_poll();
            now = SDL_GetPerformanceCounter();
        }
    }

    plat_shutdown();
    gamedata_free(&data);
    return 0;
}
