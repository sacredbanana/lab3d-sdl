/*
 * Software renderer for the Amiga port of LAB3D.
 *
 * The OpenGL back end draws the labyrinth as textured quads with a per-pixel
 * depth buffer.  That is far too much work for a 68k, but the geometry is far
 * simpler than the general case and can be exploited:
 *
 *   - The camera only ever yaws.  gluLookAt() is called with the eye and the
 *     target at the same height and an up vector of (0,0,-1), so there is no
 *     pitch and no roll.  Every upright quad therefore projects to a
 *     screen-space trapezoid whose vertical edges stay vertical, which means
 *     walls and sprites can be drawn as vertical spans with one texture
 *     column each - the classic Wolfenstein/Doom column renderer.
 *
 *   - Walls always run the full height of a cell, z = 0 (ceiling) to
 *     z = 1024 (floor).  In any given screen column a nearer wall completely
 *     covers a farther one, so a one-dimensional depth buffer with a single
 *     entry per column replaces the full depth buffer.  Sprites test against
 *     it without writing to it, and are already handed to us sorted back to
 *     front by graphx.c.
 *
 *   - Floor and ceiling are flat colours, exactly as in the OpenGL path
 *     (which clears to the floor colour and paints one rectangle for the
 *     ceiling), so there is no floor texture mapping to do at all.
 *
 * Everything is drawn into an 8 bit chunky buffer whose indices are the
 * game's own palette entries, so there is no colour conversion in the inner
 * loops.  The buffer is whatever size amiga_video.c's layout picked - 360x240
 * on a small screen, up to the full screen resolution on a big one - and the
 * game's 360x240 coordinate space is mapped onto it through amiga_mode.ppux,
 * ppuy, orgx and orgy.  The display module then scales it up to fill the
 * screen.  Shading uses the structure of that palette: it is sixteen
 * hues by sixteen brightness levels, so the OpenGL renderer's 0.9x shade for
 * one wall orientation becomes "subtract one from the low nibble".
 *
 * Texture filtering and hi-res texture replacement are not implemented: point
 * sampling is what a 68k can afford, and the settings for them are hidden
 * from the Amiga setup menu.
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ------------------------------------------------------------------ state */

/* The render buffer.  These are read from amiga_mode rather than copied, so
   a relayout from the setup menu takes effect on the next frame.  Inner loops
   that step by a row take a local copy: a store through an unsigned char
   pointer may alias anything, so the compiler would otherwise reload the
   global after every pixel. */
#define VW amiga_mode.vieww
#define VH amiga_mode.viewh

/* Per-column depth, held as 2^24/d.  Bigger is nearer.  The near plane is 16
   world units and the far plane 98304, so this spans 1048576 down to 170 -
   plenty of resolution without ever overflowing 32 bits. */
#define ZSCALE 16777216.0

static K_INT32 zbuf[AMIGA_RENDER_MAXW];

/* Camera, recomputed once per frame by R_BeginScene(). */
static double cam_fx, cam_fy;       /* unit forward vector                */
static double cam_ex, cam_ey, cam_ez;
static double proj_x;               /* pixels per unit of e/d, across      */
static double proj_y;               /* pixels per unit of z/d, down        */
static double proj_cx;              /* screen x of the view axis           */
static int    horizon_row;

/* Brightness ramp lookup: one step darker, used for the shaded wall faces. */
static unsigned char shadetab[256];

/* (64 << 16) / height, for turning a span height into a texture step. */
#define RECIP_MAX 4096
static K_INT32 *vrecip;

/* Rows the labyrinth view may touch.  The status bar is composited over the
   bottom of the frame afterwards, exactly as the OpenGL path does. */
#define VIEW_TOP 0
#define VIEW_BOT VH

extern void amiga_build_penmap(void);
extern int  amiga_num_pens(void);
extern const UBYTE *amiga_penmap(void);
extern void amiga_c2p_setmap(const UBYTE *pens);

void softtri(double *sx, double *sy, double *tu, double *tv,
             int a, int b, int c, int texnum, K_INT32 z);

/* --------------------------------------------------------------- helpers */

/* First render column / row whose pixel centre lies at or past a position in
   game units.  A span from a to b in game units covers the pixels from
   unit_col(a) up to but not including unit_col(b), which tiles exactly:
   neighbouring spans share their edge and never overlap or leave a gap,
   whatever the scale. */
static int unit_col(double gx) {
    return (int)ceil(gx * amiga_mode.ppux + amiga_mode.orgx - 0.5);
}

static int unit_row(double gy) {
    return (int)ceil(gy * amiga_mode.ppuy + amiga_mode.orgy - 0.5);
}

/* Round a double to the nearest integer rather than toward zero.  Every fixed
   point step below is rounded once and then added hundreds of times, so a step
   that is half an ulp out drags the far end of the span badly off. */
static K_INT32 fxround(double v) {
    return (K_INT32)(v < 0.0 ? v - 0.5 : v + 0.5);
}

/* Round to nearest and reduce modulo 2^32.  Converting an out of range double
   to an integer is undefined and, on most hardware, saturates rather than
   wrapping - so an accumulator that is meant to wrap has to have its starting
   value reduced here rather than by the cast.  The range test keeps the
   expensive path off the common case. */
static K_UINT32 fxwrap(double v) {
    if (v > -2147483008.0 && v < 2147483008.0)
        return (K_UINT32)fxround(v);
    v = fmod(v, 4294967296.0);
    if (v < 0.0) v += 4294967296.0;
    if (v >= 4294967296.0) v = 0.0;          /* only reachable via NaN */
    return (K_UINT32)v;
}

static void build_shadetab(void) {
    int i;

    for (i = 0; i < 256; i++) {
        int hue = i & 0xf0, lev = i & 0x0f;
        shadetab[i] = (unsigned char)(hue | (lev ? lev - 1 : 0));
    }
    shadetab[255] = 255;            /* keep the transparency key intact */
}

int R_WantsLargeOverlayTexture(void) {
    /* Not a texture at all here, but the 512x512 layout is the one the game
       code treats as a plain linear buffer, which is what we want. */
    return 1;
}

