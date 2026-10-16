/* Front-end side of the platform layer, used by main.c only. */
#ifndef PTOMB_PLATFORM_SDL_H
#define PTOMB_PLATFORM_SDL_H

#include <stddef.h>

int plat_init(int palette, int bright, char *err, size_t errlen);
void plat_shutdown(void);

/* PgUp / PgDn since the last call: -1, 0 or +1. */
int plat_take_level_step(void);

void plat_present_now(void);

/* Testing aids: capture all audio to a WAV file (call before plat_init). */
void plat_record_audio(const char *path);

/* Scripted input for testing, see platform.c. */
void plat_set_script(const char *script);

#endif
