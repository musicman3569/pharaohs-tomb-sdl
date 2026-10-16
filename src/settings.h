/* The port's own settings: what to show and how, and (from here on) which enhancements are on.
 *
 * Two presets:
 *   classic   the original game, as released in v1.0.0 (the default)
 *   enhanced  classic plus every enhancement
 * Each setting has a value for each preset. Front-end preferences (palette, aspect, ...)
 * have the same value in both; enhancements are off in classic and on in enhanced.
 *
 * Where they come from, later ones winning:
 *   1. the preset (from --classic / --enhanced, else game.preset in the file, else classic)
 *   2. ptomb.ini next to ptomb.exe (or the --config file)
 *   3. the command line: --set section.key=value, and the older --palette / --bright
 *
 * Adding an enhancement: add a field to settings_t, a row to the table in settings.c (classic
 * value = the original behaviour), and test `settings.field` where the behaviour changes. Keep
 * the classic path exactly as it was. */
#ifndef PTOMB_SETTINGS_H
#define PTOMB_SETTINGS_H

#include <stddef.h>
#include <stdio.h>

typedef enum { PRESET_CLASSIC, PRESET_ENHANCED } preset_t;

typedef struct {
    int preset;     /* game.preset: preset_t */

    /* display */
    int palette;    /* display.palette: 0 green/red/brown, 1 cyan/magenta/white (the original) */
    int bright;     /* display.bright: high intensity */
    int aspect_43;  /* display.aspect: stretch to 4:3 as on a CRT */

    /* input */
    int joystick;   /* input.joystick: start with the joystick (controller) switched on */
} settings_t;

extern settings_t settings;

/* Set everything to a preset's values. */
void settings_reset(preset_t preset);

/* Set one setting from text, e.g. ("display.palette", "1"). Returns 0, or -1 with a message. */
int settings_set(const char *key, const char *value, char *err, size_t errlen);

/* A preset's name to its value ("classic" / "enhanced"); -1 if unknown. */
int settings_preset_by_name(const char *name);

/* Read an INI file into key/value pairs without applying them, so the caller can pick the
 * preset first. Returns the number of pairs, 0 if the file doesn't exist, -1 on a read error.
 * Malformed lines are reported on stderr and skipped. */
typedef struct {
    char key[48];
    char value[48];
    int line;
} settings_pair_t;
int settings_read_file(const char *path, settings_pair_t *pairs, int max);

/* Write the current settings as an INI file, with comments (--print-settings). */
void settings_write(FILE *f);

#endif