void R_InitOverlay(void) {
    int i;

    build_shadetab();

    if (!vrecip) {
        vrecip = AllocVec(RECIP_MAX * sizeof(K_INT32), MEMF_ANY);
        if (!vrecip)
            fatal_error("Out of memory for the renderer tables.");
        vrecip[0] = 0;
        for (i = 1; i < RECIP_MAX; i++)
            vrecip[i] = (K_INT32)((64L << 16) / i);
    }

    amiga_build_penmap();
    if (amiga_num_pens() < 256)
        amiga_c2p_setmap(amiga_penmap());

    texturecreationneeded = 0;
}

void R_ClearScreen(void) {
    if (amiga_chunky)
        memset(amiga_chunky, 0, (size_t)VW * VH);
}

/* --------------------------------------------------------------- textures */

/* There is nothing to upload: the rasteriser samples walseg[] directly, so a
   texture "handle" is just its index. */

void R_LoadWallTexture(int i, unsigned char *texels, int bmpkind_,
                       int wrapmode, int minfilt, int magfilt) {
    (void)texels; (void)bmpkind_; (void)wrapmode; (void)minfilt; (void)magfilt;
    texName[i] = (GLuint)i;
    walltexcoord[i][0] = 0.0;
    walltexcoord[i][1] = 1.0;
}

void R_LoadGameOverSprite(unsigned char *texels) {
    (void)texels;   /* the wall copy is the only one we need */
}

void R_UpdateWallTexture(int walnum) {
    (void)walnum;
}

/* The automap and the game over banner are drawn by scribbling on walseg[],
   which the rasteriser reads directly, so there is nothing to re-upload. */
void updatemap(void) { }
void updategameover(void) { }

int R_HaveTransitionTextures(void) {
    return 0;       /* they only exist to hide bilinear filtering seams */
}

void R_MakeTransitionTextures(void) {
    int i;
    for (i = 0; i < numsplits; i++)
        splitTexNum[i] = -1;
}

void R_DrawSplitWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     int slot, int shaded) {
    (void)x1; (void)y1; (void)x2; (void)y2; (void)slot; (void)shaded;
}

void TextureConvert(unsigned char *from, unsigned char *to, K_INT16 type) {
    (void)from; (void)to; (void)type;
}

void clearimgcache(void) {
}

int powerof2(int in) {
    int i = 0;
    in--;
    while (in) { in >>= 1; i++; }
    return 1 << i;
}

/* ---------------------------------------------------------------- palette */

void settransferpalette(void) {
    amiga_set_palette(palette);
}

void setdarkenedpalette(void) {
    unsigned char dark[768];
    int i;

    for (i = 0; i < 768; i++)
        dark[i] = (unsigned char)((palette[i] * 27) >> 5);
    amiga_set_palette(dark);
}

/* Change part of the palette without disturbing the game's own copy: the
   animated text colours are rewritten from the music tick, many times a
   second, and palette[] is read back elsewhere for the floor and ceiling. */
void updateoverlaypalette(K_UINT16 start, K_UINT16 amount, unsigned char *cols) {
    unsigned char tmp[768];

    if (start >= 256) return;
    if (start + amount > 256) amount = 256 - start;

    memcpy(tmp + start*3, cols, (size_t)amount * 3);
    amiga_load_palette(tmp, start, amount);
}

void R_SetOverlayPalette(int darkened) {
    if (darkened) setdarkenedpalette(); else settransferpalette();
}

void R_SetOverlayPaletteRange(K_UINT16 start, K_UINT16 count,
                              unsigned char *cols) {
    updateoverlaypalette(start, count, cols);
}

void R_FadeChanged(void) {
    /* Fading is free here: it just reloads the hardware palette with the new
       factors applied. */
    amiga_refresh_palette();
}

/* ----------------------------------------------------------------- overlay */

/* Source column for each destination pixel of the blit in progress. */
static UWORD omap[AMIGA_RENDER_MAXW];

/* Bottom edge, in game units, of what the frame shows.  240 unless the view
   is cropped short or grown taller than the 4:3 area. */
static double view_bottom(void) {
    return (VH - amiga_mode.orgy) / amiga_mode.ppuy;
}

/*
 * Copy a w x h rectangle of the 8 bit overlay at (srcx,srcy) onto the frame,
 * with its top left corner at (dstx,dsty) in game units.  Index 255 is the
 * transparency key while in game, matching the alpha test the OpenGL path
 * uses; outside the game the overlay is opaque.
 *
 * The overlay is 360x240 units and the frame is usually bigger, so each
 * destination pixel shows the overlay pixel under its centre - the same
 * nearest neighbour sampling OpenGL does with filtering off.  The column
 * lookup is built once per call; at 1:1 it comes out as a straight run and
 * the copy drops to a memcpy.
 */
static void blit_overlay(int srcx, int srcy, double dstx, double dsty,
                         int w, int h) {
    const unsigned char *s;
    unsigned char *d;
    int x0, x1, y0, y1, x, y, n, stride, linear;
    double inv;
    K_INT32 u, du;

    if (!amiga_chunky || !screenbuffer) return;

    /* Clip against the overlay buffer. */
    if (srcx < 0) { w += srcx; dstx -= srcx; srcx = 0; }
    if (srcy < 0) { h += srcy; dsty -= srcy; srcy = 0; }
    if (srcx + w > screenbufferwidth)  w = screenbufferwidth  - srcx;
    if (srcy + h > screenbufferheight) h = screenbufferheight - srcy;
    if (w <= 0 || h <= 0) return;

    /* The pixels it covers, clipped against the frame. */
    x0 = unit_col(dstx);  x1 = unit_col(dstx + w);
    y0 = unit_row(dsty);  y1 = unit_row(dsty + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > VW) x1 = VW;
    if (y1 > VH) y1 = VH;
    if (x0 >= x1 || y0 >= y1) return;

    n = x1 - x0;
    stride = VW;

    /* Stepped in 16.16 and clamped: the rounding at either end can leave the
       first or last pixel centre a hair outside the source rectangle. */
    inv = 1.0 / amiga_mode.ppux;
    u  = fxround(((x0 + 0.5 - amiga_mode.orgx) * inv - dstx) * 65536.0);
    du = fxround(inv * 65536.0);
    for (x = 0; x < n; x++, u += du) {
        int c = (int)(u >> 16);
        if (c < 0) c = 0; else if (c >= w) c = w - 1;
        omap[x] = (UWORD)(srcx + c);
    }
    linear = (omap[n - 1] - omap[0] == n - 1);

    inv = 1.0 / amiga_mode.ppuy;
    d = amiga_chunky + (size_t)y0 * stride + x0;

    for (y = y0; y < y1; y++, d += stride) {
        int r = (int)floor((y + 0.5 - amiga_mode.orgy) * inv - dsty);
        if (r < 0) r = 0; else if (r >= h) r = h - 1;
        s = screenbuffer + (size_t)(srcy + r) * screenbufferwidth;

        if (!ingame) {
            if (linear)
                memcpy(d, s + omap[0], n);
            else
                for (x = 0; x < n; x++)
                    d[x] = s[omap[x]];
        } else {
            for (x = 0; x < n; x++) {
                unsigned char c = s[omap[x]];
                if (c != 255) d[x] = c;
            }
        }
    }
}

