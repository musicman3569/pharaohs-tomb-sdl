/* A tiny interpreter that runs the game's own text-screen routines from the EXE.
 *
 * The instructions, story, ordering-info, keyboard-setup and joystick screens are straight-line
 * Turbo Pascal code: font selects, DrawTextCentered / DrawTextAt calls with constant strings, a
 * few comparisons on the volume number or a key, and waits for a key. Rather than copy their
 * text into the port, the port executes that code: it understands the ~30 instruction forms
 * those routines use and routes every call to the port's own text, font, keyboard and frame
 * functions. Anything it doesn't recognise stops the screen with an error instead of guessing. */
#ifndef PTOMB_SCREENVM_H
#define PTOMB_SCREENVM_H

#include <stddef.h>
#include <stdint.h>

#include "gamedata.h"

/* Variables in the game's data segment that these screens read or write. The joystick ones
 * ([7995] [7996] [75B8] [75BA]) are joy_vars and joy_detect() in gamepad.h. */
typedef struct {
    uint8_t key_layout;        /* [8101] PTOMB.CFG byte */
    int config_changed;        /* set when the screen called SaveConfig (1854:0FB7) */
} svm_vars_t;

/* Run the routine at image offset `entry` in code segment `cs`, as if called with `args`
 * pushed in that order (Pascal parameter order). Returns 0, or -1 with a message in err. */
int svm_run(const gamedata_t *g, size_t entry, unsigned cs, const uint16_t *args, int nargs,
            svm_vars_t *vars, char *err, size_t errlen);

/* Routines in the game unit (offsets from its segment, the same in all four volumes). */
enum {
    GAME_SHOW_INSTRUCTIONS = 0x2A20, /* () */
    GAME_SHOW_STORY = 0x3A03,        /* (volume) */
    GAME_SHOW_ORDERING = 0x1E44,     /* (volume, quitting: byte) */
    GAME_CONFIG_KEYBOARD = 0x69D3,   /* () */
    GAME_VOLUME_ENDING = 0x4E01,     /* (volume) */
};

#endif
