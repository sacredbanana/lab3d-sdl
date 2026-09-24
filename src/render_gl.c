/*
 * OpenGL renderer back end for LAB3D.
 *
 * This is the original LAB3D/SDL drawing code, collected out of subs.c and
 * graphx.c behind the interface in include/render.h.  Nothing here changed
 * except that the texture uploads, palette transfers and overlay handling are
 * now reached through R_* entry points, so that the Amiga port can supply a
 * software rasteriser instead.
 */

#include "lab3d.h"
#include "adlibemu.h"
#include <math.h>
#include <ctype.h>

/* LoadImageCache() needs SDL_image whether or not hi-res texture replacement
   is compiled in, exactly as subs.c used to. */
#include "SDL_image.h"

void ConvertPartialOverlay(int x, int y, int w, int h);

/* Palette for OpenGL transfer... */

static GLfloat Red[256], Blue[256], Green[256];
static GLfloat Alpha[256];

static unsigned char ipalr[256], ipalg[256], ipalb[256];

/* Check OpenGL status and complain if necessary. */
void checkGLStatus()
{
    GLenum errCode;

    while ((errCode=glGetError())!=GL_NO_ERROR) {
        #ifdef __SWITCH__
        fprintf(stderr, "OpenGL Error: %d\n", errCode);
        #else
        const GLubyte *errString = gluErrorString(errCode);
        fprintf(stderr, "OpenGL Error: %s\n", errString);
        #endif
    }
}

int powerof2 (int in)
{
    int i = 0;
    in--;
    while (in) {
        in >>= 1;
        i++;
    }
    return 1 << i;
}

typedef struct imgcache {
    char* name;
    int w, h;
    //double tcx, tcy;
    GLuint texnum;
    struct imgcache* next;
} imgcache;


static imgcache* img_cache=NULL;

void clearimgcache()
{
    free(img_cache);
    img_cache = NULL;
}

static inline int AverageColour32 (Uint32 * p, int x, int y, int w, int h)
{
    int a, c, n = 0;
    int r = 0, g = 0, b = 0;
    for (a = -1; a <= 1; a++) {
        if (x + a < 0)
            continue;
        if (x + a >= w)
            continue;
        for (c = -1; c <= 1; c++) {
            if (y + c < 0)
                continue;
            if (y + c >= h)
                continue;
            if (ALPHACMP (p[(x + a) + w * (y + c)]) > 0) {
                Uint32 t = p[(x + a) + w * (y + c)];
                b += BLUECMP (t);
                g += GREENCMP (t);
                r += REDCMP (t);
                n++;
            }
        }
    }
    if (n > 0)
        return ((b / n) << BLUE_SHIFT) + ((g / n) << GREEN_SHIFT) +
            ((r / n) << RED_SHIFT);
    else
        return 0;
}

static inline void TextureAvg32 (Uint32 * pic, int w, int h)
{

    Uint32 *f = pic;

    int x, y;

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            if (ALPHACMP (*f) == 0) {
                *f = AverageColour32 (pic, x, y, w, h);
            }
            f++;
        }
}

#ifndef GL_EXT_texture_filter_anisotropic
#define GL_TEXTURE_MAX_ANISOTROPY_EXT     0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

static void SetAnisotropic(void) {
    checkGLStatus();

    //#ifdef GL_EXT_texture_filter_anisotropic
    GLfloat aniso;
    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &aniso);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, aniso);
    if (glGetError()!=GL_NO_ERROR) {
        fprintf(stderr, "Warning: Anisotropic filtering not supported by driver, using trilinear filtering.\n");
        anisotropic=0;
    }
    //#else
    //anisotropic=0;
    //fprintf(stderr, "Warning: Anisotropic filtering not supported at compile time, using trilinear filtering.\n");
    //#endif

}

#ifndef __SWITCH__
#define USE_GLU_MIPMAPS
#endif

#ifdef USE_GLU_MIPMAPS

void BuildMipmaps(Uint32* pix, int w, int h, int hasalpha, int maxlevel) {
    if (hasalpha) TextureAvg32(pix, w, h);
    glPixelStorei (GL_UNPACK_ROW_LENGTH, w);
    gluBuild2DMipmaps(GL_TEXTURE_2D, hasalpha?GL_RGBA:GL_RGB, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix);
}
#else

void ShrinkImageWeight (Uint32* src, Uint32* dest, int sw, int sh, int xs, int ys) {
    int dw=sw/xs;
    int dh=sh/ys;
    int sx, sy, dx, dy, xi, yi;
    double r, g, b, a;
    double rt, gt, bt, at;
    double wrt, wgt, wbt;
    double pixscl=(255.0/(xs*ys));
    double cpixscl;
    int ir, ig, ib, ia;
    for (sy=0, dy=0;dy<dh;sy+=ys, dy++) {
        for (sx=0, dx=0;dx<dw;sx+=xs, dx++) {
            rt=0; gt=0;
            bt=0; at=0;
            for (yi=0;yi<ys;yi++)
                for (xi=0;xi<xs;xi++) {
                    Uint32 sp=src[sx+xi + (sy+yi)*sw];
                    a = ALPHACMP(sp)/255.0;
                    r = REDCMP(sp)/255.0;
                    g = GREENCMP(sp)/255.0;
                    b = BLUECMP(sp)/255.0;
                    rt += r; gt += g;
                    bt += b; at += a;
                    wrt += r*a;
                    wgt += g*a;
                    wbt += b*a;
                }
            ia=(at*pixscl);
            if (at==0.0) {
                ir=rt*pixscl;
                ig=gt*pixscl;
                ib=bt*pixscl;
            } else {
                cpixscl=255.0/at;
                ir=rt*cpixscl;
                ig=gt*cpixscl;
                ib=bt*cpixscl;
            }
            *(dest++) = ir<<RED_SHIFT | ib<<BLUE_SHIFT | ig<<GREEN_SHIFT | ia<<ALPHA_SHIFT;
        }
    }
}
#if !defined(min) && !defined(__SWITCH__) 
#define min(x, y) ({ typeof(x) _x_; typeof(y) _y_; _x_=(x); _y_=(y); _x_ < _y_ ? _x_ : _y_ })
#endif