void ShowPartialOverlay(int x, int y, int w, int h, int statusbar) {
    int i;

    if (statusbar == 1) {
        /* The 320 unit status bar, pinned to the bottom of what the frame
           actually shows - above the bottom of the 240 unit area when the
           view is cropped short, below it when the view is taller. */
        double bottom = view_bottom();
        double left   = -amiga_mode.orgx / amiga_mode.ppux;
        double right  = (VW - amiga_mode.orgx) / amiga_mode.ppux;

        blit_overlay(x, y, x, bottom - statusbaryvisible, w, h);

        /* Widen it to the edges of the view by repeating the right hand
           twenty columns, which is what the OpenGL path does with its
           statusbar == 2 passes.  A widescreen view needs several. */
        for (i = 0; 340 + i < right || -i > left; i += 20) {
            ShowPartialOverlay(340 + i, statusbaryoffset, 20, statusbaryvisible, 2);
            ShowPartialOverlay(0 - i,   statusbaryoffset, 20, statusbaryvisible, 2);
        }
        return;
    }

    if (statusbar == 2) {
        blit_overlay(340, statusbaryoffset, x,
                     view_bottom() - statusbaryvisible,
                     w, statusbaryvisible);
        return;
    }

    /* Ordinary overlay: source (x,y), destination shifted by the scroll
       offset used during the intro. */
    y -= visiblescreenyoffset;
    if (x + w > 360) w = 360 - x;
    if (y + h > 240) h = 240 - y;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (w <= 0 || h <= 0) return;

    blit_overlay(x, y + visiblescreenyoffset, x, y, w, h);
}

void UploadPartialOverlay(int x, int y, int w, int h) {
    if (!ClipToBuffer(&x, &y, &w, &h))
        return;
    if (menuing) return;
    ShowPartialOverlay(x - 1, y - 1, w + 2, h + 2, 0);
}

void UploadOverlay(void) {
    settransferpalette();
    texturecreationneeded = 0;
}

void ShowStatusBar(void) {
    mixing = 1;
    ShowPartialOverlay(20, statusbaryoffset, 320, statusbaryvisible, 1);
    mixing = 0;
}

void SetVisibleScreenOffset(K_UINT16 offset) {
    int y = offset / 90;

    R_ClearScreen();
    visiblescreenyoffset = y;
    ShowPartialOverlay(0, 0 + y, 360, 240, 0);
}

/* ------------------------------------------------------------ span drawing */

/* Vertical texture-mapped span.  `col` is the 64 pixel texture column,
   `ytop`/`ybot` the (unclipped) screen rows the wall covers, in fixed point
   with `yshift` fraction bits, `skipkey` makes index 255 transparent. */
static void draw_span(unsigned char *dstcol, const unsigned char *tex,
                      K_INT32 ytop, K_INT32 ybot, int yshift,
                      int shaded, int skipkey)
{
    int y0, y1, y, height;
    const int stride = VW;
    K_INT32 v, vstep;

    height = (int)((ybot - ytop) >> yshift);
    if (height <= 0) return;

    vstep = (height < RECIP_MAX) ? vrecip[height]
                                 : (K_INT32)((64L << 16) / height);

    y0 = (int)(ytop >> yshift);
    y1 = (int)(ybot >> yshift);

    v = 0;
    if (y0 < VIEW_TOP) {
        v = (K_INT32)((VIEW_TOP - y0) * (long)vstep);
        y0 = VIEW_TOP;
    }
    if (y1 > VIEW_BOT) y1 = VIEW_BOT;
    if (y0 >= y1) return;

    dstcol += (size_t)y0 * stride;

    if (skipkey) {
        if (shaded) {
            for (y = y0; y < y1; y++) {
                unsigned char c = tex[(v >> 16) & 63];
                if (c != 255) *dstcol = shadetab[c];
                dstcol += stride; v += vstep;
            }
        } else {
            for (y = y0; y < y1; y++) {
                unsigned char c = tex[(v >> 16) & 63];
                if (c != 255) *dstcol = c;
                dstcol += stride; v += vstep;
            }
        }
    } else {
        if (shaded) {
            for (y = y0; y < y1; y++) {
                *dstcol = shadetab[tex[(v >> 16) & 63]];
                dstcol += stride; v += vstep;
            }
        } else {
            for (y = y0; y < y1; y++) {
                *dstcol = tex[(v >> 16) & 63];
                dstcol += stride; v += vstep;
            }
        }
    }
}

/*
 * The common path for every upright quad: walls, doors and billboards.
 *
 * (wx1,wy1)-(wx2,wy2) is the quad in world coordinates, running from the
 * ceiling (z=0) to the floor (z=1024).  `t0`/`t1` are the texture columns at
 * the two ends, scaled 0..64.
 *
 * `writez` is set for solid walls, which own their columns from then on.
 * `testz` is set for anything that has to respect walls already drawn.
 * `keycolour` makes index 255 transparent (sprites and doors).
 * `depthonly` draws nothing but still claims the columns, which is how the
 * invisible wall hides what is behind it.
 */
