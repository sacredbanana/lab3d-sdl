/*
 * Correctness test for the Amiga chunky to planar conversion.
 *
 * src/amiga/amiga_c2p.c is compiled here as it is, in its host build mode,
 * and run against the plain definition of the planar layout: bit 7 - (x & 7)
 * of byte x >> 3 of plane p holds bit p of pixel x.  Unlike the renderer
 * tests there is no rounding to allow for, so every byte of every plane,
 * inside the target rectangle and out, must match exactly.
 *
 * Each trial picks a depth, a pen map or none, an interleaved or a plain
 * bitmap with planes at random alignments, and a random rectangle, so the
 * byte-at-a-time head and tail and the 32 pixel blocks are all exercised
 * against each other at every offset.
 *
 *   ./harness [trials] [seed]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/amiga/amiga_c2p.c"

static unsigned long rng_state;

static unsigned rnd(unsigned n) {
    rng_state = rng_state * 1103515245UL + 12345UL;
    return (unsigned)((rng_state >> 8) % n);
}

#ifdef AMIGA_BLITTER_C2P
/*
 * A model of the blitter, as much of it as the conversion uses: channels A,
 * B and D, C as a constant, both shifts, ascending and descending, modulos,
 * the first and last word masks and any minterm.  The bits a shift brings in
 * come from the previous word of the same channel, carried across rows and
 * across blits as the hardware does; they start as garbage here so that a
 * blit relying on them fails.
 *
 * c2p_host_busy() says "busy" at random, which leaves blits queued for a
 * while and so exercises the queue.  A blit then runs all at once when it
 * starts, which is the ordering that matters: it must see its staging rows
 * written and nothing of a later frame.
 */
static int   blits_run;
static UWORD olda = 0x9e37, oldb = 0x79b9;

static UWORD rd16(const UBYTE *p) { return (UWORD)((p[0] << 8) | p[1]); }
static void  wr16(UBYTE *p, UWORD v) { p[0] = (UBYTE)(v >> 8); p[1] = (UBYTE)v; }

static UWORD minterm(UBYTE lf, UWORD a, UWORD b, UWORD c) {
    UWORD d = 0;
    int n;

    for (n = 0; n < 8; n++)
        if (lf & (1 << n))
            d |= (UWORD)((n & 4 ? a : ~a) & (n & 2 ? b : ~b) & (n & 1 ? c : ~c));
    return d;
}

void c2p_host_start(const c2p_blit *b) {
    int h = b->size >> 6, w = b->size & 63;
    int ash = b->con0 >> 12, bsh = b->con1 >> 12, desc = b->con1 & 2;
    int step = desc ? -2 : 2, x, y;
    const UBYTE *ap = b->apt, *bp = b->bpt;
    UBYTE *dp = b->dpt;

    if (!h) h = 1024;
    if (!w) w = 64;
    if ((b->con0 & 0x0f00) != 0x0d00) {
        printf("blit uses channels %03x, expected A, B and D\n", b->con0 & 0x0f00);
        exit(1);
    }

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            UWORD a = rd16(ap), bv = rd16(bp), as, bs;

            if (x == 0)     a &= 0xffff;    /* BLTAFWM, as programmed */
            if (x == w - 1) a &= 0xffff;    /* BLTALWM */
            if (!desc) {
                as = (UWORD)((((ULONG)olda << 16) | a)  >> ash);
                bs = (UWORD)((((ULONG)oldb << 16) | bv) >> bsh);
            } else {
                as = (UWORD)((((ULONG)a  << 16) | olda) >> (16 - ash));
                bs = (UWORD)((((ULONG)bv << 16) | oldb) >> (16 - bsh));
            }
            olda = a;
            oldb = bv;
            wr16(dp, minterm((UBYTE)b->con0, as, bs, b->cdat));
            ap += step; bp += step; dp += step;
        }
        ap += desc ? -(short)b->amod : (short)b->amod;
        bp += desc ? -(short)b->bmod : (short)b->bmod;
        dp += desc ? -(short)b->dmod : (short)b->dmod;
    }
    blits_run++;
}

int c2p_host_busy(void) { return rnd(3) != 0; }

void *c2p_host_chipalloc(ULONG n) {
    UBYTE *p = malloc(n);
    size_t i;

    if (p)
        for (i = 0; i < n; i++)
            p[i] = (UBYTE)rnd(256);     /* chip RAM starts out as garbage */
    return p;
}

void c2p_host_chipfree(void *p, ULONG n) { (void)n; free(p); }
#endif

/* The definition: one bit at a time. */
static void ref_c2p(const UBYTE *src, int srcmod, struct BitMap *bm,
                    int destx, int desty, int w, int h, int depth,
                    const UBYTE *map) {
    int x, y, p;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            UBYTE c = src[y * srcmod + x];
            int dx = destx + x;

            if (map) c = map[c];
            for (p = 0; p < depth; p++) {
                UBYTE *b = bm->Planes[p] + (desty + y) * bm->BytesPerRow + (dx >> 3);
                UBYTE bit = (UBYTE)(0x80 >> (dx & 7));

                if (c & (1 << p)) *b |= bit;
                else              *b &= (UBYTE)~bit;
            }
        }
}

/* Planes live in one arena, so a stray write anywhere shows up in the
   comparison rather than in some unrelated allocation. */
#define ARENA 262144

typedef struct {
    UBYTE arena[ARENA];
    struct BitMap bm;
} target;

/* Lay out a bitmap in t: interleaved (plane p is row p of each group of
   depth rows) or plain, each plane starting at a random offset mod 4. */
