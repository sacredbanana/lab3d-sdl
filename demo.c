#include "lab3d.h"
#include "SDL_endian.h"
#include "zlib.h"

typedef struct {
    char* name;
    void* ptr;
    int sgn;
    int elemsize;
    int size;
    int cnt;
} vardef_t;

typedef struct {
    char* name;
    void* ptr;
    int elemsize;
    int realcnt;
    int cnt;
} vartrack_t;

typedef struct {
    int num;
    int posx, posy;
} soundevent_t;

static vardef_t vars[] = {

#define VAR(t) { #t, &t, 0, sizeof(t), sizeof(t), 1 }
#define ARRAY(t) { #t, &t[0], 0, sizeof(t[0]), sizeof(t), sizeof(t) / sizeof(t[0]) }
    VAR(boardnum),
    VAR(scorecount),
    VAR(scoreclock),
    { "board", &board[0][0], 0, 2, 8192, 4096 },
    VAR(skilevel),
    VAR(life),
    VAR(death),
    VAR(lifevests),
    VAR(lightnings),
    ARRAY(firepowers),
    VAR(bulchoose),
    ARRAY(keys),
    VAR(coins),
    VAR(compass),
    VAR(cheated),
    VAR(statusbar),
    VAR(statusbargoal),
    VAR(posx),
    VAR(posy),
    VAR(posz),
    VAR(ang),
    VAR(startx),
    VAR(starty),
    VAR(startang),
    VAR(angvel),
    VAR(vel),
    VAR(mxvel),
    VAR(myvel),
    VAR(svel),
    VAR(hvel),
    VAR(oldposx),
    VAR(oldposy),
    VAR(bulnum),
    ARRAY(bulang),
    ARRAY(bulkind),
    ARRAY(bulx),
    ARRAY(buly),
    ARRAY(bulstat),
    VAR(lastbulshoot),
    VAR(mnum),
    ARRAY(mposx),
    ARRAY(mposy),
    ARRAY(mgolx),
    ARRAY(mgoly),
    ARRAY(moldx),
    ARRAY(moldy),
    ARRAY(mstat),
    ARRAY(mshock),
    ARRAY(mshot),
    VAR(doorx),
    VAR(doory),
    VAR(doorstat),
    VAR(numwarps),
    VAR(justwarped),
    ARRAY(xwarp),
    ARRAY(ywarp),
    VAR(totalclock),
    VAR(purpletime),
    VAR(greentime),
    ARRAY(capetime),
    VAR(fadewarpval),
    VAR(fadehurtval),
    VAR(slottime),
    ARRAY(slotpos),
    VAR(owecoins),
    VAR(owecoinwait),
    ARRAY(hiscorenam),
    VAR(hiscorenamstat),
    VAR(explonum),
    ARRAY(explox),
    ARRAY(exploy),
    ARRAY(explotime),
    ARRAY(explostat),
    
    { NULL }
};
/*
  Commands:

  00001 time(v4)
  00010 x(snv7) y(snv7) ang(snv7)
  00011 posz(b6)
  00100 slot(b1) spos(b7)[3]
  00101 mnum(v7)
  00110 bulnum(v7)
  00111 loc(b12) val(b13)
  01000 val(b13)[4096]
  01001 <reserved>
  ...
  01111 <reserved>
  10 id(6) kind(nb5) x(snv7) y(snv7) ang(snv7)
  11 id(v6) stat(nb9) shot(nb6) x(snv7) y(snv7) shock(snv7) 
  
 */

/*
enum {
    D_TIME = 1,
    D_POS = 2,
    D_POSZ = 3,
    D_SLOT = 4,
    D_MNUM = 5,
    D_BULNUM = 6,
    D_BOARD = 7,
    D_BOARDALL = 8,

    D_BULLET = 2,
    D_MONST = 3
};
*/

demorec* demorecording;
demoplay* demoplaying;

typedef struct {
    K_UINT16 x, y;
    K_INT16 shock, shot, stat;
} monster_t;

typedef struct {
    K_UINT16 x, y;
    K_INT16 ang, kind;
} bullet_t;

struct demorec {
    gzFile gzoutput;
    FILE* rawoutput;

    int format;

    K_INT32 totalclock;
    int prevsize;

    int size_data;
    /* Allocated sequentially */
    unsigned char* last_data;
    /*unsigned char* hdr;*/
    unsigned char* delta_buf;
    unsigned char* rle_buf;
};

struct demoplay {
    gzFile gzinput;
    FILE* rawinput;

    int format;
    int startpos;

    vartrack_t* vars;
    int nvars;

    K_INT32 totalclock;
    int size_data;
    soundfunc sound;

