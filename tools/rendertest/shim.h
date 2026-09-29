/*
 * Just enough of the Amiga renderer's environment to compile its inner loops
 * on the build host.  The functions under test touch only the globals below,
 * so nothing here needs AmigaOS headers and the whole thing builds with a
 * plain cc.
 */
#ifndef RENDERTEST_SHIM_H
#define RENDERTEST_SHIM_H

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef int            K_INT32;
typedef unsigned int   K_UINT32;
typedef short          K_INT16;
typedef unsigned short K_UINT16;

/* Must match src/amiga/amiga_video.h and render_soft.c. */
#define VW        360
#define VH        240
#define VIEW_LEFT  0
#define VIEW_TOP   0
#define VIEW_RIGHT VW
#define VIEW_BOT   VH
#define ZSCALE    16777216.0
#define RECIP_MAX 4096

/* ---- renderer ---------------------------------------------------------- */

extern unsigned char  *walseg[1];
extern unsigned char  *amiga_chunky;
extern K_INT32        *zbuf;
extern unsigned char   shadetab[256];
extern K_INT32        *vrecip;
extern double          cam_fx, cam_fy, cam_ex, cam_ey, cam_ez;
extern double          proj_x, proj_y, proj_cx;
extern int             horizon_row, neardist;

/* ---- ray caster -------------------------------------------------------- */

/* Must match include/lab3d.h.  The tile numbers the ray caster branches on
   are lifted from that header at build time into generated/tiles.inc. */
#define numwalls 448

enum lab3dversion_t {
    KENS_LABYRINTH_1_0,
    KENS_LABYRINTH_1_1,
    KENS_LABYRINTH_2_0,
    KENS_LABYRINTH_2_1,
    WALKEN
};
extern enum lab3dversion_t lab3dversion;

extern K_INT16       board[64][64];
extern char          bmpkind[numwalls + 1];
extern unsigned char tempbuf[4096];
extern int           walltol;
extern K_INT16       waterstat, animate2;

/* graphx.c keeps these file-local; the harness has to own them so both
   versions of the ray caster can be given a clean slate and compared. */
extern K_INT16  wallfound[64][64][4];
extern K_INT16  wallx[16384], wally[16384];
extern char     wallside[16384];
extern K_UINT16 walnum[16384];
extern K_INT32  wallsfound, rayscast;
extern K_INT16  mapfound, gameoverfound;
extern K_INT32  hitpointx, hitpointy;   /* 16.16 cell coordinates */

#include "generated/tiles.inc"

#endif /* RENDERTEST_SHIM_H */
