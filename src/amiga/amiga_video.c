/*
 * AmigaOS display back end for LAB3D.
 *
 * The shared code lays everything out in a 360x240 "virtual screen", but the
 * renderer draws into a chunky buffer sized to the screen mode the player
 * picked: the screen's own resolution divided by a whole number, so that
 * scaling it back up fills the screen with every pixel the same size.  On a
 * 1920x1080 screen that might be 480x270 scaled by four, or the full
 * 1920x1080 on a fast enough machine.  amiga_layout() decides the size and
 * how the 360x240 space maps onto it; amiga_blit_frame() does the scaling.
 *
 * Getting the frame onto the screen takes one of two paths:
 *
 *   RTG      cybergraphics.library.  An 8 bit LUT8 screen takes the chunky
 *            data verbatim; deeper screens go through WriteLUTPixelArray(),
 *            which expands our palette on the fly, so 15/16/24/32 bit RTG
 *            modes work without the game knowing.
 *
 *   Native   chunky to planar conversion into the screen bitmap.  AGA's 8
 *            bitplane modes give the full 256 colour palette; on ECS/OCS a
 *            shallower screen still works, with the palette folded down to
 *            the available number of pens.
 *
 * Either way the screen is double buffered with AllocScreenBuffer() when the
 * display allows it, so there is no tearing and no flicker.
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"
#include "amiga/amiga_c2p.h"

#include <math.h>

/* ------------------------------------------------------------------ state */

amiga_videomode amiga_mode;
struct Screen  *amiga_screen;
struct Window  *amiga_window;
UBYTE          *amiga_chunky;

struct Library *CyberGfxBase;
struct Library *AslBase;

static struct ScreenBuffer *sbuf[2];
static struct MsgPort      *dispport, *safeport;
static int                  sbcurrent;     /* the one we draw into */
static int                  frontbuf;      /* the one Intuition is showing */
static int                  safe_to_write = 1, safe_to_change = 1;
static int                  doublebuffered;

/* Sprite data for the blanked mouse pointer.  Intuition does not copy this -
   it becomes the sprite DMA source - so it has to live in chip RAM. */
static UWORD               *blankpointer;

/* Staging area for the scaled-up frame: a band of whole source rows at a
   time rather than the whole screen, which at 1920x1080 would be another two
   megabytes of fast RAM for no gain. */
#define BAND_BYTES 65536
static UBYTE   *bandbuf;
static int      bandrows;       /* source rows per band */

/* Dirty rectangles; see the section of that name further down. */
#define MAXRECTS 32

typedef struct { int x0, y0, x1, y1; } arect;
typedef struct { int n, all; arect r[MAXRECTS]; } rectlist;

static rectlist dirty_now, dirty_last;  /* everything that changed      */
static rectlist ovl_now, ovl_last;      /* the non-3D part of it        */

/* Colour table for WriteLUTPixelArray() on deep RTG screens. */
static ULONG    lut[256];

/* Defined further down; amiga_video_open() needs it before its definition. */
void amiga_build_penmap(void);

/* Maps a 256 colour game index onto a pen when the screen has fewer than
   256 of them. */
/* Screens with fewer than 256 pens need two maps: penmap[] turns a game
   colour index into a pen, and pensrc[] names, for each pen, the colour index
   whose RGB that pen should take. */
static UBYTE    penmap[256];
static UBYTE    pensrc[256];
static int      numpens;

/* Settings, saved in lab3d.cfg so the choice is remembered. */
ULONG amiga_cfg_modeid = INVALID_ID;
int   amiga_cfg_width, amiga_cfg_height, amiga_cfg_depth;
int   amiga_cfg_render = AMIGA_RENDER_AUTO;   /* see amiga_video.h */
int   amiga_cfg_viewsize = 100;
int   amiga_cfg_askmode = 1;    /* show the screen mode requester at startup */

/* ------------------------------------------------------------ mode picking */

/* Display clip for native screens that reach into the borders; see
   amiga_fit_overscan(). */
static struct Rectangle amiga_dclip;
static int              amiga_useclip;

/* Physical shape of one game unit.  The DOS game showed its 360x240 screen
   on a 4:3 monitor, so a unit is 8/9 as wide as it is tall. */
#define UNIT_ASPECT (8.0 / 9.0)

/* Snap a scale that is within rounding of 1 onto it, so that the 1:1 cases
   take the straight copy paths rather than a lookup that happens to be an
   identity. */
static double amiga_snap(double v) {
    return (v > 0.999 && v < 1.001) ? 1.0 : v;
}

/*
 * Map the 360x240 game space onto a w x h render buffer.
 *
 * The first choice keeps the original 4:3 shape of that space: scale it as
 * large as fits and let the view grow sideways (or downwards) to fill the
 * rest, which is what the OpenGL build's default "fill screen" view does.
 * That needs at least one pixel per unit each way, or the menus and the
 * status bar would lose rows and columns.  When the buffer is too small for
 * that, fall back to one pixel per unit, centred, which is how the port has
 * always handled small screens: a 320 wide screen then shows exactly the
 * x = 20..340 window the original game drew into, and only the decorative
 * margins either side go.
 */
static void amiga_fit_view(amiga_videomode *m, int w, int h, int force11) {
    double fit = w / (double)AMIGA_VIEW_W;
    double fith = h * UNIT_ASPECT / (AMIGA_VIEW_H * m->pixaspect);

    if (fith < fit) fit = fith;

    m->vieww = w;
    m->viewh = h;
    m->ppux  = amiga_snap(fit);
    m->ppuy  = amiga_snap(fit * m->pixaspect / UNIT_ASPECT);
    m->aspectok = !force11 && m->ppux >= 1.0 && m->ppuy >= 1.0;

    if (!m->aspectok)
        m->ppux = m->ppuy = 1.0;

    /* Whole pixels, so that at 1:1 unit and pixel boundaries coincide. */
    m->orgx = (int)floor(w * 0.5 - AMIGA_VIEW_W * 0.5 * m->ppux + 0.5);
    m->orgy = (int)floor(h * 0.5 - AMIGA_VIEW_H * 0.5 * m->ppuy + 0.5);
}