static void draw_upright_quad(double wx1, double wy1, double wx2, double wy2,
                              int texnum, double t0, double t1,
                              int shaded, int writez, int testz,
                              int keycolour, int depthonly)
{
    double dx1 = wx1 - cam_ex, dy1 = wy1 - cam_ey;
    double dx2 = wx2 - cam_ex, dy2 = wy2 - cam_ey;
    double d1 =  cam_fx*dx1 + cam_fy*dy1;
    double d2 =  cam_fx*dx2 + cam_fy*dy2;
    double e1 =  cam_fx*dy1 - cam_fy*dx1;
    double e2 =  cam_fx*dy2 - cam_fy*dx2;
    double near = (double)neardist;
    double sx1, sx2, invd1, invd2, tovd1, tovd2;
    double topk, botk;
    double spaninv, didx;
#define SEGLEN 16
    double zstep, ytstep, ybstep, yscale;
    K_INT32 Z, dZ, YT, dYT, YB, dYB;
    int yshift;
    const unsigned char *texbase;
    int xa, xb, x, seg;

    if (d1 < near && d2 < near)
        return;

    /* Clip the near end of the quad against the near plane, interpolating the
       texture coordinate with it. */
    if (d1 < near) {
        double f = (near - d1) / (d2 - d1);
        e1 += (e2 - e1) * f;
        t0 += (t1 - t0) * f;
        d1  = near;
    } else if (d2 < near) {
        double f = (near - d2) / (d1 - d2);
        e2 += (e1 - e2) * f;
        t1 += (t0 - t1) * f;
        d2  = near;
    }

    sx1 = proj_cx + proj_x * e1 / d1;
    sx2 = proj_cx + proj_x * e2 / d2;

    if (sx1 > sx2) {
        double s;
        s = sx1; sx1 = sx2; sx2 = s;
        s = d1;  d1  = d2;  d2  = s;
        s = t0;  t0  = t1;  t1  = s;
    }

    if (sx2 <= 0.0 || sx1 >= (double)VW || sx2 - sx1 < 1e-6)
        return;

    invd1 = 1.0 / d1;
    invd2 = 1.0 / d2;
    tovd1 = t0 * invd1;
    tovd2 = t1 * invd2;

    /* Screen rows of the top and bottom edges are both linear in 1/d, so they
       can be interpolated straight across the span. */
    topk = -proj_y * cam_ez;
    botk = -proj_y * (cam_ez - 1024.0);

    xa = (int)ceil(sx1 - 0.5);
    xb = (int)ceil(sx2 - 0.5);
    if (xa < 0) xa = 0;
    if (xb > VW) xb = VW;
    if (xa >= xb) return;

    texbase = walseg[texnum];

    /*
     * 1/d is linear in screen x, and the depth value, the top row and the
     * bottom row are each an affine function of 1/d - so all three are linear
     * in x and can be walked with one 32 bit add apiece.  Only the texture
     * column needs a real perspective divide, and that is still done once per
     * 16 column segment and interpolated in between.
     *
     * Doing it the obvious way instead costs about eleven soft float calls per
     * column, which is what made this loop the second most expensive thing in
     * the renderer on a machine with no FPU.
     */
    spaninv = 1.0 / (sx2 - sx1);
    didx    = (invd2 - invd1) * spaninv;

    /* The three steps are constant over the whole quad; the values themselves
       are reseeded at each segment boundary from the exactly computed 1/d
       there, which keeps a rounded step from accumulating across the width
       and stops the depth values drifting at the far end of a long wall. */
    zstep = ZSCALE * didx;

    /*
     * Each of these has to survive as a 32 bit accumulator, so bound them over
     * the whole span rather than at its first column: 1/d is monotonic along
     * the quad, so it is largest at one of the two ends.  The depth has a
     * fixed scale; a projection far outside the range the game uses would not
     * fit, and a garbage span is worse than a missing wall.
     *
     * The rows are another matter.  A wall at the near plane reaches about
     * 64 x proj_y rows off screen with the camera at the top or bottom of its
     * range, which is fine in 16.16 at 240 lines but not at 1080.  Only the
     * whole part of a row is ever used, so the fraction gives way instead:
     * the row scale drops a bit at a time until the span fits.  The top and
     * bottom are bounded together because draw_span() subtracts them.
     */
    {
        double idmax = (invd1 > invd2 ? invd1 : invd2) * 1.05;
        double rows  = fabs((double)horizon_row)
                     + (fabs(topk) + fabs(botk)) * idmax;
        double rstep = (fabs(topk) + fabs(botk)) * fabs(didx) * SEGLEN;

        if (!(ZSCALE * idmax + fabs(zstep) * SEGLEN < 2.0e9))
            return;

        yshift = 16;
        while ((rows + rstep) * (double)(1L << yshift) >= 2.0e9)
            if (--yshift < 4)
                return;
        yscale = (double)(1L << yshift);
    }

    ytstep = topk * didx * yscale;
    ybstep = botk * didx * yscale;

    dZ  = fxround(zstep);
    dYT = fxround(ytstep);
    dYB = fxround(ybstep);


    for (seg = xa; seg < xb; seg += SEGLEN) {
        int xe = seg + SEGLEN;
        double a0, a1, id0, id1, tv0, tv1, tc0, tc1;
        K_INT32 tcur, tinc;
        int n;

        if (xe > xb) xe = xb;
        n = xe - seg;

        a0 = ((double)seg + 0.5 - sx1) * spaninv;
        a1 = ((double)(xe - 1) + 0.5 - sx1) * spaninv;

        id0 = invd1 + (invd2 - invd1) * a0;
        id1 = invd1 + (invd2 - invd1) * a1;

        if (id0 <= 0.0 || id1 <= 0.0)
            continue;

        Z  = fxround(ZSCALE * id0);
        YT = fxround(((double)horizon_row + topk * id0) * yscale);
        YB = fxround(((double)horizon_row + botk * id0) * yscale);

        tv0 = tovd1 + (tovd2 - tovd1) * a0;
        tv1 = tovd1 + (tovd2 - tovd1) * a1;

        tc0 = tv0 / id0;
        tc1 = tv1 / id1;

        /* tcur is a texture column that is about to be floored to a whole
           texel, so truncating it is exactly right and rounding it to nearest
           would pick the wrong texel just below a boundary.  tinc is a step
           that gets added up, so that one does want rounding. */
        tcur = (K_INT32)(tc0 * 65536.0);
        tinc = (n > 1) ? fxround((tc1 - tc0) * 65536.0 / (n - 1)) : 0;

        for (x = seg; x < xe;
             x++, tcur += tinc, Z += dZ, YT += dYT, YB += dYB) {
            if (Z <= 0) continue;
            if (testz && Z <= zbuf[x]) continue;
            if (writez) zbuf[x] = Z;
            if (depthonly) continue;

            draw_span(amiga_chunky + x, texbase + (((tcur >> 16) & 63) << 6),
                      YT, YB, yshift, shaded, keycolour);
        }
    }
#undef SEGLEN
}

