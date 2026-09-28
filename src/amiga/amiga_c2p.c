/*
 * Chunky to planar conversion for the native (non-RTG) Amiga display.
 *
 * The rasteriser produces one byte per pixel; Amiga native modes want one bit
 * per pixel per bitplane.  Each row is converted 32 pixels at a time with the
 * merge method: the 32 chunky bytes are loaded as eight longwords, which hold
 * a 32 x 8 matrix of bits, and five rounds of masked swaps transpose it so
 * that each longword ends up holding one bitplane's 32 bits.  That is about
 * four operations per pixel, and - what matters more on AGA, where chip RAM
 * is the bottleneck - it writes the screen a longword at a time, a quarter of
 * the bus accesses of writing it a byte at a time.
 *
 * The head and tail of a row that do not fill a whole longword of plane go
 * through a slower table method eight pixels at a time; the head is sized to
 * line the planes up on a longword, since an unaligned longword write to chip
 * RAM costs two bus cycles.
 *
 * tools/c2ptest checks this against the plain definition of the planar
 * layout, bit for bit, for every depth and alignment.
 */

#include <stddef.h>
#include <string.h>

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <graphics/gfx.h>

/* The 68020 and up take unaligned longword accesses, and the Amiga is big
   endian, so the first chunky pixel lands in the top byte for free. */
#define load32(p)     (*(const ULONG *)(p))
#define store32(p, v) (*(ULONG *)(p) = (v))
#else
/* Host build, for the correctness test in tools/c2ptest. */
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

/* Spell out the big endian order the Amiga has natively. */
static ULONG load32(const UBYTE *p) {
    return ((ULONG)p[0] << 24) | ((ULONG)p[1] << 16) |
           ((ULONG)p[2] << 8)  |  (ULONG)p[3];
}
static void store32(UBYTE *p, ULONG v) {
    p[0] = (UBYTE)(v >> 24); p[1] = (UBYTE)(v >> 16);
    p[2] = (UBYTE)(v >> 8);  p[3] = (UBYTE)v;
}
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

/*
 * Spread tables for the eight pixel path:
 *
 *   sprlo[c]  bit 8*p set when bit p of c is set, for p = 0..3
 *   sprhi[c]  bit 8*p set when bit p+4 of c is set, for p = 0..3
 *
 * Shifting a table entry left by (7-i) and OR-ing it in for pixel i therefore
 * accumulates, in byte p of the accumulator, exactly the bitplane-p byte for
 * those eight pixels.
 */
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

/* Convert n groups of eight pixels into plane bytes pl[p][o...]. */
static const UBYTE *c2p_groups(const UBYTE *s, UBYTE *const *pl, int o,
                               int n, int depth) {
    const UBYTE *map = c2p_penmap;

    for (; n > 0; n--, o++) {
        ULONG lo = 0, hi = 0;
        int i;

        if (map) {
            for (i = 0; i < 8; i++) {
                UBYTE c = map[*s++];
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
        case 8: pl[7][o] = (UBYTE)(hi >> 24);   /* fall through */
        case 7: pl[6][o] = (UBYTE)(hi >> 16);   /* fall through */
        case 6: pl[5][o] = (UBYTE)(hi >> 8);    /* fall through */
        case 5: pl[4][o] = (UBYTE)(hi);         /* fall through */
        case 4: pl[3][o] = (UBYTE)(lo >> 24);   /* fall through */
        case 3: pl[2][o] = (UBYTE)(lo >> 16);   /* fall through */
        case 2: pl[1][o] = (UBYTE)(lo >> 8);    /* fall through */
        case 1: pl[0][o] = (UBYTE)(lo);         /* fall through */
        default: break;
        }
    }
    return s;
}

/* Swap the bits of a selected by ~m << s with the bits of b selected by m. */
#define MERGE(a, b, s, m) do {                      \
        ULONG t_ = (((a) >> (s)) ^ (b)) & (m);      \
        (b) ^= t_;                                  \
        (a) ^= t_ << (s);                           \
    } while (0)

/* Four pixels through the pen map, packed as load32() would pack them. */
#define MAP4(p) (((ULONG)map[(p)[0]] << 24) | ((ULONG)map[(p)[1]] << 16) | \
                 ((ULONG)map[(p)[2]] << 8)  |  (ULONG)map[(p)[3]])