/* Render buffer for divisor n, before the unit mapping is chosen.  A native
   screen needs the scaled width in whole bytes for the planar conversion. */
static void amiga_view_size(const amiga_videomode *m, int n, int *w, int *h) {
    *w = m->width  / n;
    *h = m->height / n;
    if (!m->rtg)
        while (*w > 8 && ((*w * n) & 7))
            (*w)--;
}

/*
 * Lay out a trial view at divisor n into *out.  Returns 0 if n cannot be used
 * on this screen at all: the buffer would be under the 320x200 the game
 * needs, or over the renderer's size limit.
 */
static int amiga_trial_layout(const amiga_videomode *m, int n,
                              amiga_videomode *out) {
    int w, h;

    *out = *m;
    amiga_view_size(m, n, &w, &h);
    if (w < AMIGA_CROP_W || h < AMIGA_CROP_H)
        return 0;
    if (w > AMIGA_RENDER_MAXW || h > AMIGA_RENDER_MAXH)
        return 0;

    out->divisor = n;
    amiga_fit_view(out, w, h, 0);
    return 1;
}

/* Whether a trial layout shows all 240 rows of the game space and at least
   the 320 columns in the middle - everything the game ever draws. */
static int amiga_shows_everything(const amiga_videomode *t) {
    return t->aspectok ||
           (t->vieww >= AMIGA_CROP_W && t->viewh >= AMIGA_VIEW_H);
}

/* Most screen pixels Automatic will fill on a native screen: a full PAL
   lores screen with a little to spare.  Past that, filling the screen costs
   more planar conversion than the port ever needed before. */
#define AMIGA_NATIVE_BUDGET (AMIGA_VIEW_W * 256L)

/*
 * What "Automatic" picks: a divisor, or AMIGA_RENDER_UNSCALED.
 *
 * This is a trade between speed and detail.  Every pixel of the render
 * buffer costs the renderer the same whatever the scale, so a bigger divisor
 * is faster; but past a point the buffer is too small to show the game
 * space in its proper shape, or at all.
 *
 * A native screen has a second cost that scaling does nothing for.  The
 * planar conversion runs once per pixel that reaches the screen, so doubling
 * a 320x256 render onto a 640x512 screen still quadruples that part of the
 * frame - and on an AGA machine it is the larger part.  There, a screen too
 * big to fill within the budget gets the 360x240 view unscaled, which is
 * what the port always did; the player can still ask for a fill by hand.
 * RTG has no such cost, because the card's memory takes chunky pixels
 * directly.
 */
static int amiga_auto_setting(const amiga_videomode *m) {
    amiga_videomode t;
    int n;

    for (n = AMIGA_RENDER_MAXDIV; n > 1; n--)
        if (amiga_trial_layout(m, n, &t) && amiga_shows_everything(&t))
            break;

    if (!m->rtg && (long)m->width * m->height > AMIGA_NATIVE_BUDGET)
        return AMIGA_RENDER_UNSCALED;
    return n;
}

/* Turn a setting into a divisor that works on this screen: a request the
   screen is too small for comes down, one past the size limit goes up. */
static int amiga_resolve_divisor(const amiga_videomode *m, int setting) {
    amiga_videomode t;
    int n = setting;

    if (n < 1) n = 1;
    if (n > AMIGA_RENDER_MAXDIV) n = AMIGA_RENDER_MAXDIV;

    while (n < AMIGA_RENDER_MAXDIV &&
           (m->width / n > AMIGA_RENDER_MAXW || m->height / n > AMIGA_RENDER_MAXH))
        n++;
    while (n > 1 && !amiga_trial_layout(m, n, &t))
        n--;
    return n;
}

/* Size of the render buffer, how the game space maps onto it, and where the
   scaled-up result sits on the screen. */
static void amiga_layout_for(amiga_videomode *m, int setting) {
    int w, h;

    if (setting == AMIGA_RENDER_AUTO)
        setting = amiga_auto_setting(m);

    if (setting == AMIGA_RENDER_UNSCALED) {
        /* The 360x240 view at 1:1 in the middle, as before any of this. */
        m->divisor = 1;
        w = m->width  < AMIGA_VIEW_W ? m->width  : AMIGA_VIEW_W;
        h = m->height < AMIGA_VIEW_H ? m->height : AMIGA_VIEW_H;
        if (!m->rtg) w &= ~7;
        amiga_fit_view(m, w, h, 1);
    } else {
        m->divisor = amiga_resolve_divisor(m, setting);
        amiga_view_size(m, m->divisor, &w, &h);
        if (w > AMIGA_RENDER_MAXW) w = AMIGA_RENDER_MAXW;
        if (h > AMIGA_RENDER_MAXH) h = AMIGA_RENDER_MAXH;
        amiga_fit_view(m, w, h, 0);
    }

    m->destx = (m->width  - m->vieww * m->divisor) / 2;
    m->desty = (m->height - m->viewh * m->divisor) / 2;

    /* Keep the destination even so the planar path can work in whole bytes. */
    if (!m->rtg)
        m->destx &= ~7;
    if (m->destx < 0) m->destx = 0;
    if (m->desty < 0) m->desty = 0;
}

static void amiga_layout(amiga_videomode *m) {
    amiga_layout_for(m, amiga_cfg_render);
}

void amiga_render_size(int setting, int *w, int *h) {
    amiga_videomode t = amiga_mode;

    if (!amiga_screen) {
        *w = AMIGA_VIEW_W;
        *h = AMIGA_VIEW_H;
        return;
    }
    amiga_layout_for(&t, setting);
    *w = t.vieww;
    *h = t.viewh;
}

