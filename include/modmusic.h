#ifndef LAB3D_MODMUSIC_H
#define LAB3D_MODMUSIC_H
#include <stdint.h>
/* Small integer player for the shipped sampled ProTracker modules.
 * Supported effects: Bxx, Cxx, Dxx, Fxx; unsupported modules fail closed.
 * Call under the game's sound lock, or while the audio device is stopped. */
int modmusic_load(const char *path);
void modmusic_free(void);
void modmusic_start(void);
void modmusic_render(int16_t *output, int frames, int rate, int channels, int volume);
#endif