    /* Allocated sequentially */
    unsigned char* last_data;
    /*unsigned char* hdr;*/
    unsigned char* delta_buf;
    unsigned char* rle_buf;
};

static void demo_write(demorec* d, const void* data, int size);
static void demo_write_hdr(demorec* d, const unsigned char* hdr, int size);
static int demo_read(demoplay* d, void* data, int size);
demoplay* demo_start_play(const char* filename) {
    int i, pos, fpos, cap;
    demoplay* d;
    vardef_t *cv, *ocv;
    vartrack_t* ct;
    unsigned char hdr[4];
    char name[256];

    d = (demoplay*)malloc(sizeof(demoplay));
    if (!d) goto memory_error;
    
    memset(d, 0, sizeof(*d));
    

    d->gzinput = gzopen(filename, "rb9");
    if (!d->gzinput) goto file_error;

    if (demo_read(d, hdr, 4) < 4) {
        goto read_error;
    }

    if (hdr[0] == 0xFF && hdr[1] == 0xFF) {
        gzclose(d->gzinput);
        d->gzinput = NULL;
        d->rawinput = fopen(filename, "rb");
        if (!d->rawinput) goto file_error;
        fseek(d->rawinput, 2, 0);
        d->format = 1;
        if (demo_read(d, hdr, 4) < 4) {
            goto read_error;
        }
    }
    cap = 128;
    ct = d->vars = (vartrack_t*)malloc(cap * sizeof(vartrack_t));
    if (!ct) goto memory_error_2;
    memset(ct, 0, cap * sizeof(vartrack_t));

    d->sound = ksaypan;
    d->nvars = 0;
    
    cv = vars - 1;
    pos = 0;
    fpos = 0;

    while (1) {
        if (pos != 0 && demo_read(d, hdr, 4) < 4) {
            fprintf(stderr, "%s: truncated (%d)\n", filename, d->nvars);
            goto read_error;
        }
        fpos += 4;
        if (hdr[0] == 0)
            break;
        if (demo_read(d, name, hdr[0]) < hdr[0]) {
            fprintf(stderr, "%s: truncated\n", filename);
            goto read_error;
        }
        fpos += hdr[0];
        name[hdr[0]] = 0;
        ocv = cv;
        ct->elemsize = hdr[1];
        ct->cnt = hdr[2] | (hdr[3] << 8);
        
        do {
            if (!(++cv)->name) cv = vars;
        } while (cv != ocv && !(strcmp(name, cv->name) == 0 && cv->elemsize == ct->elemsize));

        if (cv == ocv) {
            fprintf(stderr, "%s: WARNING: unknown variable %s\n", filename, name);
        } else {
            ct->name = cv->name;
            ct->realcnt = ct->cnt;
            ct->ptr = cv->ptr;
            if (ct->cnt > cv->cnt) {
                fprintf(stderr, "%s: WARNING: array exceeds buffer space %s[%d], alloc = %d\n", filename, name, ct->cnt, cv->cnt);
                ct->realcnt = cv->cnt;
            }
        }
        pos += ct->cnt * ct->elemsize;
        d->nvars++;
        ct++;
    }

    d->startpos = fpos;
    fprintf(stderr, "Playing demo from %s (%d raw bytes per frame)\n", filename, pos);

    pos = (pos + 3) & ~3;

    d->size_data = pos;

    d->last_data = malloc(pos * 3);
    if (!d->last_data) goto memory_error_3;
    memset(d->last_data, 0, pos);

    d->delta_buf = d->last_data + pos;
    d->rle_buf = d->delta_buf + pos;

    return d;

    /*free(d->last_data);*/
memory_error_3:

read_error:

    free(d->vars);
memory_error_2:
    if (d->gzinput)
        gzclose(d->gzinput);
    if (d->rawinput)
        fclose(d->rawinput);
file_error:

    free(d);
memory_error:

    return NULL;
}