/* Hand the layout to the shared code.  virtualscreenwidth/height are the
   size of the view in game units, which the status bar is widened to, and
   aspw tells the ray caster how far either side of the 90 degree frustum the
   view reaches, so walls at the edges of a widescreen view are found. */
void amiga_apply_view(void) {
    const amiga_videomode *m = &amiga_mode;
    double cx, hr, a;

    if (!amiga_screen || !m->vieww) {
        screenwidth  = AMIGA_VIEW_W;
        screenheight = AMIGA_VIEW_H;
        virtualscreenwidth  = AMIGA_VIEW_W;
        virtualscreenheight = AMIGA_VIEW_H;
        aspw = asph = 1.0;
        return;
    }

    screenwidth  = m->vieww;
    screenheight = m->viewh;
    virtualscreenwidth  = (float)(m->vieww / m->ppux);
    virtualscreenheight = (float)(m->viewh / m->ppuy);

    cx = AMIGA_VIEW_W * 0.5 * m->ppux + m->orgx;
    a  = (cx > m->vieww - cx ? cx : m->vieww - cx) / (AMIGA_VIEW_W * 0.5 * m->ppux);
    aspw = a > 1.0 ? a : 1.0;

    hr = AMIGA_VIEW_H * 0.5 * m->ppuy + m->orgy;
    a  = (hr > m->viewh - hr ? hr : m->viewh - hr) / (AMIGA_VIEW_H * 0.5 * m->ppuy);
    asph = a > 1.0 ? a : 1.0;
}

/*
 * Grow a native screen into the borders so the whole view fits.
 *
 * A display mode is not limited to its nominal size: the hardware will show
 * a good deal more than that, and how much is in the mode's DimensionInfo.
 * Both NTSC and PAL lores reach well past 240 rows, so the whole height of
 * the view fits on either - which matters, because unlike the columns at the
 * edges the rows are all used.  The launcher puts its title on row 16 and its
 * footer on row 220, and the status bar sits on the last rows of the frame,
 * so a plain 200 line screen cuts the top off the title and loses the rest
 * entirely.
 *
 * Asking for the extra rows is only half of it.  Intuition takes the display
 * clip from the Overscan preferences, which are usually no bigger than the
 * nominal size, and anything outside that clip is simply not displayed - so a
 * matching clip, centred in the mode's maximum overscan area, goes in with
 * the screen.
 */
static void amiga_fit_overscan(amiga_videomode *m) {
    struct DimensionInfo dims;
    int maxw, maxh, txtw, txth, clipw, cliph, offx, offy;

    amiga_useclip = 0;

    /* RTG screens have no borders to reach into; their size is their size. */
    if (m->rtg)
        return;

    if (GetDisplayInfoData(NULL, (UBYTE *)&dims, sizeof(dims),
                           DTAG_DIMS, m->modeid) <= 0)
        return;

    maxw = dims.MaxOScan.MaxX - dims.MaxOScan.MinX + 1;
    maxh = dims.MaxOScan.MaxY - dims.MaxOScan.MinY + 1;
    txtw = dims.TxtOScan.MaxX - dims.TxtOScan.MinX + 1;
    txth = dims.TxtOScan.MaxY - dims.TxtOScan.MinY + 1;

    if (maxw <= 0 || maxh <= 0)
        return;

    /* Only ever grow, and only as far as the view actually needs.  The width
       target is the 320 pixels the game draws its own picture into rather
       than the full 360, so we do not spend chip RAM and c2p time on margins
       that hold nothing. */
    if (m->width < AMIGA_CROP_W && maxw > m->width)
        m->width = maxw < AMIGA_CROP_W ? maxw : AMIGA_CROP_W;
    if (m->height < AMIGA_VIEW_H && maxh > m->height)
        m->height = maxh < AMIGA_VIEW_H ? maxh : AMIGA_VIEW_H;

    /* Whatever the screen ended up as, it needs a clip of its own as soon as
       it is bigger than the one Intuition would have picked. */
    if (txtw > 0 && txth > 0 && m->width <= txtw && m->height <= txth)
        return;

    clipw = m->width  < maxw ? m->width  : maxw;
    cliph = m->height < maxh ? m->height : maxh;

    /* 16 pixel granularity on the left edge: that is the step the display
       hardware fetches in, and an unaligned clip only gets rounded anyway. */
    offx = ((maxw - clipw) / 2) & ~15;
    offy =  (maxh - cliph) / 2;

    amiga_dclip.MinX = (WORD)(dims.MaxOScan.MinX + offx);
    amiga_dclip.MinY = (WORD)(dims.MaxOScan.MinY + offy);
    amiga_dclip.MaxX = (WORD)(amiga_dclip.MinX + clipw - 1);
    amiga_dclip.MaxY = (WORD)(amiga_dclip.MinY + cliph - 1);
    amiga_useclip = 1;
}

static void amiga_describe(const amiga_videomode *m) {
    fprintf(stderr, "Screen mode 0x%08lx: %dx%d, %d bit%s, %s\n",
            (unsigned long)m->modeid, m->width, m->height, m->depth,
            m->rtg ? "" : "planes", m->rtg ? "RTG" : "native");
    fprintf(stderr, "Rendering at %dx%d, scaled %dx to (%d,%d); "
                    "%.3f x %.3f pixels per unit%s.\n",
            m->vieww, m->viewh, m->divisor, m->destx, m->desty,
            m->ppux, m->ppuy, m->aspectok ? "" : " (1:1, 4:3 shape not kept)");
    if (amiga_useclip)
        fprintf(stderr, "Using the borders: display clip (%d,%d)-(%d,%d).\n",
                amiga_dclip.MinX, amiga_dclip.MinY,
                amiga_dclip.MaxX, amiga_dclip.MaxY);
    if (!m->rtg)
        fprintf(stderr, "Chip RAM free: %lu bytes (largest block %lu).\n",
                (unsigned long)AvailMem(MEMF_CHIP),
                (unsigned long)AvailMem(MEMF_CHIP | MEMF_LARGEST));
}