static void layout(target *t, int rowbytes, int rows, int depth, int interleaved) {
    size_t at = 16;
    int p;

    memset(&t->bm, 0, sizeof(t->bm));
    t->bm.Rows  = (UWORD)rows;
    t->bm.Depth = (UBYTE)depth;

    if (interleaved) {
        at += rnd(4);
        t->bm.BytesPerRow = (UWORD)(rowbytes * depth);
        for (p = 0; p < depth; p++)
            t->bm.Planes[p] = t->arena + at + (size_t)p * rowbytes;
    } else {
        t->bm.BytesPerRow = (UWORD)rowbytes;
        for (p = 0; p < depth; p++) {
            at += rnd(4);
            t->bm.Planes[p] = t->arena + at;
            at += (size_t)rowbytes * rows + 16;
        }
    }
}

/* One conversion into got and the same into want. */
static void convert(target *got, target *want, const UBYTE *src, int srcmod,
                    int destx, int desty, int w, int h, int depth,
                    const UBYTE *map) {
    amiga_c2p_setmap(map);
    amiga_c2p(src, srcmod, &got->bm, destx, desty, w, h, depth);
    ref_c2p(src, srcmod, &want->bm, destx, desty, w, h, depth, map);
}

int main(int argc, char **argv) {
    int trials = argc > 1 ? atoi(argv[1]) : 0;
    unsigned long seed = argc > 2 ? strtoul(argv[2], NULL, 10) : 1;
    static target got, want;
    static UBYTE src[65536], map[256];
    int i, t, fails = 0;
    long pixels = 0;

    if (trials <= 0) trials = 20000;
    rng_state = seed;

    for (i = 0; i < 256; i++)
        map[i] = (UBYTE)rnd(256);

    for (t = 0; t < trials; t++) {
        /* Now and then a row wider than one blit can cover (64 words). */
        int wide     = rnd(8) == 0;
        int depth    = 1 + (int)rnd(8);
        int usemap   = rnd(3) == 0;
        int inter    = rnd(2);
        int rowbytes = 1 + (int)rnd(wide ? 200 : 64);   /* odd widths too */
        int rows     = 1 + (int)rnd(wide ? 12 : 40);
        int destx    = 8 * (int)rnd((unsigned)rowbytes);
        int w        = 8 * (int)rnd((unsigned)(rowbytes - destx / 8) + 1);
        int desty    = (int)rnd((unsigned)rows);
        int h        = (int)rnd((unsigned)(rows - desty) + 1);
        int srcmod   = w + (int)rnd(40);
        /* A whole frame of chunky pixels, cut into overlapping rectangles
           the way the display's dirty rectangle lists are. */
        int frame    = rnd(2);
        const UBYTE *m = usemap ? map : NULL;
        size_t n;

        if (frame) srcmod = rowbytes * 8;
        if ((size_t)srcmod * (frame ? rows : h) > sizeof(src)) continue;
        for (n = 0; n < (size_t)srcmod * (frame ? rows : h); n++)
            src[n] = (UBYTE)rnd(256);

        /* Same layout, same starting garbage, in both. */
        {
            unsigned long keep = rng_state;
            layout(&got, rowbytes, rows, depth, inter);
            rng_state = keep;
            layout(&want, rowbytes, rows, depth, inter);
        }
        for (n = 0; n < ARENA; n++)
            got.arena[n] = want.arena[n] = (UBYTE)rnd(256);

#ifdef AMIGA_BLITTER_C2P
        /* A staging area that usually covers the rectangle and now and then
           does not, which must fall back to the CPU. */
        amiga_cfg_blitter = AMIGA_BLITTER_ON_VALUE;
        if (rnd(5)) {
            int ax = 8 * (int)rnd((unsigned)(destx / 8) + 1);
            int ay = (int)rnd((unsigned)desty + 1);
            amiga_c2p_blit_area(ax, ay,
                                rowbytes * 8 - ax + (int)rnd(40),
                                rows - ay);
        } else {
            amiga_c2p_blit_area(8 * (int)rnd((unsigned)rowbytes), (int)rnd((unsigned)rows),
                                8 * (int)rnd((unsigned)rowbytes), (int)rnd((unsigned)rows));
        }
        amiga_c2p_frame_done(0);
#endif

        if (!frame) {
            convert(&got, &want, src, srcmod, destx, desty, w, h, depth, m);
            pixels += (long)w * h;
        } else {
            int k, parts = 1 + (int)rnd(4);

            for (k = 0; k < parts; k++) {
                int rx = 8 * (int)rnd((unsigned)rowbytes);
                int rw = 8 * (int)rnd((unsigned)(rowbytes - rx / 8) + 1);
                int ry = (int)rnd((unsigned)rows);
                int rh = (int)rnd((unsigned)(rows - ry) + 1);

                convert(&got, &want, src + ry * srcmod + rx, srcmod,
                        rx, ry, rw, rh, depth, m);
                pixels += (long)rw * rh;
            }
        }
        amiga_c2p_sync();

        if (memcmp(got.arena, want.arena, ARENA) != 0) {
            for (n = 0; n < ARENA && got.arena[n] == want.arena[n]; n++)
                ;
            if (fails < 10)
                printf("FAIL trial %d: depth %d map %d interleaved %d "
                       "rowbytes %d rows %d frame %d dest (%d,%d) %dx%d "
                       "srcmod %d: first difference at arena byte %lu\n",
                       t, depth, usemap, inter, rowbytes, rows, frame,
                       destx, desty, w, h, srcmod, (unsigned long)n);
            fails++;
        }
    }

#ifdef AMIGA_BLITTER_C2P
    amiga_c2p_blit_area(0, 0, 0, 0);
    printf("c2p with blitter assist: %d trials, %ld pixels, %d blits, %d failed\n",
           trials, pixels, blits_run, fails);
#else
    printf("c2p: %d trials, %ld pixels, %d failed\n", trials, pixels, fails);
#endif
    return fails != 0;
}
