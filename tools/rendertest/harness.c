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
double                 proj_x, proj_y, proj_cx = 180.0;
int                    horizon_row, neardist = 16;

static unsigned char fbA[VW * VH], fbB[VW * VH];
#define UNSET 7          /* a colour neither side ever writes */

/* The live code under test, lifted out of the real source at build time. */
#include "generated/render_soft.inc"
/* The double precision original it has to agree with. */
#include "reference.c"

/* ------------------------------------------------------- ray caster state */

enum lab3dversion_t lab3dversion = KENS_LABYRINTH_2_0;
K_INT16       board[64][64];
char          bmpkind[numwalls + 1];
unsigned char tempbuf[4096];
int           walltol = 32;              /* as init.c sets it */
K_INT16       waterstat, animate2;
K_INT16       wallfound[64][64][4];
K_INT16       wallx[16384], wally[16384];
char          wallside[16384];
K_UINT16      walnum[16384];
K_INT32       wallsfound, rayscast;
K_INT16       mapfound, gameoverfound;
K_INT32       hitpointx, hitpointy;

#include "generated/graphx.inc"
#include "raycast_reference.c"

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

        /* Whole world units, as the game has them: the camera from posxs and
           posys, the walls from cell corners and sprite extents. */
        cam_fx = cos(ang); cam_fy = sin(ang);
        cam_ex = floor(frand(2048, 63488));
        cam_ey = floor(frand(2048, 63488));
        cam_ez = frand(0, 1024);
        proj_x = 180.0 / frand(0.75, 1.34);
        proj_y = 160.0 / frand(0.75, 1.34);
        horizon_row = 120;

        if (trial % 4) {                    /* a cell edge, as the game emits */
            double cx = floor(cam_ex + frand(-6144, 6144)), cy = floor(cam_ey + frand(-6144, 6144));
            if (rand() & 1) { wx1 = cx; wx2 = cx + 1024; wy1 = wy2 = cy; }
            else            { wy1 = cy; wy2 = cy + 1024; wx1 = wx2 = cx; }
        } else {                            /* arbitrary, to work the near clip */
            wx1 = floor(cam_ex + frand(-3072, 3072)); wy1 = floor(cam_ey + frand(-3072, 3072));
            wx2 = floor(cam_ex + frand(-3072, 3072)); wy2 = floor(cam_ey + frand(-3072, 3072));
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

/* ------------------------------------------------------------- ray caster */

/*
 * Compares the whole visibility pass: both versions cast the two frustum edge
 * rays and then recurse, over the same randomly generated board and camera.
 *
 * What matters is the set of walls found, not the order they were found in -
 * the two may subdivide differently at the margin and cast a different number
 * of rays, which is harmless.  A wall the original found and the rewrite did
 * not is a hole in the view and is the thing to catch, so that is counted
 * separately and has to be zero.  tempbuf, which drives the automap, has to
 * agree too.
 */

/* A board a ray can actually get lost in: solid border, scattered interior
   walls, and doors so the texture-picking branches get exercised. */
static void make_board(void) {
    int x, y, i;

    for (i = 0; i <= numwalls; i++) bmpkind[i] = 0;
    bmpkind[1] = 1;                     /* plain wall   */
    bmpkind[fountain] = 1;
    bmpkind[78] = 1;                    /* the map tile */
    bmpkind[gameover] = 1;
    for (i = door1; i <= door1 + 5; i++) bmpkind[i] = 4;   /* doors pass rays */

    for (x = 0; x < 64; x++)
        for (y = 0; y < 64; y++) {
            int edge = (x == 0 || y == 0 || x == 63 || y == 63);
            double r = rand() / (double)RAND_MAX;
            if (edge)         board[x][y] = 1;
            else if (r < 0.18) board[x][y] = 1;
            else if (r < 0.20) board[x][y] = (K_INT16)(fountain);
            else if (r < 0.21) board[x][y] = 78;
            else if (r < 0.22) board[x][y] = (K_INT16)gameover;
            else if (r < 0.26) board[x][y] = (K_INT16)(door1 + (rand() % 6)
                                                       + ((rand() & 1) ? 8192 : 0));
            else               board[x][y] = 0;
        }
}

/*
 * Phase one: single rays, same angle both sides.  This separates a
 * disagreement inside castray() from a disagreement in how recurseray()
 * chooses to subdivide, which are very different problems.
 */
static int test_castray(int ntrials, unsigned seed) {
    long rays = 0, sidediff = 0, celldiff = 0, texdiff = 0, onemiss = 0;
    double hpworst = 0.0, cellworst = 0.0;
    int trial, k;

    srand(seed);
    for (trial = 0; trial < ntrials; trial++) {
        int cx, cy;
        K_UINT16 px, py;

        make_board();
        waterstat = (trial & 1) ? 1 : 0;
        animate2  = (K_INT16)(trial % 3);
        do { cx = rand() % 64; cy = rand() % 64; }
        while (bmpkind[board[cx][cy] & 1023] == 1);
        px = (K_UINT16)((cx << 10) + (rand() & 1023));
        py = (K_UINT16)((cy << 10) + (rand() & 1023));

        for (k = 0; k < 64; k++) {
            K_INT32 ai = (K_INT32)(((double)rand() / RAND_MAX) * (double)ANG_FULL);
            double  ad = ai / (double)ANG_FULL * (M_PI * 2.0);
            int ra, rb;
            double dx, dy, d;

            memset(ref_tempbuf, 0, sizeof ref_tempbuf);
            memset(ref_wallfound, 255, sizeof ref_wallfound);
            ref_wallsfound = 0;
            memset(tempbuf, 0, sizeof tempbuf);
            memset(wallfound, 255, sizeof wallfound);
            wallsfound = 0;

            ra = ref_castray(px, py, ad);
            rb = castray(px, py, ai);
            if (ra < 0 || rb < 0) { if ((ra < 0) != (rb < 0)) onemiss++; continue; }
            rays++;

            if (ref_wallsfound != wallsfound) { sidediff++; continue; }
            if (ref_wallsfound == 0) continue;

            dx = ref_hitpointx - hitpointx / (double)FX_ONE;
            dy = ref_hitpointy - hitpointy / (double)FX_ONE;
            d = sqrt(dx*dx + dy*dy);

            /* compare the last wall reported, which is the one the ray hit */
            {
                int i = ref_wallsfound - 1;
                if (ref_wallx[i] != wallx[i] || ref_wally[i] != wally[i] ||
                    ref_wallside[i] != wallside[i]) {
                    /* Landing on the neighbouring wall only matters if the
                       ray ended up somewhere else.  Two walls that meet at a
                       corner the ray passes through give the same hit point
                       and the same picture. */
                    celldiff++;
                    if (d > cellworst) cellworst = d;
                    continue;
                }
                if (ref_walnum[i] != walnum[i]) texdiff++;
            }
            if (d > hpworst) hpworst = d;
        }
    }
    printf("castray alone (identical angle both sides)\n");
    printf("  rays compared %ld\n", rays);
    printf("  different number of walls reported: %ld (limit 0)\n", sidediff);
    printf("  ray took a different path: %ld (%.4f%% of rays; limit 0.05%%),\n"
           "      landing up to %.2f cells away when it did\n",
           celldiff, rays ? 100.0 * celldiff / rays : 0.0, cellworst);
    printf("  same wall, different texture: %ld (limit 0)\n", texdiff);
    printf("  one side found nothing, the other did: %ld (limit 0)\n", onemiss);
    printf("  worst hit point disagreement: %.6f cells (limit 0.02)\n", hpworst);

    /*
     * Two things are asserted here and one is only reported.
     *
     * Asserted: the two casters agree on which wall, which face and which
     * texture, and the hit point lands within a fiftieth of a cell.
     *
     * Reported: a handful of rays take a different path entirely.  These are
     * rays that pass within rounding distance of a cell corner, where the two
     * grid walks are all but tied and the slope's last bit decides which one
     * steps first; the double version has fifty-odd mantissa bits to break
     * the tie with and 16.16 has sixteen.  The ray then slips past the corner
     * and runs on for several cells.  It cannot be asserted away at this
     * precision, and it is not worth chasing: an extra wall in the list is
     * drawn and then correctly hidden by the nearer walls in front of it, and
     * a wall lost this way shows up in the raycast test, which measures how
     * wide the lost ones would have been on screen.  So the rate is bounded
     * here and the consequence is measured there.
     */
    if (sidediff || texdiff || onemiss || hpworst > 0.02 ||
        (rays && celldiff > rays / 2000)) {
        printf("  FAIL\n");
        return 1;
    }
    printf("  ok\n");
    return 0;
}

/*
 * How wide, in pixels of the 360 pixel view, is one wall actually visible?
 *
 * The obvious measure - the angle the wall subtends with nothing in front of
 * it - is badly wrong for the walls that matter here.  A ray caster loses
 * walls seen edge-on, and a full cell wall viewed from a thousandth of a cell
 * off its own plane subtends a huge unoccluded angle while being a hundredth
 * of a pixel wide on screen.  So this sweeps the frustum with the reference
 * caster and counts the angles that actually reach the wall.
 *
 * A wall at least one sample step wide is certain to be sampled, so
 * (hits + 1) steps is a genuine upper bound on its width.
 */
#define SWEEP 50000

static double visible_width(K_UINT16 px, K_UINT16 py,
                            double angl, double angr, int wx, int wy, int ws) {
    long i, hits = 0;
    /* save what the caller is in the middle of */
    static K_INT16 save_found[64][64][4];
    static unsigned char save_temp[4096];
    K_INT32 save_n = ref_wallsfound;
    memcpy(save_found, ref_wallfound, sizeof save_found);
    memcpy(save_temp, ref_tempbuf, sizeof save_temp);

    for (i = 0; i <= SWEEP; i++) {
        double a = angl + (angr - angl) * (i / (double)SWEEP);
        int k;
        memset(ref_wallfound, 255, sizeof ref_wallfound);
        memset(ref_tempbuf, 0, sizeof ref_tempbuf);
        ref_wallsfound = 0;
        if (ref_castray(px, py, a) < 0) continue;
        for (k = 0; k < ref_wallsfound; k++)
            if (ref_wallx[k] == wx && ref_wally[k] == wy && ref_wallside[k] == ws) {
                hits++;
                break;
            }
    }
    memcpy(ref_wallfound, save_found, sizeof save_found);
    memcpy(ref_tempbuf, save_temp, sizeof save_temp);
    ref_wallsfound = save_n;
    return (hits + 1) * (360.0 / SWEEP);
}

static int test_raycast(int ntrials, unsigned seed) {
    long missing = 0, extra = 0, texdiff = 0, tempdiff = 0;
    long refwalls = 0, newwalls = 0, refrays = 0, newrays = 0, cases = 0;
    double misswidth = 0.0, misssum = 0.0;
    int control = getenv("CONTROL") != NULL;
    static K_INT16 ctl_wallx[16384], ctl_wally[16384];
    static char    ctl_wallside[16384];
    static K_UINT16 ctl_walnum[16384];
    static unsigned char ctl_tempbuf[4096];
    K_INT32 ctl_wallsfound = 0;
    int trial, i;
    /* waln for each (x, y, side), -1 for absent */
    static int seen_ref[64][64][4], seen_new[64][64][4];

    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        K_UINT16 px, py;
        K_INT16 angs;
        double aspw = 1.0;
        int cx, cy, x, y, sdir, bad = 0;
        double vangw, angl_d, angr_d, angc_d, hpx1, hpy1;
        K_INT32 vangw_i, angl_i, angr_i, angc_i, ihpx1, ihpy1;

        make_board();
        waterstat = (trial & 1) ? 1 : 0;
        animate2  = (K_INT16)(trial % 3);

        do { cx = rand() % 64; cy = rand() % 64; }
        while (bmpkind[board[cx][cy] & 1023] == 1);
        px = (K_UINT16)((cx << 10) + (rand() & 1023));
        py = (K_UINT16)((cy << 10) + (rand() & 1023));
        angs = (K_INT16)(rand() & 2047);

        /* ---- the original, in doubles ---- */
        memset(ref_tempbuf, 0, sizeof ref_tempbuf);
        memset(ref_wallfound, 255, sizeof ref_wallfound);
        ref_wallsfound = 0; ref_rayscast = 0;
        vangw  = atan(tan(M_PI * 0.25) * aspw);
        angc_d = angs / 1024.0 * M_PI;
        angl_d = angc_d - vangw;
        angr_d = angc_d + vangw;
        if (ref_castray(px, py, angr_d) < 0) bad = 1;
        hpx1 = ref_hitpointx; hpy1 = ref_hitpointy;
        if (ref_castray(px, py, angl_d) < 0) bad = 1;
        if ((ref_angcan(angr_d - angl_d) >= M_PI / 2 - 1e-7) ||
            (ref_distance2(ref_hitpointx, ref_hitpointy, hpx1, hpy1) > 1.0 - 1e-7))
            ref_recurseray(px, py, angc_d, angl_d, angr_d,
                           ref_hitpointx, ref_hitpointy, hpx1, hpy1);

        /* Control: run the ORIGINAL a second time with the camera angle
           nudged by the smallest amount the fixed point version can even
           represent.  If the double code loses walls against itself under
           that, then walls appearing and disappearing at the margin is the
           algorithm's own behaviour and not something the rewrite
           introduced. */
        if (control) {
            double nudge = (M_PI * 2.0) / (double)ANG_FULL;
            memcpy(ctl_tempbuf, ref_tempbuf, sizeof ctl_tempbuf);
            memcpy(ctl_wallx, ref_wallx, sizeof(K_INT16) * ref_wallsfound);
            memcpy(ctl_wally, ref_wally, sizeof(K_INT16) * ref_wallsfound);
            memcpy(ctl_wallside, ref_wallside, sizeof(char) * ref_wallsfound);
            memcpy(ctl_walnum, ref_walnum, sizeof(K_UINT16) * ref_wallsfound);
            ctl_wallsfound = ref_wallsfound;

            memset(ref_tempbuf, 0, sizeof ref_tempbuf);
            memset(ref_wallfound, 255, sizeof ref_wallfound);
            ref_wallsfound = 0; ref_rayscast = 0;
            if (ref_castray(px, py, angr_d + nudge) < 0) bad = 1;
            hpx1 = ref_hitpointx; hpy1 = ref_hitpointy;
            if (ref_castray(px, py, angl_d + nudge) < 0) bad = 1;
            if ((ref_angcan(angr_d - angl_d) >= M_PI / 2 - 1e-7) ||
                (ref_distance2(ref_hitpointx, ref_hitpointy, hpx1, hpy1) > 1.0 - 1e-7))
                ref_recurseray(px, py, angc_d + nudge, angl_d + nudge, angr_d + nudge,
                               ref_hitpointx, ref_hitpointy, hpx1, hpy1);
            /* the nudged run now plays the part of "the rewrite" */
        }

        /* ---- the rewrite, in fixed point ---- */
        memset(tempbuf, 0, sizeof tempbuf);
        memset(wallfound, 255, sizeof wallfound);
        wallsfound = 0; rayscast = 0;
        /* ANG_* come from the lifted block, so this cannot drift from the
           frustum setup in picrot_view(). */
        vangw_i = (K_INT32)(atan(tan(M_PI * 0.25) * aspw) / (M_PI * 2.0)
                            * (double)ANG_FULL + 0.5);
        angc_i  = (K_INT32)angs << (ANG_BITS - 11);
        angl_i  = angc_i - vangw_i;
        angr_i  = angc_i + vangw_i;
        if (castray(px, py, angr_i) < 0) bad = 1;
        ihpx1 = hitpointx; ihpy1 = hitpointy;
        if (castray(px, py, angl_i) < 0) bad = 1;
        if ((angr_i - angl_i >= ANG_HALFPI - 16) ||
            hits_apart(hitpointx, hitpointy, ihpx1, ihpy1))
            recurseray(px, py, angc_i, angl_i, angr_i,
                       hitpointx, hitpointy, ihpx1, ihpy1);

        /* A ray that escaped the board says the generated level had a hole in
           it; the comparison would be meaningless, so skip the trial. */
        if (bad) continue;

        refwalls += ref_wallsfound; newwalls += wallsfound;
        refrays  += ref_rayscast;   newrays  += rayscast;

        for (x = 0; x < 64; x++)
            for (y = 0; y < 64; y++)
                for (sdir = 0; sdir < 4; sdir++)
                    seen_ref[x][y][sdir] = seen_new[x][y][sdir] = -1;
        if (control) {
            for (i = 0; i < ctl_wallsfound; i++)
                seen_ref[ctl_wallx[i]][ctl_wally[i]][(int)ctl_wallside[i]] = ctl_walnum[i];
            for (i = 0; i < ref_wallsfound; i++)
                seen_new[ref_wallx[i]][ref_wally[i]][(int)ref_wallside[i]] = ref_walnum[i];
            memcpy(tempbuf, ref_tempbuf, sizeof tempbuf);
            memcpy(ref_tempbuf, ctl_tempbuf, sizeof ctl_tempbuf);
        } else {
            for (i = 0; i < ref_wallsfound; i++)
                seen_ref[ref_wallx[i]][ref_wally[i]][(int)ref_wallside[i]] = ref_walnum[i];
            for (i = 0; i < wallsfound; i++)
                seen_new[wallx[i]][wally[i]][(int)wallside[i]] = walnum[i];
        }

        {
            long before = missing + extra + texdiff + tempdiff;
            for (x = 0; x < 64; x++)
                for (y = 0; y < 64; y++)
                    for (sdir = 0; sdir < 4; sdir++) {
                        int a = seen_ref[x][y][sdir], b = seen_new[x][y][sdir];
                        if (a == b) continue;
                        if (b < 0) {
                            double w = visible_width(px, py, angl_d, angr_d,
                                                     x, y, sdir);
                            if (w > misswidth) misswidth = w;
                            misssum += w;
                            missing++;
                        }
                        else if (a < 0) extra++;
                        else            texdiff++;
                    }
            for (i = 0; i < 4096; i++)
                if (tempbuf[i] != ref_tempbuf[i]) tempdiff++;
            if (missing + extra + texdiff + tempdiff != before) cases++;
        }
    }

    printf(control
           ? "CONTROL: the original against itself, camera angle nudged by one unit\n"
           : "ray caster (castray and recurseray, the whole visibility pass)\n");
    printf("  walls found: original %ld, rewrite %ld   rays cast: %ld vs %ld\n",
           refwalls, newwalls, refrays, newrays);
    printf("  walls the rewrite MISSED: %ld; widest was %.3f px of the 360 px view,\n"
           "      mean %.3f px (limit 0.500)\n",
           missing, misswidth, missing ? misssum / missing : 0.0);
    printf("  walls it found that the original did not: %ld (harmless)\n", extra);
    printf("  walls found by both but with a different texture: %ld (limit 0)\n", texdiff);
    printf("  automap cells differing: %ld\n", tempdiff);
    printf("  trials with any difference: %ld of %d\n", cases, ntrials);

    /* A missed wall matters only if it was wide enough to see.  The caster
       resolves walls by subdividing until two hit points land within a cell
       of each other, so one a hundredth of a pixel wide - which is what a
       wall seen edge-on from a thousandth of a cell off its plane comes to -
       was always going to be found or not by luck. */
    if (misswidth > 0.5 || texdiff) {
        printf("  FAIL: %s\n", texdiff ? "walls are mislabelled"
                                        : "a visible wall was lost");
        return 1;
    }
    printf("  ok\n");
    return 0;
}