void ShrinkImage (Uint32* src, Uint32* dest, int sw, int sh, int xs, int ys) {
    int dw=sw>>xs;
    int dh=sh>>ys;
    int sx, sy, dx, dy, xi, yi;
    int rt, gt, bt, at;
    int xsk=1<<xs;
    int ysk=1<<ys;
    int scale=xs+ys;
    for (sy=0, dy=0;dy<dh;sy+=ysk, dy++) {
        for (sx=0, dx=0;dx<dw;sx+=xsk, dx++) {
            rt=0; gt=0;
            bt=0; at=0;
            for (yi=0;yi<ysk;yi++)
                for (xi=0;xi<xsk;xi++) {
                    Uint32 sp=src[sx+xi + (sy+yi)*sw];
                    rt += REDCMP(sp);
                    gt += GREENCMP(sp);
                    bt += BLUECMP(sp);
                    at += ALPHACMP(sp);
                }
            rt>>=scale;
            gt>>=scale;
            bt>>=scale;
            at>>=scale;
            *(dest++) = rt<<RED_SHIFT | bt<<BLUE_SHIFT | gt<<GREEN_SHIFT | at<<ALPHA_SHIFT;
        }
    }
}
#define TWO_BUFFERS
void BuildMipmaps(Uint32* pix, int w, int h, int hasalpha, int maxmips) {
    int format=hasalpha?GL_RGBA:GL_RGB;
    Uint32* bufs[16];
    bufs[0]=pix;

#ifdef TWO_BUFFERS
    Uint32* buf1=malloc((w>>1)*(h>>1)*4);
    Uint32* buf2=malloc((w>>1)*(h>>1)*4);
    bufs[1] = bufs[3] = bufs[5] = bufs[7] = bufs[9] = bufs[11]= buf1;
    bufs[2] = bufs[4] = bufs[6] = bufs[8] = bufs[10]= bufs[12]= buf2;
#else
    int z;
    for (z=1;z<16;z++) {
        bufs[z]=malloc((w>>1)*(h>>1)*4);
    }
#endif
    int cw=w;
    int ch=h;
    int ow, oh;
    int xs, ys;
    int level=0;

    while (1) {
        if (hasalpha) TextureAvg32(bufs[level], cw, ch);
        glPixelStorei (GL_UNPACK_ROW_LENGTH, cw);
        glTexImage2D (GL_TEXTURE_2D, level, format, cw, ch, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, bufs[level]);
        glFinish();
        if (cw<=1 && ch<=1 ) break;
        ow=cw; oh=ch;
        xs=0; ys=0;
        if (cw>1) { cw >>= 1; xs=1; }
        if (ch>1) { ch >>= 1; ys=1; }
        level++;
        if (!(maxmips!=-1 && level>=maxmips)) ShrinkImage(bufs[level-1], bufs[level], ow, oh, xs, ys);
    }
#ifdef TWO_BUFFERS
    free(buf1); free(buf2);
#else
    for (z=1;z<16;z++) {
        free(bufs[z]);
    }
#endif
}

#endif /*USE_GLU_MIPMAPS*/



void UploadTexture(GLuint tex, void* pixels, int w, int h,  int repx, int repy, int hasalpha, int minfilt, int magfilt) {
    int mipmaps=1;

    glBindTexture(GL_TEXTURE_2D, tex);
    checkGLStatus ();

    glPixelStorei (GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei (GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
    checkGLStatus ();

    switch (minfilt) {
        case GL_NEAREST:
        case GL_LINEAR:
            mipmaps=0;
            if (!magfilt) magfilt=minfilt;
            break;
        case GL_NEAREST_MIPMAP_NEAREST:
        case GL_NEAREST_MIPMAP_LINEAR:
            if (!magfilt) magfilt=GL_NEAREST;
            break;
        case GL_LINEAR_MIPMAP_NEAREST:
        case GL_LINEAR_MIPMAP_LINEAR:
            if (!magfilt) magfilt=GL_LINEAR;
            break;
    }
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, repy?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, repx?GL_REPEAT:GL_CLAMP_TO_EDGE);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magfilt);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minfilt);

    if (mipmaps) {
        if (anisotropic)
            SetAnisotropic();
        BuildMipmaps (pixels, w, h, hasalpha, 20);
    } else {
        glTexImage2D (GL_TEXTURE_2D, 0, hasalpha?GL_RGBA:GL_RGB, w, h, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, pixels);
    }
    checkGLStatus ();
}

int checkalpha(Uint32* tex, int w, int cw, int ch) {
    int skip=w-cw;
    int i, j;
    for (i=0;i<ch;i++, tex += skip) {
        for (j=0;j<cw;j++, tex++) {
            if (ALPHACMP(*tex)!=0xFF) return 1;
        }
    }
    return 0;
}

imgcache* LoadImageCache(const char* fname, int repeatx, int minfilt, int magfilt) {
    imgcache* cur = img_cache;
    while (cur) {
        if (!strcmp(fname, cur->name)) {
            return cur;
        }
        cur = cur->next;
    }
    sprintf(filepath, "%s%s", gameroot, fname);
    SDL_Surface* base_image = IMG_Load(filepath);
    if (!base_image) {
        fatal_error( "Could not load image %s: %s", fname, SDL_GetError());
    }

    imgcache* new = (imgcache*)malloc(sizeof(imgcache));
    new->name = strdup(fname);

    glGenTextures (1, &new->texnum);

    SDL_SetSurfaceAlphaMod(base_image, 255);
    SDL_SetSurfaceBlendMode(base_image, SDL_BLENDMODE_NONE);
    SDL_Surface* conv = SDL_CreateRGBSurface(SDL_SWSURFACE, base_image->w, base_image->h, 32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000);
    Uint32* temptex = (Uint32*)malloc(base_image->w*base_image->h*4);

    SDL_BlitSurface(base_image, NULL, conv, NULL);

    /* Convert to column-major order */
    int xx, yy;
    Uint32* temp = (Uint32*)temptex;
    for(xx=0; xx < base_image->w; xx++)
        for(yy=0; yy < base_image->h; yy++)
            *temp++ = ((Uint32*)conv->pixels)[xx+base_image->w*yy];

    int hasalph = checkalpha((Uint32*) base_image->pixels, base_image->w, base_image->w, base_image->h);

    new->w = base_image->w;
    new->h = base_image->h;
    UploadTexture(new->texnum, temptex, base_image->h, base_image->w, 0,
                  repeatx, hasalph, minfilt, magfilt);
    SDL_FreeSurface(base_image);
    SDL_FreeSurface(conv);
    free(temptex);

    new->next=img_cache;
    img_cache=new;
    return new;

}

