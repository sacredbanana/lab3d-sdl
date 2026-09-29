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
 * The plain 68020 build (AMIGA_BLITTER_C2P) can also hand the last of the
 * five rounds to the blitter; see "Blitter assist" below.
 *
 * tools/c2ptest checks all of this against the plain definition of the
 * planar layout, bit for bit, for every depth and alignment.
 */

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include "amiga/amiga_c2p.h"

#ifdef PLATFORM_AMIGA
#include <exec/types.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <graphics/gfx.h>
#include <hardware/custom.h>
#include <hardware/dmabits.h>
#include <proto/exec.h>
#include <proto/graphics.h>

/* The 68020 and up take unaligned longword accesses, and the Amiga is big
   endian, so the first chunky pixel lands in the top byte for free. */
#define load16(p)     (*(const UWORD *)(p))
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
static ULONG load16(const UBYTE *p) {
    return ((ULONG)p[0] << 8) | p[1];
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

/* Four pixels through the pen map, packed big endian into a longword. */
#define MAP4(p) (((ULONG)map[(p)[0]] << 24) | ((ULONG)map[(p)[1]] << 16) | \
                 ((ULONG)map[(p)[2]] << 8)  |  (ULONG)map[(p)[3]])

/*
 * The 32 pixel transpose, in the pieces the CPU-only and blitter assisted
 * paths share.
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
/*
 * Load, and do round 1 on the way.  Round 1 only trades 16 bit halves
 * between longwords, so without a pen map it is the same as loading the
 * pixels a word at a time straight into the halves they end up in: two word
 * loads and a swap each, where the long loads and the merges cost about
 * four times as much.
 */
#ifdef __m68k__
/* Spelled out, because gcc builds it with seven instructions rather than
   these three, which is slower than the merges it saves. */
static __inline__ ULONG W2(const UBYTE *hi, const UBYTE *lo) {
    ULONG r;
    __asm__("move.w %1,%0\n\tswap %0\n\tmove.w %2,%0"
            : "=&d" (r)
            : "m" (*(const UWORD *)hi), "m" (*(const UWORD *)lo));
    return r;
}
#else
#define W2(hi, lo) ((load16(hi) << 16) | load16(lo))
#endif
#define C2P_LOAD_ROUND_1(s) do {                                    \
        if (map) {                                                  \
            a7 = MAP4(s);        a6 = MAP4((s) + 4);                \
            a5 = MAP4((s) + 8);  a4 = MAP4((s) + 12);               \
            a3 = MAP4((s) + 16); a2 = MAP4((s) + 20);               \
            a1 = MAP4((s) + 24); a0 = MAP4((s) + 28);               \
            MERGE(a0, a4, 16, 0x0000ffffUL);                        \
            MERGE(a1, a5, 16, 0x0000ffffUL);                        \
            MERGE(a2, a6, 16, 0x0000ffffUL);                        \
            MERGE(a3, a7, 16, 0x0000ffffUL);                        \
        } else {                                                    \
            a0 = W2((s) + 14, (s) + 30); a4 = W2((s) + 12, (s) + 28); \
            a1 = W2((s) + 10, (s) + 26); a5 = W2((s) + 8,  (s) + 24); \
            a2 = W2((s) + 6,  (s) + 22); a6 = W2((s) + 4,  (s) + 20); \
            a3 = W2((s) + 2,  (s) + 18); a7 = W2((s),      (s) + 16); \
        }                                                           \
    } while (0)

#define C2P_ROUNDS_2_TO_4 do {                                          \
        MERGE(a0, a2,  8, 0x00ff00ffUL);  MERGE(a1, a3,  8, 0x00ff00ffUL); \
        MERGE(a4, a6,  8, 0x00ff00ffUL);  MERGE(a5, a7,  8, 0x00ff00ffUL); \
        MERGE(a0, a1,  4, 0x0f0f0f0fUL);  MERGE(a2, a3,  4, 0x0f0f0f0fUL); \
        MERGE(a4, a5,  4, 0x0f0f0f0fUL);  MERGE(a6, a7,  4, 0x0f0f0f0fUL); \
        MERGE(a0, a4,  2, 0x33333333UL);  MERGE(a1, a5,  2, 0x33333333UL); \
        MERGE(a2, a6,  2, 0x33333333UL);  MERGE(a3, a7,  2, 0x33333333UL); \
    } while (0)