demorec* demo_start_record(const char* filename, int format) {
    int i, pos;
    demorec* d;
    vardef_t* cv;
    unsigned char hdr[4];

    if (format < 0 || format > 1)
        return NULL;
    d = (demorec*)malloc(sizeof(demorec));
    if (!d) goto memory_error;
    memset(d, 0, sizeof(*d));

    d->format = format;
    if (d->format == 1) {
        d->rawoutput = fopen(filename, "wb");
        if (!d->rawoutput) goto file_error;
        hdr[0] = 0xFF; hdr[1] = 0xFF;
        demo_write(d, hdr, 2);
    } else {
        d->gzoutput = gzopen(filename, "wb9");
        if (!d->gzoutput) goto file_error;
    }

    pos = 0;
    for (cv = vars; cv->name; cv++) {
        unsigned char hdr[6];
        hdr[0] = strlen(cv->name);
        hdr[1] = cv->elemsize;
        hdr[2] = cv->cnt & 0xFF;
        hdr[3] = (cv->cnt >> 8) & 0xFF;
        demo_write(d, hdr, 4);
        demo_write(d, cv->name, hdr[0]);
        pos += cv->size;
    }
    hdr[0] = hdr[1] = hdr[2] = hdr[3] = 0;
    demo_write(d, hdr, 4);

    fprintf(stderr, "Recording demo to %s (%d raw bytes per frame)\n", filename, pos);

    d->size_data = pos;

    d->last_data = malloc(pos * 3);
    if (!d->last_data) goto memory_error_2;
    memset(d->last_data, 0, pos);

    d->delta_buf = d->last_data + pos;
    d->rle_buf = d->delta_buf + pos;

    return d;

    /*free(d->last_data);*/
 memory_error_2:

    if (d->gzoutput)
        gzclose(d->gzoutput);
    if (d->rawoutput)
        fclose(d->rawoutput);
 file_error:

    free(d);
 memory_error:

    return NULL;
}

static void demo_write(demorec* d, const void* data, int size) {
    if (d->gzoutput)
        gzwrite(d->gzoutput, data, size);
    else
        fwrite(data, 1, size, d->rawoutput);
        
}
static void demo_write_hdr(demorec* d, const unsigned char* hdr, int size) {
    char xhdr[2];
    if (d->format == 1) {
        xhdr[0] = d->prevsize & 0xFF;
        xhdr[1] = (d->prevsize >> 8) & 0xFF;
        demo_write(d, xhdr, 2);
    }
    if (size)
        demo_write(d, hdr, size);
        
}

static int demo_read(demoplay* d, void* data, int size) {
    int rc;
    if (d->gzinput)
        rc =  gzread(d->gzinput, data, size);
    else
        rc = fread(data, 1, size, d->rawinput);
    /*
    int i = 0;
    printf("read %d:", size);
    for(i = 0; i < size; i++) {
        unsigned char c = ((unsigned char*)(data))[i];
        printf(" %02x", c);
    }
    printf("\n");
    */
    return rc;

}

void demo_close_record(demorec* d) {
    /*demo_write_align(d);*/
    if (d->format == 1) {
        demo_write_hdr(d, NULL, 0);
    }

    if (d->gzoutput)
        gzclose(d->gzoutput);
    if (d->rawoutput)
        fclose(d->rawoutput);
    free(d->last_data);
    free(d);
}

void demo_close_play(demoplay* d) {
    /*demo_write_align(d);*/
    if (d->gzinput)
        gzclose(d->gzinput);
    if (d->rawinput)
        fclose(d->rawinput);

    free(d->vars);
    free(d->last_data);
    free(d);
}

void demo_time_jump(demorec* d, int totalclock) {
    d->totalclock = totalclock;
}

void demo_sound(demorec* d, int which, int pan) {
    unsigned char data[6];
    data[0] = data[1] = 0xFF;
    data[2] = 1; data[3] = which;
    data[4] = pan & 0xFF;
    data[5] = pan >> 8;
    demo_write_hdr(d, data, 6);
    d->prevsize = 6;
}

void demo_set_soundfunc(demoplay* p, soundfunc f) {
    p->sound = f;
}

#define NOSWAP(X) (X)

#define ENCODELOOP(size, type, swap)            \
    case size: {                                \
        type* ndelt = (type*)delta;             \
        type* nldata = (type*)ldata;            \
        type* nptr = (type*)cv->ptr;            \
        do {                                    \
            type ov = *nldata;                  \
            type nv = swap(*nptr++);            \
            *ndelt++ = ov ^ nv;                 \
            if (ov != nv) *nldata = nv;         \
            (nldata)++;                         \
        } while (--i);                          \
        delta = (unsigned char*)ndelt;          \
        ldata = (unsigned char*)nldata;         \
    }                                           \
    break

void demo_update(demorec* d, int totalclock) {
    int i, zc, writesize, timediff;
    unsigned char *ldata, *delta, *rle, *rle_end, hdr[4];
    vardef_t* cv;
    
    ldata = d->last_data;
    delta = d->delta_buf;
    
    for (cv = vars; cv->name; cv++) {
        i = cv->cnt;
        switch(cv->elemsize) {
            ENCODELOOP(1, unsigned char, NOSWAP);
            ENCODELOOP(2, K_UINT16, SDL_SwapLE16);
            ENCODELOOP(4, K_UINT32, SDL_SwapLE32);
            default:
                fatal_error("unknown size: %d (%s)", cv->elemsize, cv->name);
        }
    }
    delta = d->delta_buf;
    rle = d->rle_buf;
    rle_end = rle + d->size_data - 3;
    i = d->size_data;
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
        writesize = d->size_data;
        hdr[3] = 0x80;
    } else {
        delta = d->rle_buf;
        writesize = rle - delta;
        hdr[3] = 0;
    }
    timediff = totalclock - d->totalclock;
    d->totalclock = totalclock;
    hdr[0] = timediff & 0xFF;
    hdr[1] = (timediff >> 8) & 0xFF;
    hdr[2] = writesize & 0xFF;
    hdr[3] |= (writesize >> 8) & 0x7F;
    demo_write_hdr(d, hdr, 4);
    demo_write(d, delta, writesize);
    d->prevsize = writesize + 4;
}