/*
 * Width over height of one pixel of a native mode.  The display database
 * gives it as ticks per pixel: PAL lores is about square, hires half as wide
 * as it is tall, NTSC a little narrower than PAL.  RTG modes are taken to be
 * square, since whatever they report says nothing about the monitor they end
 * up on and a modern one shows them square.
 */
static double amiga_pixel_aspect(const amiga_videomode *m) {
    struct DisplayInfo di;
    double a;

    if (m->rtg)
        return 1.0;
    if (GetDisplayInfoData(NULL, (UBYTE *)&di, sizeof(di),
                           DTAG_DISP, m->modeid) <= 0)
        return 1.0;
    if (di.Resolution.x <= 0 || di.Resolution.y <= 0)
        return 1.0;

    a = (double)di.Resolution.x / (double)di.Resolution.y;
    return (a >= 0.25 && a <= 4.0) ? a : 1.0;
}

/* Fill in the RTG/depth details of a mode id the player chose. */
static void amiga_probe(amiga_videomode *m) {
    m->rtg = 0;
    m->pixfmt = PIXFMT_LUT8;

    if (CyberGfxBase && IsCyberModeID(m->modeid)) {
        m->rtg = 1;
        m->pixfmt = (int)GetCyberIDAttr(CYBRIDATTR_PIXFMT, m->modeid);
        m->depth  = (int)GetCyberIDAttr(CYBRIDATTR_DEPTH, m->modeid);
    } else {
        /* Native screens are planar and AGA stops at eight bitplanes.  The
           requester will happily hand back a deeper value for a mode it
           thinks is promotable; asking Intuition for it just fails. */
        if (m->depth > 8) m->depth = 8;
        if (m->depth < 1) m->depth = 1;
    }
    m->pixaspect = amiga_pixel_aspect(m);
    amiga_fit_overscan(m);
    amiga_layout(m);
}

int amiga_select_screenmode(amiga_videomode *out) {
    struct ScreenModeRequester *req;
    int ok = 0;

    req = (struct ScreenModeRequester *)
          AllocAslRequestTags(ASL_ScreenModeRequest,
                              ASLSM_TitleText,      (ULONG)"Ken's Labyrinth - select a screen mode",
                              ASLSM_InitialDisplayID, amiga_cfg_modeid != INVALID_ID
                                                      ? amiga_cfg_modeid : 0,
                              ASLSM_InitialDisplayWidth,  amiga_cfg_width  ? amiga_cfg_width  : AMIGA_VIEW_W,
                              ASLSM_InitialDisplayHeight, amiga_cfg_height ? amiga_cfg_height : AMIGA_VIEW_H,
                              ASLSM_InitialDisplayDepth,  amiga_cfg_depth  ? amiga_cfg_depth  : 8,
                              ASLSM_DoWidth,        TRUE,
                              ASLSM_DoHeight,       TRUE,
                              ASLSM_DoDepth,        TRUE,
                              ASLSM_MinWidth,       AMIGA_CROP_W,
                              ASLSM_MinHeight,      AMIGA_CROP_H,
                              ASLSM_MinDepth,       4,
                              ASLSM_MaxDepth,       32,
                              TAG_END);
    if (!req) {
        fprintf(stderr, "Could not open the screen mode requester.\n");
        return 0;
    }

    if (AslRequestTags(req, TAG_END)) {
        out->modeid = req->sm_DisplayID;
        out->width  = (int)req->sm_DisplayWidth;
        out->height = (int)req->sm_DisplayHeight;
        out->depth  = req->sm_DisplayDepth;
        ok = 1;
    }

    FreeAslRequest(req);
    return ok;
}

/* ---------------------------------------------------------- screen opening */

/*
 * Take one message off a double buffering port.
 *
 * WaitPort() only peeks: it returns as soon as the port is non-empty and
 * leaves the message queued, so waiting twice without a GetMsg() in between
 * is satisfied by the same stale message and we carry on while the display
 * is still reading the buffer.
 */
static void amiga_wait_msg(struct MsgPort *port) {
    if (!port) return;
    while (!GetMsg(port))
        WaitPort(port);
}

/* Block until the last ChangeScreenBuffer() has been through both stages. */
static void amiga_sync_buffers(void) {
    if (!doublebuffered) return;

    if (!safe_to_change) { amiga_wait_msg(dispport); safe_to_change = 1; }
    if (!safe_to_write)  { amiga_wait_msg(safeport); safe_to_write  = 1; }
}

static void amiga_free_buffers(void) {
    int i;

    amiga_c2p_sync();

    if (doublebuffered) {
        /* Let any pending flip finish before we pull the buffers away. */
        amiga_sync_buffers();

        /*
         * CloseScreen() frees whatever bitmap the screen is displaying, and
         * that has to be the screen's own one.  If we leave sbuf[1] on show,
         * CloseScreen() frees the bitmap FreeScreenBuffer() has already
         * released and the screen's real bitplanes are never given back -
         * on a native screen that is hundreds of KB of chip RAM gone until
         * the next reboot, which is why OpenScreen() starts failing for
         * everything (this game, ScreenMode's Test gadget) after a few runs.
         */
        if (frontbuf != 0 && amiga_screen && sbuf[0]) {
            if (ChangeScreenBuffer(amiga_screen, sbuf[0])) {
                frontbuf       = 0;
                safe_to_change = 0;
                safe_to_write  = 0;
                amiga_sync_buffers();
            } else {
                fprintf(stderr, "Warning: could not restore the original "
                                "screen buffer before closing.\n");
            }
        }
    }

    for (i = 0; i < 2; i++) {
        if (sbuf[i]) {
            if (amiga_screen)
                FreeScreenBuffer(amiga_screen, sbuf[i]);
            sbuf[i] = NULL;
        }
    }
    if (dispport) { DeleteMsgPort(dispport); dispport = NULL; }
    if (safeport) { DeleteMsgPort(safeport); safeport = NULL; }
    doublebuffered = 0;
    frontbuf = 0;
}

