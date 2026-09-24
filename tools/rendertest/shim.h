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
#define VIEW_TOP  0
#define VIEW_BOT  VH
#define ZSCALE    16777216.0
#define RECIP_MAX 4096

extern unsigned char  *walseg[1];
extern unsigned char  *amiga_chunky;
extern K_INT32        *zbuf;
extern unsigned char   shadetab[256];
extern K_INT32        *vrecip;
extern double          cam_fx, cam_fy, cam_ex, cam_ey, cam_ez;
extern double          proj_x, proj_y;
extern int             horizon_row, neardist;

#endif /* RENDERTEST_SHIM_H */
