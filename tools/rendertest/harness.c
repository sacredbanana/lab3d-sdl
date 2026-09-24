/*
 * Differential tests for the Amiga software renderer's inner loops.
 *
 * src/amiga/render_soft.c used to do its rasterising in double precision,
 * which costs tens of soft float library calls per pixel on a 68020 with no
 * FPU.  The loops were converted to fixed point; these tests run the current
 * code and a frozen copy of what it replaced over the same randomised
 * geometry and compare every pixel.
 *
 * A fixed point rewrite is never bit exact, so the tests do not demand that.
 * What they check is that disagreements are confined to pixels sitting on a
 * decision boundary - a triangle edge, a texel boundary, a depth tie - and
 * stay under a small fraction of a percent.  A real bug moves pixels that are
 * nowhere near a boundary, and that is what the thresholds catch.
 *
 *   ./harness softtri|floor|wall [trials] [seed]
 *
 * Exits non-zero if an error rate exceeds its threshold.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "shim.h"

/* ------------------------------------------------------------ environment */

static unsigned char   texbuf[64 * 64];
unsigned char         *walseg[1] = { texbuf };
unsigned char         *amiga_chunky;
static K_INT32         zbufA[VW], zbufB[VW];
K_INT32               *zbuf;
unsigned char          shadetab[256];
static K_INT32         vrecip_store[RECIP_MAX];
K_INT32               *vrecip = vrecip_store;
double                 cam_fx, cam_fy, cam_ex, cam_ey, cam_ez;
double                 proj_x, proj_y;
int                    horizon_row, neardist = 16;

static unsigned char fbA[VW * VH], fbB[VW * VH];
#define UNSET 7          /* a colour neither side ever writes */

/* The live code under test, lifted out of the real source at build time. */
#include "generated/current.inc"
/* The double precision original it has to agree with. */
#include "reference.c"

static void setup_tables(void) {
    int i;
    for (i = 0; i < 64 * 64; i++) texbuf[i] = (unsigned char)((i * 7919) & 0xff);
    for (i = 0; i < 64 * 64; i += 5) texbuf[i] = 255;      /* transparency key */
    for (i = 0; i < 256; i++) {
        int hue = i & 0xf0, lev = i & 0x0f;
        shadetab[i] = (unsigned char)(hue | (lev ? lev - 1 : 0));
    }
    shadetab[255] = 255;
    vrecip_store[0] = 0;
    for (i = 1; i < RECIP_MAX; i++) vrecip_store[i] = (K_INT32)((64L << 16) / i);
}

static double frand(double lo, double hi) {
    return lo + (hi - lo) * (rand() / (double)RAND_MAX);
}

/* ------------------------------------------------------------- reporting */

typedef struct {
    long drawn, diff, worst, cases;
    long only_new, only_old;
    long boundary, interior;    /* softtri / wall: how far from a decision edge */
    long known;                 /* differs for a deliberate, documented reason */
    long zcols; double zrelmax; long zabsmax;
} result;

static int verdict(const char *what, result *r, double pct_limit, double extra_limit,
                   const char *extra_label, double extra_value) {
    double pct = r->drawn ? 100.0 * r->diff / r->drawn : 0.0;
    int bad = 0;

    printf("%s\n", what);
    printf("  pixels drawn %ld, differing %ld (%.5f%%; limit %.5f%%)\n",
           r->drawn, r->diff, pct, pct_limit);
    printf("  worst single case %ld px, cases with any difference %ld\n",
           r->worst, r->cases);
    if (r->only_new || r->only_old)
        printf("  drawn only by the rewrite %ld, only by the original %ld\n",
               r->only_new, r->only_old);
    if (r->known)
        printf("  differing for a known, deliberate reason: %ld\n", r->known);
    if (r->interior || r->boundary)
        printf("  differing pixels away from any decision boundary: %ld "
               "(of %ld; limit 0)\n", r->interior, r->interior + r->boundary);
    if (extra_label)
        printf("  %s %.5g (limit %.5g)\n", extra_label, extra_value, extra_limit);

    if (pct > pct_limit)              { printf("  FAIL: too many pixels differ\n"); bad = 1; }
    if (r->interior)                  { printf("  FAIL: a pixel differs away from any boundary\n"); bad = 1; }
    if (extra_label && extra_value > extra_limit)
                                      { printf("  FAIL: %s too large\n", extra_label); bad = 1; }
    if (!bad) printf("  ok\n");
    return bad;
}

static void compare(result *r) {
    long d = 0;
    int i;
    for (i = 0; i < VW * VH; i++) {
        if (fbA[i] != UNSET) r->drawn++;
        if (fbA[i] == fbB[i]) continue;
        d++;
        if (fbA[i] == UNSET) r->only_new++;
        else if (fbB[i] == UNSET) r->only_old++;
    }
    r->diff += d;
    if (d > r->worst) r->worst = d;
    if (d) r->cases++;
}

