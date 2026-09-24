#ifndef LAB3D_AMIGA_AUDIO_H
#define LAB3D_AMIGA_AUDIO_H

/*
 * Sound output selection for the Amiga port.  Kept free of system headers so
 * the shared setup and settings code can include it.
 */

#define AMIGA_AUDIO_AUTO   0    /* AHI on an 040 or better, Paula below that */
#define AMIGA_AUDIO_PAULA  1    /* audio.device, 8 bit                       */
#define AMIGA_AUDIO_AHI    2    /* ahi.device, 16 bit                        */

#define AMIGA_AUDIO_MODES  3

extern int amiga_cfg_audio;

/* Called from PL_PumpClock() to keep the output buffers fed. */
void amiga_audio_service(void);
void amiga_audio_close(void);

#endif /* LAB3D_AMIGA_AUDIO_H */
