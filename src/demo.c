#include <stdio.h>
#include <zlib.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "demo.h"

#ifdef WIN32
static void truncate_file(FILE* f) {
    int fd = _fileno(f);
    fflush(f);
    _chsize_s(fd, _ftelli64(fd));
}
#else
#include <sys/types.h>
#include <unistd.h>
static void truncate_file(FILE* f) {
    int fd = fileno(f);
    fflush(f);
    ftruncate(fd, lseek(fd, 0, SEEK_CUR));
}

#endif

static K_UINT16 get16(unsigned char* a) {
    return a[0] | (a[1] << 8);
}

static K_UINT16 set16(unsigned char* a, K_UINT16 v) {
    *a++ = v;
    *a = v >> 8;
    return v;
}

/* Dynamically allocated during playback */
typedef struct {
    void* ptr;
    int pos;
    int elemsize;
    int cnt;
} vartrack_t;

struct demofile {
    gzFile gzfil;
    FILE* rawfil;

    vartrack_t* vars;

    /* Allocated sequentially */
    unsigned char* cur_data;
    unsigned char* delta_buf;
    unsigned char* rle_buf;

    K_UINT32 totalclock;

    int cursize;
    int buffer_size;

    int nvars;

    unsigned char format;
    unsigned char recording;
    unsigned char compressed;
};


static int demofile_read(demofile_t* d, void* data, int size);
static int demofile_write(demofile_t* d, const void* data, int size);
static int demofile_write_hdr(demofile_t* d, int, int);
static void demofile_seek(demofile_t* d, int amt);

static int demofile_read_fileheader(demofile_t* d, demo_vardef_t*, const char* filename);
static int demofile_write_fileheader(demofile_t* d, demo_vardef_t*, const char* filename);

demofile_t* demofile_open(const char* filename, demo_vardef_t* vars, int record, int format) {
    demofile_t* d = (demofile_t*)malloc(sizeof(demofile_t));
    if (!d) return NULL;
    memset(d, 0, sizeof(*d));

    d->recording = !!record;
    if (record) {
        if (format < 0 || format > 1)
            return NULL;

        d->format = format;
        if (demofile_write_fileheader(d, vars, filename)) {
            fprintf(stderr, "Recording demo to %s (%d raw bytes per frame)\n", filename, d->buffer_size);
            return d;
        }
    } else {
        if (demofile_read_fileheader(d, vars, filename)) {
            fprintf(stderr, "Playing demo from %s (%d raw bytes per frame)\n", filename, d->buffer_size);
            return d;
        }
    }

    demofile_close(d);
    return NULL;
}

void demofile_close(demofile_t* d) {
    if (d->gzfil)
        gzclose(d->gzfil);
    if (d->rawfil) {
        fclose(d->rawfil);
    }
    if (d->vars) free(d->vars);
    if (d->cur_data) free(d->cur_data);
    free(d);
}

int demofile_rewindable(demofile_t* d) {
    return d->format == 1 && !d->compressed;
}

static int demofile_read(demofile_t* d, void* data, int size) {
    int r;
    if (d->rawfil)
        r = fread(data, 1, size, d->rawfil);
    else
        r = gzread(d->gzfil, data, size);

    if (r > 0)
        d->cursize += r;
    return r;
}

static void demofile_seek(demofile_t* d, int amt) {
    if (d->rawfil)
        fseek(d->rawfil, amt, 1);
    else
        gzseek(d->gzfil, amt, 1);
}

static int demofile_write(demofile_t* d, const void* data, int size) {
    if (d->gzfil)
        return gzwrite(d->gzfil, data, size);
    else
        return fwrite(data, 1, size, d->rawfil);

}

static int demofile_write_hdr(demofile_t* d, int size, int clock) {
    unsigned char hdr[4];
    set16(hdr, size);
    set16(hdr+2, clock);
    return demofile_write(d, hdr, 4);
}