/*
 * Convert n blocks of 32 pixels into plane longwords pl[p][o...].
 *
 * Label each bit by the pixel it belongs to, p = 0..31, and the plane it is
 * for, k = 0..7.  Loaded with pixels 0..3 in a7 down to 28..31 in a0, the
 * bit sits in longword ~p>>2 at position (~p & 3) * 8 + k; the planes want
 * it in longword k at position 31 - p, i.e. ~p.  Each MERGE swaps one bit of
 * the longword index with one bit of the position, so the transpose is five
 * of them - moving ~p's top three bits down into the position, the plane
 * number up into the index, and ~p's bottom two bits across - and the planes
 * come out in a0,a2,a4,a6,a1,a3,a5,a7 because of the order that happens in.
 */
static const UBYTE *c2p_blocks(const UBYTE *s, UBYTE *const *pl, int o,
                               int n, int depth) {
    const UBYTE *map = c2p_penmap;

    for (; n > 0; n--, o += 4, s += 32) {
        ULONG a0, a1, a2, a3, a4, a5, a6, a7;

        if (map) {
            a7 = MAP4(s);      a6 = MAP4(s + 4);
            a5 = MAP4(s + 8);  a4 = MAP4(s + 12);
            a3 = MAP4(s + 16); a2 = MAP4(s + 20);
            a1 = MAP4(s + 24); a0 = MAP4(s + 28);
        } else {
            a7 = load32(s);      a6 = load32(s + 4);
            a5 = load32(s + 8);  a4 = load32(s + 12);
            a3 = load32(s + 16); a2 = load32(s + 20);
            a1 = load32(s + 24); a0 = load32(s + 28);
        }

        MERGE(a0, a4, 16, 0x0000ffffUL);  MERGE(a1, a5, 16, 0x0000ffffUL);
        MERGE(a2, a6, 16, 0x0000ffffUL);  MERGE(a3, a7, 16, 0x0000ffffUL);

        MERGE(a0, a2,  8, 0x00ff00ffUL);  MERGE(a1, a3,  8, 0x00ff00ffUL);
        MERGE(a4, a6,  8, 0x00ff00ffUL);  MERGE(a5, a7,  8, 0x00ff00ffUL);

        MERGE(a0, a1,  4, 0x0f0f0f0fUL);  MERGE(a2, a3,  4, 0x0f0f0f0fUL);
        MERGE(a4, a5,  4, 0x0f0f0f0fUL);  MERGE(a6, a7,  4, 0x0f0f0f0fUL);

        MERGE(a0, a4,  2, 0x33333333UL);  MERGE(a1, a5,  2, 0x33333333UL);
        MERGE(a2, a6,  2, 0x33333333UL);  MERGE(a3, a7,  2, 0x33333333UL);

        MERGE(a0, a2,  1, 0x55555555UL);  MERGE(a1, a3,  1, 0x55555555UL);
        MERGE(a4, a6,  1, 0x55555555UL);  MERGE(a5, a7,  1, 0x55555555UL);

        switch (depth) {
        case 8: store32(pl[7] + o, a7);   /* fall through */
        case 7: store32(pl[6] + o, a5);   /* fall through */
        case 6: store32(pl[5] + o, a3);   /* fall through */
        case 5: store32(pl[4] + o, a1);   /* fall through */
        case 4: store32(pl[3] + o, a6);   /* fall through */
        case 3: store32(pl[2] + o, a4);   /* fall through */
        case 2: store32(pl[1] + o, a2);   /* fall through */
        case 1: store32(pl[0] + o, a0);   /* fall through */
        default: break;
        }
    }
    return s;
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
    int y, p, n;
    UBYTE *pl[8];

    if (!c2p_ready) amiga_c2p_init();
    if (depth > 8) depth = 8;

    /* Both interleaved and plain bitmaps advance one row by BytesPerRow, so
       there is nothing to special-case here. */
    for (p = 0; p < depth; p++)
        pl[p] = bm->Planes[p] + desty * rowbytes + (destx >> 3);

    n = w >> 3;     /* plane bytes per row */

    for (y = 0; y < h; y++, src += srcmod) {
        const UBYTE *s = src;
        int head = 0, blocks;

        /* Bytes until plane 0 sits on a longword.  The others usually share
           its alignment; when they do not the writes are still correct,
           just slower. */
        if (depth > 0)
            head = (int)((4 - ((size_t)pl[0] & 3)) & 3);
        if (head > n) head = n;
        blocks = (n - head) >> 2;

        s = c2p_groups(s, pl, 0, head, depth);
        s = c2p_blocks(s, pl, head, blocks, depth);
        c2p_groups(s, pl, head + blocks * 4, n - head - blocks * 4, depth);

        for (p = 0; p < depth; p++)
            pl[p] += rowbytes;
    }
}
