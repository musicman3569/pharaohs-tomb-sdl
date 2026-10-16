/* Pharaoh's Tomb port: entry point and game loop.
 *
 *   ptomb [PTOMBn.EXE] [--classic | --enhanced] [--set section.key=value ...] [--config FILE]
 *         [--print-settings] [--level N] [--palette 0|1] [--bright] [--keys 1|2|3] [--cheat]
 *         [--screenshot out.png] [--script "wait 40; down right; wait 10; up right; shot a.png; quit"]
 *
 * Starts like the original: title screen, then the main menu. --level N goes straight into a
 * level instead (for testing) and exits afterwards.
 *
 * In game: Left/Right walk, Space jump, Z X C V B N M , . / F throw a spear, P pause,
 * E sound on/off, Q or Esc quit (then Y). Port extras: PgUp/PgDn change level, F1 palette,
 * F2 intensity, F3 aspect correction, F12 screenshot. */
#include <SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cga.h"
#include "gamepad.h"
#include "game.h"
#include "gamedata.h"
#include "level.h"
#include "menu.h"
#include "platform.h"
#include "platform_sdl.h"
#include "settings.h"
#include "sound.h"

typedef struct {
    const char *exe_path;
    int level;
    int level_given;
    int preset;            /* --classic / --enhanced, or -1 */
    const char *config;    /* --config FILE, or NULL for ptomb.ini next to ptomb.exe */
    const char *sets[32];  /* --set key=value, in order (--palette / --bright become these) */
    int n_sets;
    int print_settings;
    int keys;    /* PTOMB.CFG layout for the alternate keys */
    int cheat;
    const char *screenshot;
    const char *script;
    const char *record_audio;
    int sound_test;
    int debug_exe;
    int ending;
} options_t;

/* --debug-exe: print what was found in the EXE (for checking the pattern scanners). */
static void debug_exe(const gamedata_t *g)
{
    printf("volume %d, base name '%s', dir '%s', unit shift %+d\n", g->volume, g->base_name, g->dir,
           g->seg_delta);
    const struct { const char *name; size_t at; size_t len; } fns[] = {
        {"TitleScreen", g->fn_title_screen, 0x200}, {"MainMenu", g->fn_main_menu, 0x1A0},
        {"ShowHighScores", g->fn_high_scores, 0x60}, {"JoystickMenu", g->fn_joystick_menu, 0x60},
        {"GameOver", g->fn_game_over, 0x80},
    };
    static const char *const kinds[] = {"FONT", "COLOR", "CENTER", "AT", "STRLOAD"};
    for (size_t i = 0; i < sizeof fns / sizeof fns[0]; i++) {
        printf("%-15s at %05zX\n", fns[i].name, fns[i].at);
        if (!fns[i].at)
            continue;
        text_op_t ops[40];
        const int n = gamedata_text_ops(g, fns[i].at, fns[i].len, 0, ops, 40);
        for (int k = 0; k < n; k++)
            printf("    %-7s %3d %3d  %s\n", kinds[ops[k].kind], ops[k].a, ops[k].b, ops[k].text);
    }
}