/* ------------------------------------------------------------- scene setup */

void R_BeginScene(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs,
                  double aspwv, double asphv, int yy) {
    unsigned char ceilcol, floorcol;
    int i, split;

    if (!amiga_chunky) return;

    cam_fx = sintable[(angs + 512) & 2047] / 65536.0;
    cam_fy = sintable[angs & 2047] / 65536.0;
    {
        double len = sqrt(cam_fx*cam_fx + cam_fy*cam_fy);
        if (len > 1e-9) { cam_fx /= len; cam_fy /= len; }
    }

    cam_ex = posxs;
    cam_ey = posys;
    cam_ez = poszs * 16.0;

    /*
     * The projection is fixed in game units - the view axis at x = 180, the
     * horizon at y = 120, 180 units to one unit of e/d across and 160 down,
     * which is the OpenGL frustum's 90 degrees across a 360 unit wide 4:3
     * view.  A widescreen view does not change that scale: it shows more of
     * the same projection either side, and aspw tells the ray caster how
     * much more to look for.  So aspwv and asphv are not needed here.
     */
    (void)aspwv; (void)asphv;

    proj_x  = 180.0 * amiga_mode.ppux;
    proj_y  = 160.0 * amiga_mode.ppuy;
    proj_cx = 180.0 * amiga_mode.ppux + amiga_mode.orgx;
    horizon_row = unit_row(120.0);

    /* Flat ceiling above the horizon, flat floor below - the same two colours
       the OpenGL path clears and fills with. */
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        floorcol = 0x85;
    else
        floorcol = 0x84;
    ceilcol = 0xe3;

    split = unit_row(240 - yy / 90);
    if (split < 0) split = 0;
    if (split > VH) split = VH;

    memset(amiga_chunky, ceilcol, (size_t)split * VW);
    memset(amiga_chunky + (size_t)split * VW, floorcol, (size_t)(VH - split) * VW);

    for (i = 0; i < VW; i++)
        zbuf[i] = 0;
}

void R_DrawWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                K_INT16 j, double v0, double v1,
                int shaded, int transparent) {
    /* graphx.c sets `shaded` for the brighter of the two wall orientations;
       the OpenGL path multiplies the other one by 0.9, which in this palette
       is one step down the brightness ramp. */
    draw_upright_quad((double)x1, (double)y1, (double)x2, (double)y2,
                      j, v0 * 64.0, v1 * 64.0,
                      shaded ? 0 : 1,
                      1, 1, 0, transparent);
}

void R_EndWalls(void) {
}

void R_EndScene(void) {
}

/* --------------------------------------------------------------- sprites */

void R_DrawBillboard(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     K_INT16 j, double v0, double v1,
                     K_INT16 ang, K_INT16 playerang) {
    if (ang == 0) {
        draw_upright_quad((double)x1, (double)y1, (double)x2, (double)y2,
                          j, v0 * 64.0, v1 * 64.0, 0, 0, 1, 1, 0);
        return;
    }

    /* Spinning sprites (warps, fans, some bullets) turn about the view axis.
       They are small and rare, so a general affine screen-space quad is
       cheap enough. */
    {
        double cx = (x1 + x2) * 0.5, cy = (y1 + y2) * 0.5, cz = 512.0;
        double th = ang / 2048.0 * 2.0 * M_PI;
        double ca = cos(th), sa = sin(th);
        double ax = cam_fx, ay = cam_fy, az = 0.0;
        double px[4], py[4], pz[4];
        double sxv[4], syv[4];
        double tu[4] = { 0.0, 64.0, 64.0,  0.0 };
        double tv[4] = { 0.0,  0.0, 64.0, 64.0 };
        int i;

        (void)playerang;

        px[0] = x1; py[0] = y1; pz[0] = 0.0;
        px[1] = x1; py[1] = y1; pz[1] = 1024.0;
        px[2] = x2; py[2] = y2; pz[2] = 1024.0;
        px[3] = x2; py[3] = y2; pz[3] = 0.0;

        /* Texture coordinates: S (0..1 over z) indexes the texture row, T
           (v0..v1 along the quad) the texture column. */
        tu[0] = v0 * 64.0; tv[0] = 0.0;
        tu[1] = v0 * 64.0; tv[1] = 64.0;
        tu[2] = v1 * 64.0; tv[2] = 64.0;
        tu[3] = v1 * 64.0; tv[3] = 0.0;

        for (i = 0; i < 4; i++) {
            double rx = px[i] - cx, ry = py[i] - cy, rz = pz[i] - cz;
            double dot = ax*rx + ay*ry + az*rz;
            double crx = ay*rz - az*ry;
            double cry = az*rx - ax*rz;
            double crz = ax*ry - ay*rx;
            double nx = rx*ca + crx*sa + ax*dot*(1.0 - ca);
            double ny = ry*ca + cry*sa + ay*dot*(1.0 - ca);
            double nz = rz*ca + crz*sa + az*dot*(1.0 - ca);
            double wx = cx + nx, wy = cy + ny, wz = cz + nz;
            double dxv = wx - cam_ex, dyv = wy - cam_ey;
            double d = cam_fx*dxv + cam_fy*dyv;
            double e = cam_fx*dyv - cam_fy*dxv;

            if (d < (double)neardist) return;   /* too close; skip the frame */

            sxv[i] = proj_cx + proj_x * e / d;
            syv[i] = horizon_row - proj_y * (cam_ez - wz) / d;
        }

        /* Rasterise the screen quad as two affine triangles. */
        {
            double cx2 = (px[0]+px[2])*0.5 - cam_ex;
            double cy2 = (py[0]+py[2])*0.5 - cam_ey;
            double cd  = cam_fx*cx2 + cam_fy*cy2;
            K_INT32 z  = (cd > 0.0) ? (K_INT32)(ZSCALE / cd) : 0;

            softtri(sxv, syv, tu, tv, 0, 1, 2, j, z);
            softtri(sxv, syv, tu, tv, 0, 2, 3, j, z);
        }
    }
}

