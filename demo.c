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
    gzFile output;

    

    K_INT32 totalclock;
    /*
    K_UINT32 write_nbits;
    K_UINT32 write_bucket;

    K_UINT16 posx, posy;
    K_INT16 posz, ang;
    K_INT16 slottime;
    K_INT16 slotpos[3];
    
    K_INT16 bulnum, mnum;

    K_INT16 board[4096];

    monster_t monsters[512];
    bullet_t bullets[64]; 
    */

    int size_data;
    /* Allocated sequentially */
    unsigned char* last_data;
    /*unsigned char* hdr;*/
    unsigned char* delta_buf;
    unsigned char* rle_buf;
};

struct demoplay {
    gzFile input;
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
static int demo_read(demoplay* d, void* data, int size);

demoplay* demo_start_play(const char* filename) {
    int i, pos, cap;
    demoplay* d;
    vardef_t *cv, *ocv;
    vartrack_t* ct;
    unsigned char hdr[4];
    char name[256];

    d = (demoplay*)malloc(sizeof(demoplay));
    if (!d) goto memory_error;
    
    memset(d, 0, sizeof(*d));
    
    d->input = gzopen(filename, "rb9");
    if (!d->input) goto file_error;

    cap = 128;
    ct = d->vars = (vartrack_t*)malloc(cap * sizeof(vartrack_t));
    if (!ct) goto memory_error_2;
    memset(ct, 0, cap * sizeof(vartrack_t));

    d->sound = ksaypan;
    d->nvars = 0;
    
    cv = vars - 1;
    pos = 0;
    while (1) {
        if (demo_read(d, hdr, 4) < 4) {
            fprintf(stderr, "%s: truncated (%d)\n", filename, d->nvars);
            goto read_error;
        }
        if (hdr[0] == 0)
            break;
        if (demo_read(d, name, hdr[0]) < hdr[0]) {
            fprintf(stderr, "%s: truncated\n", filename);
            goto read_error;
        }
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

    gzclose(d->input);
file_error:

    free(d);
memory_error:

    return NULL;
}

demorec* demo_start_record(const char* filename) {
    int i, pos;
    demorec* d;
    vardef_t* cv;
    unsigned char hdr[4];

    d = (demorec*)malloc(sizeof(demorec));
    if (!d) goto memory_error;
    
    memset(d, 0, sizeof(*d));
    
    d->output = gzopen(filename, "wb9");
    if (!d->output) goto file_error;

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

    gzclose(d->output);
 file_error:

    free(d);
 memory_error:

    return NULL;
}

static void demo_write(demorec* d, const void* data, int size) {
    gzwrite(d->output, data, size);
}
static int demo_read(demoplay* d, void* data, int size) {
    return gzread(d->input, data, size);
}

void demo_close_record(demorec* d) {
    /*demo_write_align(d);*/
    gzclose(d->output);
    free(d->last_data);
    free(d);
}

void demo_close_play(demoplay* d) {
    /*demo_write_align(d);*/
    gzclose(d->input);
    free(d->vars);
    free(d->last_data);
    free(d);
}

void demo_time_jump(demorec* d) {
    d->totalclock = totalclock;
}

void demo_sound(demorec* d, int which, int pan) {
    unsigned char data[6];
    data[0] = data[1] = 0xFF;
    data[2] = 1; data[3] = which;
    data[4] = pan & 0xFF;
    data[5] = pan >> 8;
    demo_write(d, data, 6);
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

void demo_update(demorec* d) {
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
    demo_write(d, hdr, 4);
    demo_write(d, delta, writesize);
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

int demo_update_play(demoplay* d) {
    int i, j;
    int readsize, userle, timediff;
    unsigned char *ldata, *delta, *rle, *delta_end, hdr[4];
    vartrack_t* ct;
    K_UINT32 *tdelta, *tdata;

    while (1) {
        if (demo_read(d, hdr, 4) < 4) return -1;
        if (hdr[0] == 0xFF && hdr[1] == 0xFF) {
            if (hdr[2] == 1) {
                int which = hdr[3];
                int pan;
                if (demo_read(d, hdr, 2) < 2) return -1;
                pan = hdr[0] | (hdr[1] << 8);
                d->sound(which, pan, 0);
            }
        } else 
            break;
    }
    
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
        while (readsize > 0 && delta < delta_end) {
            unsigned char r = *rle++;
            readsize--;
            if (r == 0) {
                unsigned int cnt = 0;
                int shift = 0;
                while (readsize > 0) {
                    unsigned char tc = *rle++;
                    readsize--;
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
    return timediff;
}

#if 0

void demo_write_8(demorec* d, unsigned char v) {
    demo_write(d, &v, 1);
}


void demo_write_16(demorec* d, K_UINT16 v) {
    unsigned char data[2];
    data[0] = v >> 8;
    data[1] = v;
    demo_write(d, data, 2);
}
void demo_write_32(demorec* d, K_UINT32 v) {
    unsigned char data[4];
    data[0] = v >> 24;
    data[1] = v >> 16;
    data[2] = v >> 8;
    data[3] = v;
    demo_write(d, data, 4);
}

inline void demo_write_bits(demorec* d, K_UINT32 val, unsigned int nbits) {
    if (d->write_nbits==0)
        switch (nbits) {
        case 8: demo_write_8(d, val); return;
        case 16: demo_write_16(d, val); return;
        case 32: demo_write_32(d, val); return;
        }
    
    while (nbits) {
        unsigned int b2w = 32 - d->write_nbits;
        if (b2w > nbits) b2w = nbits;
        d->write_nbits += b2w;
        d->write_bucket = 
            (d->write_bucket << b2w) | 
            ((val >> (nbits - b2w)) & 
             ((1 << b2w) - 1));
        nbits -= b2w;
        if (d->write_nbits == 32) {
            demo_write_32(d, d->write_bucket);
            d->write_nbits = d->write_bucket = 0;
        }
    }
}

inline void demo_write_align(demorec* d) {
    while (d->write_nbits>=16) {
        demo_write_16(d, d->write_bucket >> (d->write_nbits - 16));
        d->write_nbits -= 16;
    }
    if (d->write_nbits >= 8) {
        demo_write_8(d, d->write_bucket >> (d->write_nbits - 8));
        d->write_nbits -= 8;
    }

    if (d->write_nbits > 0) {
        demo_write_8(d, d->write_bucket << (8 - d->write_nbits));
    }
    d->write_nbits = d->write_bucket = 0;
}

int demo_write_bool(demorec* d, int i) {
    i = !!i;
    demo_write_bits(d, i, 1);
    return i;
}


void demo_write_var(demorec* d, K_UINT32 var, int bpu) {
    int mask = (1 << bpu) - 1;
    while(1) {
        demo_write_bits(d, var & mask, bpu);
        var >>= bpu;
        if (var) {
            demo_write_bits(d, 1, 1);
        } else {
            demo_write_bits(d, 0, 1);
            break;
        }
    }
}
void demo_write_nullvar(demorec* d, K_UINT32 var, int bpu) {
    if (demo_write_bool(d, var != 0))
        demo_write_var(d, var, bpu);
}
void demo_write_signullvar(demorec* d, K_INT32 var, int bpu) {
    if (!demo_write_bool(d, var != 0))
        return;

    if (demo_write_bool(d, var < 0))
        var = -var;

    demo_write_var(d, var, bpu);
}

#define UPDATE4(marker, var, field, type)       \
if (1) {                                        \
    if ((var) != d->field) {                    \
        d->field = (var);                       \
        demo_write_8(d, marker);                \
        demo_write_##type(d, (var));            \
    }                                           \
 } else

#define UPDATE(marker, field, type) UPDATE4(marker, field, field, type)

void demo_update(demorec* d) {
    int i, j, wroteboard, changecnt;
    int slottime_nz;
    int cposx, cposy, cang;

    cposx = posx >> 2;
    cposy = posy >> 2;
    cang = (ang >> 1) & 1023;

    demo_write_var(d, totalclock - d->totalclock, 4);

    d->totalclock = totalclock;

    changecnt = 0;

    for (i = 0; i < 4096; i++) {
        K_INT16 bv = board[0][i] & ~16384;
        if (bv != d->board[i])
            changecnt++;
    }
    
    demo_write_nullvar(d, changecnt, 4);
    j = 0;
    for (i = 0; i < 4096; i++) {
        K_INT16 nv = board[0][i] & 0x1fff;
        if (d->board[i] != nv) {
            d->board[i] = nv;
            demo_write_nullvar(d, i - j, 6);
            demo_write_bits(d, nv, 13);
            j = i + 1;
        }
    }

    demo_write_signullvar(d, cposx - d->posx, 7);
    demo_write_signullvar(d, cposy - d->posy, 7);
    demo_write_signullvar(d, cang - d->ang, 7);
    
    if (demo_write_bool(d, posz != d->posz))
        demo_write_bits(d, posz, 6);

    if (demo_write_bool(d, bulnum != d->bulnum))
        demo_write_bits(d, bulnum, 6);

    if (demo_write_bool(d, mnum != d->mnum))
        demo_write_bits(d, mnum, 9);

    d->posx = cposx;
    d->posy = cposy;
    d->ang = cang;
    d->posz = posz;
    d->bulnum = bulnum;
    d->mnum = mnum;

    slottime_nz = slottime > 0;

    if (demo_write_bool(d, slottime_nz != d->slottime ||
                        slotpos[0] != d->slotpos[0] ||
                        slotpos[1] != d->slotpos[1] ||
                        slotpos[2] != d->slotpos[2])) {

        demo_write_bits(d, (d->slottime = slottime_nz), 1);
        for (j = 0; j < 3; j++)
            demo_write_bits(d, (d->slotpos[j] = (slotpos[j] & 0x7f)), 7);
    }


    /*
    for (i = 0; i < mnum; i++) {
        monster_t tmon;
        monster_t* cmon = &d->monsters[i];
        int stat_change, xdiff, ydiff, 
            shockdiff, shot_change;

        tmon.x = mposx[i] >> 2;
        tmon.y = mposy[i] >> 2;
        tmon.shot = mshot[i];
        tmon.shock = (mshock[i] + 31) >> 5;
        tmon.stat = mstat[i];

        stat_change = cmon->stat != tmon.stat;
        shot_change = cmon->shot != tmon.shot;
        xdiff = tmon.x - cmon->x;
        ydiff = tmon.y - cmon->y;
        shockdiff = tmon.shock - cmon->shock;

        if (stat_change || shot_change ||
            xdiff || ydiff || shockdiff) {

            demo_write_bits(d, D_MONST, 2);
            demo_write_var(d, i, 6);

            demo_write_bits(d, stat_change, 1);
            if (stat_change)
                demo_write_bits(d, tmon.stat, 9);

            demo_write_bits(d, shot_change, 1);
            if (shot_change)
                demo_write_bits(d, tmon.shot, 6);

            demo_write_signullvar(d, xdiff, 7);
            demo_write_signullvar(d, ydiff, 7);
            demo_write_signullvar(d, shockdiff, 7);
            
        }

        memcpy(cmon, &tmon, sizeof(monster_t));
    }


    for (i = 0; i < bulnum; i++) {
        bullet_t tbul;
        bullet_t* cbul = &d->bullets[i];
        int xdiff, ydiff, 
            angdiff, kind_change;
        
        tbul.x = bulx[i] >> 2;
        tbul.y = buly[i] >> 2;
        tbul.ang = bulang[i] >> 1;
        tbul.kind = bulkind[i];

        kind_change = cbul->kind != tbul.kind;
        xdiff = tbul.x - cbul->x;
        ydiff = tbul.y - cbul->y;
        angdiff = tbul.ang - cbul->ang;

        if (kind_change || xdiff || ydiff || angdiff) {

            demo_write_bits(d, D_BULLET, 2);
            demo_write_bits(d, i, 6);

            demo_write_bits(d, kind_change, 1);
            if (kind_change)
                demo_write_bits(d, tbul.kind, 5);
            
            demo_write_signullvar(d, xdiff, 7);
            demo_write_signullvar(d, ydiff, 7);
            demo_write_signullvar(d, angdiff, 7);
            memcpy(cbul, &tbul, sizeof(bullet_t));
        }
        }*/
}
#endif
