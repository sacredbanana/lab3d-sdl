#ifndef LAB3D_RENDER_H
#define LAB3D_RENDER_H

/*
 * LAB3D renderer interface.
 *
 * Two back ends implement it:
 *
 *   src/render_gl.c          OpenGL, used by every SDL port.  This is the
 *                            original LAB3D/SDL drawing code, moved out of
 *                            subs.c/graphx.c unchanged.
 *   src/amiga/render_soft.c  A software rasteriser writing 8 bit chunky
 *                            pixels, used by the Amiga port.
 *
 * graphx.c keeps all the geometry and visibility work (ray casting, monster
 * and sprite selection, depth sorting) and hands the back end finished
 * primitives, so there is exactly one copy of the game logic.
 */

/* --------------------------------------------------------------- lifecycle */

/* Does this back end want one big overlay texture (1) or a 6x12 grid of
   64x64 tiles (0)? */
int  R_WantsLargeOverlayTexture(void);

/* Create whatever the back end needs for the 8 bit overlay buffer.  Called
   once screenbufferwidth/height are known. */
void R_InitOverlay(void);

/* Clear the whole display to black. */
void R_ClearScreen(void);

/* ---------------------------------------------------------------- textures */

/* Hand wall texture `i` (0 based, 64x64 8 bit, column major) to the back end.
   `bmpkind` is bmpkind[i+1]; wrapmode/minfilt/magfilt come from the wall
   parameters and are ignored by back ends that cannot filter. */
void R_LoadWallTexture(int i, unsigned char *texels, int bmpkind,
                       int wrapmode, int minfilt, int magfilt);

#ifdef ENABLE_HIRES_TEXTURES
struct _wallparam;
void R_ReplaceWallTexture(int i, struct _wallparam *wp,
                          int wrapmode, int minfilt, int magfilt);
#endif

/* Second copy of the "game over" tile, used as a spinning 2D overlay. */
void R_LoadGameOverSprite(unsigned char *texels);

/* Re-read walseg[walnum-1] after the game has scribbled on it (automap,
   game over banner, slot machine reels). */
void R_UpdateWallTexture(int walnum);

/* Build the smoothed wall-to-wall transition textures.  A no-op where the
   renderer samples texels directly and so has nothing to smooth. */
void R_MakeTransitionTextures(void);

/* ---------------------------------------------------------------- palette */

/* Rebuild the colour lookup tables from palette[].  `darkened` selects the
   dimmed variant used by the end of game sequences. */
void R_SetOverlayPalette(int darkened);

/* Replace `count` entries starting at `start` from `cols` (RGB, 0..63). */
void R_SetOverlayPaletteRange(K_UINT16 start, K_UINT16 count,
                              unsigned char *cols);

/* Called by fade(): the brightness factors changed. */
void R_FadeChanged(void);

/* ----------------------------------------------------------------- overlay */

void UploadPartialOverlay(int x, int y, int w, int h);
void UploadOverlay(void);
void ShowPartialOverlay(int x, int y, int w, int h, int statusbar);
void ShowStatusBar(void);
void SetVisibleScreenOffset(K_UINT16 offset);

/* ------------------------------------------------------------- 3D geometry */

/* Begin a frame of the labyrinth view: set up the camera, paint the ceiling
   and floor bands and reset the depth information.  `horizon` is the screen
   row (in the 360x240 virtual space) where ceiling meets floor. */
void R_BeginScene(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs,
                  double aspw, double asph, int horizon);

/* An upright wall quad running from (x1,y1) to (x2,y2) in world coordinates
   and from z=0 (ceiling) to z=1024 (floor).  Texture `texnum` is sampled with
   the horizontal coordinate going v0..v1 across the quad.  `shaded` picks the
   dimmer of the two wall shades; `transparent` marks the invisible wall,
   which writes depth but no colour. */
void R_DrawWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                K_INT16 texnum, double v0, double v1,
                int shaded, int transparent);

/* The two halves of a wall drawn with smoothed transition textures.  Back
   ends that answer 0 to R_HaveTransitionTextures() never see this. */
void R_DrawSplitWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     int slot, int shaded);
int  R_HaveTransitionTextures(void);

/* A billboard: an upright quad from (x1,y1) to (x2,y2), z=0..1024, spun by
   `spinang` about the view axis (0 for ordinary sprites). */
void R_DrawBillboard(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     K_INT16 texnum, double v0, double v1,
                     K_INT16 spinang, K_INT16 playerang);

/* A floor decal: a 1024x1024 quad lying on the floor centred on (x,y). */
void R_DrawFloorSprite(K_UINT16 x, K_UINT16 y, K_INT16 texnum);

/* Solid walls are done; everything that follows is depth-tested but does not
   write depth. */
void R_EndWalls(void);

/* Finish the labyrinth view (flush spans, composite). */
void R_EndScene(void);

/* --------------------------------------------------------------- 2D sprites */

/* Draw texture `texnum` centred at (x,y) in the 360x240 virtual space,
   scaled by siz/256 and rotated by `ang` (2048 = full turn). */
void R_DrawSprite2D(K_INT16 x, K_INT16 y, K_INT16 siz, K_INT16 ang,
                    K_INT16 texnum, int usegameoversprite);

/* Translucent volume/brightness bar drawn over everything else. */
void R_DrawVolumeBar(int vol, int type, float level);

/* ------------------------------------------------------------- screenshots */

/* Grab the displayed frame as bottom-up BGR into `dst` (3*w*h bytes).
   Returns 0 on success. */
int R_ReadPixelsBGR(unsigned char *dst, int w, int h);

/* -------------------------------------------------------- stereoscopic view */

/* Render one eye's worth of the labyrinth.  Lives in graphx.c; called by the
   back end's picrot(), once for mono and twice for stereo. */
void picrot_view(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs,
                 double aspw, double asph);

void setup_stereo(int s);

#endif /* LAB3D_RENDER_H */