#define COPYLINE                                \
    for(y=0;y<64;y++) {                         \
        *(t++)=spritepalette[(*f)*3]<<2;        \
        *(t++)=spritepalette[(*f)*3+1]<<2;      \
        *(t++)=spritepalette[(*f)*3+2]<<2;      \
        *(t++)=255;                             \
        f++;                                    \
    }

/* Create a smoother transition texture. */

void TransitionTexture(int left, int texture, int right) {
    unsigned char texdata[66*64*4];

    unsigned char *f=walseg[left]+(63*64), *t=texdata;

    static int texnum=0;

    int x, y;

    COPYLINE;

    f=walseg[texture];

    for(x=0;x<64;x++)
        COPYLINE;

    f=walseg[right];

    COPYLINE;

    for(x=0;x<2;x++) {

        glGenTextures(1, &splitTexName[texnum][x]);

        glBindTexture(GL_TEXTURE_2D, splitTexName[texnum][x]);
        checkGLStatus();

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, partialfilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                        fullfilter);
        checkGLStatus();

        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        checkGLStatus();
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        checkGLStatus();

        /* Add code here to upload two textures from texdata, one with cols
           0-63, other 2-65. */

        glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);

        BuildMipmaps((Uint32*)(texdata+x*512), 64, 64, 1, 10);

        checkGLStatus();
    }
    splitTexNum[texnum++]=texture;

}

/* Get average of neighbouring pixels... */

int AverageColour(unsigned char *p, int x, int y, int colour) {
    int a, b, c=0, n=0;

    for(a=-1;a<=1;a++) {
        if (x+a<0) continue;
        if (x+a>63) continue;
        for(b=-1;b<=1;b++) {
            if (y+b<0) continue;
            if (y+b>63) continue;
            if (p[(x+a)*64+(y+b)]!=255) {
                c+=spritepalette[p[(x+a)*64+(y+b)]*3+colour]<<2;
                n++;
            }
        }
    }
    if (n>0) return c/n; else return 0;
}

/* Convert a texture from 8-bit indexed to 32-bit RGBA. */
void TextureConvert(unsigned char *from, unsigned char *to, Sint16 type) {

    unsigned char *f=from, *t=to;

    int x, y;

    for(x=0;x<64;x++)
        for(y=0;y<64;y++) {
            if ((type>1)&&(*(f))==255) {
                *(t++)=0;
                *(t++)=0;
                *(t++)=0;
                *(t++)=0;
            } else {
                *(t++)=spritepalette[(*f)*3]<<2;
                *(t++)=spritepalette[(*f)*3+1]<<2;
                *(t++)=spritepalette[(*f)*3+2]<<2;
                *(t++)=255;
            }
            f++;
        }
    //TextureAvg32((Uint32*)to, 64, 64);
}

/*void TextureConvert(unsigned char *from, unsigned char *to, K_INT16 type) {

  unsigned char *f=from, *t=to;

  int x, y;

  for(x=0;x<64;x++)
  for(y=0;y<64;y++) {
  if ((type>1)&&(*(f))==255) {
  *(t++)=AverageColour(from, x, y, 0);
  *(t++)=AverageColour(from, x, y, 1);
  *(t++)=AverageColour(from, x, y, 2);
  *(t++)=0;
  } else {
  *(t++)=spritepalette[(*f)*3]<<2;
  *(t++)=spritepalette[(*f)*3+1]<<2;
  *(t++)=spritepalette[(*f)*3+2]<<2;
  *(t++)=255;
  }
  f++;
  }
  }*/

/* Darkened palette for end of game sequences... */

void setdarkenedpalette() {
    K_INT16 a;

    for(a=0;a<256;a++) {
        ipalr[a]=(palette[a*3]*27)>>3;
        ipalg[a]=(palette[a*3+1]*27)>>3;
        ipalb[a]=(palette[a*3+2]*27)>>3;

        Red[a]=palette[a*3]*27/4096.0;
        Green[a]=palette[a*3+1]*27/4096.0;
        Blue[a]=palette[a*3+2]*27/4096.0;
        Alpha[a]=1.0;
    }

    if (ingame)
        Red[255]=Green[255]=Blue[255]=Alpha[255]=0.0;

    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 256, Red);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 256, Green);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 256, Blue);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 256, Alpha);

    ConvertPartialOverlay(0, 0, 360, 240);
}

/* Normal palette... */

void settransferpalette() {
    K_INT16 a;

    for(a=0;a<256;a++) {
        ipalr[a]=(palette[a*3]*27)>>3;
        ipalg[a]=(palette[a*3+1]*27)>>3;
        ipalb[a]=(palette[a*3+2]*27)>>3;

        Red[a]=palette[a*3]/64.0;
        Green[a]=palette[a*3+1]/64.0;
        Blue[a]=palette[a*3+2]/64.0;
        Alpha[a]=1.0;
    }

    if (ingame)
        Red[255]=Green[255]=Blue[255]=Alpha[255]=0.0;

    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 256, Red);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 256, Green);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 256, Blue);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 256, Alpha);

    ConvertPartialOverlay(0, 0, 360, 240);
}

/* Change some of the palette... */