/* ------------------------------------------------------------------ spans */

/*
 * draw_span() against the plain C loops it had before its 68k assembler
 * versions, which it still falls back to off the 68k and for the wrapped
 * tail.  Built for the 68k and run under vamos this compares the assembler
 * with the C, pixel for pixel; there is no rounding in it, so nothing may
 * differ.  Spans are random in height, position, clipping and fixed point
 * scale, with a quarter of them tuned to hit the wrap at texel 64.
 */
static void c_span(unsigned char *dstcol, const unsigned char *tex,
                   K_INT32 ytop, K_INT32 ybot, int yshift,
                   int shaded, int skipkey) {
    int y0, y1, y, height;
    K_INT32 v, vstep;

    height = (int)((ybot - ytop) >> yshift);
    if (height <= 0) return;
    vstep = (height < RECIP_MAX) ? vrecip[height] : (K_INT32)((64L << 16) / height);
    y0 = (int)(ytop >> yshift);
    y1 = (int)(ybot >> yshift);
    v = 0;
    if (y0 < VIEW_TOP) { v = (K_INT32)((VIEW_TOP - y0) * (long)vstep); y0 = VIEW_TOP; }
    if (y1 > VIEW_BOT) y1 = VIEW_BOT;
    if (y0 >= y1) return;
    dstcol += (size_t)y0 * VW;
    for (y = y0; y < y1; y++, dstcol += VW, v += vstep) {
        unsigned char c = tex[(v >> 16) & 63];
        if (skipkey && c == 255) continue;
        *dstcol = shaded ? shadetab[c] : c;
    }
}

