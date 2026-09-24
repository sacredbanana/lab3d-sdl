/*
 * Chunky to planar conversion for the native (non-RTG) Amiga display.
 *
 * The rasteriser produces one byte per pixel; Amiga native modes want one bit
 * per pixel per bitplane.  This converts 8 pixels at a time using two spread
 * tables:
 *
 *   sprlo[c]  bit 8*p set when bit p of c is set, for p = 0..3
 *   sprhi[c]  bit 8*p set when bit p+4 of c is set, for p = 0..3
 *
 * Shifting a table entry left by (7-i) and OR-ing it in for pixel i therefore
 * accumulates, in byte p of the accumulator, exactly the bitplane-p byte for
 * those eight pixels.  Two 32 bit accumulators cover all eight planes.
 *
 * This is a portable formulation rather than a hand-tuned 68020 c2p: it costs
 * roughly eight operations per pixel, which is the main reason a native AGA
 * screen runs noticeably slower than an RTG one.  The interface is narrow
 * enough that dropping in an assembler routine later means replacing one
 * function.
 */

#include <string.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <graphics/gfx.h>
#else
/* Host build, for the correctness test in tools/. */
#include <stdint.h>
typedef uint8_t  UBYTE;
typedef uint16_t UWORD;
typedef uint32_t ULONG;
struct BitMap {
    UWORD BytesPerRow;
    UWORD Rows;
    UBYTE Flags;
    UBYTE Depth;
    UWORD pad;
    UBYTE *Planes[8];
};
#endif

static ULONG sprlo[256];
static ULONG sprhi[256];
static int   c2p_ready;

/* Optional 256 entry map applied before conversion, for screens with fewer
   than 256 pens.  NULL means identity. */
static const UBYTE *c2p_penmap;

void amiga_c2p_setmap(const UBYTE *pens) {
    c2p_penmap = pens;
}

void amiga_c2p_init(void) {
    int c, p;

    if (c2p_ready) return;

    for (c = 0; c < 256; c++) {
        ULONG lo = 0, hi = 0;
        for (p = 0; p < 4; p++) {
            if (c & (1 << p))        lo |= 1UL << (8 * p);
            if (c & (1 << (p + 4)))  hi |= 1UL << (8 * p);
        }
        sprlo[c] = lo;
        sprhi[c] = hi;
    }
    c2p_ready = 1;
}

/*
 * Convert a w x h block of chunky pixels into `bm` at (destx, desty).
 * destx must be a multiple of 8 and w a multiple of 8; amiga_video.c
 * guarantees both.
 */
void amiga_c2p(const UBYTE *src, int srcmod,
               struct BitMap *bm, int destx, int desty,
               int w, int h, int depth)
{
    int rowbytes = bm->BytesPerRow;
    int y, x, p;
    UBYTE *planerow[8];

    if (!c2p_ready) amiga_c2p_init();
    if (depth > 8) depth = 8;

    /* Both interleaved and plain bitmaps advance one row by BytesPerRow, so
       there is nothing to special-case here. */
    for (p = 0; p < depth; p++)
        planerow[p] = bm->Planes[p] + desty * rowbytes + (destx >> 3);

    w &= ~7;

    for (y = 0; y < h; y++) {
        const UBYTE *s = src;
        UBYTE *d[8];

        for (p = 0; p < depth; p++)
            d[p] = planerow[p];

        for (x = 0; x < w; x += 8) {
            ULONG lo = 0, hi = 0;
            int i;

            if (c2p_penmap) {
                for (i = 0; i < 8; i++) {
                    UBYTE c = c2p_penmap[*s++];
                    lo |= sprlo[c] << (7 - i);
                    hi |= sprhi[c] << (7 - i);
                }
            } else {
                for (i = 0; i < 8; i++) {
                    UBYTE c = *s++;
                    lo |= sprlo[c] << (7 - i);
                    hi |= sprhi[c] << (7 - i);
                }
            }

            switch (depth) {
            case 8: *d[7]++ = (UBYTE)(hi >> 24);   /* fall through */
            case 7: *d[6]++ = (UBYTE)(hi >> 16);   /* fall through */
            case 6: *d[5]++ = (UBYTE)(hi >> 8);    /* fall through */
            case 5: *d[4]++ = (UBYTE)(hi);         /* fall through */
            case 4: *d[3]++ = (UBYTE)(lo >> 24);   /* fall through */
            case 3: *d[2]++ = (UBYTE)(lo >> 16);   /* fall through */
            case 2: *d[1]++ = (UBYTE)(lo >> 8);    /* fall through */
            case 1: *d[0]++ = (UBYTE)(lo);         /* fall through */
            default: break;
            }
        }

        for (p = 0; p < depth; p++)
            planerow[p] += rowbytes;
        src += srcmod;
    }
}