static int demofile_read_frame(demofile_t* d) {
    int i, j;
    int readsize, timediff;
    unsigned char *delta, *rle, *delta_end, hdr[4];
    K_UINT32 *tdelta, *tdata;

    if ((j = demofile_read(d, hdr, 4)) < 4) {
        return -1;
    }

    readsize = get16(hdr);
    if (readsize < 2 || readsize > d->buffer_size + 2)
        return -1;

    readsize -= 2;

    rle = readsize == d->buffer_size ? d->delta_buf : d->rle_buf;
    if (demofile_read(d, rle, readsize) < readsize)
        return -1;

    timediff = get16(hdr + 2);
    if (readsize < d->buffer_size) {
        delta = d->delta_buf;
        delta_end = delta + d->buffer_size;
        i = readsize;
        while (i > 0 && delta < delta_end) {
            unsigned char r = *rle++;
            i--;
            if (r == 0) {
                unsigned int cnt = 0;
                int shift = 0;
                while (i > 0) {
                    unsigned char tc = *rle++;
                    i--;
                    cnt |= (tc & 0x7F) << shift;
                    shift += 7;
                    if (!(tc & 0x80)) break;
                }
                if (delta + cnt <= delta_end)
                    memset(delta, 0, cnt);
                delta += cnt;
            } else {
                *delta++ = r;
            }
        }
        if (delta < delta_end)
            memset(delta, 0, delta_end - delta);
    }

    tdata = (K_UINT32*)d->cur_data;
    tdelta = (K_UINT32*)d->delta_buf;
    i = (d->buffer_size + 3) >> 2;

    do {
        *tdata++ ^= *tdelta++;
    } while (--i);

    if (d->format == 1) {
        int prevsize;
        if (demofile_read(d, hdr, 2) < 2)
            return -1;

        prevsize = get16(hdr);
        if (prevsize != readsize + 2) {
            fprintf(stderr, "Frame end size marker does not match size!\n");
        }
    }

    return timediff;
}

int demofile_advance(demofile_t* d, int dir) {
    int rc, prevsize;
    unsigned char hdr[2];

    if (dir == -1) {
        if (!demofile_rewindable(d))
            return -1;

        demofile_seek(d, -2);
        demofile_read(d, hdr, 2);
        prevsize = get16(hdr);
        if (prevsize == 0)
            return -1;

        demofile_seek(d, -prevsize - 4);
        d->cursize = 0;
    }
    rc = demofile_read_frame(d);
    if (dir == -1) {
        demofile_seek(d, -d->cursize);
    }
    return rc;
}

void demofile_update_vars(demofile_t* d) {
    int i, j;
    unsigned char *cdata;
    vartrack_t* ct;

    for (j = 0, ct = d->vars; j < d->nvars; j++, ct++) {
        cdata = d->cur_data + ct->pos;
#if PL_BYTEORDER == PL_LIL_ENDIAN
        memcpy(ct->ptr, cdata, (size_t)ct->cnt * ct->elemsize);
#else
        /* Demo files are always little endian.  Unpack a byte at a time so
           that this works whatever the alignment of the variable is - the
           macro that used to live here referred to locals belonging to a
           different function and had never been compiled. */
        switch (ct->elemsize) {
        case 1:
            memcpy(ct->ptr, cdata, (size_t)ct->cnt);
            break;
        case 2: {
            K_UINT16 *dst = (K_UINT16 *)ct->ptr;
            for (i = 0; i < ct->cnt; i++, cdata += 2)
                dst[i] = (K_UINT16)(cdata[0] | (cdata[1] << 8));
            break;
        }
        case 4: {
            K_UINT32 *dst = (K_UINT32 *)ct->ptr;
            for (i = 0; i < ct->cnt; i++, cdata += 4)
                dst[i] = (K_UINT32)cdata[0]         |
                         ((K_UINT32)cdata[1] << 8)  |
                         ((K_UINT32)cdata[2] << 16) |
                         ((K_UINT32)cdata[3] << 24);
            break;
        }
        default:
            fprintf(stderr, "demo: unsupported variable size %d\n",
                    ct->elemsize);
            break;
        }
#endif
    }
}