void updateoverlaypalette(K_UINT16 start, K_UINT16 amount, unsigned char *cols) {
    K_INT16 i;

    for(i=0;i<amount;i++) {
        ipalr[i+start]=(cols[i*3]*27)>>3;
        ipalg[i+start]=(cols[i*3+1]*27)>>3;
        ipalb[i+start]=(cols[i*3+2]*27)>>3;

        Red[i+start]=cols[i*3]/64.0;
        Green[i+start]=cols[i*3+1]/64.0;
        Blue[i+start]=cols[i*3+2]/64.0;
        Alpha[i+start]=1.0;
    }

    if (ingame)
        Red[255]=Green[255]=Blue[255]=Alpha[255]=0.0;

    glPixelMapfv(GL_PIXEL_MAP_I_TO_R, 256, Red);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_G, 256, Green);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_B, 256, Blue);
    glPixelMapfv(GL_PIXEL_MAP_I_TO_A, 256, Alpha);

    ConvertPartialOverlay(0, 0, 360, 240);
}

void UploadPartialOverlayToTexture(int x, int y, int dx, int dy, int w, int h,
                                   GLuint tex) {
    glBindTexture(GL_TEXTURE_2D, tex);
    checkGLStatus();

    glPixelStorei(GL_UNPACK_ROW_LENGTH, screenbufferwidth);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, x);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, y);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    checkGLStatus();

    if (texturecreationneeded) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, partialfilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, partialfilter);
    }

    //glPixelTransferi(GL_MAP_COLOR, GL_TRUE);

    if (debugmode)
        fprintf(stderr, "Partial overlay upload (%d %d %d %d)... ",
                w, h, dx, dy);

    if (texturecreationneeded) {
        if (debugmode)
            fprintf(stderr, "(texturecreationneeded) ");
        glTexImage2D(GL_TEXTURE_2D, 0, colourformat, w,
                     h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE,
                     screenbuffer32);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, dx, dy, w, h,
                        GL_RGBA,
                        GL_UNSIGNED_BYTE,
                        screenbuffer32);
    }
    checkGLStatus();
    if (debugmode)
        fprintf(stderr, "done.\n");
    //glPixelTransferi(GL_MAP_COLOR, GL_FALSE);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

void ConvertPartialOverlay(int sx, int sy, int w, int h) {
    unsigned char *f, *t;

    int x, y;
    int skip;

    if (!ClipToBuffer(&sx, &sy, &w, &h))
        return;

    f=&screenbuffer[sx+screenbufferwidth*sy];
    t=(unsigned char*)&screenbuffer32[sx+screenbufferwidth*sy];

    skip=screenbufferwidth-w;
    //printf("sx=%d, sy=%d, w=%d, h=%d, spos=%d skip=%d\n", sx, sy, w, h, sx+screenbufferwidth*sy, skip);
    for(y=0;y<h;y++, f+=skip, t+=(skip*4))
        for(x=0;x<w;x++) {
            if ((*(f))==255 && ingame) {
                *(t++)=0;
                *(t++)=0;
                *(t++)=0;
                *(t++)=0;
            } else {
                *(t++)=ipalr[*f];
                *(t++)=ipalg[*f];
                *(t++)=ipalb[*f];
                *(t++)=255;
            }
            f++;
        }
    //TextureAvg32((Uint32*)to, 64, 64);

}

/* Upload rectangular part of overlay from memory to overlay texture... */

void UploadPartialOverlay(int x, int y, int w, int h) {
    int left, right, top, bottom, i, j;
    int lr, rr, tr, br;

    if (!ClipToBuffer(&x, &y, &w, &h))
        return;

    ConvertPartialOverlay(x, y, w, h);
    if (menuing) return;

    if (largescreentexture) {
        /* On my nVidia Riva TNT, uploading 1 pixel high subimages is very slow
           (driver bug?), so I upload an extra row. Very odd. Probably a driver
           issue (nVidia driver version 1.0-1541 on Linux 2.4.4-4GB).

           This only seems to affect the large textures. Very odd. */

        UploadPartialOverlayToTexture(x, y, x, y, w, (h>1)?h:2,
                                      screenbuffertexture);
    } else {
        left=(x-2)/62;
        if (left<0) left=0;
        right=(x+w-1)/62;
        if (right>5) right=5;
        top=(y-2)/62;
        if (top<0) top=0;
        bottom=(y+h-1)/62;
        if (bottom>11) bottom=11;

        for(i=top;i<=bottom;i++)
            for(j=left;j<=right;j++) {
                lr=x-62*j;
                rr=lr+w-1;
                tr=y-62*i;
                br=tr+h-1;

                if (rr<0) continue;
                if (lr>63) continue;
                if (br<0) continue;
                if (tr>63) continue;

                if (lr<0) lr=0;
                if (rr>63) rr=63;
                if (tr<0) tr=0;
                if (br>63) br=63;

                UploadPartialOverlayToTexture(lr+62*j, tr+62*i, lr, tr, rr-lr+1,
                                              br-tr+1,
                                              screenbuffertextures[i*6+j]);
            }
    }
    ShowPartialOverlay(x-1,y-1,w+2,h+2,0);
    /*ShowPartialOverlay(0, 0, virtualscreenwidth, virtualscreenheight, 0);*/
}

/* Upload entire overlay from memory to texture (creates textures)... */

void UploadOverlay(void) {
    int i, j;

    settransferpalette();
    ConvertPartialOverlay(0, 0, screenbufferwidth, screenbufferheight);

    if (largescreentexture)
        UploadPartialOverlayToTexture(0, 0, 0, 0, screenbufferwidth,
                                      screenbufferheight,
                                      screenbuffertexture);
    else {
        for(i=0;i<12;i++)
            for(j=0;j<6;j++)
                UploadPartialOverlayToTexture(62*j, 62*i, 0, 0, 64, 64,
                                              screenbuffertextures[i*6+j]);
    }
    texturecreationneeded=0;
}

/* Display rectangular part of overlay... */

