#ifndef LAB3D_AMIGA_VIDEO_H
#define LAB3D_AMIGA_VIDEO_H

/* Shared between the Amiga video, input and renderer modules. */

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/screens.h>
#include <graphics/gfxbase.h>

/* The game's own coordinate space: every overlay, menu and 2D sprite is laid
   out in a 360x240 "virtual screen", the resolution the DOS original used on
   a 4:3 monitor.  The renderer maps it onto a chunky buffer of whatever size
   the layout picks, and the display module scales that buffer up by a whole
   number to fill the screen. */
#define AMIGA_VIEW_W  360
#define AMIGA_VIEW_H  240

/* The 320x200 area inside that space which the original DOS game used, at
   (20,20).  Everything the game has to show fits in the full 240 rows but
   only in the middle 320 columns, so a view narrower than 360 units is
   cropped to this window while a shorter one is grown into the borders. */
#define AMIGA_CROP_W  320
#define AMIGA_CROP_H  200

/* Largest render buffer.  The wall rasteriser carries screen rows in 16.16
   fixed point, and the top of a wall at the near plane lands about 32 x
   proj_y rows away from the horizon, so the height is what has to be kept in
   check: 1200 rows puts that at 27000, inside the 32767 a 16.16 row holds.
   A bigger screen is still filled; it just renders at half size or less. */
#define AMIGA_RENDER_MAXW  2048
#define AMIGA_RENDER_MAXH  1200

/* amiga_cfg_render: 0 picks a divisor automatically, 1..AMIGA_RENDER_MAXDIV
   renders at screen size / n, and AMIGA_RENDER_UNSCALED is the old
   behaviour - the 360x240 view at 1:1 in the middle of the screen. */
#define AMIGA_RENDER_AUTO      0
#define AMIGA_RENDER_MAXDIV    8
#define AMIGA_RENDER_UNSCALED  (-1)

typedef struct {
    ULONG  modeid;            /* display mode chosen by the player       */
    int    width, height;     /* screen dimensions                       */
    int    depth;             /* bitplanes, or bits per pixel on RTG     */
    int    rtg;               /* non-zero when this is a CyberGraphX mode */
    int    pixfmt;            /* PIXFMT_#? for RTG modes                 */
    double pixaspect;         /* width / height of one screen pixel       */

    int    divisor;           /* screen pixels per render pixel, each way */
    int    vieww, viewh;      /* render buffer (amiga_chunky) size        */
    int    destx, desty;      /* where the scaled-up buffer lands         */

    /* Game units to render pixels: pixel = unit * ppu + org.  ppux and ppuy
       differ whenever the view keeps the original 4:3 shape on a screen of
       some other shape, because a 360x240 unit is not square. */
    double ppux, ppuy;
    int    orgx, orgy;
    int    aspectok;          /* 0 when the view fell back to 1 unit = 1 px */
} amiga_videomode;

extern amiga_videomode amiga_mode;
extern struct Screen  *amiga_screen;
extern struct Window  *amiga_window;

/* The 8 bit chunky frame the game draws into, amiga_mode.vieww by
   amiga_mode.viewh with no padding between rows. */
extern UBYTE *amiga_chunky;

/* Work out the render size for the current settings and, if the display is
   open, reallocate the frame to match so the change shows at once.  Returns
   0 on success; on failure the previous layout stays in force. */
int  amiga_video_relayout(void);

/* Render buffer size a given amiga_cfg_render value would produce on the
   open screen, for the setup menu. */
void amiga_render_size(int setting, int *w, int *h);

/* Copy the layout into the shared code's globals (screenwidth, aspw, ...). */
void amiga_apply_view(void);

/* View size, as a percentage of the frame: the 3D view is drawn in a box
   that size in the middle of the screen, with a black border round it, so a
   slow machine renders fewer pixels.  The menus and the status bar keep
   their full size.  One of the steps below; 100 is the whole frame. */
extern int amiga_cfg_viewsize;
#define AMIGA_VIEWSIZE_MIN   40
#define AMIGA_VIEWSIZE_STEP  10

/* Dirty rectangles, in render buffer pixels.  With the view shrunk only what
   changed is sent to the screen, so the static border costs nothing - which
   matters most on a native screen, where every pixel sent costs a planar
   conversion.  `overlay` marks writes that are not the 3D view: those have
   to be scrubbed out of the border again next frame. */
void amiga_mark_dirty(int x0, int y0, int x1, int y1, int overlay);
void amiga_mark_all_dirty(void);

/* Black out last frame's overlay writes; called before the view is drawn. */
void amiga_clear_leftovers(void);

/* Blank the whole frame and send all of it next time, e.g. after the view
   size changed and the old picture is still sitting in the new border. */
void amiga_video_invalidate(void);

/* A menu waiting for a selection only animates the selector icon, so rather
   than redraw every open menu (and the labyrinth behind them) each frame, the
   menu loop draws it all once, holds a copy of the overlay rectangle the
   selector moves in - and the part of the frame it lands on - and puts just
   that back before each new frame of the icon.  Returns 0 if there was no
   memory for the copy, in which case the caller keeps redrawing everything.
   Implemented in render_soft.c. */
int  amiga_hold_menu(int x, int y, int w, int h);
void amiga_restore_menu(void);
void amiga_release_menu(void);

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