/*
 * Affine textured triangle with a single depth value, used by the spinning
 * sprites and by every 2D overlay sprite.
 *
 * The barycentric coordinates and the two texture coordinates are all affine
 * functions of the screen position, so each one is evaluated once at the top
 * left of the bounding box and then stepped by a constant per pixel and per
 * row.  That leaves the inner loop with five 32 bit adds and no division at
 * all, which matters enormously on a 68k with no FPU: the straightforward
 * formulation costs two __divdf3 calls and about twenty soft float calls per
 * pixel, and a close up spinning warp covers thousands of pixels.
 *
 * Both scales are chosen per triangle from how large the quantities actually
 * get over the clipped bounding box, so the accumulators cannot overflow and
 * every bit left over goes to the fraction.  That matters: the step values are
 * rounded once and then added up to 600 times, so a step that is half an ulp
 * out drags the last column 300 ulps off.  The barycentrics are only ever sign
 * tested, so scaling them by any positive constant is free.
 */

void softtri(double *sx, double *sy, double *tu, double *tv,
             int a, int b, int c, int texnum, K_INT32 z)
{
    const unsigned char *tex = walseg[texnum];
    double x0 = sx[a], y0 = sy[a], x1 = sx[b], y1 = sy[b], x2 = sx[c], y2 = sy[c];
    double det = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0);
    double iu0 = tu[a], iu1 = tu[b], iu2 = tu[c];
    double iv0 = tv[a], iv1 = tv[b], iv2 = tv[c];
    double inv;
    double a0, b0, c0, a1, b1, c1, a2, b2, c2;
    double au, bu, cu, av, bv, cv;
    double miny, maxy, minx, maxx;
    double px0, py0, lmax, lscale, dmax, tscale;
    K_INT32 l0row, l1row, l2row;
    K_INT32 dl0dx, dl1dx, dl2dx, dl0dy, dl1dy, dl2dy;
    K_UINT32 urow, vrow, dudx, dvdx, dudy, dvdy;
    int xlo, xhi, ylo, yhi, xi, yi, i, tshift;

    if (fabs(det) < 1e-9) return;

    miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
    maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
    minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
    maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;

    /* Written so that a NaN coordinate fails the test and bails out rather
       than reaching an undefined double to int conversion below. */
    if (!(minx > -1.0e6 && maxx < 1.0e6 && miny > -1.0e6 && maxy < 1.0e6))
        return;

    xlo = (int)floor(minx); if (xlo < 0) xlo = 0;
    xhi = (int)ceil(maxx);  if (xhi > VW) xhi = VW;
    ylo = (int)floor(miny); if (ylo < VIEW_TOP) ylo = VIEW_TOP;
    yhi = (int)ceil(maxy);  if (yhi > VIEW_BOT) yhi = VIEW_BOT;
    if (xlo >= xhi || ylo >= yhi) return;

    /* l1 and l2 written out as a*x + b*y + c, and l0 from l0+l1+l2 == 1. */
    inv = 1.0 / det;
    a1 =  (y2 - y0) * inv;
    b1 = -(x2 - x0) * inv;
    c1 =  (x2*y0 - x0*y2) * inv;
    a2 = -(y1 - y0) * inv;
    b2 =  (x1 - x0) * inv;
    c2 =  (x0*y1 - x1*y0) * inv;
    a0 = -(a1 + a2);
    b0 = -(b1 + b2);
    c0 = 1.0 - (c1 + c2);

    au = a0*iu0 + a1*iu1 + a2*iu2;
    bu = b0*iu0 + b1*iu1 + b2*iu2;
    cu = c0*iu0 + c1*iu1 + c2*iu2;
    av = a0*iv0 + a1*iv1 + a2*iv2;
    bv = b0*iv0 + b1*iv1 + b2*iv2;
    cv = c0*iv0 + c1*iv1 + c2*iv2;

    px0 = xlo + 0.5;
    py0 = ylo + 0.5;

    /* Largest barycentric magnitude over the four corners of the clipped box.
       They are affine, so their extremes over a rectangle are at its corners. */
    lmax = 1.0;
    for (i = 0; i < 4; i++) {
        double px = (i & 1) ? (xhi - 0.5) : px0;
        double py = (i & 2) ? (yhi - 0.5) : py0;
        double t1 = a1*px + b1*py + c1;
        double t2 = a2*px + b2*py + c2;
        double t0 = 1.0 - t1 - t2;
        if (fabs(t0) > lmax) lmax = fabs(t0);
        if (fabs(t1) > lmax) lmax = fabs(t1);
        if (fabs(t2) > lmax) lmax = fabs(t2);
    }

    /* 2^29 rather than 2^31 for headroom.  The barycentrics are affine, so
       over the box they never exceed their values at its corners and the
       accumulators stay within 2^29 however many rows and columns it spans;
       what is left over absorbs the rounding in the steps. */
    lscale = 536870912.0 / lmax;

    l0row = fxround((a0*px0 + b0*py0 + c0) * lscale);
    l1row = fxround((a1*px0 + b1*py0 + c1) * lscale);
    l2row = fxround((a2*px0 + b2*py0 + c2) * lscale);
    dl0dx = fxround(a0 * lscale); dl0dy = fxround(b0 * lscale);
    dl1dx = fxround(a1 * lscale); dl1dy = fxround(b1 * lscale);
    dl2dx = fxround(a2 * lscale); dl2dy = fxround(b2 * lscale);

    /*
     * u and v get shifted back to whole texels, so their scale is a power of
     * two, and it is chosen from the size of the *steps* rather than from how
     * large u and v get over the box.  Sizing it by the values would be a
     * trap: on a sliver triangle the box corners lie far outside it, so the
     * values there blow up, the scale collapses, and precision is lost in the
     * middle of the triangle where it is the only thing that matters.
     *
     * The values themselves may then overflow out at those corners.  That is
     * harmless and deliberate - the accumulators are unsigned, so they wrap
     * as exact modular arithmetic, and by the time the walk re-enters the
     * triangle (the only place u and v are ever read) the true value is back
     * inside 32 bits and the low bits are exactly right.
     */
    dmax = 1.0;
    if (fabs(au) > dmax) dmax = fabs(au);
    if (fabs(bu) > dmax) dmax = fabs(bu);
    if (fabs(av) > dmax) dmax = fabs(av);
    if (fabs(bv) > dmax) dmax = fabs(bv);

    tshift = 24;
    while (tshift > 8 && dmax * (double)(1UL << tshift) > 1073741824.0)
        tshift--;
    tscale = (double)(1UL << tshift);

    urow = fxwrap((au*px0 + bu*py0 + cu) * tscale);
    vrow = fxwrap((av*px0 + bv*py0 + cv) * tscale);
    dudx = (K_UINT32)fxround(au * tscale); dudy = (K_UINT32)fxround(bu * tscale);
    dvdx = (K_UINT32)fxround(av * tscale); dvdy = (K_UINT32)fxround(bv * tscale);

    for (yi = ylo; yi < yhi; yi++) {
        K_INT32 l0 = l0row, l1 = l1row, l2 = l2row;
        K_UINT32 u = urow, v = vrow;
        unsigned char *dst = amiga_chunky + (size_t)yi * VW;

        for (xi = xlo; xi < xhi;
             xi++, l0 += dl0dx, l1 += dl1dx, l2 += dl2dx, u += dudx, v += dvdx) {
            K_INT32 tc, tr;
            unsigned char col;

            /* All three non-negative, i.e. no sign bit set in any of them. */
            if ((l0 | l1 | l2) < 0) continue;
            if (z && z <= zbuf[xi]) continue;

            tc = (K_INT32)u >> tshift; if ((K_UINT32)tc > 63) tc = (tc < 0) ? 0 : 63;
            tr = (K_INT32)v >> tshift; if ((K_UINT32)tr > 63) tr = (tr < 0) ? 0 : 63;

            col = tex[(tc << 6) + tr];
            if (col != 255)
                dst[xi] = col;
        }

        l0row += dl0dy; l1row += dl1dy; l2row += dl2dy;
        urow  += dudy;  vrow  += dvdy;
    }
}