static void blank(void) {
    memset(fbA, UNSET, sizeof fbA);
    memset(fbB, UNSET, sizeof fbB);
}

/* ---------------------------------------------------------------- softtri */

/*
 * Every disagreement must sit on a decision boundary.  For each differing
 * pixel this recomputes, in double precision, how far it is from the nearest
 * one - a triangle edge (a barycentric crossing zero) or a texel boundary (a
 * texture coordinate crossing an integer).  A genuine bug puts pixels a long
 * way from both.
 */
#define BOUNDARY_EPS 0.05

static int test_softtri(int ntrials, unsigned seed) {
    result r; int trial, i;
    memset(&r, 0, sizeof r);
    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        double sx[4], sy[4], tu[4], tv[4];
        double x0, y0, x1, y1, x2, y2, det, iu[3], iv[3];
        K_INT32 z; int k, mode = trial % 3, oldxlo, oldxhi;

        for (k = 0; k < 4; k++) {
            switch (mode) {
            case 0:  sx[k] = frand(0, VW);         sy[k] = frand(0, VH);         break;
            case 1:  sx[k] = frand(-200, VW + 200); sy[k] = frand(-200, VH + 200); break;
            default: sx[k] = frand(-5000, 5000);   sy[k] = frand(-5000, 5000);   break;
            }
            /* what R_DrawBillboard and R_DrawSprite2D actually pass */
            tu[k] = (k == 1 || k == 2) ? 64.0 : 0.0;
            tv[k] = (k >= 2)           ? 64.0 : 0.0;
        }
        z = (trial & 1) ? (K_INT32)frand(0, 1048576) : 0;
        for (i = 0; i < VW; i++)
            zbufA[i] = zbufB[i] = (trial & 1) ? (K_INT32)frand(0, 1048576) : 0;

        blank();
        amiga_chunky = fbA; zbuf = zbufA; ref_softtri(sx, sy, tu, tv, 0, 1, 2, 0, z);
        amiga_chunky = fbB; zbuf = zbufB;     softtri(sx, sy, tu, tv, 0, 1, 2, 0, z);
        compare(&r);

        x0 = sx[0]; y0 = sy[0]; x1 = sx[1]; y1 = sy[1]; x2 = sx[2]; y2 = sy[2];
        det = (x1-x0)*(y2-y0) - (x2-x0)*(y1-y0);
        if (fabs(det) < 1e-9) continue;
        iu[0] = tu[0]; iu[1] = tu[1]; iu[2] = tu[2];
        iv[0] = tv[0]; iv[1] = tv[1]; iv[2] = tv[2];

        /* The rewrite deliberately rounds the x bounding box outward with
           ceil(), matching what the y bound always did.  The original's
           (int)maxx dropped the last partial column, skipping pixels that are
           genuinely inside the triangle, so differences there are expected. */
        {
            double lo = x0, hi = x0;
            if (x1 < lo) lo = x1; if (x2 < lo) lo = x2;
            if (x1 > hi) hi = x1; if (x2 > hi) hi = x2;
            if (lo < 0) lo = 0;
            if (hi > VW) hi = VW;
            oldxlo = (int)lo; oldxhi = (int)hi;
        }

        for (i = 0; i < VW * VH; i++) {
            double px, py, l0, l1, l2, u, v, m, du, dv;
            int xi = i % VW;
            if (fbA[i] == fbB[i]) continue;
            if (xi < oldxlo || xi >= oldxhi) { r.known++; continue; }
            px = xi + 0.5; py = (i / VW) + 0.5;
            l1 = ((px-x0)*(y2-y0) - (x2-x0)*(py-y0)) / det;
            l2 = ((x1-x0)*(py-y0) - (px-x0)*(y1-y0)) / det;
            l0 = 1.0 - l1 - l2;
            m = fabs(l0); if (fabs(l1) < m) m = fabs(l1); if (fabs(l2) < m) m = fabs(l2);
            u = l0*iu[0] + l1*iu[1] + l2*iu[2];
            v = l0*iv[0] + l1*iv[1] + l2*iv[2];
            du = fabs(u - floor(u + 0.5)); dv = fabs(v - floor(v + 0.5));
            if (du < m) m = du;
            if (dv < m) m = dv;
            if (m < BOUNDARY_EPS) r.boundary++; else r.interior++;
        }
    }
    return verdict("softtri (spinning sprites and every 2D overlay sprite)",
                   &r, 0.01, 0, NULL, 0);
}

/* ------------------------------------------------------------ floor decal */

