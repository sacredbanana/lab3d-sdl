#ifndef LAB3D_AMIGA_ZLIB_H
#define LAB3D_AMIGA_ZLIB_H

/* See src/amiga/amiga_gzstub.c: the Amiga build handles uncompressed demo
   files only, so only the gz* calls demo.c makes are declared here. */

#include <stdio.h>

typedef FILE *gzFile;

gzFile gzopen(const char *path, const char *mode);
int    gzclose(gzFile f);
int    gzread(gzFile f, void *buf, unsigned n);
int    gzwrite(gzFile f, const void *buf, unsigned n);
long   gzseek(gzFile f, long off, int whence);
int    gzdirect(gzFile f);
int    gzflush(gzFile f, int flush);
long   gztell(gzFile f);
int    gzeof(gzFile f);

#endif
