#ifndef _INLINE_CYBERGRAPHICS_H
#define _INLINE_CYBERGRAPHICS_H
/*
 * Hand-written inline stubs for the cybergraphics.library calls LAB3D/Amiga
 * makes.  Same shape as the sfdc-generated NDK inlines, so <inline/macros.h>
 * does all the register juggling.
 *
 * Offsets are derived from cybergraphics_lib.fd (##bias 30, six bytes per
 * entry); the private slots are counted but not declared.
 */

#ifndef __INLINE_MACROS_H
#include <inline/macros.h>
#endif

#ifndef CYBERGRAPHICS_BASE_NAME
#define CYBERGRAPHICS_BASE_NAME CyberGfxBase
#endif

/* 30..48: cgfxPrivate1..4 */

#define IsCyberModeID(___displayID) \
      LP1(0x36, BOOL, IsCyberModeID , ULONG, ___displayID, d0,\
      , CYBERGRAPHICS_BASE_NAME)

#define BestCModeIDTagList(___tagList) \
      LP1(0x3c, ULONG, BestCModeIDTagList , CONST struct TagItem *, ___tagList, a0,\
      , CYBERGRAPHICS_BASE_NAME)

#define CModeRequestTagList(___modereq, ___tagList) \
      LP2(0x42, ULONG, CModeRequestTagList , APTR, ___modereq, a0, CONST struct TagItem *, ___tagList, a1,\
      , CYBERGRAPHICS_BASE_NAME)

#define AllocCModeListTagList(___tagList) \
      LP1(0x48, struct List *, AllocCModeListTagList , CONST struct TagItem *, ___tagList, a1,\
      , CYBERGRAPHICS_BASE_NAME)

#define FreeCModeList(___modeList) \
      LP1NR(0x4e, FreeCModeList , struct List *, ___modeList, a0,\
      , CYBERGRAPHICS_BASE_NAME)

/* 54: cgfxPrivate5 */

#define ScalePixelArray(___srcRect, ___srcW, ___srcH, ___srcMod, ___rp, ___destX, ___destY, ___destW, ___destH, ___srcFormat) \
      LP10(0x5a, LONG, ScalePixelArray , APTR, ___srcRect, a0, UWORD, ___srcW, d0, UWORD, ___srcH, d1, UWORD, ___srcMod, d2, struct RastPort *, ___rp, a1, UWORD, ___destX, d3, UWORD, ___destY, d4, UWORD, ___destW, d5, UWORD, ___destH, d6, UBYTE, ___srcFormat, d7,\
      , CYBERGRAPHICS_BASE_NAME)

#define GetCyberMapAttr(___bitMap, ___attribute) \
      LP2(0x60, ULONG, GetCyberMapAttr , CONST struct BitMap *, ___bitMap, a0, ULONG, ___attribute, d0,\
      , CYBERGRAPHICS_BASE_NAME)

#define GetCyberIDAttr(___attribute, ___displayID) \
      LP2(0x66, ULONG, GetCyberIDAttr , ULONG, ___attribute, d0, ULONG, ___displayID, d1,\
      , CYBERGRAPHICS_BASE_NAME)

#define ReadRGBPixel(___rp, ___x, ___y) \
      LP3(0x6c, ULONG, ReadRGBPixel , struct RastPort *, ___rp, a1, UWORD, ___x, d0, UWORD, ___y, d1,\
      , CYBERGRAPHICS_BASE_NAME)

#define WriteRGBPixel(___rp, ___x, ___y, ___argb) \
      LP4(0x72, LONG, WriteRGBPixel , struct RastPort *, ___rp, a1, UWORD, ___x, d0, UWORD, ___y, d1, ULONG, ___argb, d2,\
      , CYBERGRAPHICS_BASE_NAME)

#define ReadPixelArray(___destRect, ___destX, ___destY, ___destMod, ___rp, ___srcX, ___srcY, ___sizeX, ___sizeY, ___destFormat) \
      LP10(0x78, LONG, ReadPixelArray , APTR, ___destRect, a0, UWORD, ___destX, d0, UWORD, ___destY, d1, UWORD, ___destMod, d2, struct RastPort *, ___rp, a1, UWORD, ___srcX, d3, UWORD, ___srcY, d4, UWORD, ___sizeX, d5, UWORD, ___sizeY, d6, UBYTE, ___destFormat, d7,\
      , CYBERGRAPHICS_BASE_NAME)