static int parse_args(int argc, char **argv, options_t *o)
{
    memset(o, 0, sizeof *o);
    o->exe_path = "PTOMB1.EXE";
    o->level = 1;
    o->keys = 0; /* 0 = use PTOMB.CFG */
    o->preset = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *set = NULL;
        if (!strcmp(a, "--classic") || !strcmp(a, "--enhanced"))
            o->preset = settings_preset_by_name(a + 2);
        else if (!strcmp(a, "--set") && i + 1 < argc)
            set = argv[++i];
        else if (!strcmp(a, "--config") && i + 1 < argc)
            o->config = argv[++i];
        else if (!strcmp(a, "--print-settings"))
            o->print_settings = 1;
        else if (!strcmp(a, "--level") && i + 1 < argc) {
            o->level = atoi(argv[++i]);
            o->level_given = 1;
        }
        else if (!strcmp(a, "--palette") && i + 1 < argc)
            set = atoi(argv[++i]) ? "display.palette=1" : "display.palette=0";
        else if (!strcmp(a, "--bright"))
            set = "display.bright=on";
        else if (!strcmp(a, "--keys") && i + 1 < argc)
            o->keys = argv[++i][0];
        else if (!strcmp(a, "--cheat"))
            o->cheat = 1;
        else if (!strcmp(a, "--screenshot") && i + 1 < argc)
            o->screenshot = argv[++i];
        else if (!strcmp(a, "--script") && i + 1 < argc)
            o->script = argv[++i];
        else if (!strcmp(a, "--record-audio") && i + 1 < argc)
            o->record_audio = argv[++i];
        else if (!strcmp(a, "--sound-test"))
            o->sound_test = 1;
        else if (!strcmp(a, "--debug-exe"))
            o->debug_exe = 1;
        else if (!strcmp(a, "--ending"))
            o->ending = 1;
        else if (a[0] == '-') {
            fprintf(stderr, "usage: ptomb [PTOMBn.EXE] [--classic | --enhanced] [--set section.key=value] "
                            "[--config FILE] [--print-settings] [--level N] [--palette 0|1] [--bright] "
                            "[--keys 1|2|3] [--cheat] [--screenshot out.png]\n");
            return -1;
        } else
            o->exe_path = a;

        if (set) {
            if (!strncmp(set, "game.preset=", 12)) { /* a preset, not just the field */
                if ((o->preset = settings_preset_by_name(set + 12)) < 0) {
                    fprintf(stderr, "ptomb: game.preset must be classic or enhanced\n");
                    return -1;
                }
            } else if (o->n_sets < (int)(sizeof o->sets / sizeof o->sets[0])) {
                o->sets[o->n_sets++] = set;
            }
        }
    }
    return 0;
}

/* Settings: the preset, then the INI file, then the command line (see settings.h). */
static int load_settings(const options_t *o)
{
    char path[1024];
    if (o->config) {
        snprintf(path, sizeof path, "%s", o->config);
    } else {
        char *base = SDL_GetBasePath();
        snprintf(path, sizeof path, "%sptomb.ini", base ? base : "");
        SDL_free(base);
    }
    static settings_pair_t pairs[64];
    int n = settings_read_file(path, pairs, 64);
    if (n < 0) {
        fprintf(stderr, "ptomb: can't read %s\n", path);
        n = 0;
    } else if (n == 0 && o->config) {
        fprintf(stderr, "ptomb: %s is missing or empty\n", path);
    }

    int preset = o->preset;
    for (int i = 0; i < n && preset < 0; i++)
        if (!strcmp(pairs[i].key, "game.preset") && (preset = settings_preset_by_name(pairs[i].value)) < 0)
            fprintf(stderr, "ptomb: %s:%d: game.preset must be classic or enhanced\n", path, pairs[i].line);
    settings_reset(preset < 0 ? PRESET_CLASSIC : (preset_t)preset);

    char err[160];
    for (int i = 0; i < n; i++)
        if (strcmp(pairs[i].key, "game.preset") != 0 &&
            settings_set(pairs[i].key, pairs[i].value, err, sizeof err) != 0)
            fprintf(stderr, "ptomb: %s:%d: %s (ignored)\n", path, pairs[i].line, err);

    for (int i = 0; i < o->n_sets; i++) {
        char key[64];
        const char *eq = strchr(o->sets[i], '=');
        if (!eq) {
            fprintf(stderr, "ptomb: --set needs section.key=value, not '%s'\n", o->sets[i]);
            return -1;
        }
        snprintf(key, sizeof key, "%.*s", (int)(eq - o->sets[i]), o->sets[i]);
        if (settings_set(key, eq + 1, err, sizeof err) != 0) {
            fprintf(stderr, "ptomb: %s\n", err);
            return -1;
        }
    }
    return 0;
}