#define DECODELOOP(size, type, swap)            \
    case size: {                                \
        type* ndelt = (type*)delta;             \
        type* nptr = (type*)ct->ptr;            \
        do {                                    \
            type ov = *nldata++;                \
            *nptr++ = swap(ov);                 \
        } while (--i);                          \
        delta = (unsigned char*)ndelt;          \
        ldata = (unsigned char*)nldata;         \
    }                                           \
    break

static int demo_read_frame(demoplay* d, int dir) {
    int i, j;
    int readsize, userle, timediff;
    unsigned char *ldata, *delta, *rle, *delta_end, hdr[4];
    vartrack_t* ct;
    K_UINT32 *tdelta, *tdata;

    int prev_ofs;
    if (d->format == 1) {
        if (demo_read(d, hdr, 2) < 2) {
            return -1;
        }
        prev_ofs = hdr[0] | (hdr[1] << 8);
        if (dir == -1) {
            fseek(d->rawinput, -prev_ofs - 2, 1);
            if (prev_ofs == 0) return -1;
        }
    } else if (dir == -1) {
        return -1;
    }

    if (demo_read(d, hdr, 4) < 4) {
        if (d->format == 1)
            fseek(d->rawinput, -2, 1);
        return -1;
    }

    if (hdr[0] == 0xFF && hdr[1] == 0xFF) {
        if (hdr[2] == 1) {
            int which = hdr[3];
            int pan;
            if (demo_read(d, hdr, 2) < 2) return -1;
            pan = hdr[0] | (hdr[1] << 8);
            d->sound(which, pan, 0);
            timediff = 0;
            readsize = 2;
        } else {
        }
    } else {
    
        timediff = hdr[0] | (hdr[1] << 8);
        readsize = hdr[2] | (hdr[3] << 8);
        if (readsize & 0x8000) {
            rle = d->delta_buf;
            userle = 0;
            readsize &= 0x7FFF;
        } else {
            rle = d->rle_buf;
            userle = 1;
        }
        if (readsize > d->size_data) return -1;
        if (demo_read(d, rle, readsize) < readsize) return -1;
        if (userle) {
            delta = d->delta_buf;
            delta_end = delta + d->size_data;
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

        tdata = (K_UINT32*)d->last_data;
        tdelta = (K_UINT32*)d->delta_buf;
        i = d->size_data >> 2;

        do {
            *tdata++ ^= *tdelta++;
        } while (--i);

        ldata = d->last_data;
        for (j = 0, ct = d->vars; j < d->nvars; j++, ct++) {
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
            i = ct->cnt * ct->elemsize;
            memcpy(ct->ptr, ldata, i);
            ldata += i;
#else
            i = ct->cnt;

            switch(ct->elemsize) {
                DECODELOOP(1, unsigned char, NOSWAP);
                DECODELOOP(2, K_UINT16, SDL_SwapLE16);
                DECODELOOP(4, K_UINT32, SDL_SwapLE32);
                default:
                    fatal_error("unknown size: %d (%s)", ct->elemsize, ct->name);
            }
#endif
        }
    }

    if (d->format == 1 && dir == -1) {
        fseek(d->rawinput, -readsize - 6, 1);
    }
        


    return timediff;
}

int demo_update_play(demoplay* d, int dir) {
    while (1) {
        int rc = demo_read_frame(d, dir);
        if (rc != 0) return rc;
    }
}
/*
static int demo_read_frame(demoplay* d, unsigned char* hdr, int dir) {
    int prev_ofs, readsize;
    
    if (demo->format == 1) {
        if (demo_read(d, hdr, 2) < 2)
            return 0;
        prev_ofs = hdr[0] | (hdr[1] << 8);
        if (dir == -1) {
            fseek(d->rawinput, -prev_ofs - 6, 1);
        }
    }

    if (demo_read(d, hdr, 4) < 4) return 0;
    if (hdr[0] == 0xFF && hdr[1] == 0xFF) {
    }
}
*/