#define WritePixelArray(___srcRect, ___srcX, ___srcY, ___srcMod, ___rp, ___destX, ___destY, ___sizeX, ___sizeY, ___srcFormat) \
      LP10(0x7e, LONG, WritePixelArray , APTR, ___srcRect, a0, UWORD, ___srcX, d0, UWORD, ___srcY, d1, UWORD, ___srcMod, d2, struct RastPort *, ___rp, a1, UWORD, ___destX, d3, UWORD, ___destY, d4, UWORD, ___sizeX, d5, UWORD, ___sizeY, d6, UBYTE, ___srcFormat, d7,\
      , CYBERGRAPHICS_BASE_NAME)

#define MovePixelArray(___srcX, ___srcY, ___rp, ___destX, ___destY, ___sizeX, ___sizeY) \
      LP7(0x84, LONG, MovePixelArray , UWORD, ___srcX, d0, UWORD, ___srcY, d1, struct RastPort *, ___rp, a1, UWORD, ___destX, d2, UWORD, ___destY, d3, UWORD, ___sizeX, d4, UWORD, ___sizeY, d5,\
      , CYBERGRAPHICS_BASE_NAME)

/* 138: cgfxPrivate6 */

#define InvertPixelArray(___rp, ___destX, ___destY, ___sizeX, ___sizeY) \
      LP5(0x90, LONG, InvertPixelArray , struct RastPort *, ___rp, a1, UWORD, ___destX, d0, UWORD, ___destY, d1, UWORD, ___sizeX, d2, UWORD, ___sizeY, d3,\
      , CYBERGRAPHICS_BASE_NAME)

#define FillPixelArray(___rp, ___destX, ___destY, ___sizeX, ___sizeY, ___argb) \
      LP6(0x96, LONG, FillPixelArray , struct RastPort *, ___rp, a1, UWORD, ___destX, d0, UWORD, ___destY, d1, UWORD, ___sizeX, d2, UWORD, ___sizeY, d3, ULONG, ___argb, d4,\
      , CYBERGRAPHICS_BASE_NAME)

#define DoCDrawMethodTagList(___hook, ___rp, ___tagList) \
      LP3NR(0x9c, DoCDrawMethodTagList , struct Hook *, ___hook, a0, struct RastPort *, ___rp, a1, CONST struct TagItem *, ___tagList, a2,\
      , CYBERGRAPHICS_BASE_NAME)

#define CVideoCtrlTagList(___viewPort, ___tagList) \
      LP2(0xa2, LONG, CVideoCtrlTagList , struct ViewPort *, ___viewPort, a0, CONST struct TagItem *, ___tagList, a1,\
      , CYBERGRAPHICS_BASE_NAME)

#define LockBitMapTagList(___bitmap, ___tagList) \
      LP2(0xa8, APTR, LockBitMapTagList , APTR, ___bitmap, a0, CONST struct TagItem *, ___tagList, a1,\
      , CYBERGRAPHICS_BASE_NAME)

#define UnLockBitMap(___handle) \
      LP1NR(0xae, UnLockBitMap , APTR, ___handle, a0,\
      , CYBERGRAPHICS_BASE_NAME)

#define UnLockBitMapTagList(___handle, ___tagList) \
      LP2NR(0xb4, UnLockBitMapTagList , APTR, ___handle, a0, CONST struct TagItem *, ___tagList, a1,\
      , CYBERGRAPHICS_BASE_NAME)

#define ExtractColor(___rp, ___bm, ___colour, ___srcX, ___srcY, ___width, ___height) \
      LP7(0xba, LONG, ExtractColor , struct RastPort *, ___rp, a0, struct BitMap *, ___bm, a1, ULONG, ___colour, d0, ULONG, ___srcX, d1, ULONG, ___srcY, d2, ULONG, ___width, d3, ULONG, ___height, d4,\
      , CYBERGRAPHICS_BASE_NAME)

/* 192: cgfxPrivate7 */

#define WriteLUTPixelArray(___srcRect, ___srcX, ___srcY, ___srcMod, ___rp, ___cTable, ___destX, ___destY, ___sizeX, ___sizeY, ___ctabFormat) \
      LP11(0xc6, LONG, WriteLUTPixelArray , APTR, ___srcRect, a0, UWORD, ___srcX, d0, UWORD, ___srcY, d1, UWORD, ___srcMod, d2, struct RastPort *, ___rp, a1, APTR, ___cTable, a2, UWORD, ___destX, d3, UWORD, ___destY, d4, UWORD, ___sizeX, d5, UWORD, ___sizeY, d6, UBYTE, ___ctabFormat, d7,\
      , CYBERGRAPHICS_BASE_NAME)

#endif /* _INLINE_CYBERGRAPHICS_H */
