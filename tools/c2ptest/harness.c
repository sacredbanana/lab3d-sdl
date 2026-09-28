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
        int depth    = 1 + (int)rnd(8);
        int usemap   = rnd(3) == 0;
        int inter    = rnd(2);
        int rowbytes = 1 + (int)rnd(64);        /* odd widths included */
        int rows     = 1 + (int)rnd(24);
        int destx    = 8 * (int)rnd((unsigned)rowbytes);
        int w        = 8 * (int)rnd((unsigned)(rowbytes - destx / 8) + 1);
        int desty    = (int)rnd((unsigned)rows);
        int h        = (int)rnd((unsigned)(rows - desty) + 1);
        int srcmod   = w + (int)rnd(40);
        size_t n;

        if ((size_t)srcmod * (h ? h : 1) > sizeof(src)) continue;
        for (n = 0; n < (size_t)srcmod * h; n++)
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

        amiga_c2p_setmap(usemap ? map : NULL);
        amiga_c2p(src, srcmod, &got.bm, destx, desty, w, h, depth);
        ref_c2p(src, srcmod, &want.bm, destx, desty, w, h, depth,
                usemap ? map : NULL);
        pixels += (long)w * h;

        if (memcmp(got.arena, want.arena, ARENA) != 0) {
            for (n = 0; n < ARENA && got.arena[n] == want.arena[n]; n++)
                ;
            if (fails < 10)
                printf("FAIL trial %d: depth %d map %d interleaved %d "
                       "rowbytes %d dest (%d,%d) %dx%d srcmod %d: "
                       "first difference at arena byte %lu\n",
                       t, depth, usemap, inter, rowbytes, destx, desty,
                       w, h, srcmod, (unsigned long)n);
            fails++;
        }
    }

    printf("c2p: %d trials, %ld pixels, %d failed\n", trials, pixels, fails);
    return fails != 0;
}
