#ifndef LAB3D_AMIGAOS_H
#define LAB3D_AMIGAOS_H

/*
 * Small compatibility shims for the AmigaOS build.
 *
 * The Amiga port has no OpenGL, but a few OpenGL *names* survive in the shared
 * code as plain identifiers: the texture handle arrays, the fade factors and
 * the texture filter selectors that the settings file and the setup menu talk
 * about.  Giving them ordinary C types here keeps lab3d.cfg compatible with
 * the desktop builds and avoids sprinkling #ifdefs through the game logic.
 *
 * Texture filtering itself is not implemented on the Amiga: the software
 * rasteriser point-samples, which is what a 68k can afford.  The filter
 * settings are still parsed and stored so that a config file written on a PC
 * loads without complaint; the setup menu hides the filtering options.
 */

typedef float          GLfloat;
typedef double         GLdouble;
typedef int            GLint;
typedef unsigned int   GLuint;
typedef unsigned int   GLenum;

/* Filter selectors.  Values match OpenGL's so that saved settings and
   wallparams.ini files stay portable. */
#define GL_NEAREST                 0x2600
#define GL_LINEAR                  0x2601
#define GL_NEAREST_MIPMAP_NEAREST  0x2700
#define GL_LINEAR_MIPMAP_NEAREST   0x2701
#define GL_NEAREST_MIPMAP_LINEAR   0x2702
#define GL_LINEAR_MIPMAP_LINEAR    0x2703

/* Colour depth selectors, likewise only used as settings values. */
#define GL_RGBA                    0x1908
#define GL_RGBA4                   0x8056
#define GL_RGBA8                   0x8058

/* libnix has no strcasecmp prototype in <string.h> for -noixemul. */
int strcasecmp(const char *a, const char *b);
int strncasecmp(const char *a, const char *b, size_t n);

#endif /* LAB3D_AMIGAOS_H */