#define C2P_ROUND_5 do {                                                \
        MERGE(a0, a2,  1, 0x55555555UL);  MERGE(a1, a3,  1, 0x55555555UL); \
        MERGE(a4, a6,  1, 0x55555555UL);  MERGE(a5, a7,  1, 0x55555555UL); \
    } while (0)

#ifdef __m68k__
/*
 * The 32 pixel block loop in 68020 assembler, for the common case: no pen
 * map, all eight planes, and planes a constant distance `delta` apart - true
 * of every bitmap graphics.library hands out in one piece, and of the
 * blitter's staging area.  With `full` it does all five rounds and writes
 * bitplanes; without, rounds 1-4 and the eight pre-planes the blitter
 * finishes (see "Blitter assist").
 *
 * gcc's version of the same loop spends a good part of its time moving
 * values to and from the stack: eight longwords, a temporary and eight plane
 * pointers do not fit in the registers.  Here the longwords stay in d0-d7;
 * each round borrows d7 as the temporary by parking one value in a6 and
 * swapping it back with exg when its pair comes up; and the planes are
 * reached from four base registers and one index register holding delta,
 * stepping as they are stored.  About two thirds the cycles of the C.
 *
 * Which value is in which register after each round (V0..V7 are a0..a7 of
 * the C version):
 *
 *   load   d0-d7 = V0-V7
 *   2      d0=V7 d1=V1 d2=V2 d3=V3 d4=V4 d5=V5 d6=V6 a6=V0
 *   3      d0=V7 d1=V1 d2=V0 d3=V3 d4=V4 d5=V5 d6=V6 a6=V2
 *   4      d0=V7 d1=V2 d2=V0 d3=V3 d4=V4 d5=V5 d6=V6 a6=V1
 *   5      d0=V7 d1=V2 d2=V1 d3=V3 d4=V4 d5=V5 d6=V6 a6=V0
 */
void c2p32_asm(const UBYTE *src, UBYTE *dst, long delta, long n, long full);