static void amiga_setup_buffers(void) {
    int i;

    sbuf[0] = AllocScreenBuffer(amiga_screen, NULL, SB_SCREEN_BITMAP);
    sbuf[1] = AllocScreenBuffer(amiga_screen, NULL, 0);
    dispport = CreateMsgPort();
    safeport = CreateMsgPort();

    if (!sbuf[0] || !sbuf[1] || !dispport || !safeport) {
        fprintf(stderr, "Double buffering unavailable; drawing single buffered.\n");
        amiga_free_buffers();
        return;
    }

    for (i = 0; i < 2; i++) {
        sbuf[i]->sb_DBufInfo->dbi_SafeMessage.mn_ReplyPort = safeport;
        sbuf[i]->sb_DBufInfo->dbi_DispMessage.mn_ReplyPort = dispport;
    }

    /* sbuf[0] wraps the screen's own bitmap and is what Intuition is already
       showing, so the first frame belongs in sbuf[1]. */
    frontbuf  = 0;
    sbcurrent = 1;
    safe_to_write = safe_to_change = 1;
    doublebuffered = 1;
}

static struct BitMap *amiga_drawbitmap(void) {
    if (doublebuffered)
        return sbuf[sbcurrent]->sb_BitMap;
    return amiga_screen->RastPort.BitMap;
}

/* Allocate the render buffer and, if the layout scales it, the band the
   scaled rows are built in.  Frees nothing: the caller owns what was there
   before. */
static int amiga_alloc_frame(const amiga_videomode *m) {
    amiga_chunky = AllocVec((ULONG)m->vieww * m->viewh, MEMF_ANY | MEMF_CLEAR);
    if (!amiga_chunky)
        return -1;

    bandbuf  = NULL;
    bandrows = 0;
    if (m->divisor > 1) {
        ULONG rowbytes = (ULONG)m->vieww * m->divisor * m->divisor;

        bandrows = (int)(BAND_BYTES / rowbytes);
        if (bandrows < 1) bandrows = 1;
        if (bandrows > m->viewh) bandrows = m->viewh;

        bandbuf = AllocVec(rowbytes * bandrows, MEMF_ANY);
        if (!bandbuf) {
            FreeVec(amiga_chunky);
            amiga_chunky = NULL;
            return -1;
        }
    }
    return 0;
}

/* Give the blitter assisted chunky to planar conversion (68020 build only)
   a staging area covering the part of the screen the frame lands on. */
static void amiga_stage_area(void) {
    const amiga_videomode *m = &amiga_mode;

    if (m->rtg)
        amiga_c2p_blit_area(0, 0, 0, 0);
    else
        amiga_c2p_blit_area(m->destx, m->desty,
                            m->vieww * m->divisor, m->viewh * m->divisor);
}

/* Black out both buffers, so nothing from the last layout is left in the
   borders round a smaller one. */
static void amiga_clear_display(void) {
    struct RastPort rp;
    int i;

    /* SetRast() blits, and the conversion may still own the blitter. */
    amiga_c2p_sync();
    amiga_sync_buffers();

    rp = amiga_screen->RastPort;
    if (doublebuffered) {
        for (i = 0; i < 2; i++) {
            rp.BitMap = sbuf[i]->sb_BitMap;
            SetRast(&rp, 0);
        }
    } else {
        SetRast(&rp, 0);
    }
}

int amiga_video_open(void) {
    ULONG err = 0;

    amiga_probe(&amiga_mode);
    amiga_describe(&amiga_mode);

    amiga_screen = OpenScreenTags(NULL,
                                  SA_DisplayID,  amiga_mode.modeid,
                                  SA_Width,      amiga_mode.width,
                                  SA_Height,     amiga_mode.height,
                                  SA_Depth,      amiga_mode.depth,
                                  SA_Type,       CUSTOMSCREEN,
                                  amiga_useclip ? SA_DClip : TAG_IGNORE,
                                                 (ULONG)&amiga_dclip,
                                  SA_Quiet,      TRUE,
                                  SA_ShowTitle,  FALSE,
                                  SA_Draggable,  FALSE,
                                  SA_Exclusive,  TRUE,
                                  SA_AutoScroll, FALSE,
                                  SA_SharePens,  FALSE,
                                  SA_ErrorCode,  (ULONG)&err,
                                  TAG_END);
    if (!amiga_screen) {
        fprintf(stderr, "OpenScreen failed (error %ld).\n", (long)err);
        return -1;
    }

    /* The real depth we were given may differ from what was asked for. */
    amiga_mode.depth = amiga_screen->RastPort.BitMap->Depth;
    if (amiga_mode.rtg)
        amiga_mode.pixfmt = (int)GetCyberMapAttr(amiga_screen->RastPort.BitMap,
                                                 CYBRMATTR_PIXFMT);

    numpens = (amiga_mode.rtg || amiga_mode.depth >= 8)
              ? 256 : (1 << amiga_mode.depth);

    amiga_window = OpenWindowTags(NULL,
                                  WA_CustomScreen, (ULONG)amiga_screen,
                                  WA_Left,     0,
                                  WA_Top,      0,
                                  WA_Width,    amiga_mode.width,
                                  WA_Height,   amiga_mode.height,
                                  WA_Backdrop, TRUE,
                                  WA_Borderless, TRUE,
                                  WA_Activate, TRUE,
                                  WA_RMBTrap,  TRUE,
                                  WA_ReportMouse, TRUE,
                                  WA_NoCareRefresh, TRUE,
                                  WA_SimpleRefresh, TRUE,
                                  WA_IDCMP, IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS |
                                            IDCMP_MOUSEMOVE | IDCMP_ACTIVEWINDOW |
                                            IDCMP_INACTIVEWINDOW | IDCMP_DELTAMOVE,
                                  TAG_END);
    if (!amiga_window) {
        fprintf(stderr, "Could not open the game window.\n");
        CloseScreen(amiga_screen);
        amiga_screen = NULL;
        return -1;
    }

    /* The game draws its own crosshair and menus; Intuition's pointer would
       just sit on top of them. */
    /* Six zero words: position/control pair, one line of image, terminator.
       Four would leave the sprite DMA reading past the end of the array. */
    blankpointer = AllocVec(6 * sizeof(UWORD), MEMF_CHIP | MEMF_CLEAR);
    if (blankpointer)
        SetPointer(amiga_window, blankpointer, 1, 1, 0, 0);

    amiga_setup_buffers();

    if (amiga_alloc_frame(&amiga_mode) != 0) {
        /* The full resolution frame did not fit; the old fixed size one
           nearly always will. */
        fprintf(stderr, "Out of memory for a %dx%d frame; "
                        "falling back to 360x240.\n",
                amiga_mode.vieww, amiga_mode.viewh);
        amiga_layout_for(&amiga_mode, AMIGA_RENDER_UNSCALED);
        if (amiga_alloc_frame(&amiga_mode) != 0) {
            fprintf(stderr, "Out of memory for the frame buffer.\n");
            amiga_video_close();
            return -1;
        }
    }

    amiga_build_penmap();

    if (!amiga_mode.rtg)
        amiga_c2p_init();
    amiga_stage_area();

    /* Blank the screen so the borders are black rather than whatever
       Intuition left behind. */
    amiga_clear_display();
    amiga_video_invalidate();

    return 0;
}