void ShowPartialOverlay(int x, int y, int w, int h, int statusbar) {

    float tx1, tx2, ty1, ty2;
    int i, j, tr, br, lr, rr, left, right, top, bottom;

    float vl, vt1, vt2;

    if (statusbar==0) {
        y-=visiblescreenyoffset;
        if (x+w>360) w=360-x;
        if (y+h>240) h=240-y;
        if (x<0) {w+=x; x=0;}
        if (y<0) {h+=y; y=0;}
        if ((w<=0)||(h<=0)) return;
        y+=visiblescreenyoffset;
    }

    if (mixing)
        glEnable(GL_BLEND);
    else {
        glAlphaFunc(GL_GEQUAL, 0.99);
        glEnable(GL_ALPHA_TEST);
    }
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    vl=floor(-((float)virtualscreenwidth-360.0)/2.0);
    vt1=floor(240.0+(virtualscreenheight-240.0)/2.0);
    vt2=floor(statusbaryoffset+statusbaryvisible+statusbaryoffset-y);

    if (statusbar==1)
        glOrtho(vl,
                   vl+virtualscreenwidth,
                   vt2,
                   vt2-virtualscreenheight,
                   -1.0,
                   1.0);
    else if (statusbar==2) {
        glOrtho(vl+340.0-x,
                   vl+virtualscreenwidth+340.0-x,
                   vt2,
                   vt2-virtualscreenheight,
                   -1.0,
                   1.0);
        x=340; y=statusbaryoffset;
    }
    else
        glOrtho(vl,
                   vl+virtualscreenwidth,
                   vt1,
                   vt1-virtualscreenheight,
                   -1.0,
                   1.0);

//    gluOrtho2D(0.0, 360.0, 0.0, 240.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    if (largescreentexture) {
        tx1=((float)x)/(float)(screenbufferwidth);
        tx2=((float)(x+w))/(float)screenbufferwidth;

        ty1=((float)y)/(float)(screenbufferheight);
        ty2=((float)(y+h))/(float)screenbufferheight;

        y-=visiblescreenyoffset;

        glBindTexture(GL_TEXTURE_2D, screenbuffertexture);
        glBegin(GL_QUADS);
        glColor3f(redfactor, greenfactor, bluefactor);
        /*printf("ty1=%f ty2=%f y1=%d y2=%d\n", ty1, ty2, y, y+h);*/
        glTexCoord2f(tx1, ty2);
        glVertex2s(x, y+h);
        glTexCoord2f(tx2, ty2);
        glVertex2s(x+w, y+h);
        glTexCoord2f(tx2, ty1);
        glVertex2s(x+w, y);
        glTexCoord2f(tx1, ty1);
        glVertex2s(x, y);
        glEnd();
    } else {
        left=(x-1)/62;
        if (left<0) left=0;
        right=(x+w-2)/62;
        if (right>5) right=5;
        top=(y-1)/62;
        if (top<0) top=0;
        bottom=(y+h-2)/62;
        if (bottom>11) bottom=11;

//	printf("Drawing %d %d %d %d\n", x, y, w, h);

        for(i=top;i<=bottom;i++)
            for(j=left;j<=right;j++) {
                lr=x-62*j;
                rr=lr+w-1;
                tr=y-62*i;
                br=tr+h-1;

                if (rr<(j>0)) continue;
                if (lr>(63-(j<5))) continue;
                if (br<(i>0)) continue;
                if (tr>(63-(i<11))) continue;

                if (lr<(j>0)) lr=(j>0);
                if (rr>(63-(j<5))) rr=63-(j<5);
                if (tr<(i>0)) tr=(i>0);
                if (br>(63-(i<11))) br=63-(i<11);

                tx1=((float)lr)/64.0;
                tx2=((float)(rr+1))/64.0;

                ty1=((float)tr)/64.0;
                ty2=((float)(br+1))/64.0;

                if (debugmode) {
                    fprintf(stderr, "Partial overlay display... ");
                    fprintf(stderr, "%d %d %d %d %d %d... ", i, j, lr, tr, rr, br);
                }

                glBindTexture(GL_TEXTURE_2D, screenbuffertextures[i*6+j]);
                glBegin(GL_QUADS);
                glColor3f(redfactor, greenfactor, bluefactor);
                glTexCoord2f(tx1, ty2);
                glVertex2s(lr+62*j, br+1+62*i-visiblescreenyoffset);
                glTexCoord2f(tx2, ty2);
                glVertex2s(rr+1+62*j, br+1+62*i-visiblescreenyoffset);
                glTexCoord2f(tx2, ty1);
                glVertex2s(rr+1+62*j, tr+62*i-visiblescreenyoffset);
                glTexCoord2f(tx1, ty1);
                glVertex2s(lr+62*j, tr+62*i-visiblescreenyoffset);
                glEnd();

                if (debugmode)
                    fprintf(stderr, "done.\n");

            }
    }
    if (mixing)
        glDisable(GL_BLEND);
    else
        glDisable(GL_ALPHA_TEST);
    checkGLStatus();

    if (statusbar==1) {
        for(i=0;i<(virtualscreenwidth-319)/2;i+=20) {
            ShowPartialOverlay(340+i, statusbaryoffset, 20, statusbaryvisible, 2);
            ShowPartialOverlay(0-i, statusbaryoffset, 20, statusbaryvisible, 2);
        }
    }

}

/* Draw status bar if necessary. */

void ShowStatusBar() {
//    if (statusbaryoffset>=240) return;
    mixing=1;
    ShowPartialOverlay(20, statusbaryoffset, 320, statusbaryvisible, 1);
    mixing=0;
}

/* Redraw overlay as if screen offset were offset bytes... */

void SetVisibleScreenOffset(K_UINT16 offset) {

    float y=offset/90;

    glClearColor(0, 0, 0, 0);
    glClear( GL_COLOR_BUFFER_BIT);

    visiblescreenyoffset=y;

    ShowPartialOverlay(0, 0+y, 360, 240, 0);
}

/* Update map texture... */

void updatemap() {
    unsigned char *RGBATexture=malloc(64*64*4);

    glBindTexture(GL_TEXTURE_2D, texName[map-1]);
    checkGLStatus();
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    TextureConvert(walseg[map-1], RGBATexture, bmpkind[map]);

    BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);

    checkGLStatus();
    free(RGBATexture);
}

/* Update game over texture... */

