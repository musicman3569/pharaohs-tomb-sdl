/* The PC joystick (unit 20E3, game port 201h), stood in for by modern game controllers.
 *
 * The original reads an analog joystick: two axes and two buttons. The port emulates that with
 * SDL's game-controller layer, which recognises most current pads (Xbox, PlayStation, Switch,
 * 8BitDo, ...) and handles plugging in and out at any time. Any other joystick SDL can see is
 * used as-is: axes 0/1, hat 0 and buttons 0/1, the original's own layout. A
 * gamecontrollerdb.txt next to the port's EXE adds more controller mappings.
 *
 * On a controller:
 *   left stick or d-pad  the joystick (only X matters in game: walk)
 *   A (south)            button 1: jump
 *   B / X (east / west)  button 2: throw a spear
 * and, as a port extra so the game can be played without a keyboard, some buttons also type
 * keys, depending on what the game is doing (pad_set_mode):
 *   menus and "press any key":  A, Start -> Enter    B -> Esc
 *                               d-pad -> arrow keys (with menu.navigation on)
 *   in game:                    Start -> P (pause)   Back -> Esc (quit prompt)
 *   QUIT (Y/N)?:                A, Start -> Y        B, Back -> N
 */
#ifndef PTOMB_GAMEPAD_H
#define PTOMB_GAMEPAD_H

#include <stdbool.h>
#include <stdint.h>

/* The game's joystick variables. */
typedef struct {
    uint8_t enabled;   /* [7995] joystick on (J menu, option 1) */
    int center_x;      /* [75B8] calibrated centre (startup / J menu, option 2) */
    int center_y;      /* [75BA] */
} joy_vars_t;

extern joy_vars_t joy_vars;

/* The range joy_read_axis returns: like a PC joystick's counts, 0 .. 2 * JOY_CENTER. The game
 * walks when X is more than 50 from the calibrated centre, about half-way over. */
#define JOY_CENTER 100

/* Joy_Detect (20E3:0000): a controller is connected. Unlike the original, which looked once
 * at startup, this follows controllers being plugged in and out. */
bool joy_detect(void);
/* Joy_Button1 / Joy_Button2 (20E3:0040 / 0068): n = 1 or 2 is held down. */
bool joy_button(int n);
/* Joy_ReadAxis(n) (20E3:00E4): n = 0 for X, 1 for Y. */
int joy_read_axis(int n);

typedef enum { PAD_MENU, PAD_GAME, PAD_YESNO } pad_mode_t;

/* Which keys the controller's buttons type (see above). Returns the previous mode. */
pad_mode_t pad_set_mode(pad_mode_t mode);
pad_mode_t pad_get_mode(void);

/* Platform side (platform.c). */
union SDL_Event;
void pad_init(void);
void pad_event(const union SDL_Event *ev);
/* --script: "down pad_a" and so on drive a virtual controller. Returns false if `name` isn't a
 * pad control (pad_a pad_b pad_x pad_start pad_back pad_left pad_right pad_up pad_down). */
bool pad_script(const char *name, bool down);

#endif