/* Re-run the layout for a changed setting on the screen that is already
   open.  The new frame is allocated before the old one goes, so a failure
   leaves everything as it was. */
int amiga_video_relayout(void) {
    amiga_videomode old;

    if (!amiga_screen)
        return 0;       /* picked up when the display opens */

    old = amiga_mode;
    amiga_layout(&amiga_mode);

    /* A new size needs a new frame; at the same size only the mapping of
       the game space onto it has changed. */
    if (amiga_mode.vieww != old.vieww || amiga_mode.viewh != old.viewh ||
        amiga_mode.divisor != old.divisor) {
        UBYTE *oldchunky = amiga_chunky, *oldband = bandbuf;
        int    oldrows = bandrows;

        amiga_chunky = NULL;
        bandbuf = NULL;
        if (amiga_alloc_frame(&amiga_mode) != 0) {
            amiga_mode   = old;
            amiga_chunky = oldchunky;
            bandbuf      = oldband;
            bandrows     = oldrows;
            return -1;
        }
        FreeVec(oldchunky);
        if (oldband) FreeVec(oldband);
    }

    amiga_describe(&amiga_mode);
    amiga_stage_area();
    amiga_clear_display();
    amiga_video_invalidate();
    amiga_apply_view();
    return 0;
}

void amiga_video_close(void) {
    /* Order matters: the window's layer hangs off the screen's bitmap, so it
       goes first; then the buffers (which needs the screen to still exist);
       then the screen itself. */
    if (amiga_window) {
        ClearPointer(amiga_window);
        CloseWindow(amiga_window);
        amiga_window = NULL;
    }
    if (blankpointer) { FreeVec(blankpointer); blankpointer = NULL; }

    amiga_c2p_blit_area(0, 0, 0, 0);    /* waits for any blits, too */
    amiga_free_buffers();

    if (amiga_screen) { CloseScreen(amiga_screen); amiga_screen = NULL; }
    if (amiga_chunky) { FreeVec(amiga_chunky);     amiga_chunky = NULL; }
    if (bandbuf)      { FreeVec(bandbuf);          bandbuf = NULL; }

    fprintf(stderr, "Display closed.  Chip RAM free: %lu bytes "
                    "(largest block %lu).\n",
            (unsigned long)AvailMem(MEMF_CHIP),
            (unsigned long)AvailMem(MEMF_CHIP | MEMF_LARGEST));
}

/* ---------------------------------------------------------------- palette */

/* The display palette.  palette[] is the game's own working copy and the
   game reads it back (the floor and ceiling colours come from it), so the
   renderer keeps its own copy here rather than writing through it. */
static unsigned char dispal[768];

/*
 * Load `count` entries starting at `start` into the display.
 *
 * Components are the game's 0..63 values scaled by the current fade factors -
 * on the Amiga the fade goes straight into the hardware palette, which is
 * what the original DOS game did and costs nothing per frame.
 */
void amiga_load_palette(const unsigned char *pal, int start, int count) {
    ULONG table[1 + 256*3 + 1];
    int i;

    if (start < 0) start = 0;
    if (start + count > 256) count = 256 - start;
    if (count <= 0) return;

    memcpy(dispal + start*3, pal + start*3, (size_t)count * 3);

    if (!amiga_screen) return;

    if (amiga_mode.rtg && amiga_mode.pixfmt != PIXFMT_LUT8) {
        /* Deep RTG screen: we expand through our own lookup table instead of
           a hardware palette. */
        for (i = start; i < start + count; i++) {
            ULONG r = (ULONG)(dispal[i*3+0] * redfactor);
            ULONG g = (ULONG)(dispal[i*3+1] * greenfactor);
            ULONG b = (ULONG)(dispal[i*3+2] * bluefactor);
            if (r > 63) r = 63;
            if (g > 63) g = 63;
            if (b > 63) b = 63;
            /* 0..63 -> 0..255 */
            r = (r << 2) | (r >> 4);
            g = (g << 2) | (g >> 4);
            b = (b << 2) | (b >> 4);
            lut[i] = (r << 16) | (g << 8) | b;
        }
        /* The colours are only applied as the frame is sent, so all of
           it has to go again for a palette change to show. */
        dirty_now.all = dirty_last.all = 1;
        return;
    }

    if (numpens < 256) {
        /* Fewer pens than colours: every pen may have changed, so reload the
           lot rather than trying to work out which ones this range touched. */
        start = 0;
        count = numpens;
    }

    table[0] = ((ULONG)count << 16) | (ULONG)start;
    for (i = 0; i < count; i++) {
        int src = (numpens == 256) ? (start + i) : (int)pensrc[start + i];
        ULONG r = (ULONG)(dispal[src*3+0] * redfactor);
        ULONG g = (ULONG)(dispal[src*3+1] * greenfactor);
        ULONG b = (ULONG)(dispal[src*3+2] * bluefactor);
        if (r > 63) r = 63;
        if (g > 63) g = 63;
        if (b > 63) b = 63;
        /* LoadRGB32 wants 32 bit components; replicate the 6 bit value. */
        table[1 + i*3 + 0] = (r << 26) | (r << 20) | (r << 14) | (r << 8) | (r << 2) | (r >> 4);
        table[1 + i*3 + 1] = (g << 26) | (g << 20) | (g << 14) | (g << 8) | (g << 2) | (g >> 4);
        table[1 + i*3 + 2] = (b << 26) | (b << 20) | (b << 14) | (b << 8) | (b << 2) | (b >> 4);
    }
    table[1 + count*3] = 0;

    LoadRGB32(&amiga_screen->ViewPort, table);
}