void updategameover() {
    unsigned char *RGBATexture=malloc(64*64*4);

    glBindTexture(GL_TEXTURE_2D, texName[gameover-1]);
    checkGLStatus();

    TextureConvert(walseg[gameover-1], RGBATexture, bmpkind[gameover]);

    BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);

    checkGLStatus();
    free(RGBATexture);
}


/* ==================================================================== */
/*  Renderer interface                                                  */
/* ==================================================================== */

#define _DYNAMIC_OGL_FUNCS(mac)                                 \
    mac(GLGENFRAMEBUFFERS, glGenFramebuffers)                   \
    mac(GLGENRENDERBUFFERS, glGenRenderbuffers)                 \
    mac(GLDRAWBUFFERS, glDrawBuffers)                           \
    mac(GLBINDFRAMEBUFFER, glBindFramebuffer)                   \
    mac(GLBINDRENDERBUFFER, glBindRenderbuffer)                 \
    mac(GLRENDERBUFFERSTORAGE, glRenderbufferStorage)           \
    mac(GLFRAMEBUFFERTEXTURE, glFramebufferTexture)             \
    mac(GLFRAMEBUFFERRENDERBUFFER, glFramebufferRenderbuffer)

#define _DECLARE_FUNC(type, name) static PFN ##type## PROC ext_##name;
#define _LOAD_FUNC(type, name) ext_##name = SDL_GL_GetProcAddress(#name); if (!ext_##name) return #name;

_DYNAMIC_OGL_FUNCS(_DECLARE_FUNC)

static GLuint stereo_fbufs[2];
static GLuint stereo_tex[2];
static GLuint stereo_depth[2];

static char* load_ogl_ext_funcs(void) {
    _DYNAMIC_OGL_FUNCS(_LOAD_FUNC)
    return NULL;
}

void setup_stereo(int s) {
    static int loaded = 0;
    int i;
    if (!loaded) {
        char *failed;
        if ((failed = load_ogl_ext_funcs()) != NULL) {
            loaded = -1;
            fprintf(stderr, "Could not find %s, Stereo not available (OpenGL too old)", failed);
            return;
        }
        loaded = 1;
    }
    if (loaded == -1)
        return;

    ext_glGenFramebuffers(2, stereo_fbufs);
    glGenTextures(2, stereo_tex);
    ext_glGenRenderbuffers(2, stereo_depth);

    stereo = s;

    for (i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, stereo == 2 ? screenwidth/2 : screenwidth, screenheight, 0,GL_RGB, GL_UNSIGNED_BYTE, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[i]);

        ext_glBindRenderbuffer(GL_RENDERBUFFER, stereo_depth[i]);
        ext_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        ext_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, stereo_depth[i]);

        ext_glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, stereo_tex[i], 0);
        GLenum drawbuffers[1] = {GL_COLOR_ATTACHMENT0};
        ext_glDrawBuffers(1, drawbuffers);
    }
    ext_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void picrot(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs) {
    int i;
    if (newkeystatus(PLK_1)) {
        g_stereo_sep -= 1;
    } else if (newkeystatus(PLK_2)) {
        g_stereo_sep += 1;
    }
    int sep = g_stereo_sep >> 3;
    int asep = stereo == 2 ? 0 : 4;

    if (!stereo) {
        picrot_view(posxs, posys, poszs, angs, aspw, asph);
    } else {
        int yo = (sintable[(angs+512)&2047] * sep) >> 17;
        int xo = (sintable[angs&2047] * sep) >> 17;
        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[0]);
        glViewport(0, 0, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        picrot_view(posxs - xo, posys - yo, poszs, angs - asep, aspw, stereo == 2 ? asph*2 : asph);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[1]);
        glViewport(0, 0, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        picrot_view(posxs + xo, posys + yo, poszs, angs + asep, aspw, stereo == 2 ? asph*2 : asph);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

        glMatrixMode( GL_MODELVIEW );
        glLoadIdentity( );

        glViewport(0, 0, screenwidth, screenheight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(0);
        if (stereo == 2) {
            for (i = 0; i < 2; i++) {
                float ofs = i == 0 ? 0.0 : 0.5;
                glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0);
                glVertex3f(0.0+ofs, 0.0, 0.0);

                glTexCoord2f(1.0, 0.0);
                glVertex3f(0.5+ofs, 0.0, 0.0);

                glTexCoord2f(1.0, 1.0);
                glVertex3f(0.5+ofs, 1.0, 0.0);

                glTexCoord2f(0.0, 1.0);
                glVertex3f(0.0+ofs, 1.0, 0.0);
                glEnd();
            }

        } else {
            glDisable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            for (i = 0; i < 2; i++) {
                if (i == 0)
                    glColor4f(1.0, 0.0, 0.0, 1.0);
                else {
                    glEnable(GL_BLEND);
                    glColor4f(0.0, 1.0, 1.0, 1.0);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                }
                glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0);
                glVertex3f(0.0, 0.0, 0.0);

                glTexCoord2f(1.0, 0.0);
                glVertex3f(1.0, 0.0, 0.0);

                glTexCoord2f(1.0, 1.0);
                glVertex3f(1.0, 1.0, 0.0);

                glTexCoord2f(0.0, 1.0);
                glVertex3f(0.0, 1.0, 0.0);
                glEnd();
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1.0, 1.0, 1.0, 1.0);
        }
    }
}


/* -------------------------------------------------------------- lifecycle */

int R_WantsLargeOverlayTexture(void) {
    return 1;   /* one 512x512 texture */
}

void R_InitOverlay(void) {
    if (largescreentexture)
        glGenTextures(1, &screenbuffertexture);
    else
        glGenTextures(72, screenbuffertextures);
    texturecreationneeded = 1;
    glDrawBuffer(GL_BACK);
}

void R_ClearScreen(void) {
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}

/* --------------------------------------------------------------- textures */