/* Integer floor and ceiling division; C's own division truncates toward zero,
   which gives the wrong span edge for a negative numerator. */
static K_INT32 fdiv(K_INT32 a, K_INT32 b) {
    K_INT32 q = a / b, r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) q--;
    return q;
}

static K_INT32 cdiv(K_INT32 a, K_INT32 b) {
    K_INT32 q = a / b, r = a % b;
    if (r != 0 && ((r < 0) == (b < 0))) q++;
    return q;
}

/* Round to nearest, positive divisor only.  The quotients below are a start
   value and a step that is then added once per column, so rounding the step
   rather than truncating it halves the drift across a row. */
static K_INT32 rdivp(K_INT32 a, K_INT32 b) {
    return (a < 0) ? -(((-a) + (b >> 1)) / b) : (a + (b >> 1)) / b;
}

/*
 * Floor decals: a 1024x1024 texture lying flat on the floor.
 *
 * With no pitch every screen row sits at a constant distance, so the world
 * position walks linearly across the row.  Writing the distance to row
 * horizon_row + r as kdist/r makes every per-row quantity a constant over r,
 * which leaves one integer division each for the start and the step instead
 * of the floating point divide and multiply the obvious form needs.
 *
 * The columns the decal actually covers are then solved for directly rather
 * than testing every one of them, which matters because a decal usually covers
 * a small part of the screen and the old loop paid full price for every row
 * from the horizon down whether it drew anything or not.
 */
void R_DrawFloorSprite(K_UINT16 x, K_UINT16 y, K_INT16 j) {
    const unsigned char *tex = walseg[j];
    double zfloor = 1024.0 - cam_ez;
    double kdist, axd, ayd, bxd, byd, oxd, oyd, m, scale;
    K_INT32 AX, AY, BX, BY, OX, OY, HALF, zmul;
    int shift, rmax, row, rlast;

    if (zfloor <= 0.0) return;

    kdist = proj_y * zfloor;
    /* Below this the nearest row is already inside the near plane, so the
       loop would draw nothing; it also keeps zmul below in range. */
    if (kdist < (double)neardist) return;

    /* World position at screen (horizon_row + r, col):
         rx = oxd + axd/r + col * bxd/r,  and likewise for y.  */
    axd =  kdist * (cam_fx + (proj_cx - 0.5) * cam_fy / proj_x);
    bxd = -cam_fy * kdist / proj_x;
    ayd =  kdist * (cam_fy - (proj_cx - 0.5) * cam_fx / proj_x);
    byd =  cam_fx * kdist / proj_x;
    oxd =  cam_ex - (double)x;
    oyd =  cam_ey - (double)y;

    /* Largest magnitude any of the fixed point quantities has to hold, and
       the biggest binary scale that keeps it inside 30 bits. */
    m = 512.0 + fabs(oxd) + fabs(axd);
    if (512.0 + fabs(oyd) + fabs(ayd) > m) m = 512.0 + fabs(oyd) + fabs(ayd);
    shift = 16;
    while (shift > 4 && m * (double)(1UL << shift) > 1.0e9)
        shift--;
    scale = (double)(1UL << shift);

    AX = fxround(axd * scale); BX = fxround(bxd * scale); OX = fxround(oxd * scale);
    AY = fxround(ayd * scale); BY = fxround(byd * scale); OY = fxround(oyd * scale);
    HALF = (K_INT32)512 << shift;

    /* z is 2^24/d and d is kdist/r, so z is simply proportional to r. */
    zmul = (K_INT32)(ZSCALE / kdist * 256.0);

    /* Rows past this one are nearer than the near plane. */
    rmax = (int)(kdist / (double)neardist);
    rlast = horizon_row + 1 + rmax;
    if (rlast > VIEW_BOT) rlast = VIEW_BOT;

    for (row = horizon_row + 1; row < rlast; row++) {
        int r = row - horizon_row;
        K_INT32 rx0 = OX + rdivp(AX, r), sx = rdivp(BX, r);
        K_INT32 ry0 = OY + rdivp(AY, r), sy = rdivp(BY, r);
        K_INT32 rx, ry, z;
        unsigned char *dst;
        int lo = 0, hi = VW, t0, t1, col;

        /* Columns where the world position is inside the decal square. */
        if (sx > 0) {
            t0 = cdiv(-HALF - rx0, sx); t1 = cdiv(HALF - rx0, sx);
        } else if (sx < 0) {
            t0 = fdiv(HALF - rx0, sx) + 1; t1 = fdiv(-HALF - rx0, sx) + 1;
        } else {
            if (rx0 < -HALF || rx0 >= HALF) continue;
            t0 = 0; t1 = VW;
        }
        if (t0 > lo) lo = t0;
        if (t1 < hi) hi = t1;

        if (sy > 0) {
            t0 = cdiv(-HALF - ry0, sy); t1 = cdiv(HALF - ry0, sy);
        } else if (sy < 0) {
            t0 = fdiv(HALF - ry0, sy) + 1; t1 = fdiv(-HALF - ry0, sy) + 1;
        } else {
            if (ry0 < -HALF || ry0 >= HALF) continue;
            t0 = 0; t1 = VW;
        }
        if (t0 > lo) lo = t0;
        if (t1 < hi) hi = t1;

        if (lo < 0) lo = 0;
        if (hi > VW) hi = VW;
        if (lo >= hi) continue;

        z   = (zmul * r) >> 8;
        rx  = rx0 + lo * sx;
        ry  = ry0 + lo * sy;
        dst = amiga_chunky + (size_t)row * VW;

        for (col = lo; col < hi; col++, rx += sx, ry += sy) {
            /* A shift floors, where the old double code's (int) cast
               truncated toward zero and so made the texel straddling the
               middle of the decal one world unit wider than the rest.  A
               shift is the convention the wall renderer already uses. */
            K_INT32 lx = ((rx >> shift) + 512) >> 4;
            K_INT32 ly = ((ry >> shift) + 512) >> 4;
            unsigned char c;

            /* The span is exact in real arithmetic; this catches the one
               texel either end that rounding can push outside. */
            if ((K_UINT32)lx > 63 || (K_UINT32)ly > 63) continue;
            if (z <= zbuf[col]) continue;

            c = tex[(lx << 6) + ly];
            if (c != 255)
                dst[col] = c;
        }
    }
}

