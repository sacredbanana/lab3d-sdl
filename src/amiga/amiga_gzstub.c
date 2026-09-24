/*
 * Minimal stand-in for the handful of zlib entry points demo.c uses.
 *
 * zlib is not part of the m68k-amigaos toolchain, and demo recording is a
 * side feature reached only from the command line (-play, -record, -recordx),
 * so rather than vendor the whole library the Amiga port supports the
 * uncompressed demo format only: -recordx writes plain files and -play reads
 * them.  A gzip-compressed demo is detected by its magic number and refused
 * with a clear message instead of being misread.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef FILE *gzFile;

gzFile gzopen(const char *path, const char *mode) {
    FILE *f;
    char m[4];
    int i = 0;

    /* zlib mode strings carry a compression level ("wb9"); stdio does not. */
    while (mode[i] && i < 3 && (mode[i] < '0' || mode[i] > '9')) {
        m[i] = mode[i];
        i++;
    }
    m[i] = 0;

    f = fopen(path, m);
    if (!f) return NULL;

    if (strchr(m, 'r')) {
        unsigned char magic[2];

        if (fread(magic, 1, 2, f) == 2 && magic[0] == 0x1f && magic[1] == 0x8b) {
            fprintf(stderr,
                    "%s is a compressed demo; the Amiga build only supports\n"
                    "uncompressed demos (record them with -recordx).\n", path);
            fclose(f);
            return NULL;
        }
        rewind(f);
    }
    return f;
}

int gzclose(gzFile f)                       { return f ? fclose(f) : 0; }
int gzread(gzFile f, void *buf, unsigned n) { return (int)fread(buf, 1, n, f); }
int gzwrite(gzFile f, const void *buf, unsigned n) {
    return (int)fwrite(buf, 1, n, f);
}
long gzseek(gzFile f, long off, int whence) {
    if (fseek(f, off, whence) != 0) return -1;
    return ftell(f);
}
int gzdirect(gzFile f)                      { (void)f; return 1; }
int gzflush(gzFile f, int flush)            { (void)flush; return fflush(f); }
long gztell(gzFile f)                       { return ftell(f); }
int gzeof(gzFile f)                         { return feof(f); }