void amiga_set_palette(const unsigned char *pal) {
    amiga_load_palette(pal, 0, 256);
}

/* Re-send the palette we already have, after the fade factors changed. */
void amiga_refresh_palette(void) {
    amiga_load_palette(dispal, 0, 256);
}

/*
 * On a screen with fewer than 256 pens the game's sixteen hues by sixteen
 * brightness levels have to be folded down.  Every hue is kept and brightness
 * levels are merged, because losing a hue loses whole objects while losing
 * brightness steps only flattens the shading.
 */
void amiga_build_penmap(void) {
    int i, levels;

    if (numpens >= 256) {
        for (i = 0; i < 256; i++)
            penmap[i] = pensrc[i] = (UBYTE)i;
        return;
    }

    levels = numpens / 16;
    if (levels < 1) levels = 1;

    for (i = 0; i < 256; i++) {
        int hue = i >> 4, lev = i & 15;
        int nl  = (lev * levels) / 16;
        penmap[i] = (UBYTE)(hue * levels + nl);
    }

    /* The reverse map: each pen takes the colour from the middle of the
       brightness band it now stands for. */
    for (i = 0; i < numpens; i++) {
        int hue = i / levels, nl = i % levels;
        int lev = ((nl * 2 + 1) * 16) / (2 * levels);
        if (lev > 15) lev = 15;
        pensrc[i] = (UBYTE)(hue * 16 + lev);
    }
    for (; i < 256; i++)
        pensrc[i] = 0;
}

int amiga_num_pens(void) { return numpens; }
const UBYTE *amiga_penmap(void) { return penmap; }

/* -------------------------------------------------------- dirty rectangles */

/*
 * What changed in the frame since it was last sent.  With double buffering
 * the buffer being drawn into last held the frame before the previous one,
 * so it needs this frame's changes and the previous frame's as well; that is
 * why two frames' worth are kept.
 *
 * Rectangles that one contains, or that sit side by side on the same rows,
 * are merged as they come in.  That catches the status bar, which arrives
 * as a dozen 20 unit strips.  Past MAXRECTS the list gives up and sends
 * everything, which is never wrong, only slower.
 */
static void rl_add(rectlist *l, int x0, int y0, int x1, int y1) {
    int i;

    if (l->all) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > amiga_mode.vieww) x1 = amiga_mode.vieww;
    if (y1 > amiga_mode.viewh) y1 = amiga_mode.viewh;
    if (x0 >= x1 || y0 >= y1) return;

    for (i = 0; i < l->n; i++) {
        arect *r = &l->r[i];

        if (r->x0 <= x0 && r->y0 <= y0 && r->x1 >= x1 && r->y1 >= y1)
            return;
        if ((x0 <= r->x0 && y0 <= r->y0 && x1 >= r->x1 && y1 >= r->y1) ||
            (y0 == r->y0 && y1 == r->y1 && x0 <= r->x1 && x1 >= r->x0)) {
            if (x0 < r->x0) r->x0 = x0;
            if (y0 < r->y0) r->y0 = y0;
            if (x1 > r->x1) r->x1 = x1;
            if (y1 > r->y1) r->y1 = y1;
            return;
        }
    }
    if (l->n == MAXRECTS) {
        l->all = 1;
        return;
    }
    l->r[l->n].x0 = x0; l->r[l->n].y0 = y0;
    l->r[l->n].x1 = x1; l->r[l->n].y1 = y1;
    l->n++;
}

void amiga_mark_dirty(int x0, int y0, int x1, int y1, int overlay) {
    rl_add(&dirty_now, x0, y0, x1, y1);
    if (overlay)
        rl_add(&ovl_now, x0, y0, x1, y1);
}

void amiga_mark_all_dirty(void) {
    dirty_now.all = 1;
}

void amiga_clear_leftovers(void) {
    int i, y;

    if (!amiga_chunky) return;

    if (ovl_last.all) {
        memset(amiga_chunky, 0, (size_t)amiga_mode.vieww * amiga_mode.viewh);
        dirty_now.all = 1;
        return;
    }
    for (i = 0; i < ovl_last.n; i++) {
        const arect *r = &ovl_last.r[i];
        UBYTE *row = amiga_chunky + (size_t)r->y0 * amiga_mode.vieww + r->x0;

        for (y = r->y0; y < r->y1; y++, row += amiga_mode.vieww)
            memset(row, 0, r->x1 - r->x0);
        rl_add(&dirty_now, r->x0, r->y0, r->x1, r->y1);
    }
}

void amiga_video_invalidate(void) {
    if (amiga_chunky)
        memset(amiga_chunky, 0, (size_t)amiga_mode.vieww * amiga_mode.viewh);
    dirty_now.all = dirty_last.all = 1;
    ovl_now.n = ovl_last.n = 0;
    ovl_now.all = ovl_last.all = 0;
}