void R_LoadWallTexture(int i, unsigned char *texels, int bmpkind_,
                       int wrapmode, int minfilt, int magfilt) {
    unsigned char *rgba = malloc(64*64*4);

    if (!rgba) fatal_error("Out of memory converting wall texture %d", i);

    glGenTextures(1, &texName[i]);
    glBindTexture(GL_TEXTURE_2D, texName[i]);
    checkGLStatus();

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                    wrapmode ? GL_REPEAT : GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magfilt);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minfilt);
    checkGLStatus();

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    checkGLStatus();

    TextureConvert(texels, rgba, bmpkind_);

    if (anisotropic)
        SetAnisotropic();
    BuildMipmaps((Uint32*)rgba, 64, 64, 1, 10);
    checkGLStatus();

    free(rgba);
}

void R_LoadGameOverSprite(unsigned char *texels) {
    unsigned char *rgba = malloc(64*64*4);

    if (!rgba) return;

    glGenTextures(1, &gameoversprite);
    glBindTexture(GL_TEXTURE_2D, gameoversprite);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, partialfilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fullfilter);

    TextureConvert(texels, rgba, 4);

    if (anisotropic)
        SetAnisotropic();
    BuildMipmaps((Uint32*)rgba, 64, 64, 1, 10);
    checkGLStatus();

    free(rgba);
}

void R_UpdateWallTexture(int walnum) {
    unsigned char *rgba = malloc(64*64*4);

    if (!rgba) return;

    glBindTexture(GL_TEXTURE_2D, texName[walnum-1]);
    checkGLStatus();

    if (walnum == map)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    TextureConvert(walseg[walnum-1], rgba, bmpkind[walnum]);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    BuildMipmaps((Uint32*)rgba, 64, 64, 1, 10);
    checkGLStatus();

    free(rgba);
}

int R_HaveTransitionTextures(void) {
    return 1;
}

void R_MakeTransitionTextures(void) {
    /* Set up transition textures between walls properly. */
    TransitionTexture(0, 1, 2);
    TransitionTexture(2, 3, 0);
    TransitionTexture(6, 7, 8);
    TransitionTexture(8, 9, 6);

    /* Set up end of game rainbow. */
    TransitionTexture(424, 425, 426);
    TransitionTexture(47, 424, 425);
    TransitionTexture(425, 426, 47);
}

#ifdef ENABLE_HIRES_TEXTURES
void R_ReplaceWallTexture(int i, struct _wallparam *wp,
                          int wrapmode, int minfilt, int magfilt) {
    wallparam *cwparam = (wallparam *)wp;
    imgcache *cache = LoadImageCache(cwparam->texreplace, wrapmode,
                                     minfilt, magfilt);
    texName[i] = cache->texnum;
    if (cwparam->tch == -1) cwparam->tch = cache->w;
    walltexcoord[i][0] = ((double)cwparam->tcl / (double)cache->w);
    walltexcoord[i][1] = ((double)cwparam->tch / (double)cache->w);
}
#endif

/* ---------------------------------------------------------------- palette */

void R_SetOverlayPalette(int darkened) {
    if (darkened)
        setdarkenedpalette();
    else
        settransferpalette();
}

void R_SetOverlayPaletteRange(K_UINT16 start, K_UINT16 count,
                              unsigned char *cols) {
    updateoverlaypalette(start, count, cols);
}

void R_FadeChanged(void) {
    /* The GL path applies redfactor/greenfactor/bluefactor per primitive as
       a vertex colour, so there is nothing to do here. */
}

/* ------------------------------------------------------------- 3D geometry */

void R_BeginScene(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs,
                  double aspwv, double asphv, int yy) {
    GLdouble xmin, xmax, ymin, ymax;

    glDisable(GL_LIGHTING);

    /* Draw floor and roof (save time by clearing to one of them, and drawing
       only one rectangle)... */

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        glClearColor( palette[0x85*3]/64.0*redfactor,
                      palette[0x85*3+1]/64.0*greenfactor,
                      palette[0x85*3+2]/64.0*bluefactor, 0 );
    else
        glClearColor( palette[0x84*3]/64.0*redfactor,
                      palette[0x84*3+1]/64.0*greenfactor,
                      palette[0x84*3+2]/64.0*bluefactor, 0 );

    glDepthMask(1);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLfloat)360, 0.0, (GLfloat)240, -1.0, 1.0);

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );

    glDisable(GL_DEPTH_TEST);
    glDepthMask(0);

    glBegin(GL_QUADS);

    glColor3f(palette[0xe3*3]/64.0*redfactor,
              palette[0xe3*3+1]/64.0*greenfactor,
              palette[0xe3*3+2]/64.0*bluefactor);
    glVertex3i(0,240,0);
    glVertex3i(0,240-yy/90,0);
    glVertex3i(360,240-yy/90,0);
    glVertex3i(360,240,0);
    glEnd();

    checkGLStatus();

    /* Switch to labyrinth view transformations. */

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    xmax = neardist * tan(M_PI*0.25);
    xmin = -xmax;

    ymin = xmin * 0.75;
    ymax = -ymin;

    xmax *= aspwv; xmin *= aspwv;
    ymax *= asphv; ymin *= asphv;

    glFrustum(xmin, xmax, ymin, ymax, neardist, 98304.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(posxs, posys, poszs*16.0,
              posxs+sintable[(angs+512)&2047], posys+sintable[angs],
              poszs*16.0,
              0.0,0.0,-1.0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(1);
}

void R_DrawWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                K_INT16 j, double v0, double v1,
                int shaded, int transparent) {
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

    if (transparent)
        glEnable(GL_BLEND);
    glBindTexture(GL_TEXTURE_2D,texName[j]);
    glBegin(GL_QUADS);
    if (transparent)
        glColor4f(1.0,1.0,1.0,0.0); /* Must draw invisible walls to make
                                       stuff behind invisible; see board 15. */
    else if (shaded)
        glColor3f(redfactor,greenfactor,bluefactor);
    else
        glColor3f(0.9*redfactor,0.9*greenfactor,0.9*bluefactor);

    glTexCoord2f(0.0,v0);
    glVertex3i(x1,y1,0);
    glTexCoord2f(1.0,v0);
    glVertex3i(x1,y1,1024);
    glTexCoord2f(1.0,v1);
    glVertex3i(x2,y2,1024);
    glTexCoord2f(0.0,v1);
    glVertex3i(x2,y2,0);
    glEnd();
    if (transparent)
        glDisable(GL_BLEND);
    checkGLStatus();
}