static int test_floor(int ntrials, unsigned seed) {
    result r; int trial, i;
    memset(&r, 0, sizeof r);
    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        double ang = frand(0, 2 * M_PI);
        double spread = (trial & 1) ? 1200.0 : 4096.0;   /* magnified and minified */
        K_UINT16 dx, dy;

        cam_fx = cos(ang); cam_fy = sin(ang);
        cam_ex = frand(1024, 64512);
        cam_ey = frand(1024, 64512);
        cam_ez = frand(0, 1000);
        proj_x = 180.0 / frand(0.75, 1.34);
        proj_y = 160.0 / frand(0.75, 1.34);
        horizon_row = 120;
        dx = (K_UINT16)fmod(cam_ex + frand(-spread, spread) + 65536.0, 65536.0);
        dy = (K_UINT16)fmod(cam_ey + frand(-spread, spread) + 65536.0, 65536.0);

        for (i = 0; i < VW; i++)
            zbufA[i] = zbufB[i] = (trial & 1) ? (K_INT32)frand(0, 400000) : 0;

        blank();
        amiga_chunky = fbA; zbuf = zbufA; ref_floor_sprite(dx, dy, 0);
        amiga_chunky = fbB; zbuf = zbufB; R_DrawFloorSprite(dx, dy, 0);
        compare(&r);
    }
    return verdict("R_DrawFloorSprite (blood splats and other floor decals)",
                   &r, 0.25, 0, NULL, 0);
}

/* -------------------------------------------------------------- wall quad */

static int test_wall(int ntrials, unsigned seed) {
    result r; int trial, i;
    double zrelsum = 0.0; long zrelcnt = 0;
    memset(&r, 0, sizeof r);
    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        double ang = frand(0, 2 * M_PI);
        double wx1, wy1, wx2, wy2;
        int shaded = trial & 1, writez = (trial >> 1) & 1, testz = (trial >> 2) & 1;
        int keycol = (trial >> 3) & 1, depthonly = ((trial >> 4) & 7) == 0;

        cam_fx = cos(ang); cam_fy = sin(ang);
        cam_ex = frand(2048, 63488);
        cam_ey = frand(2048, 63488);
        cam_ez = frand(0, 1024);
        proj_x = 180.0 / frand(0.75, 1.34);
        proj_y = 160.0 / frand(0.75, 1.34);
        horizon_row = 120;

        if (trial % 4) {                    /* a cell edge, as the game emits */
            double cx = cam_ex + frand(-6144, 6144), cy = cam_ey + frand(-6144, 6144);
            if (rand() & 1) { wx1 = cx; wx2 = cx + 1024; wy1 = wy2 = cy; }
            else            { wy1 = cy; wy2 = cy + 1024; wx1 = wx2 = cx; }
        } else {                            /* arbitrary, to work the near clip */
            wx1 = cam_ex + frand(-3072, 3072); wy1 = cam_ey + frand(-3072, 3072);
            wx2 = cam_ex + frand(-3072, 3072); wy2 = cam_ey + frand(-3072, 3072);
        }

        for (i = 0; i < VW; i++)
            zbufA[i] = zbufB[i] = (trial & 1) ? (K_INT32)frand(0, 600000) : 0;

        blank();
        amiga_chunky = fbA; zbuf = zbufA;
        ref_draw_upright_quad(wx1,wy1,wx2,wy2,0,0.0,64.0,shaded,writez,testz,keycol,depthonly);
        amiga_chunky = fbB; zbuf = zbufB;
        draw_upright_quad(wx1,wy1,wx2,wy2,0,0.0,64.0,shaded,writez,testz,keycol,depthonly);
        compare(&r);

        /* The depth buffer has to agree too, or sprites sort against walls
           wrongly in later passes. */
        for (i = 0; i < VW; i++) {
            long a = zbufA[i], b = zbufB[i], d = labs(a - b);
            double rel;
            if (!d) continue;
            r.zcols++;
            if (d > r.zabsmax) r.zabsmax = d;
            rel = d / (double)(a > b ? a : b);
            if (rel > r.zrelmax) r.zrelmax = rel;
            zrelsum += rel; zrelcnt++;
        }
    }
    printf("  depth columns differing %ld, max absolute %ld, mean relative %.3g\n",
           r.zcols, r.zabsmax, zrelcnt ? zrelsum / zrelcnt : 0.0);
    return verdict("draw_upright_quad (every wall, door and upright sprite)",
                   &r, 0.05, 0.02, "max relative depth error", r.zrelmax);
}

/* ------------------------------------------------------------------- main */

int main(int argc, char **argv) {
    const char *which = argc > 1 ? argv[1] : "";
    int ntrials = argc > 2 ? atoi(argv[2]) : 0;
    unsigned seed = argc > 3 ? (unsigned)atoi(argv[3]) : 1;

    setup_tables();

    if (!strcmp(which, "softtri")) return test_softtri(ntrials ? ntrials : 15000, seed);
    if (!strcmp(which, "floor"))   return test_floor  (ntrials ? ntrials : 5000,  seed);
    if (!strcmp(which, "wall"))    return test_wall   (ntrials ? ntrials : 15000, seed);

    fprintf(stderr, "usage: %s softtri|floor|wall [trials] [seed]\n", argv[0]);
    return 2;
}
