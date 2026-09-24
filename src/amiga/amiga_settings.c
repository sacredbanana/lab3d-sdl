/*
 * Amiga specific entries in lab3d.cfg.
 *
 * The screen mode the player picks at startup is remembered here, so the
 * requester can be skipped on later runs (hold down a mouse button, or set
 * askmode back to 1, to get it again).
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"
#include "amiga/amiga_audio.h"

extern ULONG amiga_cfg_modeid;
extern int   amiga_cfg_width, amiga_cfg_height, amiga_cfg_depth;
extern int   amiga_cfg_scale, amiga_cfg_askmode;

/* setup.c reads and writes these through the generic int setting handlers,
   which work on `int`, so the mode id gets its own int mirror. */
int amiga_cfg_modeid_i = -1;

/*
 * Remember the mode the player picked through the requester.
 *
 * The launcher reloads settings.ini when it switches game version, which
 * parses the file straight back over amiga_cfg_*.  The snapshot has to be
 * taken at the moment of choosing, not when the reload finishes, or the file
 * values would already have overwritten it.
 */
int   amiga_mode_locked;
static ULONG locked_modeid;
static int   locked_w, locked_h, locked_d;

void amiga_lock_mode(ULONG modeid, int w, int h, int d) {
    locked_modeid = modeid;
    locked_w = w;
    locked_h = h;
    locked_d = d;
    amiga_mode_locked = 1;
}

void amiga_settings_loaded(void) {
    if (amiga_cfg_audio < 0 || amiga_cfg_audio >= AMIGA_AUDIO_MODES)
        amiga_cfg_audio = AMIGA_AUDIO_AUTO;

    if (amiga_mode_locked) {
        amiga_cfg_modeid = locked_modeid;
        amiga_cfg_width  = locked_w;
        amiga_cfg_height = locked_h;
        amiga_cfg_depth  = locked_d;
        return;
    }

    if (amiga_cfg_modeid_i == -1)
        amiga_cfg_modeid = INVALID_ID;
    else
        amiga_cfg_modeid = (ULONG)amiga_cfg_modeid_i;
}

void amiga_settings_saving(void) {
    amiga_cfg_modeid_i = (amiga_cfg_modeid == INVALID_ID)
                         ? -1 : (int)amiga_cfg_modeid;
}
