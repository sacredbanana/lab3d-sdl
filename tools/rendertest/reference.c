/*
 * Frozen copies of the three renderer inner loops as they were before the
 * fixed point conversion, in the original double precision form.
 *
 * These exist only so tools/rendertest can prove the rewrite still draws the
 * same picture.  They are deliberately NOT kept in step with the live code in
 * src/amiga/render_soft.c - they are the thing it is being compared against.
 * Lifted from src/amiga/render_soft.c at commit be0fc9d.
 */

#include "shim.h"


static void ref_draw_span(unsigned char *dstcol, const unsigned char *tex,
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

static void ref_draw_upright_quad(double wx1, double wy1, double wx2, double wy2,
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
            ref_draw_span(amiga_chunky + x, texbase + (tcol << 6),
                      ytop, ybot, shaded, keycolour);
        }
    }
#undef SEGLEN
}

static void ref_softtri(double *sx, double *sy, double *tu, double *tv,
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

static void ref_floor_sprite(K_UINT16 x, K_UINT16 y, K_INT16 j) {
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
            /* DELIBERATELY ADJUSTED, and the only such line in this file.
               The frozen original wrote (int)(...), which truncates toward
               zero and so made the texel straddling the middle of the decal
               one world unit wider than every other texel.  The rewrite
               floors instead, which is the convention the wall renderer
               already used.  Flooring here too keeps this test measuring the
               fixed point error rather than that intended change, which would
               otherwise swamp it - it accounted for 97.6% of the raw
               difference when the change was made. */
            int lx = (int)floor(wx - (double)x) + 512;
            int ly = (int)floor(wy - (double)y) + 512;
            unsigned char c;

            if (lx < 0 || lx >= 1024 || ly < 0 || ly >= 1024) continue;
            if (z <= zbuf[col]) continue;

            c = tex[((lx >> 4) << 6) + (ly >> 4)];
            if (c != 255)
                amiga_chunky[row * VW + col] = c;
        }
    }
}
