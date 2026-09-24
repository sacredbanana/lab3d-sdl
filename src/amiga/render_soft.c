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
 * Everything is drawn into a 360x240 8 bit chunky buffer whose indices are
 * the game's own palette entries, so there is no colour conversion in the
 * inner loops.  Shading uses the structure of that palette: it is sixteen
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

#define VW AMIGA_VIEW_W
#define VH AMIGA_VIEW_H

/* Per-column depth, held as 2^24/d.  Bigger is nearer.  The near plane is 16
   world units and the far plane 98304, so this spans 1048576 down to 170 -
   plenty of resolution without ever overflowing 32 bits. */
#define ZSCALE 16777216.0

static K_INT32 zbuf[VW];

/* Camera, recomputed once per frame by R_BeginScene(). */
static double cam_fx, cam_fy;       /* unit forward vector                */
static double cam_ex, cam_ey, cam_ez;
static double proj_x;               /* 180 / aspw                          */
static double proj_y;               /* 160 / asph                          */
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
        memset(amiga_chunky, 0, VW * VH);
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

/* Copy a rectangle of the 8 bit overlay onto the frame.  Index 255 is the
   transparency key while in game, matching the alpha test the OpenGL path
   uses; outside the game the overlay is opaque and this is a straight copy. */
static void blit_overlay(int srcx, int srcy, int dstx, int dsty, int w, int h) {
    const unsigned char *s;
    unsigned char *d;
    int x, y;

    if (!amiga_chunky || !screenbuffer) return;

    /* Clip against the overlay buffer. */
    if (srcx < 0) { w += srcx; dstx -= srcx; srcx = 0; }
    if (srcy < 0) { h += srcy; dsty -= srcy; srcy = 0; }
    if (srcx + w > screenbufferwidth)  w = screenbufferwidth  - srcx;
    if (srcy + h > screenbufferheight) h = screenbufferheight - srcy;

    /* Clip against the frame. */
    if (dstx < 0) { w += dstx; srcx -= dstx; dstx = 0; }
    if (dsty < 0) { h += dsty; srcy -= dsty; dsty = 0; }
    if (dstx + w > VW) w = VW - dstx;
    if (dsty + h > VH) h = VH - dsty;

    if (w <= 0 || h <= 0) return;

    s = screenbuffer + (size_t)srcy * screenbufferwidth + srcx;
    d = amiga_chunky + (size_t)dsty * VW + dstx;

    if (!ingame) {
        for (y = 0; y < h; y++) {
            memcpy(d, s, w);
            s += screenbufferwidth;
            d += VW;
        }
    } else {
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++)
                if (s[x] != 255) d[x] = s[x];
            s += screenbufferwidth;
            d += VW;
        }
    }
}