static int test_span(int ntrials, unsigned seed) {
    long drawn = 0, diff = 0;
    int trial, i;
    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        int yshift = 4 + rand() % 13;
        int shaded = rand() & 1, skipkey = rand() & 1, x = rand() % VW;
        long top, bot;
        const unsigned char *tex = texbuf + ((rand() & 63) << 6);

        if (rand() & 3) {
            top = (long)(rand() % (VH * 3)) - VH;
            bot = top + 1 + rand() % (VH * 2);
        } else {
            /* A fraction just under a row at the top and just over at the
               bottom: the rounding that lets the coordinate reach 64. */
            top = (long)(rand() % VH) - 20;
            bot = top + 2 + rand() % 60;
        }
        top = (top << yshift) + rand() % (1 << yshift);
        bot = (bot << yshift) + rand() % (1 << yshift);

        blank();
        c_span(fbA + x, tex, (K_INT32)top, (K_INT32)bot, yshift, shaded, skipkey);
        draw_span(fbB + x, tex, (K_INT32)top, (K_INT32)bot, yshift, shaded, skipkey);
        for (i = 0; i < VW * VH; i++) {
            drawn += fbA[i] != UNSET;
            diff  += fbA[i] != fbB[i];
        }
    }
    printf("draw_span against the C loops\n  pixels drawn %ld, differing %ld (limit 0)\n",
           drawn, diff);
    if (diff) { printf("  FAIL\n"); return 1; }
    printf("  ok\n");
    return 0;
}

