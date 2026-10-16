/* Front-end side of the platform layer, used by main.c only. */
#ifndef PTOMB_PLATFORM_SDL_H
#define PTOMB_PLATFORM_SDL_H

#include <stddef.h>

/* Opens the window and audio; the palette and aspect come from settings.h. */
int plat_init(char *err, size_t errlen);
void plat_shutdown(void);

void plat_present_now(void);

/* Testing aids: capture all audio to a WAV file (call before plat_init). */
void plat_record_audio(const char *path);

/* Put a key in the BIOS keyboard buffer (gamepad.c's buttons that type keys). */
void plat_push_key(int c);

/* Scripted input for testing, see platform.c. */
void plat_set_script(const char *script);

#endif