__asm__(
"       .text\n"
"       .even\n"
"       .macro  C2PM a,b,s,m\n"                /* MERGE(a,b,s,m), t = d7 */
"       move.l  \\a,d7\n"
"       lsr.l   #\\s,d7\n"
"       eor.l   \\b,d7\n"
"       and.l   #\\m,d7\n"
"       eor.l   d7,\\b\n"
"       lsl.l   #\\s,d7\n"
"       eor.l   d7,\\a\n"
"       .endm\n"
"       .macro  C2PW off1,off2,r\n"            /* r = word(off1):word(off2) */
"       move.w  (\\off1,a0),\\r\n"
"       swap    \\r\n"
"       move.w  (\\off2,a0),\\r\n"
"       .endm\n"
"       .macro  C2P14\n"                       /* load, round 1, rounds 2-4 */
"       C2PW    14,30,d0\n"
"       C2PW    10,26,d1\n"
"       C2PW    6,22,d2\n"
"       C2PW    2,18,d3\n"
"       C2PW    12,28,d4\n"
"       C2PW    8,24,d5\n"
"       C2PW    4,20,d6\n"
"       C2PW    0,16,d7\n"
"       lea     (32,a0),a0\n"
"       move.l  d7,a6\n"                       /* round 2: 8 bits */
"       C2PM    d0,d2,8,0x00ff00ff\n"
"       C2PM    d1,d3,8,0x00ff00ff\n"
"       C2PM    d4,d6,8,0x00ff00ff\n"
"       exg     d0,a6\n"
"       C2PM    d5,d0,8,0x00ff00ff\n"
"       C2PM    d2,d3,4,0x0f0f0f0f\n"          /* round 3: 4 bits */
"       C2PM    d4,d5,4,0x0f0f0f0f\n"
"       C2PM    d6,d0,4,0x0f0f0f0f\n"
"       exg     d2,a6\n"
"       C2PM    d2,d1,4,0x0f0f0f0f\n"
"       C2PM    d2,d4,2,0x33333333\n"          /* round 4: 2 bits */
"       C2PM    d1,d5,2,0x33333333\n"
"       C2PM    d3,d0,2,0x33333333\n"
"       exg     d1,a6\n"
"       C2PM    d1,d6,2,0x33333333\n"
"       .endm\n"
"       .globl  _c2p32_asm\n"
"_c2p32_asm:\n"
"       movem.l d2-d7/a2-a6,-(sp)\n"
"       move.l  (48,sp),a0\n"                  /* src */
"       move.l  (52,sp),a1\n"                  /* plane 0 */
"       move.l  (56,sp),a2\n"                  /* delta */
"       move.l  (60,sp),d0\n"                  /* blocks */
"       move.l  (64,sp),d1\n"                  /* full */
"       lea     (0,a1,a2.l*2),a3\n"            /* plane 2 */
"       lea     (0,a1,a2.l*4),a4\n"            /* plane 4 */
"       lea     (0,a3,a2.l*4),a5\n"            /* plane 6 */
"       move.l  d0,-(sp)\n"
"       tst.l   d1\n"
"       jeq     .Lc2p_stage\n"
".Lc2p_full:\n"
"       C2P14\n"
"       C2PM    d2,d1,1,0x55555555\n"          /* round 5: 1 bit */
"       C2PM    d4,d6,1,0x55555555\n"
"       C2PM    d5,d0,1,0x55555555\n"
"       exg     d2,a6\n"
"       C2PM    d2,d3,1,0x55555555\n"
"       move.l  d1,(a1,a2.l)\n"                /* planes: V0 V2 V4 V6 V1 V3 V5 V7 */
"       move.l  a6,(a1)+\n"
"       move.l  d6,(a3,a2.l)\n"
"       move.l  d4,(a3)+\n"
"       move.l  d3,(a4,a2.l)\n"
"       move.l  d2,(a4)+\n"
"       move.l  d0,(a5,a2.l)\n"
"       move.l  d5,(a5)+\n"
"       subq.l  #1,(sp)\n"
"       jne     .Lc2p_full\n"
"       jra     .Lc2p_done\n"
".Lc2p_stage:\n"
"       C2P14\n"
"       move.l  a6,(a1,a2.l)\n"                /* pre-planes: V0 .. V7 */
"       move.l  d2,(a1)+\n"
"       move.l  d3,(a3,a2.l)\n"
"       move.l  d1,(a3)+\n"
"       move.l  d5,(a4,a2.l)\n"
"       move.l  d4,(a4)+\n"
"       move.l  d0,(a5,a2.l)\n"
"       move.l  d6,(a5)+\n"
"       subq.l  #1,(sp)\n"
"       jne     .Lc2p_stage\n"
".Lc2p_done:\n"
"       addq.l  #4,sp\n"
"       movem.l (sp)+,d2-d7/a2-a6\n"
"       rts\n"
);

/* The distance between consecutive planes if it is the same for all eight,
   else 0. */
static long plane_delta(UBYTE *const *pl) {
    long d = pl[1] - pl[0];
    int p;

    if (!d) return 0;
    for (p = 2; p < 8; p++)
        if (pl[p] - pl[p - 1] != d)
            return 0;
    return d;
}
#endif

