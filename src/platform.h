/* What the game code needs from the machine: the keyboard, the PC speaker, the timer-based
 * Delay and the screen. main.c implements it with SDL2.
 *
 * The original is single-threaded and blocks in places (Delay loops in animations, ReadKey in
 * prompts). The port keeps that structure: every blocking call keeps pumping SDL events, the
 * timer and the display through plat_poll(), so the window stays live. */
#ifndef PTOMB_PLATFORM_H
#define PTOMB_PLATFORM_H

#include <stdbool.h>

/* Keyboard ISR state (1000:0000): arrow keys held, tracked from make/break codes. */
typedef struct {
    bool left;   /* [7975], scancode 4Bh */
    bool right;  /* [7976], scancode 4Dh */
} isr_keys_t;

/* INT 16h AH=02h shift flags, as ReadShiftKeys (1000:4AF1) copies them. */
typedef struct {
    bool ctrl, alt, lshift, rshift, scroll_lock; /* [7977] [7978] [7979] [797A] [797B] */
} shift_keys_t;

extern isr_keys_t isr_keys;

/* Handle window events, run the 18.2 Hz timer, and redraw the window if a frame is due. */
void plat_poll(void);

/* True once the user has closed the window; blocking calls return early after that. */
bool plat_quitting(void);

/* Crt KeyPressed / ReadKey. ReadKey returns 0 for an extended key, then its scancode, as the
 * BIOS does. ReadKey blocks (keeping the window alive) and returns 27 if the window closes. */
bool plat_key_pressed(void);
int plat_read_key(void);

shift_keys_t plat_shift_keys(void);

/* 1854:0000 Delay(n): zero timer counter [7F74], then wait until it reaches n. */
void plat_delay(int ticks);

/* Crt Sound / NoSound: PC speaker square wave. */
void plat_sound(int hz);
void plat_nosound(void);

/* Status text. The status bar is drawn in the BIOS font, which the port can't ship, so for
 * now it goes in the window title. */
void plat_status(const char *text);
void plat_message(const char *text); /* prompt line (row 25), "" clears it */

#endif