static void fail(const char *msg)
{
    fprintf(stderr, "ptomb: %s\n", msg);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Pharaoh's Tomb", msg, NULL);
}

/* --sound-test: play every sound effect once, named in the title bar. */
static void sound_test(void)
{
    static const struct { const char *name; void (*play)(bool); } fx[] = {
        {"pickup (1854:58D2)", sfx_pickup},     {"unlock (1854:598F)", sfx_unlock},
        {"bonus (1854:592F)", sfx_bonus},       {"death (1854:595F)", sfx_death},
        {"enemy killed (1854:5900)", sfx_enemy_killed}, {"artifact (1000:00B9)", sfx_artifact},
    };
    for (size_t i = 0; i < sizeof fx / sizeof fx[0] && !plat_quitting(); i++) {
        plat_message(fx[i].name);
        fx[i].play(true);
        plat_delay(12);
    }
    static const struct { const char *name; int lo, hi, gap, hold, step, up, stacc; } sw[] = {
        {"spear throw", 500, 3000, 1, 0, 1000, 1, 0},  {"crumble / build", 1000, 3000, 0, 0, 500, 0, 1},
        {"invisibility", 100, 5000, 1, 1, 1000, 1, 1}, {"freeze", 1000, 10000, 1, 1, 1000, 0, 1},
    };
    for (size_t i = 0; i < sizeof sw / sizeof sw[0] && !plat_quitting(); i++) {
        plat_message(sw[i].name);
        sfx_sweep(sw[i].lo, sw[i].hi, sw[i].gap, sw[i].hold, sw[i].step, sw[i].up, sw[i].stacc, true);
        plat_delay(12);
    }
    plat_message("");
}


int main(int argc, char **argv)
{
    options_t opt;
    if (parse_args(argc, argv, &opt) != 0 || load_settings(&opt) != 0)
        return 2;
    if (opt.print_settings) {
        settings_write(stdout);
        return 0;
    }
    joy_vars.enabled = (uint8_t)settings.joystick;

    static gamedata_t data;
    char err[256];
    if (gamedata_load(&data, opt.exe_path, err, sizeof err) != 0) {
        fail(err);
        return 1;
    }
    if (opt.debug_exe) {
        debug_exe(&data);
        gamedata_free(&data);
        return 0;
    }
    if (opt.level < 1 || opt.level > data.num_levels)
        opt.level = 1;

    if (opt.screenshot) {
        /* Render one level to a file without opening a window. */
        static level_t lv;
        level_start(&lv, &data, opt.level);
        const int rc = cga_write_png(opt.screenshot, cga_palette(settings.palette, settings.bright), 2);
        if (rc != 0)
            fprintf(stderr, "ptomb: can't write %s\n", opt.screenshot);
        gamedata_free(&data);
        return rc != 0;
    }

    if (opt.record_audio)
        plat_record_audio(opt.record_audio);
    if (plat_init(err, sizeof err) != 0) {
        fail(err);
        gamedata_free(&data);
        return 1;
    }

    if (opt.sound_test) {
        sound_test();
        plat_shutdown();
        gamedata_free(&data);
        return 0;
    }
    if (menu_init(&data, err, sizeof err) != 0) {
        fail(err);
        plat_shutdown();
        gamedata_free(&data);
        return 1;
    }
    const int layout = opt.keys ? opt.keys : menu_key_layout();
    if (opt.script)
        plat_set_script(opt.script);

    if (opt.ending) {
        /* testing: just this volume's ending pages */
        menu_volume_ending(data.volume);
    } else if (opt.level_given) {
        /* straight into a level (testing): play once, then exit */
        game_reset_state(&data, layout);
        game_play(opt.level, opt.cheat != 0);
    } else {
        /* as the original: title screen, then the main menu until Quit */
        menu_title_screen();
        menu_main(opt.cheat != 0);
    }

    plat_shutdown();
    gamedata_free(&data);
    return 0;
}