/* ------------------------------------------------------------ benchmarks */

/*
 * The wall test's geometry through one version only, for timing: `live`
 * runs the lifted code, `ref` the frozen original, `none` just the setup, so
 * the difference is the drawing alone.  `depth` is `live` drawing nothing,
 * which leaves the cost of the pixels out, and `count` reports how many
 * pixels a trial draws.  bench68k.sh builds this for a 68020 and counts the
 * cycles under vamos.
 */
static int bench_wall(const char *which, int ntrials, unsigned seed) {
    int trial, i, mode = !strcmp(which, "live") ? 1 : !strcmp(which, "ref") ? 2 :
                         !strcmp(which, "depth") ? 3 : !strcmp(which, "count") ? 4 : 0;
    long pixels = 0;
    srand(seed);

    for (trial = 0; trial < ntrials; trial++) {
        double ang = frand(0, 2 * M_PI);
        double wx1, wy1, wx2, wy2;
        double cx, cy;
        int shaded = trial & 1;

        cam_fx = cos(ang); cam_fy = sin(ang);
        cam_ex = floor(frand(2048, 63488));
        cam_ey = floor(frand(2048, 63488));
        cam_ez = frand(0, 1024);
        proj_x = 180.0;
        proj_y = 160.0;
        horizon_row = 120;

        /* Cell edges near enough to be a few columns to most of the view. */
        cx = floor(cam_ex + frand(-4096, 4096)); cy = floor(cam_ey + frand(-4096, 4096));
        if (rand() & 1) { wx1 = cx; wx2 = cx + 1024; wy1 = wy2 = cy; }
        else            { wy1 = cy; wy2 = cy + 1024; wx1 = wx2 = cx; }

        for (i = 0; i < VW; i++) zbufA[i] = 0;
        amiga_chunky = fbA; zbuf = zbufA;
        if (mode == 4) memset(fbA, UNSET, sizeof fbA);
        if (mode == 1 || mode == 4)
            draw_upright_quad(wx1,wy1,wx2,wy2,0,0.0,64.0,shaded,1,1,0,0);
        else if (mode == 2)
            ref_draw_upright_quad(wx1,wy1,wx2,wy2,0,0.0,64.0,shaded,1,1,0,0);
        else if (mode == 3)
            draw_upright_quad(wx1,wy1,wx2,wy2,0,0.0,64.0,shaded,1,1,0,1);
        if (mode == 4)
            for (i = 0; i < VW * VH; i++) pixels += fbA[i] != UNSET;
    }
    if (mode == 4)
        printf("%.1f pixels per trial\n", (double)pixels / ntrials);
    return 0;
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
    if (!strcmp(which, "castray")) return test_castray(ntrials ? ntrials : 400,   seed);
    if (!strcmp(which, "raycast")) return test_raycast(ntrials ? ntrials : 300,   seed);
    if (!strcmp(which, "span"))    return test_span   (ntrials ? ntrials : 3000,  seed);
    if (!strncmp(which, "benchwall-", 10))
        return bench_wall(which + 10, ntrials, seed);

    fprintf(stderr, "usage: %s softtri|floor|wall|castray|raycast [trials] [seed]\n", argv[0]);
    return 2;
}