static int demofile_read_fileheader(demofile_t* d, demo_vardef_t* vars, const char* filename) {
    int i, pos, nvars;
    demo_vardef_t *cvardef, *lastvardef;
    vartrack_t* cvartrack;
    unsigned char hdr[8];
    char name[256];

    d->gzfil = gzopen(filename, "rb9");
    if (!d->gzfil) return 0;

    d->compressed = !gzdirect(d->gzfil);

    if (demofile_read(d, hdr, 8) < 8)
        return 0;

    if (memcmp(hdr, "KENDEMO", 7) != 0)
        return 0;

    d->format = hdr[7];
    if (d->format > 1)
        return 0;

    if (demofile_read(d, hdr, 4) < 4)
        return 0;

    nvars = get16(hdr);
    d->buffer_size = get16(hdr + 2);

    if (nvars == 0)
        return 0;

    cvartrack = d->vars = (vartrack_t*)malloc(nvars * sizeof(vartrack_t));
    if (!cvartrack) return 0;

    memset(cvartrack, 0, nvars * sizeof(vartrack_t));

    cvardef = vars - 1;
    pos = 0;

    d->nvars = 0;
    i = nvars;
    do {
        int cnt, elemsize;

        if (demofile_read(d, hdr, 4) < 4) {
            fprintf(stderr, "%s: truncated (%d)\n", filename, d->nvars);
            return 0;
        }

        if (demofile_read(d, name, hdr[0]) < hdr[0]) {
            fprintf(stderr, "%s: truncated\n", filename);
            return 0;
        }

        name[hdr[0]] = 0;
        lastvardef = cvardef;
        elemsize = hdr[1];
        cnt = get16(hdr + 2);

        if (cnt == 0)
            return 0;

        /* Alignment & size check */
        switch(elemsize) {
            case 1: break;
            case 2: pos = (pos + 1) & ~1; break;
            case 4: pos = (pos + 3) & ~3; break;
            case 8: pos = (pos + 7) & ~7; break;
            default:
                fprintf(stderr, "%s: invalid size for variable '%s': %d\n", filename, name, elemsize);
                return 0;
        }

        /* Most likely scenario is that the variables from the demo are in the
         * same order as the variables we have here - so it's likely to match
         * the next demo_vardef_t.*/

        do {
            if (!(++cvardef)->name) cvardef = vars;
        } while (cvardef != lastvardef && !(strcmp(name, cvardef->name) == 0 && cvardef->elemsize == elemsize));

        if (cvardef == lastvardef) {
            fprintf(stderr, "%s: WARNING: unknown variable %s\n", filename, name);
        } else {
            cvartrack->ptr = cvardef->ptr;
            cvartrack->pos = pos;
            cvartrack->elemsize = elemsize;
            cvartrack->cnt = cnt;

            if (cnt > cvardef->cnt) {
                fprintf(stderr, "%s: WARNING: array exceeds buffer space %s[%d], alloc = %d\n", filename, name, cnt, cvardef->cnt);
                cvartrack->cnt = cvardef->cnt;
            }
            cvartrack++;
            d->nvars++;
        }
        pos += cnt * elemsize;
        if (pos > d->buffer_size) {
            fprintf(stderr, "%s: ERROR: variable %s extends beyond buffer size\n", filename, name);
            return 0;
        }
    } while (--i);

    if (d->format == 1) {
        demofile_read(d, hdr, 2);
    }

    /* Align the buffers on a 64-bit boundary so bulk XOR can use 64-bit operations */
    pos = (d->buffer_size + 7) & ~7;

    d->cur_data = malloc(pos * 3);
    if (!d->cur_data) return 0;
    memset(d->cur_data, 0, pos);

    d->delta_buf = d->cur_data + pos;
    d->rle_buf = d->delta_buf + pos;

    return 1;
}

static int demofile_write_fileheader(demofile_t* d, demo_vardef_t* vars, const char* filename) {
    int i, j, pos;
    demo_vardef_t* cvardef;
    vartrack_t* cvartrack;
    unsigned char hdr[8];

    for (i = 0, cvardef = vars; cvardef->name; cvardef++) {
        i++;
    }
    d->nvars = i;

    cvartrack = d->vars = (vartrack_t*)malloc(i * sizeof(vartrack_t));
    if (!cvartrack) return 0;

    memset(cvartrack, 0, i * sizeof(vartrack_t));

    pos = 0;
    for (cvardef = vars; cvardef->name; cvardef++) {

        cvartrack->ptr = cvardef->ptr;
        cvartrack->elemsize = cvardef->elemsize;
        cvartrack->cnt = cvardef->cnt;

        /* Alignment */
        j = cvardef->elemsize - 1;
        pos = (pos + j) & ~j;

        cvartrack->pos = pos;

        pos += cvardef->elemsize * cvardef->cnt;
        cvartrack++;
    }

    pos = (pos + 3) & ~3;

    d->buffer_size = pos;

    d->cur_data = malloc(pos * 3);
    if (!d->cur_data) return 0;
    memset(d->cur_data, 0, pos);

    d->delta_buf = d->cur_data + pos;
    d->rle_buf = d->delta_buf + pos;

    if (d->format == 1) {
        d->compressed = 0;
        d->rawfil = fopen(filename, "wb+");

        if (!d->rawfil) return 0;
    } else {
        d->compressed = 1;
        d->gzfil = gzopen(filename, "wb9");
        if (!d->gzfil) return 0;
    }

    strcpy((char *)hdr, "KENDEMO");
    hdr[7] = d->format;
    if (demofile_write(d, hdr, 8) < 8) return 0;

    set16(hdr+0, d->nvars);
    set16(hdr+2, d->buffer_size);
    if (demofile_write(d, hdr, 4) < 4) return 0;

    for (cvardef = vars; cvardef->name; cvardef++) {
        hdr[0] = strlen(cvardef->name);
        hdr[1] = cvardef->elemsize;
        set16(hdr+2, cvardef->cnt);
        if (demofile_write(d, hdr, 4) < 4) return 0;
        if (demofile_write(d, cvardef->name, hdr[0]) < hdr[0]) return 0;
    }
    if (d->format == 1) {
        set16(hdr, 0);
        demofile_write(d, hdr, 2);
    }
    return 1;
}

