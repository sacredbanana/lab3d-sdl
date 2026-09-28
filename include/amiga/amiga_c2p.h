#ifndef LAB3D_AMIGA_C2P_H
#define LAB3D_AMIGA_C2P_H

/*
 * Chunky to planar conversion (src/amiga/amiga_c2p.c).  Plain C with no
 * AmigaOS types, so the host test in tools/c2ptest can include it too.
 */

struct BitMap;

void amiga_c2p_init(void);
void amiga_c2p_setmap(const unsigned char *pens);
void amiga_c2p(const unsigned char *src, int srcmod,
               struct BitMap *bm, int destx, int desty,
               int w, int h, int depth);

/*
 * Blitter assist, built into the plain 68020 version only (AMIGA_BLITTER_C2P);
 * elsewhere these are stubs that report AMIGA_BLITTER_UNUSED.
 *
 * amiga_c2p_blit_area() gives the conversion a chip RAM staging area for the
 * screen rectangle the frame is drawn into (w = 0 takes it away), and
 * returns one of the statuses below.  amiga_c2p_sync() waits for every
 * queued blit and gives the blitter back; call it before flipping, clearing
 * or freeing a screen buffer.  amiga_c2p_frame_done() feeds the Auto
 * setting's timing, once a frame, after the sync.
 */
#define AMIGA_BLITTER_UNUSED  0     /* no area: RTG, or not this build   */
#define AMIGA_BLITTER_READY   1
#define AMIGA_BLITTER_NOTCPU  2     /* 68040/060: the CPU is faster      */
#define AMIGA_BLITTER_NOCHIP  3     /* the staging area did not fit      */

int  amiga_c2p_blit_area(int x, int y, int w, int h);
void amiga_c2p_sync(void);
void amiga_c2p_frame_done(unsigned long ms);

/* Setting "blitter" in settings.ini.  The values are visible to every build
   so the FPU ones can carry the 020 build's choice through a save. */
#define AMIGA_BLITTER_OFF_VALUE   0
#define AMIGA_BLITTER_AUTO_VALUE  1     /* time both ways, keep the faster */
#define AMIGA_BLITTER_ON_VALUE    2

#ifdef AMIGA_BLITTER_C2P
/* The setup menu's view of the setting. */
extern int amiga_cfg_blitter;

const char *amiga_c2p_blitter_status(void);
void amiga_c2p_blitter_changed(void);
#endif

#endif /* LAB3D_AMIGA_C2P_H */
