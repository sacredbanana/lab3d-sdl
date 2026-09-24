#ifndef LAB3D_AMIGA_VIDEO_H
#define LAB3D_AMIGA_VIDEO_H

/* Shared between the Amiga video, input and renderer modules. */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>

/* The game always renders into a 360x240 chunky buffer, which is then
   centred (and optionally pixel-doubled) in whatever screen mode the player
   picked.  Keeping the internal resolution fixed keeps every coordinate in
   the shared game code valid and makes the frame cost predictable. */
#define AMIGA_VIEW_W  360
#define AMIGA_VIEW_H  240

/* The 320x200 area inside that buffer which the original DOS game used, at
   (20,20).  Everything the game has to show fits in the full 240 rows but
   only in the middle 320 columns, so a screen narrower than the view is
   cropped to this window while a shorter one is grown into the borders. */
#define AMIGA_CROP_W  320
#define AMIGA_CROP_H  200

typedef struct {
    ULONG  modeid;            /* display mode chosen by the player       */
    int    width, height;     /* screen dimensions                       */
    int    depth;             /* bitplanes, or bits per pixel on RTG     */
    int    rtg;               /* non-zero when this is a CyberGraphX mode */
    int    pixfmt;            /* PIXFMT_#? for RTG modes                 */
    int    scale;             /* 1 or 2: integer pixel doubling          */
    int    srcx, srcy;        /* top-left of the region we display       */
    int    srcw, srch;        /* size of the region we display           */
    int    destx, desty;      /* where it lands on screen                */
} amiga_videomode;

extern amiga_videomode amiga_mode;
extern struct Screen  *amiga_screen;
extern struct Window  *amiga_window;

/* The 8 bit chunky frame the game draws into (AMIGA_VIEW_W stride). */
extern UBYTE *amiga_chunky;

/* Bottom edge, in view coordinates, of the part of the frame that reaches the
   display.  The status bar hangs off this rather than off AMIGA_VIEW_H so it
   stays on screen when the view has to be cropped. */
int amiga_view_bottom(void);

/* Ask the player for a screen mode; returns 0 if they cancelled. */
int  amiga_select_screenmode(amiga_videomode *out);

/* Push amiga_chunky to the display and flip. */
void amiga_blit_frame(void);

/* Load the 256 entry palette (RGB 0..63 triples, 768 bytes) scaled by the
   current fade factors. */
void amiga_set_palette(const unsigned char *pal);

/* Load only part of the palette; used by the animated text colours, which
   are rewritten many times a second. */
void amiga_load_palette(const unsigned char *pal, int start, int count);

/* Re-send the current palette after the fade factors changed. */
void amiga_refresh_palette(void);

int  amiga_video_open(void);
void amiga_video_close(void);

#endif /* LAB3D_AMIGA_VIDEO_H */