/* ------------------------------------------------------------- 2D sprites */

void R_DrawSprite2D(K_INT16 x, K_INT16 y, K_INT16 siz, K_INT16 ang,
                    K_INT16 j, int usegameoversprite) {
    const unsigned char *tex;
    double s = siz / 256.0;
    double th = ang / 2048.0 * 2.0 * M_PI;
    double ca = cos(th), sa = sin(th);
    double v0 = walltexcoord[j][0], v1 = walltexcoord[j][1];
    double sxv[4], syv[4], tu[4], tv[4];
    int i;
    /* Quad corners in local 0..64 coordinates, in the same order the OpenGL
       path emits them. */
    static const double lx[4] = {  0.0, 64.0, 64.0,  0.0 };
    static const double ly[4] = {  0.0,  0.0, 64.0, 64.0 };

    (void)usegameoversprite;    /* we sample the one and only copy */

    if (!amiga_chunky) return;
    tex = walseg[j];
    if (!tex) return;

    for (i = 0; i < 4; i++) {
        double qx = lx[i] - 32.0, qy = ly[i] - 32.0;
        double rx = qx*ca - qy*sa;
        double ry = qx*sa + qy*ca;

        /* OpenGL y is up, screen rows go down. */
        sxv[i] = (x + s*rx) * amiga_mode.ppux + amiga_mode.orgx;
        syv[i] = (y - s*ry) * amiga_mode.ppuy + amiga_mode.orgy;

        /* S runs 1..0 down the quad and picks the texture row; T runs v0..v1
           across it and picks the texture column. */
        tu[i] = (v0 + (v1 - v0) * (lx[i] / 64.0)) * 64.0;
        tv[i] = (1.0 - ly[i] / 64.0) * 64.0;
    }

    softtri(sxv, syv, tu, tv, 0, 1, 2, j, 0);
    softtri(sxv, syv, tu, tv, 0, 2, 3, j, 0);
}

/* ---------------------------------------------------- overlays and extras */

void R_DrawVolumeBar(int vol, int type, float level) {
    int y0, x, y, xa, xf, xb, ya, yb, stride;

    (void)level;    /* no alpha in an 8 bit palette; the bar is solid */

    if (!amiga_chunky) return;

    /* The OpenGL path shifts the bar by 30 rows for the music/sound variant;
       do the same, then keep it inside the 240 unit area. */
    y0 = 110 - 30 * type;
    if (y0 < 0) y0 = 0;
    if (y0 + 20 > AMIGA_VIEW_H) y0 = AMIGA_VIEW_H - 20;

    /* 96..224 across, filled up to 96 + vol/2. */
    xa = unit_col(96);
    xf = unit_col(96 + (vol >> 1));
    xb = unit_col(224);
    ya = unit_row(y0);
    yb = unit_row(y0 + 20);
    if (xa < 0) xa = 0;
    if (xb > VW) xb = VW;
    if (ya < 0) ya = 0;
    if (yb > VH) yb = VH;
    stride = VW;

    for (y = ya; y < yb; y++) {
        unsigned char *row = amiga_chunky + (size_t)y * stride;
        for (x = xa; x < xb; x++)
            row[x] = (x < xf) ? (type ? 0x9f : 0x2f) : 0x10;
    }
}

int R_ReadPixelsBGR(unsigned char *dst, int w, int h) {
    int x, y;

    if (!amiga_chunky) return -1;

    /* BMP rows run bottom to top. */
    for (y = 0; y < h; y++) {
        int sy = h - 1 - y;
        const unsigned char *src = amiga_chunky + (size_t)sy * VW;
        for (x = 0; x < w; x++) {
            unsigned char c = (x < VW && sy < VH) ? src[x] : 0;
            *dst++ = (unsigned char)(palette[c*3+2] << 2);
            *dst++ = (unsigned char)(palette[c*3+1] << 2);
            *dst++ = (unsigned char)(palette[c*3+0] << 2);
        }
    }
    return 0;
}

/* Stereoscopic rendering is an OpenGL-only feature. */
void setup_stereo(int s) {
    (void)s;
    stereo = 0;
}

void picrot(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs) {
    picrot_view(posxs, posys, poszs, angs, aspw, asph);
}
