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

/* Where the rasterisers may draw.  For the labyrinth this is the view
   window - the whole frame, or a smaller box in the middle of it when the
   player has shrunk the view to save time - and the status bar is then
   composited over the bottom of the frame, as the OpenGL path does.  The 2D
   sprites the intro and the menus use get the whole frame. */
static int clip_x0, clip_y0, clip_x1, clip_y1;
#define VIEW_LEFT  clip_x0
#define VIEW_TOP   clip_y0
#define VIEW_RIGHT clip_x1
#define VIEW_BOT   clip_y1

static void clip_to_frame(void) {
    clip_x0 = clip_y0 = 0;
    clip_x1 = VW;
    clip_y1 = VH;
}

/*
 * The status bar is composited once and then kept.  While it is up,
 * R_BeginScene() keeps the labyrinth off the rows it covers, so nothing
 * needs drawing there again - not the flat fill, not the walls, not the
 * conversion to the screen - until the overlay under it changes or someone
 * else writes to those rows, which sets amiga_bar_stale.
 *
 * bar_after_scene records whether the last scene was followed by
 * ShowStatusBar(); only then does the next scene leave the bar rows alone,
 * and bar_kept says it did, so a scene drawn without the bar (the intro,
 * say) is never left showing an old one.
 */
int amiga_bar_stale = 1;
static int bar_after_scene, bar_kept;
static int bar_shown_vis = -1;      /* statusbaryvisible the frame holds */

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

/* The binary exponent of a positive, normal double, and 2^k as a double,
   straight from the bits - on a machine without an FPU a frexp() or a
   halving loop costs soft float calls for what is just a field in the word. */
/* rendertest:begin-dbits  (tools/rendertest lifts this block verbatim) */
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define DHI 0
#else
#define DHI 1
#endif
typedef union { double d; K_UINT32 w[2]; } dbits;
/* rendertest:end-dbits */

static int dexp2(double v) {
    dbits u;
    u.d = v;
    return (int)((u.w[DHI] >> 20) & 0x7ff) - 1023;
}

static double dpow2(int k) {
    dbits u;
    u.w[DHI]     = (K_UINT32)(k + 1023) << 20;
    u.w[1 - DHI] = 0;
    return u.d;
}

/* v * 2^k, exactly, by adjusting the exponent: the scales below are all
   powers of two, and a soft float multiply to apply one is a waste.  Zero
   stays zero; nothing here comes near the ends of the exponent range. */
static double dscale2(double v, int k) {
    dbits u;
    u.d = v;
    if (u.w[DHI] & 0x7ff00000)
        u.w[DHI] += (K_UINT32)k << 20;
    return u.d;
}

/* v * 2^k as a double, for a 64 bit integer v held as hi:lo.  Built from
   the bits, like dexp2(), so a soft float build pays no library calls; below
   2^53 it is exact, above it the low bits are dropped. */
static double dfrom64(long long v, int k) {
    unsigned long long m;
    K_UINT32 hi, lo, sign = 0;
    int p, sh;
    dbits u;

    if (!v) return 0.0;
    if (v < 0) { m = (unsigned long long)-v; sign = 0x80000000; }
    else       m = (unsigned long long)v;
    hi = (K_UINT32)(m >> 32);
    lo = (K_UINT32)m;

    /* p: the top set bit.  Bring it to bit 52 - bit 20 of the high word. */
    p = hi ? 63 - __builtin_clz(hi) : 31 - __builtin_clz(lo);   /* bfffo */
    sh = 52 - p;
    if (sh >= 32) {
        hi = lo << (sh - 32);
        lo = 0;
    } else if (sh > 0) {
        hi = (hi << sh) | (lo >> (32 - sh));
        lo <<= sh;
    } else if (sh < 0) {
        lo = (lo >> -sh) | (hi << (32 + sh));
        hi >>= -sh;
    }
    u.w[DHI]     = sign | ((K_UINT32)(p + 1023 + k) << 20) | (hi & 0xfffff);
    u.w[1 - DHI] = lo;
    return u.d;
}

/* Whether |v| < 2^k. */
static int dbelow2(double v, int k) {
    return dexp2(v) < k;
}

/* Whether |v| < 2e9, i.e. comfortably fits in 32 bits once rounded; the
   exponent settles nearly every case without a soft float compare. */
static int dfits(double v) {
    return dbelow2(v, 30) || fabs(v) < 2.0e9;
}

/*
 * Round a double to the nearest integer rather than toward zero.  Every fixed
 * point step below is rounded once and then added hundreds of times, so a step
 * that is half an ulp out drags the far end of the span badly off.
 *
 * Without an FPU the obvious v + 0.5 costs a soft float compare and add, and
 * the renderer does it a dozen times a wall.  The same rounding - half away
 * from zero - done on the bits is a few shifts: floor(2|v|), plus one, halved.
 * It gives the identical result for every |v| below 2^31, which is all it is
 * ever asked for.
 */
static K_INT32 fxround(double v) {
#ifndef __HAVE_68881__
    dbits u;
    K_UINT32 hi, lo, m, x;
    int e, sh;

    u.d = v;
    hi = u.w[DHI];
    lo = u.w[1 - DHI];
    e  = (int)((hi >> 20) & 0x7ff) - 1023;
    if (e < -1)                         /* |v| < 0.5, zero, denormal */
        return 0;
    if (e > 30)                         /* out of range, infinite or NaN */
        return (hi & 0x80000000) ? -0x7fffffff - 1 : 0x7fffffff;
    m  = (hi & 0xfffff) | 0x100000;     /* the 53 bit mantissa is m:lo */
    sh = 51 - e;                        /* floor(2|v|) = mantissa >> sh */
    x  = sh >= 32 ? m >> (sh - 32) : (m << (32 - sh)) | (lo >> sh);
    x  = (x + 1) >> 1;
    return (hi & 0x80000000) ? -(K_INT32)x : (K_INT32)x;
#else
    return (K_INT32)(v < 0.0 ? v - 0.5 : v + 0.5);
#endif
}

/* Round to nearest and reduce modulo 2^32.  Converting an out of range double
   to an integer is undefined and, on most hardware, saturates rather than
   wrapping - so an accumulator that is meant to wrap has to have its starting
   value reduced here rather than by the cast.  The range test keeps the
   expensive path off the common case. */
static K_UINT32 fxwrap(double v) {
    if (dbelow2(v, 30) || (v > -2147483008.0 && v < 2147483008.0))
        return (K_UINT32)fxround(v);
    v = fmod(v, 4294967296.0);
    if (v < 0.0) v += 4294967296.0;
    if (v >= 4294967296.0) v = 0.0;          /* only reachable via NaN */
    return (K_UINT32)v;
}