void R_DrawSplitWall(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     int k, int shaded) {
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);

    glBindTexture(GL_TEXTURE_2D,splitTexName[k][0]);
    glBegin(GL_QUADS);
    if (shaded)
        glColor3f(redfactor,greenfactor,bluefactor);
    else
        glColor3f(0.9*redfactor,0.9*greenfactor,0.9*bluefactor);

    glTexCoord2f(0.0,1.0/64.0);
    glVertex3i(x1,y1,0);
    glTexCoord2f(1.0,1.0/64.0);
    glVertex3i(x1,y1,1024);
    glTexCoord2f(1.0,33.0/64.0);
    glVertex3i((x1+x2)>>1,(y1+y2)>>1,1024);
    glTexCoord2f(0.0,33.0/64.0);
    glVertex3i((x1+x2)>>1,(y1+y2)>>1,0);
    glEnd();

    glBindTexture(GL_TEXTURE_2D,splitTexName[k][1]);
    glBegin(GL_QUADS);
    glTexCoord2f(0.0,31.0/64.0);
    glVertex3i((x1+x2)>>1,(y1+y2)>>1,0);
    glTexCoord2f(1.0,31.0/64.0);
    glVertex3i((x1+x2)>>1,(y1+y2)>>1,1024);
    glTexCoord2f(1.0,63.0/64.0);
    glVertex3i(x2,y2,1024);
    glTexCoord2f(0.0,63.0/64.0);
    glVertex3i(x2,y2,0);
    glEnd();
    checkGLStatus();
}

void R_EndWalls(void) {
    glDepthMask(0);
}

void R_DrawBillboard(K_INT32 x1, K_INT32 y1, K_INT32 x2, K_INT32 y2,
                     K_INT16 j, double v0, double v1,
                     K_INT16 ang, K_INT16 playerang) {
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    if (ang) {
        glPushMatrix();
        glTranslatef((x1+x2)/2.0,(y1+y2)/2.0,512);
        glRotatef(ang/2048.0*360.0,sintable[(playerang+512)&2047],
                  sintable[playerang],0.0);
        glTranslatef(-(x1+x2)/2.0,-(y1+y2)/2.0,-512);
    }

    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,texName[j]);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);
    glTexCoord2f(0.0,v0);
    glVertex3i(x1,y1,0);
    glTexCoord2f(1.0,v0);
    glVertex3i(x1,y1,1024);
    glTexCoord2f(1.0,v1);
    glVertex3i(x2,y2,1024);
    glTexCoord2f(0.0,v1);
    glVertex3i(x2,y2,0);
    glEnd();
    checkGLStatus();

    if (ang)
        glPopMatrix();

    glDisable(GL_BLEND);
}

void R_DrawFloorSprite(K_UINT16 x, K_UINT16 y, K_INT16 j) {
    glBindTexture(GL_TEXTURE_2D,texName[j]);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);

    glTexCoord2f(0.0,walltexcoord[j][0]);
    glVertex3i(x-512,y-512,1024);
    glTexCoord2f(1.0,walltexcoord[j][0]);
    glVertex3i(x-512,y+512,1024);
    glTexCoord2f(1.0,walltexcoord[j][1]);
    glVertex3i(x+512,y+512,1024);
    glTexCoord2f(0.0,walltexcoord[j][1]);
    glVertex3i(x+512,y-512,1024);
    glEnd();
    glDisable(GL_BLEND);
}

void R_EndScene(void) {
    glDepthMask(1);
    glDisable(GL_LIGHTING);
}

/* -------------------------------------------------------------- 2D sprites */

void R_DrawSprite2D(K_INT16 x, K_INT16 y, K_INT16 siz, K_INT16 ang,
                    K_INT16 j, int usegameoversprite) {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-(virtualscreenwidth-360)/2,
            360+(virtualscreenwidth-360)/2,
            -(virtualscreenheight-240)/2,
            240+(virtualscreenheight-240)/2, -1.0, 1.0);

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );
    glTranslatef(x,240.0-y,0.0);
    glScalef(siz/256.0,siz/256.0,siz/256.0);
    glRotatef(((GLfloat)ang)/2048.0*360.0,0.0,0.0,1.0);
    glTranslatef(-32.0,-32.0,0.0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    if (usegameoversprite)
        glBindTexture(GL_TEXTURE_2D,gameoversprite); /* Horrible kludge. */
    else
        glBindTexture(GL_TEXTURE_2D,texName[j]);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);
    glTexCoord2f(1.0,walltexcoord[j][0]);
    glVertex3f(0.0,0.0,0);
    glTexCoord2f(1.0,walltexcoord[j][1]);
    glVertex3f(64.0,0.0,0);
    glTexCoord2f(0.0,walltexcoord[j][1]);
    glVertex3f(64.0,64.0,0);
    glTexCoord2f(0.0,walltexcoord[j][0]);
    glVertex3f(0.0,64.0,0);
    glEnd();
    checkGLStatus();

    glDisable(GL_BLEND);
}

/* ------------------------------------------------------------- screenshots */

int R_ReadPixelsBGR(unsigned char *dst, int w, int h) {
    glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, dst);
    return 0;
}

void R_DrawVolumeBar(int vol, int type, float level) {
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 360.0, -15+30*type, 225+30*type, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);
    glColor4f(0,0,0,level);
    glVertex2s(96,110);
    glVertex2s(96,130);
    glColor4f(0.25,0.25,0.25,level);
    glVertex2s(224,130);
    glVertex2s(224,110);
    if (type)
        glColor4f(0,0,255,level);
    else
        glColor4f(255,0,0,level);
    glVertex2s(96,110);
    glVertex2s(96,130);
    glVertex2s(96+(vol>>1),130);
    glVertex2s(96+(vol>>1),110);
    glEnd();
    glDisable(GL_BLEND);
    checkGLStatus();
}
