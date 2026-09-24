/*
 * AmigaOS display back end for LAB3D.
 *
 * The game renders into a fixed 360x240 chunky buffer (the "virtual screen"
 * every coordinate in the shared code is expressed in) and this module gets
 * it onto whatever screen mode the player picked at startup.  Two paths:
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

/* Scaled copy used when amiga_mode.scale == 2. */
static UBYTE   *scalebuf;

/* Colour table for WriteLUTPixelArray() on deep RTG screens. */
static ULONG    lut[256];

/* Defined further down; amiga_video_open() needs it before its definition. */
void amiga_build_penmap(void);

/* Chunky to planar helper tables (see amiga_c2p.c). */
extern void amiga_c2p_init(void);
extern void amiga_c2p(const UBYTE *src, int srcmod,
                      struct BitMap *bm, int destx, int desty,
                      int w, int h, int depth);

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
int   amiga_cfg_scale = 0;      /* 0 = automatic, 1 or 2 = forced */
int   amiga_cfg_askmode = 1;    /* show the screen mode requester at startup */

/* ------------------------------------------------------------ mode picking */

/* Display clip for native screens that reach into the borders; see
   amiga_fit_overscan(). */
static struct Rectangle amiga_dclip;
static int              amiga_useclip;

/* How much of the 360x240 buffer can we show, and where? */
static void amiga_layout(amiga_videomode *m) {
    int scale = amiga_cfg_scale;
    int vieww, viewh;

    if (scale != 1 && scale != 2) {
        /* Automatic: double up when the mode is big enough to hold the whole
           virtual screen twice over. */
        scale = (m->width >= AMIGA_VIEW_W*2 && m->height >= AMIGA_VIEW_H*2)
                ? 2 : 1;
    }
    if (m->width < AMIGA_VIEW_W*2 || m->height < AMIGA_VIEW_H*2)
        scale = 1;

    m->scale = scale;

    /*
     * Fit each axis on its own and centre it on the view.  A screen that is
     * tall enough but not wide enough then only loses pixels horizontally,
     * where there is nothing to lose: a 320 pixel wide screen lands on
     * exactly the x = 20..340 window the original game drew into, and only
     * the decorative margins either side go.  Doing both axes together (the
     * old "whole view or the 320x200 window" choice) threw away forty rows
     * on a 320x240 screen that could have shown all of them.
     */
    vieww = m->width  / scale;
    viewh = m->height / scale;

    m->srcw = vieww < AMIGA_VIEW_W ? vieww : AMIGA_VIEW_W;
    m->srch = viewh < AMIGA_VIEW_H ? viewh : AMIGA_VIEW_H;
    m->srcx = (AMIGA_VIEW_W - m->srcw) / 2;
    m->srcy = (AMIGA_VIEW_H - m->srch) / 2;

    m->destx = (m->width  - m->srcw * scale) / 2;
    m->desty = (m->height - m->srch * scale) / 2;

    /* Keep the destination even so the planar path can work in whole bytes. */
    m->destx &= ~7;
    if (m->destx < 0) m->destx = 0;
    if (m->desty < 0) m->desty = 0;
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
    fprintf(stderr, "Showing %dx%d of the %dx%d view at (%d,%d), %dx scale.\n",
            m->srcw, m->srch, AMIGA_VIEW_W, AMIGA_VIEW_H,
            m->destx, m->desty, m->scale);
    if (amiga_useclip)
        fprintf(stderr, "Using the borders: display clip (%d,%d)-(%d,%d).\n",
                amiga_dclip.MinX, amiga_dclip.MinY,
                amiga_dclip.MaxX, amiga_dclip.MaxY);
    if (!m->rtg)
        fprintf(stderr, "Chip RAM free: %lu bytes (largest block %lu).\n",
                (unsigned long)AvailMem(MEMF_CHIP),
                (unsigned long)AvailMem(MEMF_CHIP | MEMF_LARGEST));
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

    amiga_chunky = AllocVec(AMIGA_VIEW_W * AMIGA_VIEW_H, MEMF_ANY | MEMF_CLEAR);
    if (!amiga_chunky) {
        fprintf(stderr, "Out of memory for the frame buffer.\n");
        amiga_video_close();
        return -1;
    }

    if (amiga_mode.scale == 2) {
        scalebuf = AllocVec(AMIGA_VIEW_W * 2 * AMIGA_VIEW_H * 2,
                            MEMF_ANY | MEMF_CLEAR);
        if (!scalebuf) {
            fprintf(stderr, "Out of memory for 2x scaling; falling back to 1x.\n");
            amiga_mode.scale = 1;
            amiga_layout(&amiga_mode);
        }
    }

    amiga_build_penmap();

    if (!amiga_mode.rtg)
        amiga_c2p_init();

    /* Blank the screen so the borders are black rather than whatever
       Intuition left behind. */
    SetRast(&amiga_screen->RastPort, 0);
    if (doublebuffered) {
        struct RastPort rp = amiga_screen->RastPort;
        rp.BitMap = sbuf[1]->sb_BitMap;
        SetRast(&rp, 0);
    }

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

    amiga_free_buffers();

    if (amiga_screen) { CloseScreen(amiga_screen); amiga_screen = NULL; }
    if (amiga_chunky) { FreeVec(amiga_chunky);     amiga_chunky = NULL; }
    if (scalebuf)     { FreeVec(scalebuf);         scalebuf = NULL; }

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

/* The last row of the view that reaches the display.  Equal to AMIGA_VIEW_H
   unless the screen is too short to show the whole thing. */
int amiga_view_bottom(void) {
    if (!amiga_screen)
        return AMIGA_VIEW_H;
    return amiga_mode.srcy + amiga_mode.srch;
}

int amiga_num_pens(void) { return numpens; }
const UBYTE *amiga_penmap(void) { return penmap; }

/* ----------------------------------------------------------------- blitting */

static void amiga_scale2x(void) {
    const UBYTE *s = amiga_chunky + amiga_mode.srcy * AMIGA_VIEW_W + amiga_mode.srcx;
    UBYTE *d = scalebuf;
    int dstride = amiga_mode.srcw * 2;
    int x, y;

    for (y = 0; y < amiga_mode.srch; y++) {
        UBYTE *d0 = d;
        for (x = 0; x < amiga_mode.srcw; x++) {
            UBYTE c = s[x];
            *d++ = c;
            *d++ = c;
        }
        memcpy(d, d0, dstride);     /* duplicate the row */
        d += dstride;
        s += AMIGA_VIEW_W;
    }
}

void amiga_blit_frame(void) {
    struct BitMap *bm;
    const UBYTE *src;
    int stride, w, h;

    if (!amiga_screen) return;

    if (doublebuffered && !safe_to_write) {
        while (!GetMsg(safeport))
            WaitPort(safeport);
        safe_to_write = 1;
    }

    if (amiga_mode.scale == 2 && scalebuf) {
        amiga_scale2x();
        src    = scalebuf;
        stride = amiga_mode.srcw * 2;
        w      = amiga_mode.srcw * 2;
        h      = amiga_mode.srch * 2;
    } else {
        src    = amiga_chunky + amiga_mode.srcy * AMIGA_VIEW_W + amiga_mode.srcx;
        stride = AMIGA_VIEW_W;
        w      = amiga_mode.srcw;
        h      = amiga_mode.srch;
    }

    bm = amiga_drawbitmap();

    if (amiga_mode.rtg) {
        struct RastPort rp = amiga_screen->RastPort;
        rp.BitMap = bm;

        if (amiga_mode.pixfmt == PIXFMT_LUT8) {
            WritePixelArray((APTR)src, 0, 0, stride, &rp,
                            amiga_mode.destx, amiga_mode.desty, w, h,
                            RECTFMT_LUT8);
        } else {
            WriteLUTPixelArray((APTR)src, 0, 0, stride, &rp, lut,
                               amiga_mode.destx, amiga_mode.desty, w, h,
                               CTABFMT_XRGB8);
        }
    } else {
        amiga_c2p(src, stride, bm, amiga_mode.destx, amiga_mode.desty,
                  w, h, amiga_mode.depth);
    }

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