/* (hi:lo) / d for a quotient known to fit in 32 bits.  The 68020, 030 and
   040 do that in one divu.l; the 060 dropped the 64 bit forms, and gcc would
   otherwise call a library routine for it everywhere. */
static K_UINT32 udiv64(K_UINT32 hi, K_UINT32 lo, K_UINT32 d) {
#if defined(__mc68020__) || defined(__mc68030__) || defined(__mc68040__)
    __asm__("divu.l %2,%0:%1" : "+d"(hi), "+d"(lo) : "dm"(d) : "cc");
    return lo;
#else
    return (K_UINT32)((((unsigned long long)hi << 32) | lo) / d);
#endif
}

/*
 * (tv << sh) / z, truncated, for z > 0: a texture column in 16.16 from its
 * perspective numerator t/d and 1/d, each at its own power of two scale,
 * with sh making up the difference plus the 16 fraction bits.  One 64 by 32
 * bit divide, where the double version it replaced was a soft float divide.
 */
static K_INT32 tcdiv(K_INT32 tv, K_INT32 z, int sh) {
    K_UINT32 t, hi, lo, q;
    int neg = tv < 0;

    t = neg ? (K_UINT32)-tv : (K_UINT32)tv;
    if (sh <= 0) {
        hi = 0;
        lo = sh > -32 ? t >> -sh : 0;
    } else if (sh < 32) {
        hi = t >> (32 - sh);
        lo = t << sh;
    } else {
        hi = sh < 64 ? t << (sh - 32) : 0;
        lo = 0;
    }
    if (hi >= (K_UINT32)z >> 1)             /* 32768 texels or more */
        return neg ? -0x7fffffff : 0x7fffffff;
    q = udiv64(hi, lo, (K_UINT32)z);
    return neg ? -(K_INT32)q : (K_INT32)q;
}

/*
 * 1 / (D * 2^k) as a double, for an integer D > 0: D cut to its top 32 bits
 * and divided into 2^63 by one divu.l.  Good to a couple of parts in 2^32,
 * where a soft float divide costs as much as a few hundred of these.
 */
static double recip64(long long D, int k) {
    unsigned long long m = (unsigned long long)D;
    K_UINT32 hi = (K_UINT32)(m >> 32), lo = (K_UINT32)m, dn;
    int p, sh;

    p = hi ? 63 - __builtin_clz(hi) : 31 - __builtin_clz(lo);   /* bfffo */
    sh = p - 31;                        /* dn = D >> sh, in [2^31, 2^32) */
    if (sh >= 32)     dn = hi >> (sh - 32);
    else if (sh > 0)  dn = (lo >> sh) | (hi << (32 - sh));
    else              dn = lo << -sh;
    return dfrom64((long long)udiv64(0x7fffffff, 0xffffffff, dn), -63 - sh - k);
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
    clip_to_frame();

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
    if (amiga_chunky) {
        memset(amiga_chunky, 0, (size_t)VW * VH);
        amiga_mark_all_dirty();
    }
    amiga_bar_stale = 1;
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
    static K_INT16 shown = -1;

    /* A fade with mixing set is for the overlay alone: the level story pulses
       its text with fade(j) between fade(27)s for the view.  OpenGL tints the
       text; reloading the one palette here would flash the whole screen at j
       for part of every frame.  Hold just the text's pens - the grey ramp the
       story loads at 240 - at j instead. */
    if (mixing) {
        amiga_hold_pen_fade(240, 16);
        return;
    }

    /* Otherwise fading is free: it just reloads the hardware palette with the
       new factors applied.  The story loop asks for the same level twice a
       frame; skipping those keeps the text's pens held. */
    if (fadelevel == shown) return;
    shown = fadelevel;
    amiga_release_pen_fade();
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
    amiga_mark_dirty(x0, y0, x1, y1, 1);

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

    /* Anything that lands on the kept status bar means drawing it again. */
    if (y + h > 240 - statusbaryvisible) amiga_bar_stale = 1;

    blit_overlay(x, y + visiblescreenyoffset, x, y, w, h);
}

void UploadPartialOverlay(int x, int y, int w, int h) {
    if (!ClipToBuffer(&x, &y, &w, &h))
        return;
    /* The bar's part of the overlay changed; ShowStatusBar() shows it. */
    if (y + h + 1 > statusbaryoffset) amiga_bar_stale = 1;
    if (menuing) return;
    ShowPartialOverlay(x - 1, y - 1, w + 2, h + 2, 0);
}

/* A held menu: see amiga_hold_menu() in amiga_video.h.  `ovl` is the overlay
   rectangle the selector draws in, `frame` every frame pixel an upload of any
   part of it can reach - the same one unit margin UploadPartialOverlay() adds,
   less the intro's scroll offset, as ShowPartialOverlay() subtracts it. */
static struct {
    int ox, oy, ow, oh;
    int fx0, fy0, fx1, fy1;
    unsigned char *ovl, *frame;
} held;

int amiga_hold_menu(int x, int y, int w, int h) {
    int fx0, fy0, fx1, fy1, r;

    amiga_release_menu();
    if (!amiga_chunky || !screenbuffer || !ClipToBuffer(&x, &y, &w, &h))
        return 0;

    fx0 = unit_col(x - 1);
    fx1 = unit_col(x + w + 1);
    fy0 = unit_row(y - 1 - visiblescreenyoffset);
    fy1 = unit_row(y + h + 1 - visiblescreenyoffset);
    if (fx0 < 0) fx0 = 0;
    if (fy0 < 0) fy0 = 0;
    if (fx1 > VW) fx1 = VW;
    if (fy1 > VH) fy1 = VH;
    if (fx0 >= fx1 || fy0 >= fy1)
        return 0;

    held.ovl   = AllocVec((ULONG)w * h, MEMF_ANY);
    held.frame = AllocVec((ULONG)(fx1 - fx0) * (fy1 - fy0), MEMF_ANY);
    if (!held.ovl || !held.frame) {
        amiga_release_menu();
        return 0;
    }

    held.ox = x;   held.oy = y;   held.ow = w;   held.oh = h;
    held.fx0 = fx0; held.fy0 = fy0; held.fx1 = fx1; held.fy1 = fy1;

    for (r = 0; r < h; r++)
        memcpy(held.ovl + (size_t)r * w,
               screenbuffer + (size_t)(y + r) * screenbufferwidth + x, w);
    for (r = fy0; r < fy1; r++)
        memcpy(held.frame + (size_t)(r - fy0) * (fx1 - fx0),
               amiga_chunky + (size_t)r * VW + fx0, fx1 - fx0);
    return 1;
}