/* Convert n blocks of 32 pixels into plane longwords pl[p][o...]. */
static const UBYTE *c2p_blocks(const UBYTE *s, UBYTE *const *pl, int o,
                               int n, int depth) {
    const UBYTE *map = c2p_penmap;

#ifdef __m68k__
    if (n > 0 && !map && depth == 8) {
        long delta = plane_delta(pl);
        if (delta) {
            c2p32_asm(s, pl[0] + o, delta, n, 1);
            return s + 32 * n;
        }
    }
#endif

    for (; n > 0; n--, o += 4, s += 32) {
        ULONG a0, a1, a2, a3, a4, a5, a6, a7;

        C2P_LOAD_ROUND_1(s);
        C2P_ROUNDS_2_TO_4;
        C2P_ROUND_5;

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

/* One row the CPU way: head bytes up to a longword, blocks, tail bytes. */
static void c2p_row_cpu(const UBYTE *s, UBYTE *const *pl, int n, int depth) {
    int head = 0, blocks;

    /* Bytes until plane 0 sits on a longword.  The others usually share its
       alignment; when they do not the writes are still correct, just
       slower. */
    if (depth > 0)
        head = (int)((4 - ((size_t)pl[0] & 3)) & 3);
    if (head > n) head = n;
    blocks = (n - head) >> 2;

    s = c2p_groups(s, pl, 0, head, depth);
    s = c2p_blocks(s, pl, head, blocks, depth);
    c2p_groups(s, pl, head + blocks * 4, n - head - blocks * 4, depth);
}

/* ------------------------------------------------------------ blitter assist */

#ifdef AMIGA_BLITTER_C2P

/*
 * On a plain 68020 the CPU is slow enough that sharing the work pays.  The
 * CPU runs rounds 1-4 and stores the eight half-finished longwords into a
 * staging area in chip RAM, one "pre-plane" per longword; the blitter then
 * does round 5 from there straight into the screen, one blit per bitplane.
 * Round 5 pairs pre-planes (0,2), (1,3), (4,6) and (5,7), and for a pair
 * (a,b) with m = 0x5555:
 *
 *   b' = (b & ~m) | ((a >> 1) & m)     ascending blit, A = a >> 1
 *   a' = (a &  m) | ((b << 1) & ~m)    descending blit, A = b << 1
 *
 * with B the pre-plane itself, C a constant mask and the same minterm for
 * both.  A shift carries a bit in from the neighbouring word, but the mask
 * always takes that bit from B, so where each blit starts does not matter.
 * Every 16 pixel word of a pre-plane depends only on its own 16 pixels, so
 * the staging area can be laid out like the screen, and overlapping dirty
 * rectangles simply write the same words twice.
 *
 * Rows are done in bands; each band's blits are queued as soon as its rows
 * are staged, and the queue is fed between rows, so the blitter works on one
 * band while the CPU does the next.  Everything is finished and the blitter
 * given back by amiga_c2p_sync(), which the display calls before it flips.
 *
 * Whether this is a win depends on the machine: the blitter runs at the same
 * speed on everything, the CPU does not.  Under a JIT, or with a fast 020
 * core, the CPU alone is quicker.  So "Auto" times both ways, frame about,
 * and keeps whichever converts a pixel faster.
 */

int amiga_cfg_blitter = AMIGA_BLITTER_AUTO_VALUE;

#define BAND_ROWS     16
#define BLT_MAXWORDS  64    /* widest blit BLTSIZE can describe */
#define BLT_MINBLOCKS 2     /* narrower than this is not worth a blit */
#define BQ_SIZE       64

typedef struct {
    UWORD con0, con1, cdat, amod, bmod, dmod, size;
    const UBYTE *apt, *bpt;
    UBYTE *dpt;
} c2p_blit;

/* Hardware access.  On the host these are the test harness's blitter model;
   there the blitter finishes every blit the moment it starts. */
#ifdef PLATFORM_AMIGA
static volatile struct Custom *const cust = (volatile struct Custom *)0xdff000;

static void hw_start(const c2p_blit *b) {
    cust->bltcon0 = b->con0;
    cust->bltcon1 = b->con1;
    cust->bltafwm = 0xffff;
    cust->bltalwm = 0xffff;
    cust->bltcdat = b->cdat;
    cust->bltapt  = (APTR)b->apt;
    cust->bltbpt  = (APTR)b->bpt;
    cust->bltdpt  = (APTR)b->dpt;
    cust->bltamod = b->amod;
    cust->bltbmod = b->bmod;
    cust->bltdmod = b->dmod;
    cust->bltsize = b->size;            /* starts it */
}

/* Read twice: early Agnus revisions do not raise BBUSY on the first read
   after a blit starts. */
static int hw_busy(void) {
    (void)cust->dmaconr;
    return (cust->dmaconr & DMAF_BLTDONE) != 0;
}

static void  hw_wait(void)   { WaitBlit(); }
static void  hw_own(void)    { OwnBlitter(); WaitBlit(); }
static void  hw_disown(void) { WaitBlit(); DisownBlitter(); }
static void *hw_chipalloc(ULONG n) { return AllocMem(n, MEMF_CHIP); }
static void  hw_chipfree(void *p, ULONG n) { FreeMem(p, n); }
#else
void  c2p_host_start(const c2p_blit *b);
int   c2p_host_busy(void);
void *c2p_host_chipalloc(ULONG n);
void  c2p_host_chipfree(void *p, ULONG n);
#define hw_start       c2p_host_start
#define hw_busy        c2p_host_busy
#define hw_wait()      ((void)0)
#define hw_own()       ((void)0)
#define hw_disown()    ((void)0)
#define hw_chipalloc   c2p_host_chipalloc
#define hw_chipfree    c2p_host_chipfree
#endif

/* The staging area: eight pre-planes covering screen pixels [st_x0, st_x0 +
   st_bpr*8) by rows [st_y0, st_y0 + st_h), st_bpr bytes a row. */
static UBYTE *st_buf;
static ULONG  st_size, st_plane;
static int    st_x0, st_y0, st_bpr, st_h;
static int    st_status = AMIGA_BLITTER_UNUSED;

static c2p_blit bq[BQ_SIZE];
static int      bq_head, bq_count, blt_owned;

/* Which pre-plane each bitplane comes from, and its partner in round 5. */
static const UBYTE pre_self[8]    = { 0, 2, 4, 6, 1, 3, 5, 7 };
static const UBYTE pre_partner[8] = { 2, 0, 6, 4, 3, 1, 7, 5 };

/* Auto: time per pixel both ways, then settle. */
#define TUNE_PIXELS 1500000UL
static int           use_blitter;       /* for the frame in progress */
static int           tuning;
static unsigned long tune_ms[2], tune_px[2], frame_px;

static void bq_start_next(void) {
    hw_start(&bq[bq_head]);
    bq_head = (bq_head + 1) % BQ_SIZE;
    bq_count--;
}

/* Start the next queued blit if the blitter has finished the last one. */
static void bq_pump(void) {
    if (bq_count && !hw_busy())
        bq_start_next();
}

static void bq_push(const c2p_blit *b) {
    while (bq_count == BQ_SIZE) {
        hw_wait();
        bq_start_next();
    }
    bq[(bq_head + bq_count) % BQ_SIZE] = *b;
    bq_count++;
    bq_pump();
}

void amiga_c2p_sync(void) {
    while (bq_count) {
        hw_wait();
        bq_start_next();
    }
    if (blt_owned) {
        hw_disown();
        blt_owned = 0;
    }
}

static void tune_reset(void) {
    tuning = (amiga_cfg_blitter == AMIGA_BLITTER_AUTO_VALUE);
    tune_ms[0] = tune_ms[1] = tune_px[0] = tune_px[1] = frame_px = 0;
    use_blitter = st_buf && amiga_cfg_blitter != AMIGA_BLITTER_OFF_VALUE;
}

/*
 * Give the blitter a staging area for the part of the screen the frame is
 * drawn into, or take it away with w = 0.  Returns the resulting status, one
 * of AMIGA_BLITTER_*.
 */
int amiga_c2p_blit_area(int x, int y, int w, int h) {
    int x0, x1;

    amiga_c2p_sync();
    if (st_buf) {
        hw_chipfree(st_buf, st_size);
        st_buf = NULL;
    }
    st_status = AMIGA_BLITTER_UNUSED;

    if (w > 0 && h > 0) {
#ifdef PLATFORM_AMIGA
        /* The 040 and 060 builds never get here; the 020 one running on an
           040 or 060 would only slow them down. */
        if (SysBase->AttnFlags & (AFF_68040 | AFF_68060)) {
            st_status = AMIGA_BLITTER_NOTCPU;
            tune_reset();
            return st_status;
        }
#endif
        /* Blits write whole words, and the CPU stores longwords. */
        x0 = x & ~15;
        x1 = (x + w + 31) & ~31;
        st_x0    = x0;
        st_y0    = y;
        st_bpr   = (x1 - x0) >> 3;
        st_h     = h;
        st_plane = (ULONG)st_bpr * h;
        st_size  = st_plane * 8;
        st_buf   = hw_chipalloc(st_size);
        st_status = st_buf ? AMIGA_BLITTER_READY : AMIGA_BLITTER_NOCHIP;
#ifdef PLATFORM_AMIGA
        fprintf(stderr, st_buf ? "Blitter assist: %lu bytes of chip RAM staging.\n"
                               : "Blitter assist off: no %lu bytes of chip RAM.\n",
                (unsigned long)st_size);
#endif
    }
    tune_reset();
    return st_status;
}

/* Called once a frame by the display, after amiga_c2p_sync(), with how long
   the frame's conversion took. */
void amiga_c2p_frame_done(unsigned long ms) {
    if (tuning && st_buf && frame_px) {
        tune_ms[use_blitter] += ms;
        tune_px[use_blitter] += frame_px;

        if (tune_px[0] >= TUNE_PIXELS && tune_px[1] >= TUNE_PIXELS) {
            double cpu = (double)tune_ms[0] / tune_px[0];
            double blt = (double)tune_ms[1] / tune_px[1];

            tuning = 0;
            use_blitter = blt < cpu;
            fprintf(stderr, "Chunky to planar: %.1f ms per 100k pixels by CPU, "
                            "%.1f with the blitter; using the %s.\n",
                    cpu * 100000.0, blt * 100000.0,
                    use_blitter ? "blitter" : "CPU alone");
        } else {
            use_blitter ^= 1;       /* alternate, so both see similar frames */
        }
    }
    frame_px = 0;
}

const char *amiga_c2p_blitter_status(void) {
    if (amiga_cfg_blitter == AMIGA_BLITTER_OFF_VALUE)
        return "Off";
    switch (st_status) {
    case AMIGA_BLITTER_NOTCPU: return "Off (040/060)";
    case AMIGA_BLITTER_NOCHIP: return "Off (chip RAM)";
    case AMIGA_BLITTER_READY:  break;
    default:                   return "Off (RTG)";
    }
    if (amiga_cfg_blitter != AMIGA_BLITTER_AUTO_VALUE)
        return "On";
    if (tuning)
        return "Auto (testing)";
    return use_blitter ? "Auto (in use)" : "Auto (CPU faster)";
}

void amiga_c2p_blitter_changed(void) {
    amiga_c2p_sync();
    tune_reset();
}

/* Round 1-4 half of c2p_blocks(), into pre-planes tp[j][o...]. */
static const UBYTE *c2p_stage(const UBYTE *s, UBYTE *const *tp, int o, int n) {
    const UBYTE *map = c2p_penmap;

#ifdef __m68k__
    if (n > 0 && !map) {                /* the pre-planes are st_plane apart */
        c2p32_asm(s, tp[0] + o, (long)st_plane, n, 0);
        return s + 32 * n;
    }
#endif

    for (; n > 0; n--, o += 4, s += 32) {
        ULONG a0, a1, a2, a3, a4, a5, a6, a7;

        C2P_LOAD_ROUND_1(s);
        C2P_ROUNDS_2_TO_4;

        store32(tp[0] + o, a0); store32(tp[1] + o, a1);
        store32(tp[2] + o, a2); store32(tp[3] + o, a3);
        store32(tp[4] + o, a4); store32(tp[5] + o, a5);
        store32(tp[6] + o, a6); store32(tp[7] + o, a7);
    }
    return s;
}

/* Queue round 5 for screen rows [y, y + rows), pixels [bx0, bx0 + 16*words). */
static void queue_band(struct BitMap *bm, int bx0, int y, int rows,
                       int words, int depth) {
    int bpr = bm->BytesPerRow;
    int c, k;

    for (c = 0; c < words; c += BLT_MAXWORDS) {
        int cw = words - c < BLT_MAXWORDS ? words - c : BLT_MAXWORDS;
        ULONG toff = (ULONG)(y - st_y0) * st_bpr + ((bx0 - st_x0) >> 3) + c * 2;
        ULONG soff = (ULONG)y * bpr + (bx0 >> 3) + c * 2;
        /* Where a descending blit starts: the last word of the last row. */
        ULONG tlast = (ULONG)(rows - 1) * st_bpr + cw * 2 - 2;
        ULONG slast = (ULONG)(rows - 1) * bpr + cw * 2 - 2;
        c2p_blit b;

        b.con0 = (1 << 12) | 0x0d00 | 0xe4;   /* A>>1 or <<1; A,B,D; AC + B~C */
        b.amod = b.bmod = (UWORD)(st_bpr - cw * 2);
        b.dmod = (UWORD)(bpr - cw * 2);
        b.size = (UWORD)((rows << 6) | (cw & 63));

        for (k = 0; k < depth; k++) {
            const UBYTE *self = st_buf + pre_self[k] * st_plane + toff;
            const UBYTE *part = st_buf + pre_partner[k] * st_plane + toff;
            UBYTE *dst = bm->Planes[k] + soff;

            if (k & 1) {            /* b' */
                b.con1 = 0;
                b.cdat = 0x5555;
                b.apt = part;
                b.bpt = self;
                b.dpt = dst;
            } else {                /* a' */
                b.con1 = 0x0002;    /* descending */
                b.cdat = 0xaaaa;
                b.apt = part + tlast;
                b.bpt = self + tlast;
                b.dpt = dst + slast;
            }
            bq_push(&b);
        }
    }
}

/*
 * The blitter assisted conversion.  The whole 32 pixel blocks from the first
 * word boundary on go through the staging area; the up to 8 pixels before
 * that and up to 24 after are converted by the CPU as usual.  Returns 0,
 * having done nothing, if this rectangle is not worth it or not covered.
 */
static int c2p_blitted(const UBYTE *src, int srcmod, struct BitMap *bm,
                       int destx, int desty, int w, int h, int depth) {
    int bx0 = (destx + 15) & ~15, x1 = destx + w;
    int nblk, lead, trail, y, r, p, rows;
    UBYTE *pl[8], *tp[8];

    if (depth < 1 || bx0 >= x1) return 0;
    nblk = (x1 - bx0) >> 5;
    if (nblk < BLT_MINBLOCKS) return 0;
    if (bx0 < st_x0 || bx0 + nblk * 32 > st_x0 + st_bpr * 8 ||
        desty < st_y0 || desty + h > st_y0 + st_h)
        return 0;

    lead  = (bx0 - destx) >> 3;
    trail = (x1 - bx0 - nblk * 32) >> 3;

    for (p = 0; p < depth; p++)
        pl[p] = bm->Planes[p] + desty * bm->BytesPerRow + (destx >> 3);
    for (p = 0; p < 8; p++)
        tp[p] = st_buf + p * st_plane + (ULONG)(desty - st_y0) * st_bpr +
                ((bx0 - st_x0) >> 3);

    if (!blt_owned) {
        hw_own();
        blt_owned = 1;
    }

    for (y = 0; y < h; y += rows) {
        rows = h - y < BAND_ROWS ? h - y : BAND_ROWS;

        for (r = 0; r < rows; r++, src += srcmod) {
            const UBYTE *s = src;

            s = c2p_groups(s, pl, 0, lead, depth);
            s = c2p_stage(s, tp, 0, nblk);
            c2p_groups(s, pl, lead + nblk * 4, trail, depth);

            for (p = 0; p < depth; p++) pl[p] += bm->BytesPerRow;
            for (p = 0; p < 8; p++)     tp[p] += st_bpr;
            bq_pump();
        }
        queue_band(bm, bx0, desty + y, rows, nblk * 2, depth);
    }
    return 1;
}

#else  /* !AMIGA_BLITTER_C2P */

int  amiga_c2p_blit_area(int x, int y, int w, int h) {
    (void)x; (void)y; (void)w; (void)h;
    return AMIGA_BLITTER_UNUSED;
}
void amiga_c2p_sync(void) { }
void amiga_c2p_frame_done(unsigned long ms) { (void)ms; }

#endif /* AMIGA_BLITTER_C2P */

/*
 * A row that is one colour throughout.  Three probe pixels reject a typical
 * wall row in a couple of compares (ceiling at the edge, wall in the
 * middle); a full scan then confirms the ceiling and floor bands, which
 * become a memset per bitplane instead of a transpose.
 */
static int row_looks_solid(const UBYTE *s, int w, UBYTE *col) {
    UBYTE c = s[0];
    if (s[w - 1] != c || s[w >> 1] != c)
        return 0;
    *col = c;
    return 1;
}

static int row_is_solid(const UBYTE *s, int w, UBYTE c) {
    int i;

    if (w >= 4 && !((size_t)s & 3)) {
        ULONG v = c;
        const ULONG *p;
        int n4;

        v |= v << 8;
        v |= v << 16;
        p = (const ULONG *)s;
        n4 = w >> 2;
        for (i = 0; i < n4; i++)
            if (p[i] != v)
                return 0;
        for (i = n4 << 2; i < w; i++)
            if (s[i] != c)
                return 0;
        return 1;
    }
    for (i = 1; i < w; i++)
        if (s[i] != c)
            return 0;
    return 1;
}

/* Write `h` solid rows of colour `c` into the planes at (destx, desty). */
static void c2p_fill_rows(struct BitMap *bm, int destx, int desty,
                          int w, int h, int depth, UBYTE c) {
    int bpr = bm->BytesPerRow;
    int nbytes = w >> 3;
    int o0 = desty * bpr + (destx >> 3);
    int p, y;
    UBYTE fill[8];

    for (p = 0; p < depth; p++)
        fill[p] = (UBYTE)((c & (1 << p)) ? 0xff : 0x00);

    for (p = 0; p < depth; p++) {
        UBYTE *pl = bm->Planes[p] + o0;
        if (bpr == nbytes) {
            memset(pl, fill[p], (size_t)nbytes * h);
        } else {
            for (y = 0; y < h; y++, pl += bpr)
                memset(pl, fill[p], nbytes);
        }
    }
}

/* The existing CPU / blitter conversion, for a run of mixed rows. */
static void c2p_mixed(const UBYTE *src, int srcmod,
                      struct BitMap *bm, int destx, int desty,
                      int w, int h, int depth) {
    int rowbytes = bm->BytesPerRow;
    int y, p;
    UBYTE *pl[8];

#ifdef AMIGA_BLITTER_C2P
    if (use_blitter && st_buf &&
        c2p_blitted(src, srcmod, bm, destx, desty, w, h, depth))
        return;
#endif

    for (p = 0; p < depth; p++)
        pl[p] = bm->Planes[p] + desty * rowbytes + (destx >> 3);

    for (y = 0; y < h; y++, src += srcmod) {
        c2p_row_cpu(src, pl, w >> 3, depth);
        for (p = 0; p < depth; p++)
            pl[p] += rowbytes;
    }
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
    int y;

    if (!c2p_ready) amiga_c2p_init();
    if (depth > 8) depth = 8;
    if (w < 8 || h < 1) return;

#ifdef AMIGA_BLITTER_C2P
    frame_px += (unsigned long)w * h;
#endif

    for (y = 0; y < h; ) {
        UBYTE col;

        if (row_looks_solid(src + (size_t)y * srcmod, w, &col) &&
            row_is_solid(src + (size_t)y * srcmod, w, col)) {
            int run = 1;
            while (y + run < h) {
                const UBYTE *r = src + (size_t)(y + run) * srcmod;
                UBYTE c2;
                if (!row_looks_solid(r, w, &c2) || c2 != col ||
                    !row_is_solid(r, w, col))
                    break;
                run++;
            }
            if (c2p_penmap)
                col = c2p_penmap[col];
            c2p_fill_rows(bm, destx, desty + y, w, run, depth, col);
            y += run;
        } else {
            int run = 1;
            while (y + run < h) {
                const UBYTE *r = src + (size_t)(y + run) * srcmod;
                UBYTE c2;
                if (row_looks_solid(r, w, &c2) && row_is_solid(r, w, c2))
                    break;
                run++;
            }
            c2p_mixed(src + (size_t)y * srcmod, srcmod,
                      bm, destx, desty + y, w, run, depth);
            y += run;
        }
    }
}