void ShowPartialOverlay(int x, int y, int w, int h, int statusbar) {
    int i;

    if (statusbar == 1) {
        /* The 320 pixel status bar, pinned to the bottom of the frame. */
        blit_overlay(x, y, x, VH - statusbaryvisible, w, h);

        /* Widen it to the full 360 by repeating the right hand edge, which is
           what the OpenGL path does with its statusbar == 2 passes. */
        for (i = 0; i < (int)((virtualscreenwidth - 319) / 2); i += 20) {
            ShowPartialOverlay(340 + i, statusbaryoffset, 20, statusbaryvisible, 2);
            ShowPartialOverlay(0 - i,   statusbaryoffset, 20, statusbaryvisible, 2);
        }
        return;
    }

    if (statusbar == 2) {
        blit_overlay(340, statusbaryoffset, x, VH - statusbaryvisible,
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
   `ytop`/`ybot` the (unclipped) 16.16 screen rows the wall covers, `skipkey`
   makes index 255 transparent. */
static void draw_span(unsigned char *dstcol, const unsigned char *tex,
                      K_INT32 ytop, K_INT32 ybot, int shaded, int skipkey)
{
    int y0, y1, y, height;
    K_INT32 v, vstep;

    height = (int)((ybot - ytop) >> 16);
    if (height <= 0) return;

    vstep = (height < RECIP_MAX) ? vrecip[height]
                                 : (K_INT32)((64L << 16) / height);

    y0 = (int)(ytop >> 16);
    y1 = (int)(ybot >> 16);

    v = 0;
    if (y0 < VIEW_TOP) {
        v = (K_INT32)((VIEW_TOP - y0) * (long)vstep);
        y0 = VIEW_TOP;
    }
    if (y1 > VIEW_BOT) y1 = VIEW_BOT;
    if (y0 >= y1) return;

    dstcol += (size_t)y0 * VW;

    if (skipkey) {
        if (shaded) {
            for (y = y0; y < y1; y++) {
                unsigned char c = tex[(v >> 16) & 63];
                if (c != 255) *dstcol = shadetab[c];
                dstcol += VW; v += vstep;
            }
        } else {
            for (y = y0; y < y1; y++) {
                unsigned char c = tex[(v >> 16) & 63];
                if (c != 255) *dstcol = c;
                dstcol += VW; v += vstep;
            }
        }
    } else {
        if (shaded) {
            for (y = y0; y < y1; y++) {
                *dstcol = shadetab[tex[(v >> 16) & 63]];
                dstcol += VW; v += vstep;
            }
        } else {
            for (y = y0; y < y1; y++) {
                *dstcol = tex[(v >> 16) & 63];
                dstcol += VW; v += vstep;
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

    sx1 = 180.0 + proj_x * e1 / d1;
    sx2 = 180.0 + proj_x * e2 / d2;

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

    /* Perspective correction is exact every 16 columns and interpolated in
       between, which is invisible at this resolution and saves a divide per
       pixel column. */
#define SEGLEN 16

    for (seg = xa; seg < xb; seg += SEGLEN) {
        int xe = seg + SEGLEN;
        double a0, a1, id0, id1, idstep, tv0, tv1, tc0, tc1;
        K_INT32 tcur, tinc;
        double id;
        int n;

        if (xe > xb) xe = xb;
        n = xe - seg;

        a0 = ((double)seg + 0.5 - sx1) / (sx2 - sx1);
        a1 = ((double)(xe - 1) + 0.5 - sx1) / (sx2 - sx1);

        id0 = invd1 + (invd2 - invd1) * a0;
        id1 = invd1 + (invd2 - invd1) * a1;
        if (id0 <= 0.0 || id1 <= 0.0) continue;

        tv0 = tovd1 + (tovd2 - tovd1) * a0;
        tv1 = tovd1 + (tovd2 - tovd1) * a1;

        tc0 = tv0 / id0;
        tc1 = tv1 / id1;

        tcur   = (K_INT32)(tc0 * 65536.0);
        tinc   = (n > 1) ? (K_INT32)((tc1 - tc0) * 65536.0 / (n - 1)) : 0;
        id     = id0;
        idstep = (n > 1) ? (id1 - id0) / (n - 1) : 0.0;

        for (x = seg; x < xe; x++, tcur += tinc, id += idstep) {
            K_INT32 z, ytop, ybot;
            int tcol;

            if (id <= 0.0) continue;

            z = (K_INT32)(ZSCALE * id);

            if (testz && z <= zbuf[x]) continue;
            if (writez) zbuf[x] = z;
            if (depthonly) continue;

            ytop = (K_INT32)(((double)horizon_row + topk * id) * 65536.0);
            ybot = (K_INT32)(((double)horizon_row + botk * id) * 65536.0);

            tcol = (tcur >> 16) & 63;
            draw_span(amiga_chunky + x, texbase + (tcol << 6),
                      ytop, ybot, shaded, keycolour);
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

    proj_x = 180.0 / aspwv;
    proj_y = 160.0 / asphv;

    horizon_row = 120;

    /* Flat ceiling above the horizon, flat floor below - the same two colours
       the OpenGL path clears and fills with. */
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        floorcol = 0x85;
    else
        floorcol = 0x84;
    ceilcol = 0xe3;

    split = 240 - yy / 90;
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

            sxv[i] = 180.0 + proj_x * e / d;
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

/* Affine textured triangle with a single depth value, used only for the
   spinning sprites and the 2D overlay sprites. */
void softtri(double *sx, double *sy, double *tu, double *tv,
             int a, int b, int c, int texnum, K_INT32 z)
{
    const unsigned char *tex = walseg[texnum];
    double x0 = sx[a], y0 = sy[a], x1 = sx[b], y1 = sy[b], x2 = sx[c], y2 = sy[c];
    double det = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0);
    double miny, maxy, minx, maxx;
    double iu[3], iv[3];
    int yi, xi, ytop, ybot;

    if (fabs(det) < 1e-9) return;

    /* Barycentric setup: u,v as linear functions of screen position. */
    iu[0] = tu[a]; iu[1] = tu[b]; iu[2] = tu[c];
    iv[0] = tv[a]; iv[1] = tv[b]; iv[2] = tv[c];

    miny = y0; if (y1 < miny) miny = y1; if (y2 < miny) miny = y2;
    maxy = y0; if (y1 > maxy) maxy = y1; if (y2 > maxy) maxy = y2;
    minx = x0; if (x1 < minx) minx = x1; if (x2 < minx) minx = x2;
    maxx = x0; if (x1 > maxx) maxx = x1; if (x2 > maxx) maxx = x2;

    ytop = (int)floor(miny); if (ytop < VIEW_TOP) ytop = VIEW_TOP;
    ybot = (int)ceil(maxy);  if (ybot > VIEW_BOT) ybot = VIEW_BOT;
    if (minx < 0) minx = 0;
    if (maxx > VW) maxx = VW;

    for (yi = ytop; yi < ybot; yi++) {
        double py = yi + 0.5;
        for (xi = (int)minx; xi < (int)maxx; xi++) {
            double px = xi + 0.5;
            double l1 = ((px-x0)*(y2-y0) - (x2-x0)*(py-y0)) / det;
            double l2 = ((x1-x0)*(py-y0) - (px-x0)*(y1-y0)) / det;
            double l0 = 1.0 - l1 - l2;
            double u, v;
            int tc, tr;
            unsigned char col;

            if (l0 < 0.0 || l1 < 0.0 || l2 < 0.0) continue;
            if (z && z <= zbuf[xi]) continue;

            u = l0*iu[0] + l1*iu[1] + l2*iu[2];
            v = l0*iv[0] + l1*iv[1] + l2*iv[2];

            tc = (int)u; if (tc < 0) tc = 0; if (tc > 63) tc = 63;
            tr = (int)v; if (tr < 0) tr = 0; if (tr > 63) tr = 63;

            col = tex[(tc << 6) + tr];
            if (col != 255)
                amiga_chunky[yi * VW + xi] = col;
        }
    }
}

/* Floor decals: a 1024x1024 texture lying flat on the floor.  With no pitch
   every screen row is at a constant distance, so one division per row and a
   linear walk across it is all this needs. */
void R_DrawFloorSprite(K_UINT16 x, K_UINT16 y, K_INT16 j) {
    const unsigned char *tex = walseg[j];
    double zfloor = 1024.0 - cam_ez;
    int row;

    if (zfloor <= 0.0) return;

    for (row = horizon_row + 1; row < VIEW_BOT; row++) {
        double d = proj_y * zfloor / (row - horizon_row);
        double wx, wy, stepx, stepy;
        K_INT32 z;
        int col;

        if (d < (double)neardist) continue;

        z = (K_INT32)(ZSCALE / d);

        /* World position at the left edge of the row, and the step per pixel. */
        {
            double e0 = (0.5 - 180.0) * d / proj_x;
            double estep = d / proj_x;

            wx = cam_ex + cam_fx*d - cam_fy*e0;
            wy = cam_ey + cam_fy*d + cam_fx*e0;
            stepx = -cam_fy * estep;
            stepy =  cam_fx * estep;
        }

        for (col = 0; col < VW; col++, wx += stepx, wy += stepy) {
            int lx = (int)(wx - (double)x) + 512;
            int ly = (int)(wy - (double)y) + 512;
            unsigned char c;

            if (lx < 0 || lx >= 1024 || ly < 0 || ly >= 1024) continue;
            if (z <= zbuf[col]) continue;

            c = tex[((lx >> 4) << 6) + (ly >> 4)];
            if (c != 255)
                amiga_chunky[row * VW + col] = c;
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

        sxv[i] = x + s*rx;
        syv[i] = y - s*ry;      /* OpenGL y is up, screen rows go down */

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
    int y0, x, y, filled;

    (void)level;    /* no alpha in an 8 bit palette; the bar is solid */

    if (!amiga_chunky) return;

    /* The OpenGL path shifts the bar by 30 rows for the music/sound variant;
       do the same, then keep it on screen. */
    y0 = 110 - 30 * type;
    if (y0 < 0) y0 = 0;
    if (y0 + 20 > VH) y0 = VH - 20;

    filled = 96 + (vol >> 1);

    for (y = y0; y < y0 + 20; y++) {
        unsigned char *row = amiga_chunky + (size_t)y * VW;
        for (x = 96; x < 224; x++)
            row[x] = (x < filled) ? (type ? 0x9f : 0x2f) : 0x10;
    }
}

int R_ReadPixelsBGR(unsigned char *dst, int w, int h) {
    int x, y;

    if (!amiga_chunky) return -1;

    /* BMP rows run bottom to top. */
    for (y = 0; y < h; y++) {
        const unsigned char *src = amiga_chunky + (size_t)(h - 1 - y) * VW;
        for (x = 0; x < w; x++) {
            unsigned char c = (x < VW) ? src[x] : 0;
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