#define NOSWAP(X) (X)

/* The swap macros evaluate their argument more than once, so the value has to
   be read into a temporary first - passing *nptr++ straight in incremented the
   pointer several times per element on big endian machines. */
#define ENCODELOOP(size, type, swap)                \
    case size: {                                    \
        type* ndelta = (type*)(delta + ct->pos);    \
        type* nldata = (type*)(ldata + ct->pos);    \
        type* nptr = (type*)ct->ptr;                \
        do {                                        \
            type ov = *nldata;                      \
            type raw = *nptr++;                     \
            type nv = swap(raw);                    \
            *nldata++ = nv;                         \
            *ndelta++ = ov ^ nv;                    \
        } while (--i);                              \
    }                                               \
    break

void demofile_write_frame(demofile_t* d, int timediff) {
    int i, j, zc, writesize;
    unsigned char *ldata, *delta, *rle, *rle_end;
    unsigned char hdr[2];
    vartrack_t* ct;

    ldata = d->cur_data;
    delta = d->delta_buf;

    for (j = 0, ct = d->vars; j < d->nvars; j++, ct++) {
        i = ct->cnt;

        switch(ct->elemsize) {
            ENCODELOOP(1, unsigned char, NOSWAP);
            ENCODELOOP(2, K_UINT16, PL_SwapLE16);
            ENCODELOOP(4, K_UINT32, PL_SwapLE32);
            ENCODELOOP(8, uint64_t, PL_SwapLE64);
        }
    }

    delta = d->delta_buf;
    rle = d->rle_buf;
    rle_end = rle + d->buffer_size - 3;
    i = d->buffer_size;
    zc = 0;
    do {
        unsigned char bv = *delta++;
        if (bv == 0) {
            zc++;
        } else {
            if (zc) {
                *rle++ = 0;
                do {
                    *rle++ = 0x80 | (zc & 0x7f);
                    zc >>= 7;
                } while (zc && rle < rle_end);
                rle[-1] &= 0x7F;
            }
            if (rle >= rle_end)
                break;
            *rle++ = bv;
        }
    } while(--i);

    if (rle >= rle_end) {
        delta = d->delta_buf;
        writesize = d->buffer_size;
    } else {
        delta = d->rle_buf;
        writesize = rle - delta;
    }

    if (timediff >= 0xFFFF) timediff = 0xFFFF;

    demofile_write_hdr(d, writesize + 2, timediff);

    demofile_write(d, delta, writesize);
    if (d->format == 1) {
        set16(hdr, writesize + 2);
        demofile_write(d, hdr, 2);
    }
    if (d->rawfil) {
        truncate_file(d->rawfil);
    }
}

/*
static int demofile_read_frame(demofile_t* d, unsigned char* hdr, int dir) {
    int prev_ofs, readsize;

    if (demo->format == 1) {
        if (demofile_read(d, hdr, 2) < 2)
            return 0;
        prev_ofs = hdr[0] | (hdr[1] << 8);
        if (dir == -1) {
            fseek(d->rawinput, -prev_ofs - 6, 1);
        }
    }

    if (demofile_read(d, hdr, 4) < 4) return 0;
    if (hdr[0] == 0xFF && hdr[1] == 0xFF) {
    }
}
*/