void amiga_restore_menu(void) {
    int r, fw = held.fx1 - held.fx0;

    if (!held.ovl) return;

    for (r = 0; r < held.oh; r++)
        memcpy(screenbuffer + (size_t)(held.oy + r) * screenbufferwidth + held.ox,
               held.ovl + (size_t)r * held.ow, held.ow);
    for (r = held.fy0; r < held.fy1; r++)
        memcpy(amiga_chunky + (size_t)r * VW + held.fx0,
               held.frame + (size_t)(r - held.fy0) * fw, fw);
    amiga_mark_dirty(held.fx0, held.fy0, held.fx1, held.fy1, 1);
    amiga_bar_stale = 1;
}

void amiga_release_menu(void) {
    if (held.ovl)   FreeVec(held.ovl);
    if (held.frame) FreeVec(held.frame);
    held.ovl = held.frame = NULL;
}

void UploadOverlay(void) {
    settransferpalette();
    texturecreationneeded = 0;
}

void ShowStatusBar(void) {
    bar_after_scene = 1;
    if (bar_kept && !amiga_bar_stale && bar_shown_vis == statusbaryvisible)
        return;     /* still on the frame from last time */

    mixing = 1;
    ShowPartialOverlay(20, statusbaryoffset, 320, statusbaryvisible, 1);
    mixing = 0;

    amiga_bar_stale = 0;
    bar_shown_vis = statusbaryvisible;
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
    unsigned char scol[64];
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

    /*
     * Shade the whole column once when the span is tall enough that doing so
     * is cheaper than a table lookup per pixel.  The inner loops then stay
     * on the plain (or key) path, which is the one the 68k unrolls hardest.
     */
    if (shaded && (y1 - y0) >= 24) {
        int i;
        for (i = 0; i < 64; i++)
            scol[i] = shadetab[tex[i]];
        tex = scol;
        shaded = 0;
    }

#ifdef __m68k__
    {
        /*
         * The 68k inner loops.  The texture coordinate is kept as a whole
         * word and a fraction word: add.w steps the fraction and leaves the
         * carry in X, addx.w adds it to the whole part with the step's whole
         * part, and the texel index is then already sitting in a register -
         * no shift, no bit field extract, no mask.
         *
         * Going without the mask means stopping before the coordinate
         * reaches 64.  The span's height and its first and last rows are
         * rounded separately, so the last pixel or two can get there; those
         * few go through the C loops below, which wrap exactly as before.
         */
        int n = y1 - y0;
        K_INT32 vlast = v + (K_INT32)(n - 1) * vstep;

        while (n > 0 && vlast >= (64L << 16)) {
            n--;
            vlast -= vstep;
        }
        if (n > 0) {
            short vi = (short)(v >> 16), vf = (short)v;
            short is = (short)(vstep >> 16), fs = (short)vstep;
            long  st = stride, c = 0;   /* c: upper bytes stay clear */
            unsigned char *d = dstcol;

            /* One pixel of each kind.  The common plain wall path unrolls
               eight to a dbra; the others stay at four, so the longer
               compare-and-shade sequences still fit the 020 I-cache. */
#define PX_STEP  "add.l %[st],%[d]\n\tadd.w %[fs],%[vf]\n\taddx.w %[is],%[vi]\n\t"
#define PX_PLAIN "move.b (%[tex],%[vi].w),(%[d])\n\t" PX_STEP
#define PX_SHADE "move.b (%[tex],%[vi].w),%[c]\n\t" \
                 "move.b (%[sh],%[c].w),(%[d])\n\t" PX_STEP
#define PX_KEY   "move.b (%[tex],%[vi].w),%[c]\n\tcmp.b #-1,%[c]\n\tjeq 2f\n\t" \
                 "move.b %[c],(%[d])\n2:\n\t" PX_STEP
#define PX_KEYSH "move.b (%[tex],%[vi].w),%[c]\n\tcmp.b #-1,%[c]\n\tjeq 2f\n\t" \
                 "move.b (%[sh],%[c].w),(%[d])\n2:\n\t" PX_STEP
#define PX_LOOPS(PX)                                                    \
    do {                                                                \
        short one_ = (short)((n & 3) - 1), four_ = (short)((n >> 2) - 1); \
        __asm__ volatile (                                              \
            "tst.w %[one]\n\t"                                          \
            "jmi 3f\n"                                                  \
            "1:\n\t" PX "dbra %[one],1b\n"                              \
            "3:\n\t"                                                    \
            "tst.w %[four]\n\t"                                         \
            "jmi 5f\n"                                                  \
            "4:\n\t" PX PX PX PX "dbra %[four],4b\n"                    \
            "5:"                                                        \
            : [d] "+a" (d), [vi] "+d" (vi), [vf] "+d" (vf),             \
              [one] "+d" (one_), [four] "+d" (four_), [c] "+d" (c)      \
            : [tex] "a" (tex), [sh] "a" (shadetab), [st] "d" (st),      \
              [fs] "d" (fs), [is] "d" (is)                              \
            : "cc", "memory");                                          \
    } while (0)
#define PX_LOOPS8(PX)                                                   \
    do {                                                                \
        short one_ = (short)((n & 7) - 1), eight_ = (short)((n >> 3) - 1); \
        __asm__ volatile (                                              \
            "tst.w %[one]\n\t"                                          \
            "jmi 3f\n"                                                  \
            "1:\n\t" PX "dbra %[one],1b\n"                              \
            "3:\n\t"                                                    \
            "tst.w %[eight]\n\t"                                        \
            "jmi 5f\n"                                                  \
            "4:\n\t" PX PX PX PX PX PX PX PX "dbra %[eight],4b\n"       \
            "5:"                                                        \
            : [d] "+a" (d), [vi] "+d" (vi), [vf] "+d" (vf),             \
              [one] "+d" (one_), [eight] "+d" (eight_), [c] "+d" (c)    \
            : [tex] "a" (tex), [sh] "a" (shadetab), [st] "d" (st),      \
              [fs] "d" (fs), [is] "d" (is)                              \
            : "cc", "memory");                                          \
    } while (0)

            if (!skipkey)
                if (!shaded) PX_LOOPS8(PX_PLAIN); else PX_LOOPS(PX_SHADE);
            else
                if (!shaded) PX_LOOPS(PX_KEY);   else PX_LOOPS(PX_KEYSH);
#undef PX_LOOPS8
#undef PX_LOOPS
#undef PX_KEYSH
#undef PX_KEY
#undef PX_SHADE
#undef PX_PLAIN
#undef PX_STEP
            y0 += n;
            if (y0 >= y1)               /* the usual case: no tail */
                return;
            dstcol = d;
            v += (K_INT32)n * vstep;
        }
    }
#endif

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

#ifdef __HAVE_68881__
/*
 * The common path for every upright quad: walls, doors and billboards.
 * This is the floating point version, for the builds with an FPU; the
 * fixed point one that follows it is for the builds without.
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
static void draw_upright_quad(K_INT32 iwx1, K_INT32 iwy1, K_INT32 iwx2, K_INT32 iwy2,
                              int texnum, K_INT32 it0, K_INT32 it1,
                              int shaded, int writez, int testz,
                              int keycolour, int depthonly)
{
    double wx1 = iwx1, wy1 = iwy1, wx2 = iwx2, wy2 = iwy2;
    double t0 = it0 / 65536.0, t1 = it1 / 65536.0;
    double d1, d2, e1, e2;
    double near = (double)neardist;
    double sx1, sx2, invd1, invd2, tovd1, tovd2;
    double topk, botk;
    double spaninv, didx;
#define SEGLEN 16
    double zstep, ytstep, ybstep;
    K_INT32 Z, dZ, YT, dYT, YB, dYB;
    K_INT32 sYT, sYB, sID, sTV;         /* at the start of each segment */
    K_INT32 gYT, gYB, gID, gTV;         /* from one segment to the next */
    K_INT32 dID, dTV;
    int zk;                             /* depth = sID >> zk */
    int tsh;                            /* column = (sTV << tsh) / sID */
    int yshift;
    const unsigned char *texbase;
    int xa, xb, x, seg;

    /* Depth and sideways offset of both ends. */
    d1 = cam_fx*(wx1 - cam_ex) + cam_fy*(wy1 - cam_ey);
    d2 = cam_fx*(wx2 - cam_ex) + cam_fy*(wy2 - cam_ey);
    e1 = cam_fx*(wy1 - cam_ey) - cam_fy*(wx1 - cam_ex);
    e2 = cam_fx*(wy2 - cam_ey) - cam_fy*(wx2 - cam_ex);

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

    invd1 = 1.0 / d1;
    invd2 = 1.0 / d2;
    sx1 = proj_cx + proj_x * e1 * invd1;
    sx2 = proj_cx + proj_x * e2 * invd2;

    if (sx1 > sx2) {
        double s;
        s = sx1;   sx1   = sx2;   sx2   = s;
        s = d1;    d1    = d2;    d2    = s;
        s = t0;    t0    = t1;    t1    = s;
        s = invd1; invd1 = invd2; invd2 = s;
    }

    if (sx2 <= (double)VIEW_LEFT || sx1 >= (double)VIEW_RIGHT || sx2 - sx1 < 1e-6)
        return;

    tovd1 = t0 * invd1;
    tovd2 = t1 * invd2;

    /* Screen rows of the top and bottom edges are both linear in 1/d, so they
       can be interpolated straight across the span. */
    topk = -proj_y * cam_ez;
    botk = -proj_y * (cam_ez - 1024.0);

    xa = (int)ceil(sx1 - 0.5);
    xb = (int)ceil(sx2 - 0.5);
    if (xa < VIEW_LEFT) xa = VIEW_LEFT;
    if (xb > VIEW_RIGHT) xb = VIEW_RIGHT;
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
    zstep = dscale2(didx, 24);          /* ZSCALE is 2^24 */

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
        double rstep = dscale2((fabs(topk) + fabs(botk)) * fabs(didx), 4);
        double need  = rows + rstep;

        if (!(dscale2(idmax, 24) + dscale2(fabs(zstep), 4) < 2.0e9))
            return;

        /* The largest row scale up to 16 bits that keeps need under 2e9:
           the exponent gets within one of it, a compare settles the rest. */
        yshift = 30 - dexp2(need);
        if (yshift > 16) yshift = 16;
        if (dscale2(need, yshift) >= 2.0e9) yshift--;
        if (yshift < 4)
            return;
    }

    ytstep = dscale2(topk * didx, yshift);
    ybstep = dscale2(botk * didx, yshift);

    dZ  = fxround(zstep);
    dYT = fxround(ytstep);
    dYB = fxround(ybstep);

    /*
     * The segments are reseeded from these rather than from doubles: depth,
     * top and bottom row, and the texture column's perspective pair t/d and
     * 1/d are all linear in x, so each segment's starting values are the
     * last one's plus a step worked out once here, rounded once.  That
     * leaves no floating point at all per segment - on a 68020 without an
     * FPU the dozen soft float calls there cost as much as the columns they
     * set up.  The rounding of a segment step adds up, but by half a unit a
     * segment it stays far below anything that shows.
     *
     * The perspective pair gets scales of its own rather than the depth
     * buffer's.  At that scale a far wall's depth is only a couple of
     * hundred, and dividing by a value rounded that coarsely moves the
     * texture column by a fair part of a texel.  Each of the two is scaled
     * by the power of two that puts its largest value near 2^29, so both
     * keep eight or nine significant digits however far away the wall is and
     * however many texels it spans; the divide's shift makes up the
     * difference.  The depth buffer's scale is a power of two as well, so
     * each segment's depth comes from the finer 1/d by a shift and does not
     * collect the rounding a step at the coarse scale would.
     */
    {
        double x0   = (double)xa + 0.5 - sx1;
        double dtv  = (tovd2 - tovd1) * spaninv;
        double id_a = invd1 + didx * x0;
        double tv_a = tovd1 + dtv * x0;
        double tbig = fabs(tovd1) > fabs(tovd2) ? fabs(tovd1) : fabs(tovd2);
        double ibig = invd1 > invd2 ? invd1 : invd2;
        double g;
        int pk, tk;

        /* Powers of two putting each largest value in [2^28, 2^29). */
        pk = 28 - dexp2(ibig);
        tk = dexp2(tbig) > -100 ? 28 - dexp2(tbig) : pk;
        zk  = pk - 24;                  /* ZSCALE is 2^24 */
        tsh = 16 + pk - tk;
        if (zk < 0)                     /* nearer than the near plane */
            return;

        sYT = fxround(dscale2((double)horizon_row + topk * id_a, yshift));
        sYB = fxround(dscale2((double)horizon_row + botk * id_a, yshift));
        sID = fxround(dscale2(id_a, pk));
        sTV = fxround(dscale2(tv_a, tk));

        gYT = fxround(dscale2(ytstep, 4));      /* SEGLEN is 2^4 */
        gYB = fxround(dscale2(ybstep, 4));
        /* A quad more than a column or a segment wide keeps these bounded by
           the values themselves; a sliver narrower than that can have a
           huge slope, and never uses it, so it only must not overflow. */
        g = dscale2(didx, pk);
        dID = dfits(g) ? fxround(g) : 0;
        g = dscale2(dtv, tk);
        dTV = dfits(g) ? fxround(g) : 0;
        g = dscale2(didx, pk + 4);
        gID = dfits(g) ? fxround(g) : 0;
        g = dscale2(dtv, tk + 4);
        gTV = dfits(g) ? fxround(g) : 0;
    }

    for (seg = xa; seg < xb; seg += SEGLEN,
         sYT += gYT, sYB += gYB, sID += gID, sTV += gTV) {
        int xe = seg + SEGLEN;
        K_INT32 tcur, tinc;
        int n;

        if (xe > xb) xe = xb;
        n = xe - seg;

        Z = zk ? (sID + (1L << (zk - 1))) >> zk : sID;
        if (Z <= 0 || Z + dZ * (n - 1) <= 0)
            continue;

        YT = sYT;
        YB = sYB;

        /* tcur is a texture column that is about to be floored to a whole
           texel, so truncating it is exactly right and rounding it to nearest
           would pick the wrong texel just below a boundary.  tinc is a step
           that gets added up, so that one does want rounding. */
        tcur = tcdiv(sTV, sID, tsh);
        tinc = 0;
        if (n > 1) {
            K_INT32 d = tcdiv(sTV + dTV * (n - 1), sID + dID * (n - 1), tsh) - tcur;
            int m = n - 1;

            tinc = (d + (d >= 0 ? m / 2 : -(m / 2))) / m;
        }

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
#else /* !__HAVE_68881__ */

/*
 * The common path for every upright quad: walls, doors and billboards, for
 * the builds without an FPU.  Nothing in here is floating point: the setup
 * is 32 bit integer arithmetic with 64 bit products and a handful of 64/32
 * divides, which a 68020 does in one instruction each, where the soft float
 * version cost some fifty library calls a quad - as much as the columns
 * themselves for a wall of ordinary size.
 *
 * (wx1,wy1)-(wx2,wy2) is the quad in whole world units, running from the
 * ceiling (z=0) to the floor (z=1024).  `t0`/`t1` are the texture columns at
 * the two ends in 16.16, scaled 0..64.
 *
 * `writez` is set for solid walls, which own their columns from then on.
 * `testz` is set for anything that has to respect walls already drawn.
 * `keycolour` makes index 255 transparent (sprites and doors).
 * `depthonly` draws nothing but still claims the columns, which is how the
 * invisible wall hides what is behind it.
 */

/* The camera as the quad setup wants it, from quad_frame_setup(). */
/* rendertest:begin-quadcam  (tools/rendertest lifts this block verbatim) */
static K_INT32 qfx, qfy;            /* view direction, 2.30                  */
static K_INT32 qcx, qcy;            /* camera position, 24.8                 */
static K_INT32 qpx, qpcx;           /* proj_x and proj_cx, 16.16             */
static K_INT32 qtopk, qbotk;        /* rows per unit of 1/d to the ceiling   */
                                    /* and the floor, 24.8                   */
/* rendertest:end-quadcam */

static void quad_frame_setup(void) {
    qfx   = fxround(dscale2(cam_fx, 30));
    qfy   = fxround(dscale2(cam_fy, 30));
    qcx   = fxround(dscale2(cam_ex, 8));
    qcy   = fxround(dscale2(cam_ey, 8));
    qpx   = fxround(dscale2(proj_x, 16));
    qpcx  = fxround(dscale2(proj_cx, 16));
    qtopk = fxround(dscale2(-proj_y * cam_ez, 8));
    qbotk = fxround(dscale2(-proj_y * (cam_ez - 1024.0), 8));
}

/* Bits in a 64 bit value: 0 for 0, 64 for the top bit set. */
static int bitlen64(unsigned long long v) {
    K_UINT32 hi = (K_UINT32)(v >> 32), lo = (K_UINT32)v;
    return hi ? 64 - __builtin_clz(hi) : lo ? 32 - __builtin_clz(lo) : 0;
}

/* p >> sh for 0 <= sh < 64, done in halves so gcc does not reach for
   libgcc's variable 64 bit shift. */
static K_INT32 shr64(long long p, int sh) {
    K_INT32 hi = (K_INT32)(p >> 32);
    K_UINT32 lo = (K_UINT32)p;

    if (sh >= 32) return hi >> (sh - 32);
    if (sh == 0)  return (K_INT32)lo;
    return (K_INT32)((lo >> sh) | ((K_UINT32)hi << (32 - sh)));
}

static K_UINT32 ushr64(unsigned long long p, int sh) {
    K_UINT32 hi = (K_UINT32)(p >> 32), lo = (K_UINT32)p;

    if (sh >= 32) return hi >> (sh - 32);
    if (sh == 0)  return lo;
    return (lo >> sh) | (hi << (32 - sh));
}

/* (a * b) >> sh, rounded to nearest, for 0 < sh < 64. */
static K_INT32 mulshr(K_INT32 a, K_INT32 b, int sh) {
    return shr64((long long)a * b + (1LL << (sh - 1)), sh);
}

/*
 * (n << sh) / d, truncated toward zero, for d > 0 and 0 <= sh <= 32.  When
 * the quotient would not fit in 31 bits it returns 0 and clears *fits: the
 * callers ask that of a per column step only for a quad narrower than a
 * column, which never adds the step to anything.
 */
static K_INT32 divsh(K_INT32 n, K_UINT32 d, int sh, int *fits) {
    K_UINT32 t = n < 0 ? (K_UINT32)-n : (K_UINT32)n, hi, lo, q;

    hi = sh ? t >> (32 - sh) : 0;
    lo = sh < 32 ? t << sh : 0;
    if (hi >= d >> 1) { *fits = 0; return 0; }
    q = udiv64(hi, lo, d);
    return n < 0 ? -(K_INT32)q : (K_INT32)q;
}

/* -g1 / (g2 - g1) in 0.16, for g1 < 0 <= g2: how far along a segment from
   an end outside a plane to one inside it the plane cuts. */
static K_INT32 plane_frac(long long g1, long long g2) {
    unsigned long long num = (unsigned long long)-g1;
    unsigned long long den = (unsigned long long)(g2 - g1);
    int s = bitlen64(den) - 31;
    K_UINT32 n32, d32;

    if (s < 0) s = 0;
    n32 = ushr64(num, s);
    d32 = ushr64(den, s);
    if (!d32 || n32 >= d32) return 65536;
    return (K_INT32)udiv64(n32 >> 16, n32 << 16, d32);
}

static void draw_upright_quad(K_INT32 wx1, K_INT32 wy1, K_INT32 wx2, K_INT32 wy2,
                              int texnum, K_INT32 t0, K_INT32 t1,
                              int shaded, int writez, int testz,
                              int keycolour, int depthonly)
{
    K_INT32 dx1, dy1, dx2, dy2;         /* the ends from the camera, 24.8   */
    long long D1, D2, E1, E2;           /* depth and offset, exact, 2^38    */
    K_INT32 sx1, sx2, spn, x0;          /* screen columns, 16.16            */
    K_INT32 ID1, ID2, IDa, IDmax;       /* 1/d at the ends and at xa, 2^pk  */
    K_INT32 sTV1, sTV2, TVa;            /* t/d, 2^tk                        */
    int p1, p2, pk, plane, fits;
#define SEGLEN 16
    K_INT32 Z, dZ, YT, dYT, YB, dYB;
    K_INT32 sYT, sYB, sID, sTV;         /* at the start of each segment */
    K_INT32 gYT, gYB, gID, gTV;         /* from one segment to the next */
    K_INT32 dID, dTV;
    int zk;                             /* depth = sID >> zk */
    int tsh;                            /* column = (sTV << tsh) / sID */
    int yshift, rsh;
    const unsigned char *texbase;
    int xa, xb, x, seg;

    /*
     * Depth and sideways offset of both ends.  The game's positions are
     * whole world units and its view direction comes from a 16.16 table, so
     * with positions in 24.8 and the direction in 2.30 these four widening
     * multiplies are exact.
     */
    dx1 = (wx1 << 8) - qcx;  dy1 = (wy1 << 8) - qcy;
    dx2 = (wx2 << 8) - qcx;  dy2 = (wy2 << 8) - qcy;
    D1 = (long long)qfx * dx1 + (long long)qfy * dy1;
    E1 = (long long)qfx * dy1 - (long long)qfy * dx1;
    D2 = (long long)qfx * dx2 + (long long)qfy * dy2;
    E2 = (long long)qfx * dy2 - (long long)qfy * dx2;

    /*
     * Clip to the near plane and, so that the screen positions stay within
     * 16.16, to two side planes at |e| = 4d - a 152 degree field, well clear
     * of any view the game can show.  An end outside a plane is moved along
     * the quad to where it crosses, texture coordinate and all, and its
     * depth and offset are recomputed from the moved point: that keeps the
     * ends and their 1/d consistent to the last bit, which the reseeding
     * below relies on.
     */
    for (plane = 0; plane < 3; plane++) {
        long long g1, g2;

        if (plane == 0) {
            g1 = D1 - ((long long)neardist << 38);
            g2 = D2 - ((long long)neardist << 38);
        } else if (plane == 1) {
            g1 = (D1 << 2) - E1;
            g2 = (D2 << 2) - E2;
        } else {
            g1 = (D1 << 2) + E1;
            g2 = (D2 << 2) + E2;
        }
        if (g1 < 0 && g2 < 0)
            return;
        if (g1 < 0 || g2 < 0) {
            K_INT32 f;

            if (g1 < 0) {
                f = plane_frac(g1, g2);
                dx1 += mulshr(dx2 - dx1, f, 16);
                dy1 += mulshr(dy2 - dy1, f, 16);
                t0  += mulshr(t1 - t0, f, 16);
                D1 = (long long)qfx * dx1 + (long long)qfy * dy1;
                E1 = (long long)qfx * dy1 - (long long)qfy * dx1;
            } else {
                f = plane_frac(g2, g1);
                dx2 += mulshr(dx1 - dx2, f, 16);
                dy2 += mulshr(dy1 - dy2, f, 16);
                t1  += mulshr(t0 - t1, f, 16);
                D2 = (long long)qfx * dx2 + (long long)qfy * dy2;
                E2 = (long long)qfx * dy2 - (long long)qfy * dx2;
            }
        }
    }
    /* Rounding the moved point to 1/256 unit can leave it a hair short of
       the near plane, or over the side planes; nothing below minds that, but
       a depth of zero or less would. */
    if (D1 <= 0 || D2 <= 0)
        return;

    /*
     * 1/d at both ends, at a power of two scale that puts the nearer one in
     * [2^28, 2^29], and the screen column of each: e/d from a 64/32 divide,
     * times the projection.  p is the bit length of D less one, so D >> (p-31)
     * has 32 significant bits and 2^63 over that is 1/d to 32 bits; the
     * offset is cut down alongside, and |e| <= 4d after clipping keeps its
     * 32 bits in range.
     */
    p1 = bitlen64((unsigned long long)D1) - 1;
    p2 = bitlen64((unsigned long long)D2) - 1;
    pk = (p1 < p2 ? p1 : p2) - 9;
    zk = pk - 24;
    if (zk < 0 || p1 < 34 || p2 < 34)   /* nearer than 2^-4 units: never */
        return;
    {
        int i;
        for (i = 0; i < 2; i++) {
            long long D = i ? D2 : D1, E = i ? E2 : E1;
            int p = i ? p2 : p1, sh = p - 31, r = sh + 25 - pk;
            K_UINT32 dn = ushr64((unsigned long long)D, sh);
            K_UINT32 q = udiv64(0x7fffffff, 0xffffffff, dn);
            K_INT32 id, en, e16, sx;

            /* r is at least 3, so this rounds to at most 2^29. */
            id = r >= 32 ? 0
               : (K_INT32)ushr64((unsigned long long)q + (1UL << (r - 1)), r);
            en = shr64(E, sh + 3);
            e16 = divsh(en, dn, 19, &fits);
            sx = qpcx + mulshr(qpx, e16, 16);
            if (i) { ID2 = id; sx2 = sx; } else { ID1 = id; sx1 = sx; }
        }
    }

    if (sx1 > sx2) {
        K_INT32 s;
        s = sx1; sx1 = sx2; sx2 = s;
        s = ID1; ID1 = ID2; ID2 = s;
        s = t0;  t0  = t1;  t1  = s;
    }
    spn = sx2 - sx1;
    if (spn <= 0 || sx2 <= (VIEW_LEFT << 16) || sx1 >= (VIEW_RIGHT << 16))
        return;

    /* ceil(sx - 0.5): the columns whose centres the quad covers. */
    xa = (sx1 + 32767) >> 16;
    xb = (sx2 + 32767) >> 16;
    if (xa < VIEW_LEFT) xa = VIEW_LEFT;
    if (xb > VIEW_RIGHT) xb = VIEW_RIGHT;
    if (xa >= xb) return;
    x0 = (xa << 16) + 32768 - sx1;      /* first column centre past sx1 */

    texbase = walseg[texnum];
    IDmax = ID1 > ID2 ? ID1 : ID2;

    /*
     * 1/d is linear in screen x, and the depth value, the top row and the
     * bottom row are each an affine function of 1/d - so all three are linear
     * in x and can be walked with one 32 bit add apiece.  Only the texture
     * column needs a real perspective divide, and that is still done once per
     * 16 column segment and interpolated in between.
     *
     * The per column steps come from the two ends: their difference over
     * the span in columns.  A quad narrower than a column has no use for a
     * step and may not be able to hold one; divsh() leaves those at zero.
     * The per segment steps are the same over sixteen columns, rounded on
     * their own so the error of a column step is not multiplied up.
     */
    fits = 1;
    dID = divsh(ID2 - ID1, (K_UINT32)spn, 16, &fits);
    gID = divsh(ID2 - ID1, (K_UINT32)spn, 20, &fits);
    IDa = ID1 + mulshr(dID, x0, 16);
    dZ  = zk ? (dID + (1L << (zk - 1))) >> zk : dID;

    /*
     * The texture column's perspective pair, t/d and 1/d, gets a scale of
     * its own: t/d at the two ends, exact from t and 1/d, is cut to 29 bits
     * so it keeps eight or nine significant digits however far the wall and
     * however many texels it spans, and the divide's shift makes up the
     * difference.
     */
    {
        long long TV1 = (long long)t0 * ID1, TV2 = (long long)t1 * ID2;
        unsigned long long a1 = (unsigned long long)(TV1 < 0 ? -TV1 : TV1);
        unsigned long long a2 = (unsigned long long)(TV2 < 0 ? -TV2 : TV2);

        tsh = bitlen64(a1 > a2 ? a1 : a2) - 29;
        if (tsh < 0) tsh = 0;
        sTV1 = shr64(TV1, tsh);
        sTV2 = shr64(TV2, tsh);
    }
    dTV = divsh(sTV2 - sTV1, (K_UINT32)spn, 16, &fits);
    gTV = divsh(sTV2 - sTV1, (K_UINT32)spn, 20, &fits);
    TVa = sTV1 + mulshr(dTV, x0, 16);

    /*
     * Rows.  Each row accumulator has to survive as a 32 bit value over the
     * whole span, and a wall at the near plane reaches about 64 x proj_y
     * rows off screen with the camera at the top or bottom of its range,
     * which is fine in 16.16 at 240 lines but not at 1080.  Only the whole
     * part of a row is ever used, so the fraction gives way instead: the
     * row scale is the largest up to 16 bits that keeps the furthest row
     * the quad can reach, plus one segment step, under 2^30.  The top and
     * bottom are bounded together because draw_span() subtracts them.
     */
    {
        K_INT32 k = (qtopk < 0 ? -qtopk : qtopk) + (qbotk < 0 ? -qbotk : qbotk);
        K_INT32 rows = shr64((long long)k * IDmax, pk + 8);
        K_INT32 step = shr64((long long)k * (gID < 0 ? -gID : gID), pk + 8);
        K_INT32 need = (horizon_row < 0 ? -horizon_row : horizon_row) + rows + step + 2;

        yshift = __builtin_clz((K_UINT32)need) - 2;
        if (yshift > 16) yshift = 16;
        if (yshift < 4)
            return;
    }
    rsh = pk + 8 - yshift;              /* 24.8 rows/(1/d) times 2^pk (1/d) */

    sYT = (horizon_row << yshift) + mulshr(qtopk, IDa, rsh);
    sYB = (horizon_row << yshift) + mulshr(qbotk, IDa, rsh);
    dYT = mulshr(qtopk, dID, rsh);
    dYB = mulshr(qbotk, dID, rsh);
    gYT = mulshr(qtopk, gID, rsh);
    gYB = mulshr(qbotk, gID, rsh);
    sID = IDa;
    sTV = TVa;

    for (seg = xa; seg < xb; seg += SEGLEN,
         sYT += gYT, sYB += gYB, sID += gID, sTV += gTV) {
        int xe = seg + SEGLEN;
        K_INT32 tcur, tinc;
        int n;

        if (xe > xb) xe = xb;
        n = xe - seg;

        Z = zk ? (sID + (1L << (zk - 1))) >> zk : sID;
        if (Z <= 0 || Z + dZ * (n - 1) <= 0)
            continue;

        YT = sYT;
        YB = sYB;

        /* tcur is a texture column that is about to be floored to a whole
           texel, so truncating it is exactly right and rounding it to nearest
           would pick the wrong texel just below a boundary.  tinc is a step
           that gets added up, so that one does want rounding. */
        tcur = tcdiv(sTV, sID, tsh);
        tinc = 0;
        if (n > 1) {
            K_INT32 d = tcdiv(sTV + dTV * (n - 1), sID + dID * (n - 1), tsh) - tcur;
            int m = n - 1;

            tinc = (d + (d >= 0 ? m / 2 : -(m / 2))) / m;
        }

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
#endif /* __HAVE_68881__ */

/* ------------------------------------------------------------- scene setup */

void R_BeginScene(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs,
                  double aspwv, double asphv, int yy) {
    unsigned char ceilcol, floorcol;
    int y, split, hr, full;
    double cx, f;

    if (!amiga_chunky) return;

    /* sintable is already a unit circle in 16.16; renormalising it is a
       soft-float sqrt and two divides for a change in the last bit. */
    cam_fx = sintable[(angs + 512) & 2047] / 65536.0;
    cam_fy = sintable[angs & 2047] / 65536.0;

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

    cx = 180.0 * amiga_mode.ppux + amiga_mode.orgx;
    hr = unit_row(120.0);

    /*
     * The view window.  Shrinking the view scales the whole picture down
     * about the view axis and the horizon, so it keeps its field of view and
     * the ray caster's culling still matches - it is the same picture, just
     * smaller, and the renderer's cost falls with its area.
     */
    f = amiga_cfg_viewsize / 100.0;
    full = (amiga_cfg_viewsize >= 100);
    if (full) {
        clip_to_frame();
    } else {
        clip_x0 = (int)floor(cx - f * cx + 0.5);
        clip_x1 = (int)floor(cx + f * (VW - cx) + 0.5);
        clip_y0 = hr - (int)floor(f * hr + 0.5);
        clip_y1 = hr + (int)floor(f * (VH - hr) + 0.5);
        if (clip_x0 < 0)  clip_x0 = 0;
        if (clip_x1 > VW) clip_x1 = VW;
        if (clip_y0 < 0)  clip_y0 = 0;
        if (clip_y1 > VH) clip_y1 = VH;

        /* The overlays drew into the border last frame; put it back. */
        amiga_clear_leftovers();
    }

    /* With the status bar up, and ShowStatusBar() following the scene as it
       did last time, the rows under it are its: leave them be, and the bar
       need not be composited again. */
    bar_kept = 0;
    if (bar_after_scene && statusbaryvisible > 0) {
        int top = unit_row(view_bottom() - statusbaryvisible);

        if (top < clip_y1) clip_y1 = top;
        if (clip_y1 < clip_y0) clip_y1 = clip_y0;
        bar_kept = 1;
    }
    bar_after_scene = 0;
    amiga_mark_dirty(clip_x0, clip_y0, clip_x1, clip_y1, 0);

    proj_x  = 180.0 * amiga_mode.ppux * f;
    proj_y  = 160.0 * amiga_mode.ppuy * f;
    proj_cx = cx;
    horizon_row = hr;
#ifndef __HAVE_68881__
    quad_frame_setup();
#endif

    /* Flat ceiling above the horizon, flat floor below - the same two colours
       the OpenGL path clears and fills with. */
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        floorcol = 0x85;
    else
        floorcol = 0x84;
    ceilcol = 0xe3;

    split = unit_row(240 - yy / 90);
    split = hr + (int)floor((split - hr) * f + 0.5);
    if (split < clip_y0) split = clip_y0;
    if (split > clip_y1) split = clip_y1;

    if (full) {
        memset(amiga_chunky, ceilcol, (size_t)split * VW);
        memset(amiga_chunky + (size_t)split * VW, floorcol,
               (size_t)(clip_y1 - split) * VW);
    } else {
        int w = clip_x1 - clip_x0, stride = VW;
        unsigned char *row = amiga_chunky + (size_t)clip_y0 * stride + clip_x0;

        for (y = clip_y0; y < clip_y1; y++, row += stride)
            memset(row, y < split ? ceilcol : floorcol, w);
    }

    memset(zbuf + clip_x0, 0, (size_t)(clip_x1 - clip_x0) * sizeof(K_INT32));
}

void R_DrawWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                K_INT16 j, double v0, double v1,
                int shaded, int transparent) {
    /* graphx.c sets `shaded` for the brighter of the two wall orientations;
       the OpenGL path multiplies the other one by 0.9, which in this palette
       is one step down the brightness ramp. */
    {
        K_INT32 t0 = 0, t1 = 64L << 16;
        if (v0 != 0.0 || v1 != 1.0) {
            t0 = fxround(dscale2(v0, 22));
            t1 = fxround(dscale2(v1, 22));
        }
        draw_upright_quad(x1, y1, x2, y2, j, t0, t1,
                          shaded ? 0 : 1,
                          1, 1, 0, transparent);
    }
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
        {
            K_INT32 t0 = 0, t1 = 64L << 16;
            if (v0 != 0.0 || v1 != 1.0) {
                t0 = fxround(dscale2(v0, 22));
                t1 = fxround(dscale2(v1, 22));
            }
            draw_upright_quad(x1, y1, x2, y2, j, t0, t1,
                              0, 0, 1, 1, 0);
        }
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

    xlo = (int)floor(minx); if (xlo < VIEW_LEFT) xlo = VIEW_LEFT;
    xhi = (int)ceil(maxx);  if (xhi > VIEW_RIGHT) xhi = VIEW_RIGHT;
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
        int lo = VIEW_LEFT, hi = VIEW_RIGHT, t0, t1, col;

        /* Columns where the world position is inside the decal square. */
        if (sx > 0) {
            t0 = cdiv(-HALF - rx0, sx); t1 = cdiv(HALF - rx0, sx);
        } else if (sx < 0) {
            t0 = fdiv(HALF - rx0, sx) + 1; t1 = fdiv(-HALF - rx0, sx) + 1;
        } else {
            if (rx0 < -HALF || rx0 >= HALF) continue;
            t0 = VIEW_LEFT; t1 = VIEW_RIGHT;
        }
        if (t0 > lo) lo = t0;
        if (t1 < hi) hi = t1;

        if (sy > 0) {
            t0 = cdiv(-HALF - ry0, sy); t1 = cdiv(HALF - ry0, sy);
        } else if (sy < 0) {
            t0 = fdiv(HALF - ry0, sy) + 1; t1 = fdiv(-HALF - ry0, sy) + 1;
        } else {
            if (ry0 < -HALF || ry0 >= HALF) continue;
            t0 = VIEW_LEFT; t1 = VIEW_RIGHT;
        }
        if (t0 > lo) lo = t0;
        if (t1 < hi) hi = t1;

        if (lo < VIEW_LEFT) lo = VIEW_LEFT;
        if (hi > VIEW_RIGHT) hi = VIEW_RIGHT;
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

    {
        int c0 = clip_x0, c1 = clip_x1, r0 = clip_y0, r1 = clip_y1;
        double minx = sxv[0], maxx = sxv[0], miny = syv[0], maxy = syv[0];

        for (i = 1; i < 4; i++) {
            if (sxv[i] < minx) minx = sxv[i];
            if (sxv[i] > maxx) maxx = sxv[i];
            if (syv[i] < miny) miny = syv[i];
            if (syv[i] > maxy) maxy = syv[i];
        }

        clip_to_frame();
        softtri(sxv, syv, tu, tv, 0, 1, 2, j, 0);
        softtri(sxv, syv, tu, tv, 0, 2, 3, j, 0);
        clip_x0 = c0; clip_x1 = c1; clip_y0 = r0; clip_y1 = r1;

        if (minx > -1.0e6 && maxx < 1.0e6 && miny > -1.0e6 && maxy < 1.0e6) {
            amiga_mark_dirty((int)floor(minx), (int)floor(miny),
                             (int)ceil(maxx), (int)ceil(maxy), 1);
            if (maxy > r1) amiga_bar_stale = 1;   /* reached below the view */
        }
    }
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
    amiga_mark_dirty(xa, ya, xb, yb, 1);

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
