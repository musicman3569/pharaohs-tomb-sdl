#include "settings.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

settings_t settings;

typedef enum { T_BOOL, T_INT, T_PRESET } type_t;

/* Every setting, with its value in each preset. Rows of one section stay together (the
 * written file follows this order). */
static const struct {
    const char *key;   /* section.name */
    type_t type;
    size_t offset;     /* in settings_t */
    int lo, hi;        /* T_INT range */
    int classic, enhanced;
    const char *help;
} table[] = {
    {"game.preset", T_PRESET, offsetof(settings_t, preset), 0, 1, PRESET_CLASSIC, PRESET_ENHANCED,
     "classic = the original game; enhanced = with every enhancement. Settings below override it"},
    {"display.palette", T_INT, offsetof(settings_t, palette), 0, 1, 0, 0,
     "CGA palette: 0 = green/red/brown, 1 = cyan/magenta/white (the original's). F1 switches"},
    {"display.bright", T_BOOL, offsetof(settings_t, bright), 0, 1, 0, 0,
     "high-intensity colours. F2 switches"},
    {"display.aspect", T_BOOL, offsetof(settings_t, aspect_43), 0, 1, 1, 1,
     "stretch to 4:3, as a CRT showed it (off = square pixels). F3 switches"},
    {"display.fullscreen", T_BOOL, offsetof(settings_t, fullscreen), 0, 1, 0, 0,
     "start in fullscreen. F11 or Alt+Enter switches"},
    {"input.joystick", T_BOOL, offsetof(settings_t, joystick), 0, 1, 1, 1,
     "start with the joystick (game controller) on; the original started with it off"},
    {"gameplay.solid_pickups", T_BOOL, offsetof(settings_t, solid_pickups), 0, 1, 1, 0,
     "pickups (gems, keys, spears, T, artifacts...) block you like walls until you touch them, as\n"
     "; in the original, so some jumps into them stop short. Off = you always reach them"},
    {"menu.navigation", T_BOOL, offsetof(settings_t, navigation), 0, 1, 0, 1,
     "main menu cursor: arrows / d-pad / mouse move it, Enter / Space / A / click choose; clicks\n"
     "; also work as a key on \"press any key\" screens. Off = the original's letter keys only"},
};
#define N_SETTINGS (sizeof table / sizeof table[0])

static int *field(size_t i)
{
    return (int *)((char *)&settings + table[i].offset);
}

void settings_reset(preset_t preset)
{
    for (size_t i = 0; i < N_SETTINGS; i++)
        *field(i) = preset == PRESET_ENHANCED ? table[i].enhanced : table[i].classic;
    settings.preset = (int)preset;
}

int settings_preset_by_name(const char *name)
{
    if (!strcmp(name, "classic"))
        return PRESET_CLASSIC;
    if (!strcmp(name, "enhanced"))
        return PRESET_ENHANCED;
    return -1;
}

static int parse_bool(const char *v)
{
    static const char *const yes[] = {"1", "on", "true", "yes"}, *const no[] = {"0", "off", "false", "no"};
    for (int i = 0; i < 4; i++) {
        if (!strcmp(v, yes[i]))
            return 1;
        if (!strcmp(v, no[i]))
            return 0;
    }
    return -1;
}

int settings_set(const char *key, const char *value, char *err, size_t errlen)
{
    for (size_t i = 0; i < N_SETTINGS; i++) {
        if (strcmp(key, table[i].key) != 0)
            continue;
        int v = -1;
        switch (table[i].type) {
        case T_BOOL:
            v = parse_bool(value);
            break;
        case T_PRESET:
            v = settings_preset_by_name(value);
            break;
        case T_INT: {
            char *end;
            const long n = strtol(value, &end, 10);
            if (*value && !*end && n >= table[i].lo && n <= table[i].hi)
                v = (int)n;
            break;
        }
        }
        if (v < 0) {
            static const char *const expect[] = {"on or off", "a number", "classic or enhanced"};
            if (table[i].type == T_INT)
                snprintf(err, errlen, "%s must be %d..%d, not '%s'", key, table[i].lo, table[i].hi, value);
            else
                snprintf(err, errlen, "%s must be %s, not '%s'", key, expect[table[i].type], value);
            return -1;
        }
        *field(i) = v;
        return 0;
    }
    snprintf(err, errlen, "unknown setting '%s'", key);
    return -1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1]))
        *--e = '\0';
    return s;
}

int settings_read_file(const char *path, settings_pair_t *pairs, int max)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    char line[256], section[32] = "";
    int n = 0, lineno = 0;
    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *c = strpbrk(line, ";#"); /* comments, whole-line or trailing */
        if (c)
            *c = '\0';
        char *s = trim(line);
        if (!*s)
            continue;
        if (*s == '[') {
            char *close = strchr(s, ']');
            if (!close || close[1]) {
                fprintf(stderr, "ptomb: %s:%d: bad section header\n", path, lineno);
                continue;
            }
            *close = '\0';
            snprintf(section, sizeof section, "%s", trim(s + 1));
            continue;
        }
        char *eq = strchr(s, '=');
        if (!eq) {
            fprintf(stderr, "ptomb: %s:%d: expected key = value\n", path, lineno);
            continue;
        }
        *eq = '\0';
        const char *k = trim(s), *v = trim(eq + 1);
        if (n == max) {
            fprintf(stderr, "ptomb: %s:%d: too many settings, ignoring the rest\n", path, lineno);
            break;
        }
        /* A key in a section is section.key; one before any section may name its section itself. */
        if (*section)
            snprintf(pairs[n].key, sizeof pairs[n].key, "%s.%s", section, k);
        else
            snprintf(pairs[n].key, sizeof pairs[n].key, "%s", k);
        snprintf(pairs[n].value, sizeof pairs[n].value, "%s", v);
        pairs[n].line = lineno;
        n++;
    }
    const int bad = ferror(f);
    fclose(f);
    return bad ? -1 : n;
}

void settings_write(FILE *f)
{
    fprintf(f, "; Pharaoh's Tomb port settings. Put this file next to ptomb.exe as ptomb.ini.\n"
               "; Command-line --set section.key=value overrides it.\n");
    char section[32] = "";
    for (size_t i = 0; i < N_SETTINGS; i++) {
        const char *dot = strchr(table[i].key, '.');
        const int slen = (int)(dot - table[i].key);
        if ((int)strlen(section) != slen || strncmp(section, table[i].key, (size_t)slen) != 0) {
            snprintf(section, sizeof section, "%.*s", slen, table[i].key);
            fprintf(f, "\n[%s]\n", section);
        }
        const int v = *field(i);
        fprintf(f, "; %s\n%s = ", table[i].help, dot + 1);
        if (table[i].type == T_BOOL)
            fprintf(f, "%s\n", v ? "on" : "off");
        else if (table[i].type == T_PRESET)
            fprintf(f, "%s\n", v == PRESET_ENHANCED ? "enhanced" : "classic");
        else
            fprintf(f, "%d\n", v);
    }
}