/* ----------------------------------------------------------------- blitting */

/*
 * Widen `rows` rows of a w pixel wide piece of the frame by n into the band, each source pixel
 * becoming n bytes and each source row n identical rows.  Only the first
 * copy of a row is built pixel by pixel; the others are memcpy()s of it.
 * The common factors write a whole word or longword per source pixel, which
 * the band's layout keeps aligned: it comes from AllocVec() and every row is
 * a multiple of n bytes long.
 */
static void amiga_expand(const UBYTE *src, int stride, int w, int rows, int n,
                         UBYTE *dst) {
    int dw = w * n;
    int r, x, k;

    for (r = 0; r < rows; r++, src += stride) {
        UBYTE *d0 = dst;

        switch (n) {
        case 2: {
            UWORD *d = (UWORD *)d0;
            for (x = 0; x < w; x++)
                d[x] = (UWORD)(src[x] * 0x0101U);
            break;
        }
        case 4: {
            ULONG *d = (ULONG *)d0;
            for (x = 0; x < w; x++)
                d[x] = (ULONG)src[x] * 0x01010101UL;
            break;
        }
        default: {
            UBYTE *d = d0;
            for (x = 0; x < w; x++) {
                UBYTE c = src[x];
                for (k = 0; k < n; k++)
                    *d++ = c;
            }
            break;
        }
        }

        dst += dw;
        for (k = 1; k < n; k++, dst += dw)
            memcpy(dst, d0, dw);
    }
}

/* Put a w x h block of chunky pixels on the screen at (x,y). */
static void amiga_put(struct RastPort *rp, const UBYTE *src, int stride,
                      int x, int y, int w, int h) {
    if (amiga_mode.rtg) {
        if (amiga_mode.pixfmt == PIXFMT_LUT8)
            WritePixelArray((APTR)src, 0, 0, stride, rp, x, y, w, h,
                            RECTFMT_LUT8);
        else
            WriteLUTPixelArray((APTR)src, 0, 0, stride, rp, lut,
                               x, y, w, h, CTABFMT_XRGB8);
    } else {
        amiga_c2p(src, stride, rp->BitMap, x, y, w, h, amiga_mode.depth);
    }
}

/* Send one rectangle of the frame, scaling it up on the way if the layout
   says so.  A native screen gets it widened to whole bytes, since the planar
   conversion works eight pixels at a time. */
static void amiga_blit_rect(struct RastPort *rp, int x0, int y0, int x1, int y1) {
    int n = amiga_mode.divisor, w = amiga_mode.vieww;
    int rw, y;

    if (!amiga_mode.rtg) {
        x0 &= ~7;
        x1 = (x1 + 7) & ~7;
        if (x1 > w) x1 = w;
    }
    rw = x1 - x0;
    if (rw <= 0 || y1 <= y0) return;

    if (n <= 1 || !bandbuf) {
        amiga_put(rp, amiga_chunky + (size_t)y0 * w + x0, w,
                  amiga_mode.destx + x0, amiga_mode.desty + y0, rw, y1 - y0);
        return;
    }

    for (y = y0; y < y1; y += bandrows) {
        int rows = y1 - y < bandrows ? y1 - y : bandrows;

        amiga_expand(amiga_chunky + (size_t)y * w + x0, w, rw, rows, n, bandbuf);
        amiga_put(rp, bandbuf, rw * n,
                  amiga_mode.destx + x0 * n, amiga_mode.desty + y * n,
                  rw * n, rows * n);
    }
}

void amiga_blit_frame(void) {
    struct RastPort rp;
    K_UINT32 t0;
    int i;

    if (!amiga_screen || !amiga_chunky) return;

    if (doublebuffered && !safe_to_write) {
        while (!GetMsg(safeport))
            WaitPort(safeport);
        safe_to_write = 1;
    }

    rp = amiga_screen->RastPort;
    rp.BitMap = amiga_drawbitmap();
    t0 = PL_GetTicks();

    /* Everything, unless the view is shrunk and nothing says otherwise:
       at full size the view covers the frame and is redrawn every frame
       anyway, so there is nothing to save. */
    if (amiga_cfg_viewsize >= 100 || dirty_now.all ||
        (doublebuffered && dirty_last.all)) {
        amiga_blit_rect(&rp, 0, 0, amiga_mode.vieww, amiga_mode.viewh);
    } else {
        for (i = 0; i < dirty_now.n; i++)
            amiga_blit_rect(&rp, dirty_now.r[i].x0, dirty_now.r[i].y0,
                            dirty_now.r[i].x1, dirty_now.r[i].y1);
        if (doublebuffered)
            for (i = 0; i < dirty_last.n; i++)
                amiga_blit_rect(&rp, dirty_last.r[i].x0, dirty_last.r[i].y0,
                                dirty_last.r[i].x1, dirty_last.r[i].y1);
    }

    /* The blitter may still be finishing the conversion; the frame has to
       be complete before it is shown, and the staging area free for the
       next one.  How long all that took goes to the blitter's Auto setting. */
    if (!amiga_mode.rtg) {
        amiga_c2p_sync();
        amiga_c2p_frame_done(PL_GetTicks() - t0);
    }

    dirty_last = dirty_now;
    ovl_last   = ovl_now;
    dirty_now.n = dirty_now.all = 0;
    ovl_now.n   = ovl_now.all   = 0;

    if (doublebuffered) {
        if (!safe_to_change) {
            while (!GetMsg(dispport))
                WaitPort(dispport);
            safe_to_change = 1;
        }

        if (ChangeScreenBuffer(amiga_screen, sbuf[sbcurrent])) {
            frontbuf       = sbcurrent;
            safe_to_change = 0;
            safe_to_write  = 0;
            sbcurrent ^= 1;
        }
    }
}
