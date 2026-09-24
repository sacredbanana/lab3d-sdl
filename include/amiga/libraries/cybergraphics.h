#ifndef LIBRARIES_CYBERGRAPHICS_H
#define LIBRARIES_CYBERGRAPHICS_H
/*
 * Minimal cybergraphics.library definitions for LAB3D/Amiga.
 *
 * bebbo's m68k-amigaos toolchain ships the stock Commodore NDK, which has no
 * RTG headers at all, so we carry the handful of constants and the one
 * structure we need.  Both CyberGraphX and Picasso96 export
 * cybergraphics.library, so this single path covers every RTG board.
 *
 * Values follow cybergraphics.library V41.
 */

#ifndef EXEC_TYPES_H
#include <exec/types.h>
#endif
#ifndef EXEC_NODES_H
#include <exec/nodes.h>
#endif
#ifndef UTILITY_TAGITEM_H
#include <utility/tagitem.h>
#endif
#ifndef GRAPHICS_DISPLAYINFO_H
#include <graphics/displayinfo.h>
#endif

#define CYBERGFXNAME "cybergraphics.library"

/* Node returned in the list from AllocCModeListTagList(). */
struct CyberModeNode {
    struct Node     Node;
    char            ModeText[DISPLAYNAMELEN];
    ULONG           DisplayID;
    UWORD           Width;
    UWORD           Height;
    UWORD           Depth;
    struct TagItem *DisplayTagList;
};

/* GetCyberMapAttr() attributes. */
#define CYBRMATTR_XMOD        0x80000001UL   /* bytes per row       */
#define CYBRMATTR_BPPIX       0x80000002UL   /* bytes per pixel     */
#define CYBRMATTR_DISPADR     0x80000003UL   /* private             */
#define CYBRMATTR_PIXFMT      0x80000004UL   /* PIXFMT_#?           */
#define CYBRMATTR_WIDTH       0x80000005UL
#define CYBRMATTR_HEIGHT      0x80000006UL
#define CYBRMATTR_DEPTH       0x80000007UL
#define CYBRMATTR_ISCYBERGFX  0x80000008UL   /* -1 when RTG         */
#define CYBRMATTR_ISLINEARMEM 0x80000009UL

/* GetCyberIDAttr() attributes. */
#define CYBRIDATTR_PIXFMT     0x80000001UL
#define CYBRIDATTR_WIDTH      0x80000002UL
#define CYBRIDATTR_HEIGHT     0x80000003UL
#define CYBRIDATTR_DEPTH      0x80000004UL
#define CYBRIDATTR_BPPIX      0x80000005UL

/* BestCModeIDTagList() tags. */
#define CYBRBIDTG_TB            (TAG_USER + 0x50000)
#define CYBRBIDTG_Depth         (CYBRBIDTG_TB + 0)
#define CYBRBIDTG_NominalWidth  (CYBRBIDTG_TB + 1)
#define CYBRBIDTG_NominalHeight (CYBRBIDTG_TB + 2)
#define CYBRBIDTG_MonitorID     (CYBRBIDTG_TB + 3)
#define CYBRBIDTG_BoardName     (CYBRBIDTG_TB + 5)

/* AllocCModeListTagList() shares the BestCModeIDTagList() filter tags. */

/* Pixel formats. */
#define PIXFMT_LUT8     0UL
#define PIXFMT_RGB15    1UL
#define PIXFMT_BGR15    2UL
#define PIXFMT_RGB15PC  3UL
#define PIXFMT_BGR15PC  4UL
#define PIXFMT_RGB16    5UL
#define PIXFMT_BGR16    6UL
#define PIXFMT_RGB16PC  7UL
#define PIXFMT_BGR16PC  8UL
#define PIXFMT_RGB24    9UL
#define PIXFMT_BGR24    10UL
#define PIXFMT_ARGB32   11UL
#define PIXFMT_BGRA32   12UL
#define PIXFMT_RGBA32   13UL

/* Rectangle formats for the xxxPixelArray() calls. */
#define RECTFMT_RGB     0UL
#define RECTFMT_RGBA    1UL
#define RECTFMT_ARGB    2UL
#define RECTFMT_LUT8    3UL
#define RECTFMT_GREY8   4UL

/* Colour table format for WriteLUTPixelArray(): ULONG 0x00RRGGBB entries. */
#define CTABFMT_XRGB8   0UL

/* LockBitMapTagList() tags. */
#define LBMI_WIDTH       0x84001001UL
#define LBMI_HEIGHT      0x84001002UL
#define LBMI_DEPTH       0x84001003UL
#define LBMI_PIXFMT      0x84001004UL
#define LBMI_BYTESPERPIX 0x84001005UL
#define LBMI_BYTESPERROW 0x84001006UL
#define LBMI_BASEADDRESS 0x84001007UL

/* UnLockBitMapTagList() tags. */
#define UBMI_UPDATERECTS 0x85001001UL
#define UBMI_REALLYUNLOCK 0x85001002UL

/* AllocBitMap() extended flags, used to request a specific RTG format. */
#ifndef BMB_SPECIALFMT
#define BMB_SPECIALFMT  7UL
#define BMF_SPECIALFMT  (1UL << BMB_SPECIALFMT)
#endif
#define SHIFT_PIXFMT(fmt) (((ULONG)(fmt)) << 24UL)

#endif /* LIBRARIES_CYBERGRAPHICS_H */
