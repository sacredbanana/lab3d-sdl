#include "lab3d.h"
#include "adlibemu.h"
#include <math.h>
#include <ctype.h>
#include "SDL_endian.h"
#include "SDL_image.h"

/* Various constants that really should be stored in the data files... */

#define giflen1 7649
#define giflen2 8258

/* Stack size in LZW. Probably overkill. */

#define LZW_STACK_SIZE 4096

/* Last time tick handler was called... */
static Uint32 lastTick=0;

/* Fractional part of milliseconds. */
static Uint32 tickFrac=0;

/* Sequencer buffer. Completely overkill, unless Ken wants to play hundreds of
   notes at once. */

#ifdef USE_OSS
SEQ_DEFINEBUF (2048);
#endif

/* Palette for OpenGL transfer... */

static GLfloat Red[256], Blue[256], Green[256];
static GLfloat Alpha[256];

static unsigned char ipalr[256], ipalg[256], ipalb[256];

static char BADNAME[13]="MESTINXBADLY";

void fatal_error(const char* fmt, ...) {
    char txt[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(txt, sizeof(txt), fmt, ap);
    va_end(ap);
    txt[sizeof(txt)-1] = 0;

    char* e = txt + strlen(txt) - 1;
    if (*e == '\n') *e = 0;

    fprintf (stderr, "ERROR: %s\n", txt);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "LAB3D-SDL Fatal Error", txt, mainwindow);
    SDL_Quit();
    exit (-1);

}

int newkeystatus(int key) {
    int i, *ck;
    /* Assuming usually < 5 keys pressed at a time, linear search is fast enough. */
    for (i = numkeyspressed, ck = keyspressed; i; i--, ck++) {
        if (*ck == key)
            return 1;
    }
    return 0;
}

void setnewkeystatus(int key, int val) {
    int i, *ck;
    for (i = 0, ck = keyspressed; i < numkeyspressed; i++, ck++) {
        if (key == *ck)
            break;
    }
    if (val) {
        /*fprintf(stderr, "PRESS %08x (%s)\n", key, SDL_GetKeyName(key));*/
        if (i == numkeyspressed && i < 128) {
            keyspressed[numkeyspressed++] = key;
        }
    } else {
        /*fprintf(stderr, "RELEASE %08x (%s)\n", key, SDL_GetKeyName(key));*/
        if (i != numkeyspressed) {
            keyspressed[i] = keyspressed[--numkeyspressed];
        }
    }
}

int get_pckey(SDL_Keycode key) {
    pckeymap_t* c;
    for(c = PCkey; c->scan != -1; c++) {
        if (c->key == key) {
            /*fprintf(stderr, "PCKEY lookup: %08x (%s) = %x\n", key, SDL_GetKeyName(key), c->scan);*/
            return c->scan;
        }
    }
    /*fprintf(stderr, "PCKEY lookup: %08x (%s) not found\n", key, SDL_GetKeyName(key));*/
    return -1;
}

#ifdef USE_OSS
/* Linux sequencer write routine... */

void seqbuf_dump ()
{
    if (_seqbufptr)
        if (write (sequencerdevice, _seqbuf, _seqbufptr) == -1)
        {
            fatal_error("write /dev/sequencer failed.");
        }
    _seqbufptr = 0;
}
#endif

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

void ConvertPartialOverlay(int x, int y, int w, int h);

/* Endian-converting reads. */

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
#define SWAPBLOCK16 for(a=0;a<(cnt>>1);a++) *(b+a)=SDL_Swap16(*(b+a));
#define SWAPBLOCK32 for(a=0;a<(cnt>>2);a++) *(b+a)=SDL_Swap32(*(b+a));

ssize_t readLE16(int fd, void *buf, size_t cnt) {
    ssize_t r=read(fd, buf, cnt);
    K_UINT16 *b=buf;
    int a;

    SWAPBLOCK16;
    return r;
}
ssize_t readLE32(int fd, void *buf, size_t cnt) {
    ssize_t r=read(fd, buf, cnt);
    K_UINT32 *b=buf;
    int a;

    SWAPBLOCK32;
    return r;
}

ssize_t writeLE16(int fd, void *buf, size_t cnt) {
    ssize_t r;
    K_UINT16 *b=buf;
    int a;

    SWAPBLOCK16;
    r=write(fd, buf, cnt);
    SWAPBLOCK16;

    return r;
}
ssize_t writeLE32(int fd, void *buf, size_t cnt) {
    ssize_t r;
    K_UINT32 *b=buf;
    int a;

    SWAPBLOCK32;
    r=write(fd, buf, cnt);
    SWAPBLOCK32;
    return r;
}
#endif

K_INT32 readlong(unsigned char *t) {
    return((*t)|((*(t+1))<<8)|((*(t+2))<<16)|((*(t+3))<<24));
}

void writelong(unsigned char *t, K_INT32 v) {
    *t=v&255;
    *(t+1)=(v>>8)&255;
    *(t+2)=(v>>16)&255;
    *(t+3)=(v>>24)&255;
}

K_UINT16 readshort(unsigned char *t) {
    return((*t)|((*(t+1))<<8));
}

void writeshort(unsigned char *t, K_UINT16 v) {
    *t=v&255;
    *(t+1)=(v>>8)&255;
}

#define GKP_AXIS(type, sdltype)                                         \
if (cur_##type && action_##type[keydef] != ACTION_UNBOUND) {            \
    int act = action_##type[keydef];                                    \
    if (act & JOY_FLAG_AXIS) {                                          \
        int which = act & JOY_MASK;                                     \
        int value = SDL_##sdltype##GetAxis(cur_##type, which);          \
        int neg = act & JOY_FLAG_NEG ? 1 : 0;                           \
        if (neg) value = -value;                                        \
        if (value < low_deadzone)                                       \
            value = 0;                                                  \
        else if (value > high_deadzone)                                 \
            value = 32767;                                              \
        else                                                            \
            value = ((int)(value - low_deadzone) * (32767.0 / (high_deadzone - low_deadzone))); \
        if (pressval == 1) {                                            \
            unsigned int mask = 1 << which;                             \
            if (value >= 12*1024) {                                     \
                cur_##type##_axis_active |= mask;                       \
                return 1;                                               \
            } else if (value < 8192) {                                  \
                cur_##type##_axis_active &= ~mask;                      \
                return 0;                                               \
            } else {                                                    \
                return cur_##type##_axis_active & mask ? 1 : 0;         \
            }                                                           \
        }                                                               \
        if (value) return value;                                        \
    } else {                                                            \
        if (SDL_##sdltype##GetButton(cur_##type, act & JOY_MASK)) {     \
            if (pressval == runpressval)                                \
                return pressval;                                        \
            return getkeydefstat(ACTION_RUN) ? runpressval : pressval;  \
        }                                                               \
    }                                                                   \
}

int getkeypressure(int keydef, int pressval, int runpressval) {
    int low_deadzone = 6000;
    int high_deadzone = 32767 - 700;
    if (action_key[keydef] != ACTION_UNBOUND) {
        if (newkeystatus(action_key[keydef])) {
            if (pressval == runpressval)
                return pressval;
            return getkeydefstat(ACTION_RUN) ? runpressval : pressval;
        }
    }
    if (window_in_focus) {
        GKP_AXIS(joystick, Joystick);
        GKP_AXIS(controller, GameController);
    }
    return 0;
}

int getkeydefstat(int keydef) {
    int val = getkeypressure(keydef, 1, 1) ? 1 : 0;
    if (val) {
        if (SDL_GetTicks() < action_repeat_lock[keydef])
            return 0;
    } else {
        action_repeat_lock[keydef] = 0;
    }
    return val;
}

int getkeydefstatlock(int keydef) {
    int val = getkeydefstat(keydef);
    if (val)
        action_repeat_lock[keydef] = 0xFFFFFFFF;
    return val;

}

int _repeatkeydef(int keydef, int startms, int repms) {
    int val = getkeypressure(keydef, 1, 1) ? 1 : 0;
    if (val) {
        if (SDL_GetTicks() < action_repeat_lock[keydef])
            return 0;
        action_repeat_lock[keydef] = SDL_GetTicks() + (action_repeat_lock[keydef] ? repms : startms);
    } else {
        action_repeat_lock[keydef] = 0;
    }
    return val;
}

int repeatkeydef(int keydef) {
    return _repeatkeydef(keydef, KEY_REPEAT_START, KEY_REPEAT);
}

void clearkeydefstat(int keydef) {
#ifdef __GNUC__
    /* Ugly hack to work around GCC generating unaligned MOVDQA instructions*/
    __asm__ volatile ("");
#endif
    action_repeat_lock[keydef] = 0xFFFFFFFF;
}


int smooth_input(int cvel, int dvel, int accel, int decel) {
    if (cvel < dvel) {
        cvel += cvel < 0 ? decel : accel;
        if (cvel > dvel)
            cvel = dvel;
    } else {
        cvel -= cvel > 0 ? decel : accel;
        if (cvel < dvel)
            cvel = dvel;
    }
    return cvel;
}

void BuildMipmaps(Uint32* pix, int w, int h, int hasalpha, int maxlevel);

/* Load a board. Uses LZW compression. Get a lawyer. */

void loadboard()
{
    unsigned char bitcnt, numbits;
    K_INT16 i, j, k, fil, bytecnt1, bytecnt2;
    K_INT16 currstr, strtot, compleng, dat, goalstr;
    K_INT32 templong;
    K_INT16 prepdie=0;

    K_UINT16 stack[LZW_STACK_SIZE];
    K_UINT16 stackp=0;

#if SDL_BYTEORDER != SDL_LIL_ENDIAN
    int a; K_UINT16 *b; size_t cnt;
#endif

    unsigned char *RGBATexture=malloc(64*64*4);
    
    sprintf(filepath, "%sboards.dat", gameroot);
    sprintf(filepathUpper, "%sBOARDS.DAT", gameroot);
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) != -1)||
            ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) != -1)) {
            lseek(fil, ((long)boardnum)<<13, SEEK_SET);
            read(fil, &board[0], 8192);
            close(fil);
            numwarps=0;
            justwarped=0;
        } else {
            fprintf(stderr, "Can't find boards.dat.\n");
            SDL_Quit();
            exit(1);
        }
    } else {
        sprintf(filepath, "%sboards.kzp", gameroot);
        sprintf(filepathUpper, "%sBOARDS.KZP", gameroot);
        if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) != -1)||
            ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) != -1))
        {
            prepdie = 0;
            numwarps = 0;
            justwarped = 0;
            readLE16(fil, &boleng[0], 30*4);
            templong = (long)(30*4);
            for(i=0;i<(boardnum<<1);i++)
                templong += ((long)(boleng[i]+2));
            lseek(fil, templong, SEEK_SET);

            for(i=1;i<=256;i++) {
                lzwbuf[i]=i&255;
                lzwbuf2[i]=i;
            }
            lzwbuf2[0]=0;
            lzwbuf[0]=0;

            for(i=0;i<2;i++)
            {
                unsigned char* boardp = ((unsigned char*)(board)) + (i << 12);

                compleng = boleng[(boardnum<<1)+i];
                readLE16(fil, &strtot, 2);
                read(fil, &tempbuf[0], compleng);

                if (strtot > 0)
                {
                    tempbuf[compleng] = 0;
                    tempbuf[compleng+1] = 0;
                    tempbuf[compleng+2] = 0;
                    bytecnt2 = 0;
                    bytecnt1 = 0;
                    bitcnt = 0;
                    currstr = 256;
                    goalstr = 512;
                    numbits = 9;
                    do
                    {
                        dat=(((tempbuf[bytecnt2])|
                              ((K_UINT16)(tempbuf[bytecnt2+1]))<<8)>>bitcnt)&
                            ((1<<numbits)-1);

/*		    dat=((*((K_UINT16 *)(tempbuf+bytecnt2)))
                    >>bitcnt)&
                    ((1<<numbits)-1);*/
                        if (bitcnt+numbits>16) {
                            dat+=(((K_UINT16)tempbuf[bytecnt2+2])&
                                  ((1<<((bitcnt+numbits)&15))-1))<<
                                (16-bitcnt);
                        }

                        bitcnt+=numbits;
                        bytecnt2+=bitcnt>>3;
                        bitcnt&=7;

                        lzwbuf2[currstr]=dat;

                        while(dat>=256) {
                            stack[stackp++]=lzwbuf[dat];

                            dat=lzwbuf2[dat];
                        }

                        lzwbuf[currstr-1]=dat;
                        lzwbuf[currstr]=dat;

                        dat=lzwbuf2[dat];
                        stack[stackp++]=dat;

                        while(stackp>0) {
                            stackp--;
                            if (bytecnt1<4096)
                                boardp[bytecnt1++]=
                                    stack[stackp];
                        }

                        currstr++;
                        if (currstr == goalstr)
                        {
                            numbits++;
                            goalstr = (goalstr<<1);
                        }
                    }
                    while (currstr <= strtot);
                }
                else {
                    memcpy(boardp, tempbuf, 4096);
                }
            }
            close(fil);
        } else {
            fatal_error("Can't find boards.kzp.");
        }
    }
#if SDL_BYTEORDER != SDL_LIL_ENDIAN
    b=(K_UINT16 *)board;
    cnt=8192;
    SWAPBLOCK16;
#endif

    /* Generate map texture... */
    for(i=0;i<4096;i++)
        walseg[map-1][i]=*(((K_INT16 *)board)+i)&255;

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        spritepalette[0]=63;
        spritepalette[1]=63;
        spritepalette[2]=63;
    }
    glBindTexture(GL_TEXTURE_2D, texName[map-1]);
    checkGLStatus();

    TextureConvert(walseg[map-1], RGBATexture, bmpkind[map]);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, fullfilter);

    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    checkGLStatus();
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    checkGLStatus();

    BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);
    checkGLStatus();

    /* Place warps and monsters... */

    mnum = 0;
    bossmonster = 0;
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        for(i=0;i<63;i++)
            for(j=0;j<63;j++)
            {
                mboard[i][j] = 0;
                k = board[i][j]&1023;
                if ((k == 123) && (numwarps < 16))
                {
                    xwarp[(int)numwarps] = (char)i;
                    ywarp[(int)numwarps] = (char)j;
                    numwarps++;
                }
                if ((k == 66) || (k == 38) || (k == 54) || (k == 68) || (k == 94) || (k == 98) || (k == 109) || (k == 160) || (k == 165) || (k == 187))
                {
                    mposx[mnum] = (i<<10)+512;
                    mposy[mnum] = (j<<10)+512;
                    mgolx[mnum] = mposx[mnum];
                    mgoly[mnum] = mposy[mnum];
                    moldx[mnum] = mposx[mnum];
                    moldy[mnum] = mposy[mnum];
                    mstat[mnum] = board[i][j]&255;
                    mshock[mnum] = 0;
                    if (k == 66)
                        mshot[mnum] = 1;
                    if (k == 68)
                        mshot[mnum] = 255;
                    if (k == 38)
                        mshot[mnum] = 2;
                    if (k == 54)
                        mshot[mnum] = 5;
                    if (k == 94)
                    {
                        mshot[mnum] = 10;
                        prepdie |= 2;
                    }
                    if (k == 98)
                        mshot[mnum] = 255;
                    if (k == 109)
                    {
                        mshot[mnum] = 100;
                        prepdie |= 1;
                    }
                    if (k == 160)
                        mshot[mnum] = 1;
                    if (k == 165)
                        mshot[mnum] = 1;
                    if (k == 187)
                        mshot[mnum] = 1;
                    mnum++;
                    mboard[i][j] = k;
                    board[i][j] = 1024;
                }
                if ((board[i][j]&4096) > 0)
                {
                    posx = (i<<10)+512;
                    posy = (j<<10)+512;
                    ang = ((board[i][j]&3)<<9);
                    startx = posx;
                    starty = posy;
                    startang = ang;
                    board[i][j] = 62+1024;
                }
            }
    else
        for(i=0;i<63;i++)
            for(j=0;j<63;j++)
            {
                board[i][j] &= 0xbfff;
                k = board[i][j]&1023;
                if ((k == warp) && (numwarps < 16))
                {
                    xwarp[(int)numwarps] = (char)i;
                    ywarp[(int)numwarps] = (char)j;
                    numwarps++;
                }
                if ((k == monken) || (k == mongre) || (k == monand) ||
                    (k == monbal) || (k == monali) || (k == monhol) ||
                    (k == monzor) || (k == monbat) || (k == mongho) ||
                    (k == monske) || (k == monke2) || (k == monan2) ||
                    (k == monan3) || (k == monwit) || (k == monbee) ||
                    (k == monspi) || (k == mongr2) || (k == monear) ||
                    (k == monrob) || (k == monro2) || (k == mondog) ||
                    (k == monmum) || (k == hive))
                {
                    mposx[mnum] = (i<<10)+512;
                    mposy[mnum] = (j<<10)+512;
                    mgolx[mnum] = mposx[mnum];
                    mgoly[mnum] = mposy[mnum];
                    moldx[mnum] = mposx[mnum];
                    moldy[mnum] = mposy[mnum];
                    mstat[mnum] = k;
                    mshock[mnum] = 0;
                    if (k == monken) mshot[mnum] = 1;
                    if (k == monbal) mshot[mnum] = 255;
                    if (k == mongre) mshot[mnum] = 2;
                    if (k == mongr2) mshot[mnum] = 3;
                    if (k == monrob) mshot[mnum] = 5;
                    if (k == monwit) mshot[mnum] = 1;
                    if (k == monand) mshot[mnum] = 5;
                    if (k == monali) mshot[mnum] = 10, prepdie |= 2;
                    if (k == monhol) mshot[mnum] = 255;
                    if (k == monzor) mshot[mnum] = 48, prepdie |= 1;
                    if (k == monbat) mshot[mnum] = 1;
                    if (k == monear) mshot[mnum] = 1;
                    if (k == monbee) mshot[mnum] = 1;
                    if (k == monspi) mshot[mnum] = 1;
                    if (k == mongho) mshot[mnum] = 1;
                    if (k == monske) mshot[mnum] = 1;
                    if (k == monmum) mshot[mnum] = 2;
                    if (k == monke2) mshot[mnum] = 48, prepdie |= 1;
                    if (k == monan2) mshot[mnum] = 48, prepdie |= 1;
                    if (k == monan3) mshot[mnum] = 1;
                    if (k == hive) mshot[mnum] = 15;
                    if (k == monro2) mshot[mnum] = 2;
                    if (k == mondog) mshot[mnum] = 1;
                    mnum++;
                    board[i][j] = 16384+1024;
                }
                if ((board[i][j]&4096) > 0)
                {
                    posx = (i<<10)+512;
                    posy = (j<<10)+512;
                    yourhereoldpos = ((posx>>10)<<6)+(posy>>10);
                    youarehere();
                    ang = ((board[i][j]&3)<<9);
                    startx = posx;
                    starty = posy;
                    startang = ang;
                    board[i][j] = stairtop+1024;
                }
            }
    if ((prepdie&1) > 0)
        ksay(21);
    else if ((prepdie&2) > 0)
        ksay(20);
    posz = 32;
    angvel = 0;
    vel = 0;
    mxvel = 0;
    myvel = 0;
    svel = 0;
    hvel = 0;
    for(i=0;i<32;i++)
        bulstat[i] = 0;
    lastbulshoot = 0;
    bulnum = 0;
    keys[0] = 0;
    keys[1] = 0;
    death = 4095;
    free(RGBATexture);
}

/* Load tables and settings. The settings are ignored. */

void loadtables()
{
    K_INT16 fil;

    sprintf(filepath, "%stables.dat", gameroot);
    sprintf(filepathUpper, "%sTABLES.DAT", gameroot);
    if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) != -1)||
        ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) != -1))
    {
        readLE32(fil, &sintable[0], 8192);
        readLE32(fil, &tantable[0], 4096);
        readLE16(fil, &radarang[0], 720);
/*	read(fil, &option[0], numoptions);
        read(fil, &keydefs[0], numkeys);
        readLE16(fil, &joyx1, 2);
        readLE16(fil, &joyy1, 2);
        readLE16(fil, &joyx2, 2);
        readLE16(fil, &joyy2, 2);
        readLE16(fil, &joyx3, 2);
        readLE16(fil, &joyy3, 2);
        readLE16(fil, &ksayfreq, 2);*/
        close(fil);

        /* Override joystick values with SDL limits... */

        joyx1=joyy1=-32768;
        joyx2=joyy2=0;
        joyx3=joyy3=32767;

    } else {
        fatal_error("Can't find tables.dat.");
    }
}

/* ksay sound filenum. pan = 0 for left, 128 centre, 256 right. */

K_INT16 ksaypan(K_UINT16 filenum, K_UINT16 pan, int ui) {
    K_INT16 numfiles;
    K_UINT16 leng;
    K_INT32 sndfiloffs;
    K_INT32 blocksize=(musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)?SOUNDBLOCKSIZE44KHZ:SOUNDBLOCKSIZE11KHZ;

    if (!ui) {
        psounds[psoundnum & 15] = filenum;
        psoundpan[psoundnum & 15] = pan > 255 ? 255 : pan;
        psoundnum++;
    }

    if (!soundpan) pan=128;

    SDL_LockMutex(soundmutex);
    if ((speechstatus == 0) || (mute == 1)) {
        SDL_UnlockMutex(soundmutex);
        return(-1);
    }
    numfiles=readshort(SoundFile);
    if (filenum >= numfiles) {
        SDL_UnlockMutex(soundmutex);
        return(-1);
    }

    sndfiloffs=readlong(SoundFile+(2+filenum*6));
    leng=readshort(SoundFile+(6+filenum*6));

    DumpSound(SoundFile+sndfiloffs, leng, ((FeedPoint+blocksize)&65535), pan);
    SDL_UnlockMutex(soundmutex);
    return 0;
}

/* ksay sound filenum from position x, y in game. */

K_INT16 ksaystereo(K_UINT16 filenum, K_UINT16 x, K_UINT16 y) {
    K_INT32 k, m, dir, pan, templong;

    if ((x==posx)&&(y==posy)) return(ksay(filenum));

    k = 512;
    if (x != posx)
    {
        templong = (((((K_INT32)y-(K_INT32)posy)<<12)/
                     ((K_INT32)x-(K_INT32)posx))<<4);
        if (templong < 0)
            k = 768;
        else
            k = 256;
        for (m=128;m>0;m>>=1)
        {
            if (tantable[k] < templong)
                k += m;
            else
                k -= m;
        }
    }
    if (y > posy)
        k += 1024;

    dir=(k+2048-ang)&2047;
    dir^=2047;

    if (dir>=1536)
        pan=(dir-1536)>>2; /* Forward left */
    else if (dir>=512)
        pan=(1536-dir)>>2; /* Back */
    else pan=128+(dir>>2); /* Forward right */

    return ksaypan(filenum, pan, 0);
}

/* Play a digital sound... */

K_INT16 ksay(K_UINT16 filenum)
{
    return(ksaypan(filenum, 128, 0));
}

K_INT16 ksayui(K_UINT16 filenum)
{
    return(ksaypan(filenum, 128, 1));
}

/* Wipe digital sound buffer... */

void reset_dsp()
{
    memset(SoundBuffer, 0, 65536*2);
    FeedPoint=0;
}

#ifndef min
int min(long a, long b) {
    return((a<b)?a:b);
}
#endif

static long minicnt = 0;
void preparesound(void *dasnd, long numbytestoprocess)
{
    long i, prepcnt;

    prepcnt = numbytestoprocess;
    while (prepcnt > 0)
    {
        i = min(prepcnt, (minicnt/speed+4)&~3);
        adlibgetsample(dasnd, i);
        dasnd = (void *)(((INT_TYPE)dasnd)+i);
        prepcnt -= i;

        minicnt -= speed*i;
        while (minicnt < 0)
        {
            minicnt += 44100*channels*2;
            ksmhandler();
        }
    }
}

/* SDL audio callback. Feed a chunk from sound buffer. */

void AudioCallback(void *userdata, Uint8 *stream, int len) {
    int rl;
    int i;
    int t=0;
    int j1, j2;

    if (quitgame) return;

    if (soundtimer) {
        soundtimerbytes+=len;
        SDL_LockMutex(timermutex);
        while(soundtimerbytes>=soundbytespertick) {
            soundtimerbytes-=soundbytespertick;
            clockspeed++;
        }
        SDL_UnlockMutex(timermutex);
    }

    SDL_LockMutex(soundmutex);

    if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM) len>>=2;

    len>>=1;
    rl=len;

    if (FeedPoint+rl>=65536) rl=65536-FeedPoint;

    if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM) {
        /* mute=2: stop music, but don't mute. */
        if ((mute!=1)&&musicstatus)
            preparesound(stream, rl*2*4);
        else
            memset(stream, 0, rl*2*4);
        j1=FeedPoint;
        j2=(FeedPoint+channels)&65535;

        if (mute!=1) {
            for(i=0;i<(len<<2);i++) {
                if (channels==1)
                    switch(i&3) {
                        case 0:
                            t=SoundBuffer[j1]+((Sint16 *)stream)[i];
                            break;
                        case 1:
                            t=3*(SoundBuffer[j1]>>2)+(SoundBuffer[j2]>>2)+
                                ((Sint16 *)stream)[i];
                            break;
                        case 2:
                            t=(SoundBuffer[j1]>>1)+(SoundBuffer[j2]>>1)+
                                ((Sint16 *)stream)[i];
                            break;
                        case 3:
                            t=(SoundBuffer[j1]>>2)+3*(SoundBuffer[j2]>>2)+
                                ((Sint16 *)stream)[i];
                            j1++;
                            j2++;
                            j2&=65535;
                            break;
                    }
                else
                    switch(i&7) {
                        case 0:
                        case 1:
                            t=SoundBuffer[j1+(i&1)]+((Sint16 *)stream)[i];
                            break;
                        case 2:
                        case 3:
                            t=3*(SoundBuffer[j1+(i&1)]>>2)+
                                (SoundBuffer[j2+(i&1)]>>2)+
                                ((Sint16 *)stream)[i];
                            break;
                        case 4:
                        case 5:
                            t=(SoundBuffer[j1+(i&1)]>>1)+
                                (SoundBuffer[j2+(i&1)]>>1)+
                                ((Sint16 *)stream)[i];
                            break;
                        case 6:
                        case 7:
                            t=(SoundBuffer[j1+(i&1)]>>2)+
                                3*(SoundBuffer[j2+(i&1)]>>2)+
                                ((Sint16 *)stream)[i];
                            if ((i&7)==7) {
                                j1+=2;
                                j2+=2;
                                j2&=65535;
                            }
                            break;
                    }
                if (t<-32768) t=-32768;
                if (t>32767) t=32767;
                ((Sint16 *)stream)[i]=t;
            }
        }
    }
    else
        memcpy(stream, SoundBuffer+FeedPoint, rl*2);
    memset(SoundBuffer+FeedPoint, 0, rl*2);

    FeedPoint+=rl;
    FeedPoint&=65535;

    SDL_UnlockMutex(soundmutex);

    if (rl<len) {
        if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
            AudioCallback(userdata, stream+rl, len-rl);
        else
            AudioCallback(userdata, stream+(rl<<2), (len-rl)<<2);
    }
}

/* Copy sound to sound buffer. */

void DumpSound(unsigned char *sound, K_UINT16 leng, K_UINT32 playpoint, int pan) {
    K_INT32 a, t;

    K_UINT32 e1, pl;

    if (channels==1) {
        e1=playpoint+leng;
        if (e1>=65536) e1=65536;
        pl=e1-playpoint;

        for(a=playpoint;a<e1;a++) {
            t=SoundBuffer[a]+(sound[a-playpoint]-128)*soundvolume;
            if (t<-32768) t=-32768;
            if (t>32767) t=32767;
            SoundBuffer[a]=t;
        }
    } else {
        e1=playpoint+(leng<<1);
        if (e1>=65536) e1=65536;
        pl=(e1-playpoint)>>1;

        for(a=playpoint;a<e1;a++) {
            t=((a-playpoint)&1)?pan:(256-pan);
            t=SoundBuffer[a]+
                (((sound[(a-playpoint)>>1]-128)*soundvolume*t)>>7);
            if (t<-32768) t=-32768;
            if (t>32767) t=32767;
            SoundBuffer[a]=t;
        }
    }

    if (pl<leng) DumpSound(sound+pl, leng-pl, 0, pan);
}

/* Check if object is visible... */

void checkobj(K_UINT16 x, K_UINT16 y, K_UINT16 posxs, K_UINT16 posys,
              K_INT16 angs, K_INT16 num)
{
    K_INT16 angle, siz, ysiz;

    if (sortcnt >= (sizeof(sortx)/sizeof(sortx[0])))
        return;

    if (shadow[num&1023]) {
        checkobj(x, y, posxs, posys, angs, shadow[num&1023]);
    }
    siz = (int)(((((long)x-(long)posxs)>>2)*sintable[(angs+512)&2047]+(((long)y-(long)posys)>>2)*sintable[angs])>>16);
    if (siz != 0)
        {
            ysiz = (int)(((((long)x-(long)posxs)>>2)*sintable[angs]-(((long)y-(long)posys)>>2)*sintable[(angs+512)&2047])>>16);
            angle = (K_INT16)(180.0-((180.0*(double)ysiz)/(double)siz/aspw));
            siz = (int)(163840L/((long)siz));
            sortx[sortcnt] = x;
            sorty[sortcnt] = y;
            if (bmpkind[num&1023]==5) {
            sorti[sortcnt]=0;
            } else {
            sorti[sortcnt]=163840L/sqrt((((long)x-(long)posxs)*
                                         ((long)x-(long)posxs))+
                                        (((long)y-(long)posys)*
                                         ((long)y-(long)posys)))*4;
            }
            sortbnum[sortcnt] = (num&1023);
            if (((angle+(siz>>3)) >= -2) && ((angle-(siz>>3)) <= 362) && ((siz&0xc000) == 0))
            sortcnt++;
        }
}

void addexplosion(K_UINT16 x, K_UINT16 y, K_UINT16 stat) {
    explotime[explonum]=totalclock+16;
    explox[explonum]=x;
    exploy[explonum]=y;
    explostat[explonum]=stat;
    explonum++;
}


/* Set status bar position... */

void linecompare(K_UINT16 lin)
{

    statusbaryvisible=240-((lin+1)>>1);

}

/* Draw energy meter... */

void drawmeter(int life, int oldlife, int x, int y)
{
    K_INT16 lifespot, olifespot;

    if (life < 0) life = 0;
    if (life > 4095) life = 4095;
    lifespot = (life>>6);
    olifespot = (oldlife>>6);
    if (lifespot < olifespot)
        statusbardraw(lifespot, 57, olifespot-lifespot+1, 7, lifespot+x, y, statusbarinfo);
    else if (lifespot > olifespot)
        statusbardraw(olifespot, 50, lifespot-olifespot+1, 7, olifespot+x, y, statusbarinfo);
}
void drawlife()
{
    if (statusbar == 479)
        return;
    drawmeter(life, oldlife, 128, 4+statusbaryoffset);
    oldlife = life;
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
#ifndef min
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

int read_ini(FILE* input, char* buf, int buflen, char **keyp, char **valp, int *linep) {
    char *line, *end, *val;
    while (fgets(buf, buflen, input)) {
        if (linep) (*linep)++;
        line = buf;
        if (!*line) continue;
        while (*line && isspace(*line)) line++;

        if (*line == '#') continue;

        end = line + strlen(line) - 1;
        while (strlen(line) > 0 && isspace(*end)) 
            *end-- = 0;

        if (*line =='[' && *end == ']') {
            *end-- = 0;
            line++;
            *keyp = line;
            *valp = NULL;
            return 2;
        }
        if ((end = strchr(line, '='))) {
            val = end + 1;
            *end-- = 0;
            while (isspace(*val)) val++;
            while (end > line && isspace(*end)) *end-- = 0;
            *valp = val;
            *keyp = line;

            return 1;
        }
    }
    return 0;
}

enumpair filtermodes[] = {
    {"GL_NEAREST", GL_NEAREST},
    {"GL_LINEAR", GL_LINEAR},
    {"GL_NEAREST_MIPMAP_NEAREST", GL_NEAREST_MIPMAP_NEAREST},
    {"GL_NEAREST_MIPMAP_LINEAR", GL_NEAREST_MIPMAP_LINEAR},
    {"GL_LINEAR_MIPMAP_NEAREST", GL_LINEAR_MIPMAP_NEAREST},
    {"GL_LINEAR_MIPMAP_LINEAR", GL_LINEAR_MIPMAP_LINEAR},
    {NULL, -1},
};

enumpair wrapmodes[] = {
    {"clamp", 0},
    {"repeat", 1},
    {NULL, -1},
};

int get_enum(char* str, enumpair* cur) {
    while (cur->name) {
        if (!strcasecmp(str, cur->name)) {
            return cur->value;
        }
        cur++;
    }
    return cur->value;
}

typedef struct {
    int wrapmode;
    int tcl, tch;
    int minfilt, magfilt;
    int bmpkind_override;
    char* texreplace;
} wallparam;

wallparam default_wallparam={
    -1,
    0, -1,
    GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR,
    -1,
    NULL
};

typedef struct {
    wallparam* cwparam;
    wallparam wparams[numwalls];
} wallparams_ini;

/* Load textures. Use LZW without a license. */

void loadwalls(int replace)
{
    unsigned char bitcnt, numbits;
    wallparam wparams[numwalls];
    //imgcache* cache=NULL;

    FILE* params;

    K_INT16 i, j, fil, bytecnt1, bytecnt2;
#ifdef COLOURTEST
    K_INT16 k;
#endif
    K_INT16 currstr, strtot, compleng, dat, goalstr;
    unsigned char *walsegg;

    K_UINT16 stack[LZW_STACK_SIZE];
    K_UINT16 stackp=0;

    unsigned char *RGBATexture=malloc(64*64*4);

    for (i=0;i<numwalls;i++) {
        wparams[i]=default_wallparam;
    }
    int dotransition=1;
    wallparam* cwparam=NULL;
    memset(shadow, 0, sizeof(shadow));

    sprintf(filepath, "%swallparams.ini", gameroot);
    if (replace && (params = fopen(filepath, "rt")) != NULL) {
        dotransition = 0;
        int curwall = 0;
        char buf[256];
        char *key, *val;
        int curline = 0;
        while (read_ini(params, buf, 256, &key, &val, &curline)) {
            if (!val) {
                curwall = atoi(key);
                cwparam = &wparams[curwall];
                if (curwall >= numwalls || curwall<0) {
                    cwparam=NULL;
                }
            } else {
                if (!cwparam) {
                    fatal_error("wallparams.ini:%d: Key/value outside of section", curline);
                }
                int ival = atoi(val);
                if (!strcmp(key, "src")) {
                    cwparam->texreplace = strdup(val);
                } else if (!strcmp(key, "range")) {
                    i = sscanf(val, "%d %d", &cwparam->tcl, &cwparam->tch);
                    if (i != 2)
                        fatal_error("wallparams.ini:%d: Expected 2 values for range", curline);
                } else if (!strcmp(key, "shadow")) {
                    shadow[curwall+1] = ival;
                    if (ival < 0 || ival > numwalls)
                        fatal_error("wallparams.ini:%d: Invalid value for shadow: %d", curline, ival);
                    //printf("Set shadow for %d to %d (%s)\n", curwall, ival, val);
                } else if (!strcmp(key, "bmpkind")) {
                    cwparam->bmpkind_override = ival;
                    if (ival < 0 || ival > 5) {
                        fatal_error("wallparams.ini:%d: Invalid value for bmpkind: %d", curline, ival);
                    }
                } else if (!strcmp(key, "minfilt")) {
                    cwparam->minfilt = get_enum(val, filtermodes);
                    if (cwparam->minfilt == -1) {
                        fatal_error("wallparams.ini:%d: Invalid value for filter: %s", curline, val);
                    }
                } else if (!strcmp(key, "magfilt")) {
                    cwparam->magfilt = get_enum(val, filtermodes);
                    if (cwparam->magfilt == -1) {
                        fatal_error("wallparams.ini:%d: Invalid value for filter: %s", curline, val);
                    }
                } else if (!strcmp(key, "wrap")) {
                    cwparam->wrapmode = get_enum(val, wrapmodes);
                    if (cwparam->wrapmode == -1) {
                        fatal_error("wallparams.ini:%d: Invalid value for wrap mode: %s", curline, val);
                    }
                } else {
                    fatal_error("wallparams.ini:%d: Invalid key: %s", curline, key);
                }
                //printf("key='%s' val='%s'\n", key, val);

            }
        }
        fclose(params);
    }
    sprintf(filepath, "%swalls.kzp", gameroot);
    sprintf(filepathUpper, "%sWALLS.KZP", gameroot);
    if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) != -1)||
        ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) != -1))
    {
        bmpkind[0] = 0;
        wallheader[0] = 8;
        read(fil, &wallheader[1], rnumwalls);
        if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)
            readLE16(fil, &tileng[0], numwalls*2);
        tioffs[0] = (long)(numwalls+numwalls+numwalls);
        for(i=1;i<=rnumwalls;i++)
        {
            tioffs[i] = tioffs[i-1]+((long)(tileng[i-1]+2));
            bmpkind[i] = 1+(wallheader[i]&7);
            if (bmpkind[i] == 3)
                bmpkind[i] = 4;
        }

        bmpkind[sharewaremessage]=4;

        for(i=1;i<=256;i++) {
            lzwbuf[i]=i&255;
            lzwbuf2[i]=i;
        }
        lzwbuf2[0]=0;
        lzwbuf[0]=0;

        for(i=0;i<rnumwalls;i++)
        {
            readLE16(fil, &strtot, 2);
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
                readLE16(fil, &compleng, 2);
            else
                compleng = tileng[i];
            read(fil, &tempbuf[0], compleng);

            walsegg=walseg[i];

            if (strtot > 0)
            {
                tempbuf[compleng] = 0;
                tempbuf[compleng+1] = 0;
                tempbuf[compleng+2] = 0;
                bytecnt2 = 0;
                bytecnt1 = 0;
                bitcnt = 0;
                currstr = 256;
                goalstr = 512;
                numbits = 9;
                do
                {
                    dat=(((tempbuf[bytecnt2])|
                          ((K_UINT16)(tempbuf[bytecnt2+1]))<<8)>>bitcnt)&
                        ((1<<numbits)-1);
/*		    dat=((*((K_UINT16 *)(tempbuf+bytecnt2)))
                    >>bitcnt)&
                    ((1<<numbits)-1);*/
                    if (bitcnt+numbits>16) {
                        dat+=(((K_UINT16)tempbuf[bytecnt2+2])&
                              ((1<<((bitcnt+numbits)&15))-1))<<
                            (16-bitcnt);
                    }

                    bitcnt+=numbits;
                    bytecnt2+=bitcnt>>3;
                    bitcnt&=7;

                    lzwbuf2[currstr]=dat;

                    while(dat>=256) {
                        stack[stackp++]=lzwbuf[dat];

                        dat=lzwbuf2[dat];
                    }

                    lzwbuf[currstr-1]=dat;
                    lzwbuf[currstr]=dat;
                    dat=lzwbuf2[dat];
                    stack[stackp++]=dat;

                    while(stackp>0) {
                        stackp--;
                        if (bytecnt1<4096)
                            walsegg[bytecnt1++]=stack[stackp];
                    }
                    currstr++;
                    if (currstr == goalstr)
                    {
                        numbits++;
                        goalstr = (goalstr<<1);
                    }
                }
                while (currstr <= strtot);
            }
            else
                memcpy(walsegg, tempbuf, 4096);

            if (bmpkind[i+1] >= 2)
            {
                j=0;
                while((j<4096)&&(walsegg[j]!=255))
                    j++;
                j&=0xfc0;
                lborder[i+1]=j;
                j=4095;
                while((j>=0)&&(walsegg[j]!=255))
                    j--;
                j&=0xfc0;
                j+=64;
                rborder[i+1]=j;
            }
            else
            {
                lborder[i+1] = 0;
                rborder[i+1] = 4096;
            }
            if ((i < 127) && ((i&1)==0)) {
                if (debugmode)
                    fprintf(stderr, "Trying to draw screen buffer.\n");
                glDrawBuffer(GL_BACK);
                fade(64+(i>>1));
                SetVisibleScreenOffset(0);
                if (debugmode)
                    fprintf(stderr, "Screen buffer draw OK.\n");
            } else {
                glDrawBuffer(GL_FRONT);

                if (i==128) {
                    fade(63);
                }
            }

            j=(160-(rnumwalls>>2)+i);

            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
                if (i < (rnumwalls>>1)) {
                    screenbuffer[screenbufferwidth*219+j]=255;
                }
                else {
                    j-=rnumwalls>>1;
                    screenbuffer[screenbufferwidth*219+j]=0;
                }
                UploadPartialOverlay(j, 219, 1, 1);
            } else {
                if (i < (rnumwalls>>1)) {
                    screenbuffer[screenbufferwidth*199+j]=255;
                }
                else {
                    j-=rnumwalls>>1;
                    screenbuffer[screenbufferwidth*199+j]=63;
                }
                if (debugmode)
                    fprintf(stderr, "Trying to update screen buffer.\n");
                UploadPartialOverlay(j, 199, 1, 1);
                if (debugmode)
                    fprintf(stderr, "Screen buffer update OK.\n");
            }

            /* Use double buffer when fading, single buffer when not.
               Yes, I know I'm too clever for my own good. */

            if ((i<127) && ((i&1)==0))
                SDL_GL_SwapWindow(mainwindow);
            else
                glFlush();
            cwparam=&wparams[i];
            int minfilt=cwparam->minfilt;
            int magfilt=cwparam->magfilt;

            if (fullfilter==GL_NEAREST) {
                magfilt=GL_NEAREST;
                minfilt=GL_NEAREST;
            }
            if (cwparam->bmpkind_override!=-1)
                bmpkind[i+1] = cwparam->bmpkind_override;

            int wrapmode=(bmpkind[i+1]<2?1:0);

            if (cwparam->wrapmode!=-1)
                wrapmode=cwparam->wrapmode;

#if 0
            printf("Wall information for wall %d:\n", i);
            printf("Replacement image: %s\n", cwparam->texreplace);
            printf("Range: %d-%d\n", cwparam->tcl, cwparam->tch);
            printf("Wrap Mode: %d\n", wrapmode);
            printf("Min Filter: %d\n", minfilt);
            printf("Mag Filter: %d\n", magfilt);
            printf("bmpkind: %d\n", bmpkind[i+1]);
            printf("Shadow: %d\n", shadow[i+1]);
            printf("\n");
#endif
            if (cwparam->texreplace) {
                imgcache* cache=LoadImageCache(cwparam->texreplace, wrapmode, minfilt, magfilt);
                texName[i]=cache->texnum;
                if (cwparam->tch==-1) cwparam->tch=cache->w;
                walltexcoord[i][0]= ((double)cwparam->tcl/(double)cache->w);
                walltexcoord[i][1] = ((double)cwparam->tch / (double)cache->w);
                //printf("TexCoords for %d: %d %d /%d %.6f %.6f\n", i, cwparam->tcl, cwparam->tch, cache->w, walltexcoord[i][0], walltexcoord[i][1]);
            } else {
                walltexcoord[i][0]=0.0;
                walltexcoord[i][1]=1.0;

                glGenTextures(1, &texName[i]);

                glBindTexture(GL_TEXTURE_2D, texName[i]);
                checkGLStatus();

                if (wrapmode) {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                } else {
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                }
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magfilt);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minfilt);
                checkGLStatus();

                glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
                checkGLStatus();
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                checkGLStatus();

                /* Replace door1 with colour test image. */

#ifdef COLOURTEST
                if (i==door1-1) {
                    for(j=0;j<64;j++)
                        for(k=0;k<64;k++)
                            walsegg[j*64+k]=((j>>2)<<4)|(k>>2);
                }
#endif

                TextureConvert(walsegg, RGBATexture, bmpkind[i+1]);

                if (debugmode)
                    fprintf(stderr, "Trying to upload texture.\n");

                if (anisotropic)
                    SetAnisotropic();
                BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);

                if (debugmode)
                    fprintf(stderr, "Upload texture complete.\n");
                if (i==gameover-1) {
                    /* Keep two copies of this; one for walls, the other for spinning
                       overlay text. */
                    glGenTextures(1, &gameoversprite);

                    glBindTexture(GL_TEXTURE_2D, gameoversprite);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, partialfilter);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    fullfilter);
                    TextureConvert(walsegg, RGBATexture, 4);

                    if (anisotropic)
                        SetAnisotropic();
                    BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);
                }

                checkGLStatus();

                /*	    printf("Wall number %d:\n", i);

                        for(k=0;k<64;k++) {
                        for(j=0;j<64;j++) {
                        printf("%2x", walsegg[k+j*64]);
                        }
                        printf("\n");
                        }
                        printf("\n");*/
            }
        }
        close(fil);
    } else {
        fatal_error("Can't find walls.kzp.");
    }
    free(RGBATexture);

//      Examine textures...

/*  for(i=0;i<rnumwalls;i++) {
  glClear(GL_COLOR_BUFFER_BIT);
  printf("Texture number %d.\n", i);
  spridraw(180, 50, 512, i+1);
SDL_GL_SwapWindow(mainwindow);
pressakey();
}
/**/

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        visiblescreenyoffset=0;
        sprintf(textbuf,
               "\"LAB3D/SDL\" conversion v%d.%d.%d", LAB3D_VERSION_MAJOR, LAB3D_VERSION_MINOR, LAB3D_VERSION_PATCH);
        textprint(30, 222, 0);

        strcpy(textbuf,
               ".");
        textprint(260, 226, 0);
        textprint(264, 226, 0);
        strcpy(textbuf,
               "Copyright (c) 2002-2004 Jan Lonnberg");
        textprint(30, 232, 0);
    }

    if (dotransition && (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)) {
        /* Set up transition textures between walls properly. */

        TransitionTexture(0, 1, 2);
        TransitionTexture(2, 3, 0);
        TransitionTexture(6, 7, 8);
        TransitionTexture(8, 9, 6);

        /* Set up end of game rainbow. */

        TransitionTexture(424, 425, 426);
        TransitionTexture(47, 424, 425);
        TransitionTexture(425, 426, 47);
    } else {
        for (i=0;i<numsplits;i++) {
            splitTexNum[i]=-1;
        }

    }
    glDrawBuffer(GL_BACK);
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

/* Load a saved game... */

K_INT16 loadgame(K_INT16 gamenum)
{
    char filename[20];
    K_INT16 i;
    int fil;

    filename[0] = 'S', filename[1] = 'A', filename[2] = 'V';
    filename[3] = 'G', filename[4] = 'A', filename[5] = 'M';
    filename[6] = 'E', filename[7] = gamenum+48;
    filename[8] = '.', filename[9] = 'D', filename[10] = 'A';
    filename[11] = 'T', filename[12] = 0;
    sprintf(filepathUpper, "%s%s", gameroot, filename);
    if((fil=open(filepathUpper, O_RDONLY|O_BINARY, 0))==-1) {
        filename[0] = 's', filename[1] = 'a', filename[2] = 'v';
        filename[3] = 'g', filename[4] = 'a', filename[5] = 'm';
        filename[6] = 'e', filename[7] = gamenum+48;
        filename[8] = '.', filename[9] = 'd', filename[10] = 'a';
        filename[11] = 't', filename[12] = 0;
        sprintf(filepath, "%s%s", gameroot, filename);

        if((fil=open(filepath, O_RDONLY|O_BINARY, 0))==-1)
            return -1;
    }
    musicoff();
    read(fil, &hiscorenam[0], 16);
    read(fil, &hiscorenamstat, 1);
    readLE16(fil, &boardnum, 2);

    if (boardnum >= numboards)
        quitgame = 2;
    filename[0] = 'L', filename[1] = 'A', filename[2] = 'B';
    filename[3] = 'S', filename[4] = 'N', filename[5] = 'G';
    filename[6] = (boardnum/10)+48, filename[7] = (boardnum%10)+48;
    filename[8] = 0;
    mute = 1;
    loadmusic(filename);
    musicon();

    readLE32(fil, &scorecount, 4);
    readLE32(fil, &scoreclock, 4);

    readLE16(fil, &board[0][0], 8192);
    readLE16(fil, &skilevel, 2);
    readLE16(fil, &life, 2);
    readLE16(fil, &death, 2);
    readLE16(fil, &lifevests, 2);
    readLE16(fil, &lightnings, 2);
    readLE16(fil, &firepowers[0], 6);
    readLE16(fil, &bulchoose, 2);
    readLE16(fil, &keys[0], 4);
    readLE16(fil, &coins, 2);
    readLE16(fil, &compass, 2);
    readLE16(fil, &cheated, 2);
    readLE16(fil, &animate2, 2);
    readLE16(fil, &animate3, 2);
    readLE16(fil, &animate4, 2);
    readLE16(fil, &oscillate3, 2);
    readLE16(fil, &oscillate5, 2);
    readLE16(fil, &animate6, 2);
    readLE16(fil, &animate7, 2);
    readLE16(fil, &animate8, 2);
    readLE16(fil, &animate10, 2);
    readLE16(fil, &animate11, 2);
    readLE16(fil, &animate15, 2);
    readLE16(fil, &statusbar, 2);
    readLE16(fil, &statusbargoal, 2);
    readLE16(fil, &posx, 2);
    readLE16(fil, &posy, 2);
    readLE16(fil, &posz, 2);
    readLE16(fil, &ang, 2);
    readLE16(fil, &startx, 2);
    readLE16(fil, &starty, 2);
    readLE16(fil, &startang, 2);
    readLE16(fil, &angvel, 2);
    readLE16(fil, &vel, 2);
    readLE16(fil, &mxvel, 2);
    readLE16(fil, &myvel, 2);
    readLE16(fil, &svel, 2);
    readLE16(fil, &hvel, 2);
    readLE16(fil, &oldposx, 2);
    readLE16(fil, &oldposy, 2);
    readLE16(fil, &bulnum, 2);
    readLE16(fil, &bulang[0], bulnum<<1);
    readLE16(fil, &bulkind[0], bulnum<<1);
    readLE16(fil, &bulx[0], bulnum<<1);
    readLE16(fil, &buly[0], bulnum<<1);
    readLE32(fil, &bulstat[0], bulnum<<2);
    readLE32(fil, &lastbulshoot, 4);
    readLE16(fil, &mnum, 2);
    readLE16(fil, &mposx[0], mnum<<1);
    readLE16(fil, &mposy[0], mnum<<1);
    readLE16(fil, &mgolx[0], mnum<<1);
    readLE16(fil, &mgoly[0], mnum<<1);
    readLE16(fil, &moldx[0], mnum<<1);
    readLE16(fil, &moldy[0], mnum<<1);
    readLE16(fil, &mstat[0], mnum<<1);
    readLE16(fil, &mshock[0], mnum<<1);
    read(fil, &mshot[0], mnum);
    readLE16(fil, &doorx, 2);
    readLE16(fil, &doory, 2);
    readLE16(fil, &doorstat, 2);
    read(fil, &numwarps, 1);
    read(fil, &justwarped, 1);
    read(fil, &xwarp[0], numwarps);
    read(fil, &ywarp[0], numwarps);
    readLE32(fil, &totalclock, 4);
    ototclock = totalclock;
    readLE32(fil, &purpletime, 4);
    readLE32(fil, &greentime, 4);
    readLE32(fil, &capetime[0], 8);
    readLE32(fil, &musicstatus, 4);
    readLE16(fil, (K_INT16 *)(&clockspeed), 2); /* volatile warning here. */
    readLE32(fil, &count, 4);
    readLE32(fil, &countstop, 4);
    readLE16(fil, &nownote, 2);
    readLE16(fil, &i, 2);
    readLE32(fil, &chanage, 18<<2);
    read(fil, &chanfreq, 18);
    readLE16(fil, &midiinst, 2);
    readLE16(fil, &mute, 2);
    read(fil, &namrememberstat, 1);
    readLE16(fil, &fadewarpval, 2);
    readLE16(fil, &fadehurtval, 2);
    readLE16(fil, &slottime, 2);
    readLE16(fil, &slotpos[0], 6);
    readLE16(fil, &owecoins, 2);
    readLE16(fil, &owecoinwait, 2);
    close(fil);

    bossmonster = 0;

    if (((i == -1) || (i > 2)) && (musicsource != MUSIC_SOURCE_NONE))
    {
        musicoff();
        loadmusic(filename);
        musicon();
    }
    totalclock -= 2;
    copyslots(slotto);
    totalclock++;
    copyslots(slotto+1);
    totalclock++;

    yourhereoldpos = ((posx>>10)<<6)+(posy>>10);

    for(i=0;i<4096;i++)
        walseg[map-1][i]=board[0][i]&255;
    walseg[map-1][yourhereoldpos]=255;

    if ((vidmode == 0) && (statusbargoal > 400))
    {
        statusbar -= 80;
        statusbargoal -= 80;
    }
    if ((vidmode == 1) && (statusbargoal < 400))
    {
        statusbar += 80;
        statusbargoal += 80;
    }
    if (vidmode == 0)
    {
        scrsize = 18000-2880;
        if (statusbar == 399)
            scrsize += 2880;
    }
    else
    {
        scrsize = 21600-2880;
        if (statusbar == 479)
        {
            scrsize += 2880;
        }
    }
    linecompare(statusbar);
    statusbaralldraw();
    if (compass>0) showcompass(ang);
    fade(63);
    return 0;
}

/* Save game... */

K_INT16 savegame(K_INT16 gamenum)
{
    char filename[20];
    int i, fil;

    /* If we have an upper case copy of this save game, destroy it. */

    sprintf(filename, "SAVGAME%d.DAT", gamenum);
    sprintf(filepathUpper, "%s%s", gameroot, filename);

    unlink(filepathUpper);

    sprintf(filename, "savgame%d.dat", gamenum);
    sprintf(filepath, "%s%s", gameroot, filename);
    if((fil=open(filepath, O_CREAT|O_WRONLY|O_BINARY,
                 S_IWRITE|S_IREAD|S_IRGRP|S_IROTH))==-1) {
        return(-1);
    }
    write(fil, &hiscorenam[0], 16);
    write(fil, &hiscorenamstat, 1);
    writeLE16(fil, &boardnum, 2);
    writeLE32(fil, &scorecount, 4);
    writeLE32(fil, &scoreclock, 4);

    for(i=0;i<16;i++)
        gamehead[gamenum][i] = hiscorenam[i];
    gamehead[gamenum][16] = hiscorenamstat;

    writeshort(&(gamehead[gamenum][17]), boardnum);
    writelong(&(gamehead[gamenum][19]), scorecount);
    writelong(&(gamehead[gamenum][23]), scoreclock);

    gamexist[gamenum] = 1;
    writeLE16(fil, &board[0][0], 8192);
    writeLE16(fil, &skilevel, 2);
    writeLE16(fil, &life, 2);
    writeLE16(fil, &death, 2);
    writeLE16(fil, &lifevests, 2);
    writeLE16(fil, &lightnings, 2);
    writeLE16(fil, &firepowers[0], 6);
    writeLE16(fil, &bulchoose, 2);
    writeLE16(fil, &keys[0], 4);
    writeLE16(fil, &coins, 2);
    writeLE16(fil, &compass, 2);
    writeLE16(fil, &cheated, 2);
    writeLE16(fil, &animate2, 2);
    writeLE16(fil, &animate3, 2);
    writeLE16(fil, &animate4, 2);
    writeLE16(fil, &oscillate3, 2);
    writeLE16(fil, &oscillate5, 2);
    writeLE16(fil, &animate6, 2);
    writeLE16(fil, &animate7, 2);
    writeLE16(fil, &animate8, 2);
    writeLE16(fil, &animate10, 2);
    writeLE16(fil, &animate11, 2);
    writeLE16(fil, &animate15, 2);
    writeLE16(fil, &statusbar, 2);
    writeLE16(fil, &statusbargoal, 2);
    writeLE16(fil, &posx, 2);
    writeLE16(fil, &posy, 2);
    writeLE16(fil, &posz, 2);
    writeLE16(fil, &ang, 2);
    writeLE16(fil, &startx, 2);
    writeLE16(fil, &starty, 2);
    writeLE16(fil, &startang, 2);
    writeLE16(fil, &angvel, 2);
    writeLE16(fil, &vel, 2);
    writeLE16(fil, &mxvel, 2);
    writeLE16(fil, &myvel, 2);
    writeLE16(fil, &svel, 2);
    writeLE16(fil, &hvel, 2);
    writeLE16(fil, &oldposx, 2);
    writeLE16(fil, &oldposy, 2);
    writeLE16(fil, &bulnum, 2);
    writeLE16(fil, &bulang[0], bulnum<<1);
    writeLE16(fil, &bulkind[0], bulnum<<1);
    writeLE16(fil, &bulx[0], bulnum<<1);
    writeLE16(fil, &buly[0], bulnum<<1);
    writeLE32(fil, &bulstat[0], bulnum<<2);
    writeLE32(fil, &lastbulshoot, 4);
    writeLE16(fil, &mnum, 2);
    writeLE16(fil, &mposx[0], mnum<<1);
    writeLE16(fil, &mposy[0], mnum<<1);
    writeLE16(fil, &mgolx[0], mnum<<1);
    writeLE16(fil, &mgoly[0], mnum<<1);
    writeLE16(fil, &moldx[0], mnum<<1);
    writeLE16(fil, &moldy[0], mnum<<1);
    writeLE16(fil, &mstat[0], mnum<<1);
    writeLE16(fil, &mshock[0], mnum<<1);
    write(fil, &mshot[0], mnum);
    writeLE16(fil, &doorx, 2);
    writeLE16(fil, &doory, 2);
    writeLE16(fil, &doorstat, 2);
    write(fil, &numwarps, 1);
    write(fil, &justwarped, 1);
    write(fil, &xwarp[0], numwarps);
    write(fil, &ywarp[0], numwarps);
    writeLE32(fil, &totalclock, 4);
    writeLE32(fil, &purpletime, 4);
    writeLE32(fil, &greentime, 4);
    writeLE32(fil, &capetime[0], 8);
    writeLE32(fil, &musicstatus, 4);
    writeLE16(fil, (K_INT16 *)(&clockspeed), 2);
    writeLE32(fil, &count, 4);
    writeLE32(fil, &countstop, 4);
    writeLE16(fil, &nownote, 2);
    writeLE16(fil, &musicsource, 2);
    writeLE32(fil, &chanage, 18<<2);
    write(fil, &chanfreq, 18);
    writeLE16(fil, &midiinst, 2);
    writeLE16(fil, &mute, 2);
    write(fil, &namrememberstat, 1);
    writeLE16(fil, &fadewarpval, 2);
    writeLE16(fil, &fadehurtval, 2);
    writeLE16(fil, &slottime, 2);
    writeLE16(fil, &slotpos[0], 6);
    writeLE16(fil, &owecoins, 2);
    writeLE16(fil, &owecoinwait, 2);
    close(fil);
    ksayui(16);
    return 0;
}

/* Common variables between introduction() and drawintroduction().
   Messy, but works. */

static K_INT32 dalasti, pickskiltime;
static K_INT16 animater2, sharemessplc, newgamepisode;
static K_UINT16 introplc;

/* Draw introduction frame... */

void drawintroduction() {
    K_INT32 dai=totalclock>>2;
    K_INT16 m;

    if ((dai >= 0) && (dai < 300))
    {
        introplc = 5+times90[dai>>2];
        if (introplc > 3785)
            introplc = 3785;

        SetVisibleScreenOffset(introplc);

        if (dai < 240)
        {
            pictur(180+(240-((int)dai)), 164, 64*4, ((((unsigned)dai)<<8)/15)&2047, copyright);
            pictur(180-(240-((int)dai)), 164, 64*4, (((((unsigned)dai)<<8)/15)&2047)^2047, copyright);
        }
        else
        {
            if (vidmode == 0)
                m = 20;
            else
                m = 0;
            statusbardraw(0, 44, 63, 20, 128+m, 112, sodapics);
            spridraw(180-32, 164-32, 64*4, copyright);
        }
    }
    if ((dai >= 300) && (dai < 364))
    {
        SetVisibleScreenOffset(introplc);

        if (vidmode == 0)
            m = 20;
        else
            m = 0;
        statusbardraw(0, 44, 63, 20, 128+m, 112, sodapics);
        m = (364-dai);
        spridraw(180-(m>>1), 164-(m>>1), m<<2, copyright);
    }
    if ((dai >= 364) && (dai < 424))
    {
        SetVisibleScreenOffset(introplc);

        wipeoverlay(0, 174, 361, 20);
        if (vidmode == 0)
            m = 20;
        else
            m = 0;
        statusbardraw(0, 36, 63, 8, 128+m, 112, sodapics);
        strcpy(&textbuf[0], "Select Episode");
        textprint(124, 112, (char)96);

        if (vidmode == 0)
            m = 20;
        else
            m = 0;
/*	statusbardraw(0, 44, 63, 20, 128+m, 112, sodapics);*/
        m = (dai-360);
        spridraw(180-96-(m>>1), 152-(m>>1), m<<2, episodesign1off);
        spridraw(180-(m>>1), 152-(m>>1), m<<2, episodesign2off);
        spridraw(180+96-(m>>1), 152-(m>>1), m<<2, episodesign3off);
    }
    if (dai >= 424)
    {
        if (sharemessplc >= 0)
        {
            if (vidmode == 0)
                m = 20;
            else
                m = 0;
            /*
            k = 0;
            if (numboards == 10)
                k = 32;
        
              statusbardraw(sharemessplc, k, 64-sharemessplc, 32, m, 32, sharewaremessage);
              statusbardraw(0, k, 64-sharemessplc, 32, 256+m+sharemessplc, 32, sharewaremessage);*/
            sharemessplc--;
        }
        if (pickskiltime < 0)
        {
            SetVisibleScreenOffset(introplc);

/*	    if (dalasti < 424)
            {*/
            wipeoverlay(0, 174, 361, 20);
            if (vidmode == 0)
                m = 20;
            else
                m = 0;
            statusbardraw(0, 36, 63, 8, 128+m, 112, sodapics);
            strcpy(&textbuf[0], "Select Episode");
            textprint(124, 112, (char)96);
/*	    }*/
            animater2 = (dai&16);
            if ((newgamepisode != 1) || (animater2 == 0))
                spridraw(180-128, 120, 64*4, episodesign1off);
            else
                spridraw(180-128, 120, 64*4, episodesign1on);
            if ((newgamepisode != 2) || (animater2 == 0))
                spridraw(180-32, 120, 64*4, episodesign2off);
            else
                spridraw(180-32, 120, 64*4, episodesign2on);
            if ((newgamepisode != 3) || (animater2 == 0))
                spridraw(180+64, 120, 64*4, episodesign3off);
            else
                spridraw(180+64, 120, 64*4, episodesign3on);
        }
        else if (dai >= pickskiltime+128)
        {
            SetVisibleScreenOffset(introplc);

/*	    if (dalasti < pickskiltime+128)
            {*/
            wipeoverlay(0, 174, 361, 8);
            strcpy(&textbuf[0], "Select Difficulty");
            textprint(112, 112, (char)96);
/*	    }*/
            animater2 = (dai&16);
            if ((skilevel != 0) || (animater2 == 0))
                spridraw(180-48-32, 120, 64*4, skilevel1);
            else {
                spridraw(180-48-32, 120, 64*4, skilevel1);
                spridraw(180-48-32, 120, 64*4, skilblank);
            }
            if ((skilevel != 1) || (animater2 == 0))
                spridraw(180+48-32, 120, 64*4, skilevel2);
            else {
                spridraw(180+48-32, 120, 64*4, skilevel2);
                spridraw(180+48-32, 120, 64*4, skilblank);
            }
        }
    }
    if ((pickskiltime >= 0) && (dai < pickskiltime+64))
    {
        SetVisibleScreenOffset(introplc);

        wipeoverlay(0, 174, 361, 20);
        if (vidmode == 0)
            m = 20;
        else
            m = 0;
        statusbardraw(0, 36, 63, 8, 128+m, 112, sodapics);
        strcpy(&textbuf[0], "Select Episode");
        textprint(124, 112, (char)96);

        m = ((pickskiltime+63)-dai);
        spridraw(180-96-(m>>1), 152-(m>>1), m<<2, episodesign1off);
        spridraw(180-(m>>1), 152-(m>>1), m<<2, episodesign2off);
        spridraw(180+96-(m>>1), 152-(m>>1), m<<2, episodesign3off);
    }
    if ((pickskiltime >= 0) && (dai >= pickskiltime+64) && (dai < pickskiltime+128))
    {
        SetVisibleScreenOffset(introplc);

        wipeoverlay(0, 174, 361, 8);
        strcpy(&textbuf[0], "Select Difficulty");
        textprint(112, 112, (char)96);

        m = (dai-(pickskiltime+64));
        spridraw(180-48-(m>>1), 152-(m>>1), m<<2, skilevel1);
        spridraw(180+48-(m>>1), 152-(m>>1), m<<2, skilevel2);
    }
}

/* Introduction... */

void introduction(K_INT16 songnum)
{
    K_INT16 j;
    K_INT16 leaveintro;
    K_INT32 dai=0;

    ingame=0;
    dside = 200;
    halfheight = 100;
    scrsize = 18000;
    dalasti = 0;

    spriteyoffset=20;

    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!introskip) {
        if (songnum == 0)
            loadmusic("INTRO");
        else
            loadmusic("INTRO2");
    }
    clockspeed = 0;
    ototclock = -1;
    totalclock = 1;
    introplc = 5;

    if (!introskip) {
        musicon();
        if (saidwelcome == 0)
        {
            ksayui(18);
            saidwelcome = 1;
        }
    }
/*    for(i=0;i<360;i++)
      height[i] = 0;*/
    boardnum = 0;
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_CANCEL);
    sharemessplc = 64;
    newgamepisode = 1;
    animater2 = 0;
    totalclock = 0;
    leaveintro = introskip;
    pickskiltime = -32768;

    while (leaveintro == 0)
    {
        PollInputs();
        bstatus = 0;
        if (moustat == 0)
        {
            bstatus=readmouse(NULL, NULL);
        }
        j = (((int)labs((totalclock%120)-60))>>3);

        SDL_LockMutex(timermutex);
        while(clockspeed<4) {
            SDL_UnlockMutex(timermutex);
            SDL_Delay(10);
            SDL_LockMutex(timermutex);
        }
        totalclock += clockspeed;
        clockspeed = 0;
        SDL_UnlockMutex(timermutex);

        dalasti = dai;
        dai = (totalclock>>2);
        if (repeatkeydef(ACTION_MENU_UP1) ||
            repeatkeydef(ACTION_MENU_UP2) ||
            repeatkeydef(ACTION_MENU_LEFT1) ||
            repeatkeydef(ACTION_MENU_LEFT2))
        {
            if (pickskiltime < 0)
            {
                newgamepisode--;
                if (newgamepisode == 0)
                    newgamepisode = 3;
            }
            else
                skilevel = 1 - skilevel;
            ksayui(27);
        }
        if (repeatkeydef(ACTION_MENU_DOWN1) ||
            repeatkeydef(ACTION_MENU_DOWN2) ||
            repeatkeydef(ACTION_MENU_RIGHT1) ||
            repeatkeydef(ACTION_MENU_RIGHT2))
        {
            if (pickskiltime < 0)
            {
                newgamepisode++;
                if (newgamepisode == 4)
                    newgamepisode = 1;
            }
            else
                skilevel = 1 - skilevel;
            ksayui(27);
        }
        if (getkeydefstat(ACTION_MENU) > 0)
        {
            lastunlock = 1;
            lastshoot = 1;
            lastbarchange = 1;
            sortcnt = -1;
            saidwelcome = introplc;
            spriteyoffset=0;
            j = mainmenu();
            spriteyoffset=20;
            saidwelcome = 1;
            sortcnt = 0;
            if ((j == MAINMENU_NEWGAME) && (newgameplace >= 0))
            {
                if (newgameplace == 0) boardnum = 0, newgamepisode = 1;
                if (newgameplace == 1) boardnum = 10, newgamepisode = 2;
                if (newgameplace == 2) boardnum = 20, newgamepisode = 3;
                leaveintro = 1;
            }
            if ((j == MAINMENU_LOADGAME) && (loadsavegameplace >= 0))
            {
                musicoff();
                setgamevideomode();
                if (vidmode == 0)
                {
                    dside = 200;
                    halfheight = 100;
                    scrsize = 18000-2880;
                }
                else
                {
                    dside = 240;
                    halfheight = 120;
                    scrsize = 21600-2880;
                }
                spriteyoffset=0;
                visiblescreenyoffset=0;
                ingame=1;
                settransferpalette();
                loadgame(loadsavegameplace);
                wipeoverlay(0, 0, 361, statusbaryoffset);
                fade(63);
                return;
            }
            if (j == MAINMENU_EXIT)
            {
                newgamepisode = 1;
                leaveintro = 1;
                quitgame = 1;
            }
            clearkeydefstat(ACTION_MENU);
            clearkeydefstat(ACTION_MENU_CANCEL);
            clearkeydefstat(ACTION_MENU_SELECT3);
            clearkeydefstat(ACTION_MENU_SELECT1);
            SDL_LockMutex(timermutex);
            clockspeed = 0;
            SDL_UnlockMutex(timermutex);
        }
        if ((getkeydefstatlock(ACTION_MENU_CANCEL) != 0) ||
            (getkeydefstatlock(ACTION_MENU_SELECT1) != 0) ||
            (getkeydefstatlock(ACTION_MENU_SELECT2) != 0) ||
            (getkeydefstatlock(ACTION_MENU_SELECT3) != 0) ||
            (bstatus != 0))
        {
            if (dai < 420)
                leaveintro = 1;
            else if (((newgamepisode == 2) && (numboards < 20)) || ((newgamepisode == 3) && (numboards < 30)))
            {
                ksayui(12);
                SDL_LockMutex(timermutex);
                clockspeed = 0;
                SDL_UnlockMutex(timermutex);
            }
            else
            {
                if (pickskiltime == -32768)
                    pickskiltime = dai;
                else
                    leaveintro = 1;
            }
            clearkeydefstat(ACTION_MENU_CANCEL);
            clearkeydefstat(ACTION_MENU_SELECT1);
            clearkeydefstat(ACTION_MENU_SELECT2);
            clearkeydefstat(ACTION_MENU_SELECT3);
        }
        if ((dai >= 0) && (dai < 128))
            fade(64+((int)(dai>>1)));
        if ((dai >= 128) && (dalasti < 128))
            fade(63);
        drawintroduction();
        SDL_GL_SwapWindow(mainwindow);
    }

    spriteyoffset=0;

    if (newgamepisode == 1) boardnum = 0;
    if (newgamepisode == 2) boardnum = 10;
    if (newgamepisode == 3) boardnum = 20;
    if ((newgamepisode == 2) && (numboards < 20))
        newgamepisode = 1, boardnum = 0;
    if ((newgamepisode == 3) && (numboards < 30))
        newgamepisode = 1, boardnum = 0;
    musicoff();
    setgamevideomode();
    fade(0);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_CANCEL);
    if (vidmode == 0)
    {
        dside = 200;
        halfheight = 100;
        scrsize = 18000-2880;
    }
    else
    {
        dside = 240;
        halfheight = 120;
        scrsize = 21600-2880;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    loadboard();
    owecoins = 0;
    sortcnt = 0;
    oldlife = 0;
    life = 4095;
    death = 4095;
    lifevests = 1;
    switch (boardnum)
    {
        case 0: firepowers[0] = 0; firepowers[1] = 0;
            firepowers[2] = 0; lightnings = 0;
            break;
        case 10: firepowers[0] = 3; firepowers[1] = 2;
            firepowers[2] = 0; lightnings = 1;
            break;
        case 20: firepowers[0] = 4; firepowers[1] = 3;
            firepowers[2] = 2; lightnings = 2;
            break;
    }
    coins = 0;
    bulchoose = 0;
    animate2 = 0;
    animate3 = 0;
    animate4 = 0;
    oscillate3 = 0;
    oscillate5 = 0;
    animate6 = 0;
    animate7 = 0;
    animate8 = 0;
    animate10 = 0;
    animate11 = 0;
    animate15 = 0;
    ototclock = -1;
    totalclock = 1;
    purpletime = 0;
    greentime = 0;
    capetime[0] = 0;
    capetime[1] = 0;
    compass = 0;
    if (boardnum >= 10) compass = 1;
    cheated = 0;
    doorstat = 0;
    statusbar = 335;
    if (vidmode == 1)
        statusbar += 80;
    statusbargoal = statusbar;
    linecompare(statusbar);
    ingame=1;
    settransferpalette();
    namrememberstat = hiscorenamstat;
    hiscorenamstat = 0;
    hiscorenam[0] = 0;
    SDL_LockMutex(timermutex);
    clockspeed = 0;
    SDL_UnlockMutex(timermutex);
    scoreclock = 0;
    scorecount = 0;
    fadewarpval = 63;
    fadehurtval = 0;
    visiblescreenyoffset=0;
    wipeoverlay(0, 0, 361, statusbaryoffset);
    glDrawBuffer(GL_BACK);
    statusbaralldraw();
}

#ifdef __SWITCH__
void configureResolution()
{
    // Get current console status
    appletMainLoop();

    // Calculate the target resolution depending on the operation mode:
    // - In handheld mode, we render at 720p (which is the native screen resolution).
    // - In docked mode, we render at full 1080p (which is outputted to a compatible HDTV screen).
    switch (appletGetOperationMode())
    {
        default:
        case AppletOperationMode_Handheld:
            screenwidth = 1280;
            screenheight = 720;
            break;
        case AppletOperationMode_Docked:
            screenwidth = 1920;
            screenheight = 1080;
            break;
    }

    // Apply the resolution, and configure the correct GL viewport.
    // We want to render to the top left corner of the framebuffer (other areas will
    // remain unused when rendering at a smaller resolution than the framebuffer).
    // Note that glViewport expects the coordinates of the bottom-left corner of
    // the viewport, so we have to calculate that too.
    // nwindowSetCrop(win, 0, 0, screenwidth, screenheight);
    // glViewport(0, 1080-screenheight, screenwidth, screenheight);
}
#endif

/* Load KSM file... */

K_INT16 loadmusic(char *filename)
{
    unsigned char buffer[256], instbuf[11];
    int infile;
    K_INT16 i, j, k, numfiles;
    K_INT32 filoffs;

    strcpy(lastPlayedMusicFile, filename);

    FILE *file;

    if (musicsource == MUSIC_SOURCE_NONE)
        return(-1);
    if (firstime == 1)
    {
        if (musicsource == MUSIC_SOURCE_MIDI)
        {
            /* Open KSM->MIDI instrument translation table... */
            sprintf(filepath, "%sksmmidi.txt", gameroot);
            file=fopen(filepath, "rt");
            if (file==NULL) {
                fprintf(stderr, "ksmmidi.txt not found; music disabled.\n");
                musicsource = MUSIC_SOURCE_NONE;
                return -1;
            }
            for(i=0;i<256;i++)
                fscanf(file, "%d", &gminst[i]);
            fclose(file);
        }
        if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
        {
            sprintf(filepath, "%sinsts.dat", gameroot);
            sprintf(filepathUpper, "%sINSTS.DAT", gameroot);
            if(((infile=open(filepath, O_RDONLY|O_BINARY, 0))==-1)&&
               ((infile=open(filepathUpper, O_RDONLY|O_BINARY, 0))==-1))
                return(-1);
            for(i=0;i<256;i++)
            {
                read(infile, &buffer, 33);
                for(j=0;j<11;j++)
                    inst[i][j] = buffer[j+20];
            }
            close(infile);
            numchans = 9;

            outdata((char)0, (char)0x1, (char)32);  //clear test stuff
            outdata((char)0, (char)0x4, (char)0);   //reset
            outdata((char)0, (char)0x8, (char)0);   //2-operator synthesis

            firstime = 0;
        }
    }
    sprintf(filepath, "%ssongs.kzp", gameroot);
    sprintf(filepathUpper, "%sSONGS.KZPT", gameroot);
    if (((infile = open(filepath, O_RDONLY|O_BINARY, 0))==-1)&&
        ((infile = open(filepathUpper, O_RDONLY|O_BINARY, 0))==-1))
        return(-1);
    readLE16(infile, &numfiles, 2);
    i = 0;
    j = 1;
    while ((j == 1) && (i < numfiles))
    {
        read(infile, &buffer[0], 12);
        j = 0;
        k = 0;
        while ((filename[k] != 0) && (k < 8))
        {
            if (buffer[k] != filename[k])
                j = 1;
            k++;
        }
        i++;
    }
    if (j == 1)
    {
        close(infile);
        return(-1);
    }

    filoffs=readlong(buffer+8);
    lseek(infile, filoffs, SEEK_SET);
    read(infile, &trinst[0], 16);
    read(infile, &trquant[0], 16);
    read(infile, &trchan[0], 16);
    read(infile, &trprio[0], 16);
    read(infile, &trvol[0], 16);
    readLE16(infile, &numnotes, 2);
    readLE32(infile, note, numnotes*4);
    close(infile);
    numchans = 9-trchan[11]*3;

    switch (musicsource) {
        case MUSIC_SOURCE_MIDI:
            setmidiinsts();
            break;
        case MUSIC_SOURCE_ADLIB_RANDOM:
            for (int i = 0; i < numchans; i++)
                trinst[i] = rand() % 85;
        case MUSIC_SOURCE_ADLIB:
            if (trchan[11] == 0) {
                drumstat = 0;
                outdata((char)0, (unsigned char)0xbd, (unsigned char)drumstat);
            }
            if (trchan[11] == 1) {
                for(i=0;i<11;i++)
                    instbuf[i] = inst[trinst[11]][i];
                instbuf[1] = ((instbuf[1]&192)|((trvol[11])^63));
                setinst(0, 6, instbuf[0], instbuf[1], instbuf[2], instbuf[3], instbuf[4], instbuf[5], instbuf[6], instbuf[7], instbuf[8], instbuf[9], instbuf[10]);
                for(i=0;i<5;i++)
                    instbuf[i] = inst[trinst[12]][i];
                for(i=5;i<11;i++)
                    instbuf[i] = inst[trinst[15]][i];
                instbuf[1] = ((instbuf[1]&192)|((trvol[12])^63));
                instbuf[6] = ((instbuf[6]&192)|((trvol[15])^63));
                setinst(0, 7, instbuf[0], instbuf[1], instbuf[2], instbuf[3], instbuf[4], instbuf[5], instbuf[6], instbuf[7], instbuf[8], instbuf[9], instbuf[10]);
                for(i=0;i<5;i++)
                    instbuf[i] = inst[trinst[14]][i];
                for(i=5;i<11;i++)
                    instbuf[i] = inst[trinst[13]][i];
                instbuf[1] = ((instbuf[1]&192)|((trvol[14])^63));
                instbuf[6] = ((instbuf[6]&192)|((trvol[13])^63));
                setinst(0, 8, instbuf[0], instbuf[1], instbuf[2], instbuf[3], instbuf[4], instbuf[5], instbuf[6], instbuf[7], instbuf[8], instbuf[9], instbuf[10]);
                outdata((char)0, (unsigned char)0xa6, (unsigned char)(600&255));
                outdata((char)0, (unsigned char)0xb6, (unsigned char)((600>>8)&223));
                outdata((char)0, (unsigned char)0xa7, (unsigned char)(400&255));
                outdata((char)0, (unsigned char)0xb7, (unsigned char)((400>>8)&223));
                outdata((char)0, (unsigned char)0xa8, (unsigned char)(5510&255));
                outdata((char)0, (unsigned char)0xb8, (unsigned char)((5510>>8)&223));
                drumstat = 32;
                outdata((char)0, (unsigned char)0xbd, (unsigned char)drumstat);
            }

    }
    return 0;
}

void outdata(unsigned char synth, unsigned char index, unsigned char data)
{
    adlib0(index, data);
}

/* Start music (and start timer!). */

void musicon()
{
    K_INT16 i, j, k, ksaystat;
    unsigned char instbuf[11];
    K_UINT32 templong;

    if (musicsource != MUSIC_SOURCE_NONE)
    {
        for(i=0;i<numchans;i++)
        {
            chantrack[i] = 0;
            chanage[i] = 0;
        }
        j = 0;
        for(i=0;i<16;i++)
            if ((trchan[i] > 0) && (j < numchans))
            {
                k = trchan[i];
                while ((j < numchans) && (k > 0))
                {
                    chantrack[j] = i;
                    k--;
                    j++;
                }
            }
        if (musicsource == MUSIC_SOURCE_MIDI)
            setmidiinsts();
        for(i=0;i<numchans;i++)
        {
            if (musicsource == MUSIC_SOURCE_ADLIB || MUSIC_SOURCE_ADLIB_RANDOM)
            {   
                for(j=0;j<11;j++)
                    instbuf[j] = inst[trinst[chantrack[i]]][j];
                instbuf[1] = ((instbuf[1]&192)|(63-trvol[chantrack[i]]));
                setinst(0, i, instbuf[0], instbuf[1], instbuf[2], instbuf[3], instbuf[4], instbuf[5], instbuf[6], instbuf[7], instbuf[8], instbuf[9], instbuf[10]);
            }
            chanfreq[i] = 0;
        }
    }
    templong=note[0];
    count = (templong>>12)-1;
    countstop = (templong>>12)-1;
    nownote = 0;
    SDL_LockMutex(soundmutex);
    musicstatus = 1;
    ksaystat = 0;
    if (musicpan) randominsts();

    if (ksaystat == 0)
    {
        lastTick=SDL_GetTicks();
        if (!timer)
            timer=SDL_AddTimer(4, tickhandler, NULL);/* 250 Hz, should be 240.*/
    }
    SDL_UnlockMutex(soundmutex);
}

/* Stop music (and timer!). */

void musicoff()
{
    K_INT16 i, ksaystat;

    ksaystat = 0;
    if (ksaystat == 0)
    {
        if (timer) {
            SDL_RemoveTimer(timer);
            timer=0;
        }
    }
    if (musicsource == MUSIC_SOURCE_MIDI) {
#ifdef WIN32
        midiOutReset(sequencerdevice);
#endif
#ifdef USE_OSS
        ioctl(sequencerdevice, SNDCTL_SEQ_PANIC);
        ioctl(sequencerdevice, SNDCTL_SEQ_RESET);
#endif
        setmidiinsts();
    }
    if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
        for(i=0;i<numchans;i++)
        {
            outdata((char)0, (unsigned char)(0xa0+i), (char)0);
            outdata((char)0, (unsigned char)(0xb0+i), (char)0);
        }
    SDL_LockMutex(soundmutex);
    musicstatus = 0;
    SDL_UnlockMutex(soundmutex);
}

/* SDL tick handler, complete with routine to even out timer ticks at funny
   intervals... */

void updateclock(void) {
    Uint32 now;

    now=SDL_GetTicks();

    while(((lastTick+(4+(tickFrac==0)))<=now)||(lastTick>now)) {
        if (!soundtimer) clockspeed++;
        if (musicsource != MUSIC_SOURCE_ADLIB && musicsource != MUSIC_SOURCE_ADLIB_RANDOM) ksmhandler();
        lastTick+=4+(tickFrac==0);
        tickFrac++;
        if (tickFrac==6) tickFrac=0;
        }
}

Uint32 tickhandler(Uint32 interval, void *param) {
    SDL_LockMutex(timermutex);
    updateclock();
    SDL_UnlockMutex(timermutex);
    return interval;
}

/* Update clock and music... */

void ksmhandler(void)
{
    K_INT16 i, j, quanter, bufnum, chan, drumnum, freq;
    K_UINT32 temp, templong;

    count++;
    if ((count >= countstop) && (musicsource != MUSIC_SOURCE_NONE))
    {
        bufnum = 0;
        while (count >= countstop)
        {
            templong=note[nownote];
            if (musicsource == MUSIC_SOURCE_NONE)
                if ((((templong>>8)&15) == 0) && ((templong&64) > 0))
                    databuf[bufnum++] = (unsigned char)(templong&63);
            if (musicsource != MUSIC_SOURCE_NONE)
            {
                if (((templong&255) >= 1) && ((templong&255) <= 61))
                {
                    i = 0;
                    while (((chanfreq[i] != (templong&63)) || (chantrack[i] != ((templong>>8)&15))) && (i < numchans))
                        i++;
                    if (i < numchans)
                    {
                        if (musicsource == MUSIC_SOURCE_MIDI)
                        {
                            databuf[bufnum++] = (unsigned char)(0x80)+i;
                            databuf[bufnum++] = (unsigned char)(templong&63)+
                                23;
                            databuf[bufnum++] = (unsigned char)0;
                        }
                        if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
                        {
                            databuf[bufnum++] = (unsigned char)(0xa0+i);
                            databuf[bufnum++] = (unsigned char)(adlibfreq[templong&63]&255);
                            databuf[bufnum++] = (unsigned char)(0xb0+i);
                            databuf[bufnum++] = (unsigned char)((adlibfreq[templong&63]>>8)&223);
                        }
                        chanfreq[i] = 0;
                        chanage[i] = 0;
                    }
                }
                else if (((templong&255) >= 65) && ((templong&255) <= 125))
                {
                    if (((templong>>8)&15) < 11)
                    {
                        temp = 0;
                        i = numchans;
                        for(j=0;j<numchans;j++)
                            if ((countstop - chanage[j] >= temp) && (chantrack[j] == ((templong>>8)&15)))
                            {
                                temp = countstop - chanage[j];
                                i = j;
                            }
                        if (i < numchans)
                        {
                            if (musicsource == MUSIC_SOURCE_MIDI)
                            {
                                if (chanfreq[i]!=0) {
                                    databuf[bufnum++] = (unsigned char)
                                        (0x80+i);
                                    databuf[bufnum++] = (unsigned char)
                                        chanfreq[i]+23;
                                    databuf[bufnum++] = (unsigned char)0;
                                }
                                databuf[bufnum++] = (unsigned char)(0x90+i);
                                databuf[bufnum++] = (unsigned char)(templong&63)+23;
                                databuf[bufnum++] = (unsigned char)
                                    trvol[chantrack[i]]<<1;
                            }
                            if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
                            {
                                databuf[bufnum++] = (unsigned char)(0xa0+i);
                                databuf[bufnum++] = (unsigned char)0;
                                databuf[bufnum++] = (unsigned char)(0xb0+i);
                                databuf[bufnum++] = (unsigned char)0;
                                databuf[bufnum++] = (unsigned char)(0xa0+i);
                                databuf[bufnum++] = (unsigned char)(adlibfreq[templong&63]&255);
                                databuf[bufnum++] = (unsigned char)(0xb0+i);
                                databuf[bufnum++] = (unsigned char)((adlibfreq[templong&63]>>8)|32);
                            }
                            chanfreq[i] = templong&63;
                            chanage[i] = countstop;
                        }
                    }
                    else
                    {
                        if (musicsource == MUSIC_SOURCE_MIDI)
                        {
                            databuf[bufnum++] = (unsigned char)(0x99);
                            switch((templong>>8)&15)
                            {
                                case 11: drumnum = 36; break;
                                case 12: drumnum = 38; break;
                                case 13: drumnum = 37; break;
                                case 14: drumnum = 51; break;
                                case 15: drumnum = 42; break;
                                default: drumnum = 36; break;
                            }
                            databuf[bufnum++] = (unsigned char)drumnum;
                            databuf[bufnum++] = (unsigned char)64;
                        }
                        if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
                        {
                            freq = adlibfreq[templong&63];
                            switch((unsigned char)((templong>>8)&15))
                            {
                                case 11: drumnum = 16; chan = 6; freq -= 2048; break;
                                case 12: drumnum = 8; chan = 7; freq -= 2048; break;
                                case 13: drumnum = 4; chan = 8; break;
                                case 14: drumnum = 2; chan = 8; break;
                                case 15: drumnum = 1; chan = 7; freq -= 2048; break;
                                default: drumnum = 16; chan=6; freq-=2048; break;
                            }
                            databuf[bufnum++] = (unsigned char)(0xa0+chan);
                            databuf[bufnum++] = (unsigned char)(freq&255);
                            databuf[bufnum++] = (unsigned char)(0xb0+chan);
                            databuf[bufnum++] = (unsigned char)((freq>>8)&223);
                            databuf[bufnum++] = (unsigned char)(0xbd);
                            databuf[bufnum++] = (((unsigned char)(drumstat))&((unsigned char)(255-drumnum)));
                            drumstat |= drumnum;
                            databuf[bufnum++] = (unsigned char)(0xbd);
                            databuf[bufnum++] = (unsigned char)(drumstat);
                        }
                    }
                }
            }
            nownote++;
            if (nownote >= numnotes)
                nownote = 0;
            templong=note[nownote];
            if (nownote == 0)
                count = (templong>>12)-1;
            quanter = (240/trquant[(templong>>8)&15]);
            countstop = (((templong>>12)+(quanter>>1)) / quanter) * quanter;
        }
        if (mute == 0)
        {
            if (musicsource == MUSIC_SOURCE_NONE)
            {
                j = 0;
                for(i=0;i<bufnum;i++)
                    if (databuf[i] > j)
                        j = databuf[i];
                if (j == 0)
                {
                    /* Removed: Mute beeper. */
                }
                if (j > 0)
                {
                    /* Removed: Beep at pcfreq[j]. */
                }
            }
            if (musicsource == MUSIC_SOURCE_MIDI) {
#ifdef WIN32
                for(i=0;i<bufnum;i+=3) {
                    midiOutShortMsg(sequencerdevice, databuf[i]|(databuf[i+1]<<8)|(databuf[i+2]<<16));
                }
#endif
#ifdef USE_OSS
                for(i=0;i<bufnum;i++)
                {
                    SEQ_MIDIOUT(nrmidis-1, databuf[i]);
                }
                SEQ_DUMPBUF();
#endif
            }
            if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
                for(i=0;i<bufnum;i+=2)
                {
                    outdata(0, databuf[i], databuf[i+1]);
                }
        }
    }
}

/* Adlib set instrument... */

void setinst(unsigned char synth, K_INT16 chan, unsigned char v0,
             unsigned char v1, unsigned char v2, unsigned char v3,
             unsigned char v4, unsigned char v5, unsigned char v6,
             unsigned char v7, unsigned char v8, unsigned char v9,
             unsigned char v10)
{
    K_INT16 offs;

    outdata(synth, (unsigned char)(0xa0+chan), (unsigned char)0);
    outdata(synth, (unsigned char)(0xb0+chan), (unsigned char)0);
    outdata(synth, (unsigned char)(0xc0+chan), v10);
/*    if (chan == 0)*/
    offs = 0;
    if (chan == 1)
        offs = 1;
    if (chan == 2)
        offs = 2;
    if (chan == 3)
        offs = 8;
    if (chan == 4)
        offs = 9;
    if (chan == 5)
        offs = 10;
    if (chan == 6)
        offs = 16;
    if (chan == 7)
        offs = 17;
    if (chan == 8)
        offs = 18;
    outdata(synth, (unsigned char)(0x20+offs), v5);
    outdata(synth, (unsigned char)(0x40+offs), v6);
    outdata(synth, (unsigned char)(0x60+offs), v7);
    outdata(synth, (unsigned char)(0x80+offs), v8);
    outdata(synth, (unsigned char)(0xe0+offs), v9);
    offs+=3;
    outdata(synth, (unsigned char)(0x20+offs), v0);
    outdata(synth, (unsigned char)(0x40+offs), v1);
    outdata(synth, (unsigned char)(0x60+offs), v2);
    outdata(synth, (unsigned char)(0x80+offs), v3);
    outdata(synth, (unsigned char)(0xe0+offs), v4);
}

/* MIDI set instruments. */

void setmidiinsts()
{
#ifdef WIN32
    int i;

    midiOutReset(sequencerdevice);
    for(i=0;i<16;i++)
    	midiOutShortMsg(sequencerdevice, 0xc0|i|(((i<numchans)?gminst[trinst[chantrack[i]]]:0)<<8));
#endif
#ifdef USE_OSS
    int i;

    _seqbufptr = 0;
    ioctl(sequencerdevice, SNDCTL_SEQ_RESET);
    for (i = 0; i < 16; i++) {
        SEQ_CONTROL(nrmidis-1, i, 0, 0);
        SEQ_CONTROL(nrmidis-1, i, 32, 0);
        SEQ_MIDIOUT(nrmidis-1, MIDI_PGM_CHANGE + i);
/*	if (i<numchans) {
        printf("Channel %d: KSM inst %d -> GM inst %d.\n", i,
        trinst[chantrack[i]], gminst[trinst[chantrack[i]]]);
        }*/
        SEQ_MIDIOUT(nrmidis-1, (i<numchans)?gminst[trinst[chantrack[i]]]:0);
    }
    SEQ_DUMPBUF();
#endif
}

/* Did we hit a wall? If so, push us out of it... */

void checkhitwall(K_UINT16 oposx, K_UINT16 oposy, K_UINT16 posix,
                  K_UINT16 posiy)
{
    K_INT16 i, j, k, m, xdir, ydir, cntx, cnty, xpos, ypos, xinc, yinc;
    K_UINT16 x1, y1, x2, y2, x3, y3, xspan, yspan;
    K_INT32 templong;

    if (!cliptowall) {
        posx=posix;
        posy=posiy;
        if (posx<256) posx=256;
        if (posy<256) posy=256;
        if (posx>65536-256) posx=65536-256;
        if (posy>65536-256) posy=65536-256;
        return;
    }
    if (oposx < posix)
    {
        x1 = (oposx>>10);
        xdir = 1;
        xinc = posix-oposx;
    }
    else
    {
        x1 = (posix>>10);
        xdir = -1;
        xinc = oposx-posix;
    }
    if (oposy < posiy)
    {
        y1 = (oposy>>10);
        ydir = 1;
        yinc = posiy-oposy;
    }
    else
    {
        y1 = (posiy>>10);
        ydir = -1;
        yinc = oposy-posiy;
    }
    xspan = abs(((int)(posix>>10))-((int)(oposx>>10)))+1;
    yspan = abs(((int)(posiy>>10))-((int)(oposy>>10)))+1;
    xpos = oposx-(x1<<10);
    ypos = oposy-(y1<<10);
    for(i=0;i<=(xspan<<2);i++)
    {
        x2 = ((i+3)>>2)+x1-1;
        x3 = ((i+5)>>2)+x1-1;
        if (((x2|x3)&0xffc0) == 0)
            for(j=0;j<=(yspan<<2);j++)
            {
                y2 = ((j+3)>>2)+y1-1;
                y3 = ((j+5)>>2)+y1-1;
                k = i+(j<<6);
                tempbuf[k] = 0;
                if (((y2|y3)&0xffc0) == 0)
                {
                    m = board[x2][y2];
                    if (((m&3072) != 1024) && ((m&1023) != 0)) tempbuf[k] = 1;
                    m = board[x2][y3];
                    if (((m&3072) != 1024) && ((m&1023) != 0)) tempbuf[k] = 1;
                    m = board[x3][y2];
                    if (((m&3072) != 1024) && ((m&1023) != 0)) tempbuf[k] = 1;
                    m = board[x3][y3];
                    if (((m&3072) != 1024) && ((m&1023) != 0)) tempbuf[k] = 1;
                }
                else
                    tempbuf[k] = 1;
            }
    }
    cntx = 0, cnty = 0;
    for(i=0;i<256;i++)
    {
        cntx+=xinc;
        j=cntx>>8;
        cntx&=255;
        for(k=0;k<j;k++) {
            if (tempbuf[((xpos+xdir)>>8)+((ypos&0xff00)>>2)]==0)
                xpos+=xdir;
        }

        cnty+=yinc;
        j=cnty>>8;
        cnty&=255;
        for(k=0;k<j;k++) {
            if (tempbuf[(xpos>>8)+(((ypos+ydir)&0xff00)>>2)]==0)
                ypos+=ydir;
        }
    }
    posx = xpos + (((unsigned)x1)<<10);
    posy = ypos + (((unsigned)y1)<<10);
    if (((posx&0xfc00) != (oposx&0xfc00)) || ((posy&0xfc00) != (oposy&0xfc00)))
    {
        if ((board[posx>>10][posy>>10]&1023) == stairs)
        {
            if ((boardnum >= 10) && (boardnum < 20))
            {
                j = -1;
                for(i=0;i<mnum;i++)
                    if (mstat[i] == mondog)
                    {
                        if (mposx[i] > posx) templong = (long)(mposx[i]-posx);
                        else templong = (long)(posx-mposx[i]);
                        if (mposy[i] > posy) templong += (long)(mposy[i]-posy);
                        else templong += (long)(posy-mposy[i]);
                        j = 0;
                        if (templong >= 4096)
                            j = 1;
                    }
                if (j != 0)
                {
                    posx = oposx;
                    posy = oposy;
                    mixing=1;
                    strcpy(&textbuf[0], "Where's your dog?");
                    wipeoverlay(112, 40, 200, 10);
                    textprint(112, 40+1, (char)96);
                    mixing=0;
                }
            }
        }
        youarehere();
    }
}

/* Palette for text in endgame sequences... */

unsigned char textpalette[48]={
    3, 3, 3,
    7, 7, 7,
    11, 11, 11,
    15, 15, 15,
    19, 19, 19,
    23, 23, 23,
    27, 27, 27,
    31, 31, 31,
    35, 35, 35,
    39, 39, 39,
    43, 43, 43,
    47, 47, 47,
    51, 51, 51,
    55, 55, 55,
    59, 59, 59,
    63, 63, 63
};

/* Win episode 1 or 2... */

void wingame(K_INT16 episode)
{
    K_UINT16 startx, starty;
    K_INT16 oldmute;
    K_INT32 revtotalclock, revototclock, templong;

    K_INT32 tempototclock=ototclock, temptotalclock=totalclock;

    oldmute = mute;
    revototclock = -1;
    revtotalclock = 0;
    musicoff();
    if (episode == 1) loadmusic("WINGAME0");
    if (episode == 2) loadmusic("WINGAME1");
    musicon();
    posz = 32;
    if (episode == 1)
    {
        startx = (((unsigned)58)<<10);
        starty = (((unsigned)19)<<10);
    }
    else
    {
        startx = (((unsigned)5)<<10);
        starty = (((unsigned)12)<<10);
    }
    while ((death != 4096))
    {
        PollInputs();
        bstatus = 0;
        if (moustat == 0)
        {
            bstatus=readmouse(NULL, NULL);
        }
        if (death != 4096)
        {
            ang = 512;
            posx = (startx+512);
            if (revtotalclock < 3840)
                posy = starty+512+(unsigned)((revtotalclock<<6)/15);
            else
                posy = starty+16384+512;
            picrot(posx, posy, posz, ang);
            SDL_GL_SwapWindow(mainwindow);
            sortcnt = 0;
            SDL_LockMutex(soundmutex);
            SDL_LockMutex(timermutex);
            if ((musicstatus == 1) && (clockspeed >= 0) && (clockspeed < 3))
                while(clockspeed<3) {
                    SDL_UnlockMutex(timermutex);
                    SDL_UnlockMutex(soundmutex);
                    SDL_Delay(10);
                    SDL_LockMutex(soundmutex);
                    SDL_LockMutex(timermutex);
                }
            SDL_UnlockMutex(timermutex);
            SDL_UnlockMutex(soundmutex);
        }
        if (episode == 2)
        {
            if ((revtotalclock >= 840) && (revototclock < 840))
            {
                life += 1280;
                if (life > 4095)
                    life = 4095;
                drawlife();
                ksayui(7);
            }
            if ((revtotalclock >= 1560) && (revototclock < 1560))
            {
                life += 1280;
                if (life > 4095)
                    life = 4095;
                drawlife();
                ksayui(7);
            }
        }
        if (revtotalclock >= 3840)
        {
            if (revototclock < 3840)
            {
                death = 4094;
                if (episode == 1) ksayui(6);
                if (episode == 2) ksayui(9);
                mute=mute?1:2;
            }
            if (death < 4095)
            {
                if (death > 0)
                {
                    posz+=2;
                    if (posz > 64)
                        posz = 64;
                    death -= (((((int)(revtotalclock-revototclock))<<5)&0xff80)+128);
                    if (death <= 0)
                    {
                        death = 0;
                        posz = 32;
                    }
                    fade(death>>6);
                }
                if ((death == 0) && (revtotalclock >= 4800))
                {
                    templong=note[0];

                    SDL_LockMutex(timermutex);
                    count = (templong>>12)-1;
                    countstop = (templong>>12)-1;
                    nownote = 0;
                    mute = oldmute;
                    SDL_UnlockMutex(timermutex);

                    death = 4096;
                    // if ((vidmode == 0) && (statusbar < 399))
                    // {
                    //     l = times90[((unsigned)statusbar+1)>>1]+5;
                    // }
                    // if ((vidmode == 1) && (statusbar < 479))
                    // {
                    //     l = times90[((unsigned)statusbar+1)>>1];
                    // }
                    fade(63);
                    setdarkenedpalette();
                    glClearColor( 0, 0, 0, 0 );
                    glClear(GL_COLOR_BUFFER_BIT);
                    wipeoverlay(0, 0, 361, 241);
                    drawmenu(304, 192, menu);

                    updateoverlaypalette(240, 16, textpalette);

                    if (episode == 1) loadstory(-21);
                    if (episode == 2) loadstory(-19);

                    finalisemenu();

                    ksayui(23);
                    SDL_GL_SwapWindow(mainwindow);
                    pressakey();
                    glClear(GL_COLOR_BUFFER_BIT);
                    drawmenu(304, 192, menu);
                    clearkeydefstat(ACTION_MENU_CANCEL);
                    clearkeydefstat(ACTION_MENU_SELECT1);
                    clearkeydefstat(ACTION_MENU_SELECT2);
                    clearkeydefstat(ACTION_MENU_SELECT3);
                    if (episode == 1) loadstory(-20);
                    if (episode == 2) loadstory(-18);
                    finalisemenu();
                    settransferpalette();
                    SDL_GL_SwapWindow(mainwindow);
                    pressakey();
                }
            }
        }
        revototclock = revtotalclock;
        SDL_LockMutex(timermutex);
        if (clockspeed==0) {
            SDL_UnlockMutex(timermutex);
            SDL_Delay(10); /* To avoid soaking up all CPU. */
            SDL_LockMutex(timermutex);
        }
        revtotalclock += clockspeed;
        totalclock += clockspeed;
        animate2 = animate2 ^ 1;
        clockspeed = 0;
        SDL_UnlockMutex(timermutex);
    }
    musicoff();
    fade(0);

    /* Previously -1 and 1, but this gave the player all the cloaks and potions
       he had in the previous episode for a time corresponding to the time to
       the time until he last ran out of them. Probably for half an hour or so.
       I hope this wasn't a feature. */

    ototclock = tempototclock;
    totalclock = temptotalclock;
    linecompare(statusbar);
    fade(63);
    statusbaralldraw();
    fadewarpval = 63;
    fadehurtval = 0;
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_CANCEL);
    bstatus = 0;
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
    death = 4095;
}

/* Win episode 3... */

void winallgame()
{
    K_INT16 leavewin;
    K_INT32 revtotalclock;

    ingame=0;
    revtotalclock = 1;
    linecompare(479);
    musicoff();
    loadmusic("WINGAME2");
    musicon();

    fade(63);
    leavewin = 0;
    bstatus = 0;
    clearkeydefstat(ACTION_MENU_CANCEL);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_SELECT1);
    while (leavewin == 0)
    {
        PollInputs();
        bstatus = 0;
        if (moustat == 0) {
            bstatus=readmouse(NULL, NULL);
        }
        if (revtotalclock < 3260)
        {
            /*
              for(i=lside;i<rside;i++)
              height[i] = 0;
            */
            glClearColor( 0, 0, 0, 0 );
            glClear(GL_COLOR_BUFFER_BIT);
            pictur(180, halfheight, 4+(((int)revtotalclock)>>2), ((int)((revtotalclock<<2))&2047)^2047, earth);
            SDL_GL_SwapWindow(mainwindow);
        }
        else
        {
            glClearColor( 0, 0, 0, 0 );
            glClear(GL_COLOR_BUFFER_BIT);
            wipeoverlay(0, 0, 361, 241);
            setdarkenedpalette();
            drawmenu(304, 192, menu);
            updateoverlaypalette(240, 16, textpalette);
            fade(63);
            loadstory(-17);
            finalisemenu();
            ksayui(23);
            SDL_GL_SwapWindow(mainwindow);
            pressakey();
            glClear(GL_COLOR_BUFFER_BIT);
            drawmenu(304, 192, menu);
            clearkeydefstat(ACTION_MENU_CANCEL);
            clearkeydefstat(ACTION_MENU_SELECT1);
            clearkeydefstat(ACTION_MENU_SELECT2);
            clearkeydefstat(ACTION_MENU_SELECT3);
            loadstory(-16);
            finalisemenu();
            settransferpalette();
            SDL_GL_SwapWindow(mainwindow);
            pressakey();
            leavewin = 1;
        }
        SDL_LockMutex(timermutex);
        if (clockspeed==0) {
            SDL_UnlockMutex(timermutex);
            SDL_Delay(10); /* To avoid soaking up all CPU. */
            SDL_LockMutex(timermutex);
        }
        revtotalclock += clockspeed;
        totalclock += clockspeed;
        clockspeed = 0;
        SDL_UnlockMutex(timermutex);
    }
    musicoff();
    fade(0);
    ototclock = -1;
    totalclock = 1;
    linecompare(statusbar);
    fade(63);
    fadewarpval = 63;
    fadehurtval = 0;
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_CANCEL);
    bstatus = 0;
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
    death = 4095;
}

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

/* Fade... */

void fade(K_INT16 brightness)
{
    fadelevel=brightness;

    if (brightness == 63) {
        redfactor=greenfactor=bluefactor=1.0;
    }
    else if (brightness < 64) {
        redfactor=greenfactor=bluefactor=brightness/64.0;
    }
    else if (brightness < 128) {
        redfactor=greenfactor=bluefactor=(brightness-64)/64.0;
    }
    else {
        redfactor=(brightness-65)/64.0;
        greenfactor=bluefactor=1.0;

        /* Factors can't exceed 1.0, so we scale them all (makes the hurting
           a bit darker, but a lot easier/faster). */

        greenfactor/=redfactor;
        bluefactor/=redfactor;
        redfactor=1.0;
    }
}

/* Show the compass. */

void showcompass(K_INT16 compang)
{
    K_INT16 i;

    i = (((compang+64)&2047)>>7);
    statusbardraw((i&2)<<4, ((i&1)<<5)+2, 29, 29, 238, 1+statusbaryoffset, (i>>2)+compassplc);
}

/* Load a GIF. Violate a patent.
   0-2 = GIFs in LAB3D.KZP, -1 = lab3d.gif, -2 to -4 = endn.gif. */
K_INT16 kgif(K_INT16 filenum)
{
    unsigned char header[13], imagestat[10], bitcnt, numbits;
    unsigned char globalflag, localflag, chunkind;
    K_INT16 i, j, k, x, y, ydim;
    K_INT16 numcols, numpals, fil, blocklen;
    K_UINT16 gifdatacnt, firstring, currstr, bytecnt, numbitgoal;

    K_UINT16 stack[LZW_STACK_SIZE];
    K_UINT16 stackp=0, dat;

    memset(screenbuffer, (filenum==2)?255:((filenum==1)?0x50:0),
           screenbufferwidth*screenbufferheight);

    if (filenum<0) {
        switch(filenum) {
            case -1:
                sprintf(filepath, "%slab3d.gif", gameroot);
                sprintf(filepathUpper, "%sLAB3D.GIF", gameroot);
                if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
                    ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
                    return(-1);
                break;
            case -2:
                sprintf(filepath, "%send1.gif", gameroot);
                sprintf(filepathUpper, "%sEND1.GIF", gameroot);
                if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
                    ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
                    return(-1);
                break;
            case -3:
                sprintf(filepath, "%send2.gif", gameroot);
                sprintf(filepathUpper, "%sEND2.GIF", gameroot);
                if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
                    ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
                    return(-1);
                break;
            case -4:
                sprintf(filepath, "%send3.gif", gameroot);
                sprintf(filepathUpper, "%sEND3.GIF", gameroot);
                if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
                    ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
                    return(-1);
                break;
            default:
                return(-1);
        }
    } else {
        sprintf(filepath, "%slab3d.kzp", gameroot);
        sprintf(filepathUpper, "%sLAB3D.KZP", gameroot);
        if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
            ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
                return(-1);
    }

    if (filenum == 1)
    {
        lseek(fil, giflen1, SEEK_SET);
    }
    if (filenum == 2)
        lseek(fil, giflen1+giflen2, SEEK_SET);

    gifdatacnt = 0;
    read(fil, &tempbuf[0], (unsigned)gifbuflen);
    for(j=0;j<13;j++)
        header[j] = tempbuf[j+gifdatacnt];
    gifdatacnt += 13;
/*    if ((header[0] != 'L') || (header[1] != 'A') || (header[2] != 'B'))
      return(-1);
      if ((header[3] != '3') || (header[4] != 'D') || (header[5] != '!'))
      return(-1);*/
    globalflag = header[10];
    numcols = (1<<((globalflag&7)+1));
    firstring = numcols+2;
    // backg = header[11];
    if (header[12] != 0)
        return(-1);
    if ((globalflag&128) > 0)
    {
        numpals = numcols+numcols+numcols;
        for(j=0;j<numpals;j++)
            palette[j]=tempbuf[j+gifdatacnt]>>2;
        gifdatacnt += numpals;
    }
    chunkind = tempbuf[gifdatacnt], gifdatacnt++;
    while (chunkind == '!')
    {
        gifdatacnt++;
        do
        {
            chunkind = tempbuf[gifdatacnt], gifdatacnt++;
            if (chunkind > 0)
                gifdatacnt += chunkind;
        }
        while (chunkind > 0);
        chunkind = tempbuf[gifdatacnt], gifdatacnt++;
    }
    if (chunkind == ',')
    {
        for(j=0;j<9;j++)
            imagestat[j] = tempbuf[j+gifdatacnt];
        gifdatacnt += 9;
        // xdim = imagestat[4]+(imagestat[5]<<8);
        ydim = imagestat[6]+(imagestat[7]<<8);
        localflag = imagestat[8];
        if ((localflag&128) > 0)
        {
            numpals = numcols+numcols+numcols;
            for(j=0;j<numpals;j++)
                palette[j]=tempbuf[j+gifdatacnt];

            gifdatacnt += numpals;
        }
        gifdatacnt++;

        x = 20, y = (filenum==2)?0:20;
        bitcnt = 0;
        for(i=1;i<=numcols;i++) {
            lzwbuf[i]=i&255;
            lzwbuf2[i]=i;
        }
        lzwbuf2[0]=0;
        lzwbuf[0]=0;

        currstr = firstring;
        numbits = (globalflag&7)+2;
        numbitgoal = (numcols<<1);
        blocklen = 0;
        blocklen = tempbuf[gifdatacnt], gifdatacnt++;

        for(j=0;j<blocklen;j++) {
            ((unsigned char *)(lincalc))[j]=tempbuf[j+gifdatacnt];
        }

        gifdatacnt += blocklen;
        bytecnt = 0;

        while (y < ydim+((filenum==2)?0:20))
        {
            dat=(((((unsigned char *)lincalc)[bytecnt])|
                  ((K_UINT16)(((unsigned char *)lincalc)[bytecnt+1]))<<8)>>bitcnt)&
                ((1<<numbits)-1);
/*	    dat=((*((K_UINT16 *)(((unsigned char *)lincalc)+bytecnt)))
            >>bitcnt)&
            ((1<<numbits)-1);*/
            if (bitcnt+numbits>16) {
                dat+=(((K_UINT16)((unsigned char *)lincalc)[bytecnt+2])&
                      ((1<<((bitcnt+numbits)&15))-1))<<
                    (16-bitcnt);
            }

            bitcnt+=numbits;
            bytecnt+=bitcnt>>3;
            bitcnt&=7;

            if (bytecnt > blocklen-3)
            {
                writeshort((unsigned char *)lincalc,
                           readshort(&(((unsigned char *)lincalc)[bytecnt])));

                i = blocklen-bytecnt;

                blocklen=tempbuf[gifdatacnt++];

                if (gifdatacnt+blocklen < gifbuflen)
                {
                    memcpy(((unsigned char *)lincalc)+i,
                           tempbuf+gifdatacnt,
                           (blocklen+1)&~1);
                    gifdatacnt += blocklen;
                }
                else
                {
                    k = gifbuflen-gifdatacnt;
                    memcpy(((unsigned char *)lincalc)+i,
                           tempbuf+gifdatacnt,
                           (k+1)&~1);

                    read(fil, &tempbuf[0],
                         (unsigned)gifbuflen);
                    memcpy(((unsigned char *)lincalc)+i+k,
                           tempbuf,
                           ((gifdatacnt=blocklen-k)+1)&~1);

                }
                bytecnt = 0;
                blocklen += i;
            }
            if (currstr == numbitgoal)
                if (numbits < 12)
                {
                    numbits++;
                    numbitgoal <<= 1;
                }
            if (dat == numcols)
            {
                currstr = firstring;
                numbits = (globalflag&7)+2;
                numbitgoal = (numcols<<1);
            }
            else
            {
                lzwbuf2[currstr]=dat;
                while(dat>=firstring) {
                    stack[stackp++]=lzwbuf[dat];

                    dat=lzwbuf2[dat];
                }

                lzwbuf[currstr-1]=dat&255;
                lzwbuf[currstr]=dat&255;

                dat=lzwbuf2[dat];
                stack[stackp++]=dat;

                while(stackp>0) {
                    stackp--;

                    screenbuffer[y*screenbufferwidth+x]=stack[stackp];

                    x++;
                    if (x>=340) {
                        y++;
                        x-=320;
                    }
                }

                currstr++;
            }
        }
    }
    close(fil);

    /*
    char dbgfn[32];
    sprintf(dbgfn, "data%d.dat", filenum);
    FILE* dfil = fopen(dbgfn, "wb");
    fwrite(screenbuffer, screenbufferwidth, screenbufferheight, dfil);
    fclose(dfil);
    */

    UploadOverlay();

    return 0;
}

/* Upload rectangular part of overlay from memory to specified texture. */

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

int ClipToBuffer(int *sx, int *sy, int *w, int *h) {
    SDL_Rect scn, param, out;

    scn.x = 0; scn.y = 0; scn.w = screenbufferwidth; scn.h = screenbufferheight;
    param.x = *sx; param.y = *sy; param.w = *w; param.h = *h;
    if (!SDL_IntersectRect(&scn, &param, &out))
        return 0;
    *sx = out.x; *sy = out.y; *w = out.w; *h = out.h;
    return 1;
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

void setgamevideomode()
{
    fade(63);
    videotype=0;
}

/* Draw text string in textbuf at (x, y) with colour offset coloffs... */

void textprint(K_INT16 x, K_INT16 y, char coloffs)
{
    unsigned char character;
    K_INT16 charcnt, walnume;

    if ((vidmode == 1) && (y>=statusbaryoffset))
        x += 20;

    charcnt = 0;
    while ((textbuf[charcnt] != 0) && (charcnt < 40))
    {
        character = (textbuf[charcnt])&127;
        if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
            walnume = (character>>6)+125;
        else
            walnume = (character>>6)+textwall;
            
        drawtooverlay((character&7)<<3, ((character&63)>>3)<<3,
                      8, 8, x, y, walnume-1, coloffs);
        x+=8;
        charcnt++;
    }
}

/* Load and display some text (story, help, whatever). */

K_INT16 loadstory(K_INT16 boardnume)
{
    unsigned char xordat, otextcol, textcol;
    K_UINT16 storyoffs[128];
    K_INT16 fil, i, textbufcnt, textypos;

    ototclock = totalclock;
    sprintf(filepath, "%sstory.kzp", gameroot);
    sprintf(filepathUpper, "%sSTORY.KZP", gameroot);
    if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
        ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
        return(-1);
    readLE16(fil, &storyoffs[0], 256);
    lseek(fil, (long)(storyoffs[boardnume+34]), SEEK_SET);
    read(fil, &tempbuf[0], 4096);
    i = 0;
    xordat = 0;
    if (vidmode == 0)
        textypos = 3;
    else
        textypos = 23;
    if ((boardnume >= 0) || ((boardnume >= -21) && (boardnume <= -16)))
        otextcol = 0;
    else
        otextcol = 96;
    textcol = otextcol;
    if ((boardnume >= -14) && (boardnume <= -2))
        textypos += 12;
    textbufcnt = 0;
    while ((tempbuf[i] != 0) && (textbufcnt < 40))
    {
        xordat ^= tempbuf[i];
        if (tempbuf[i] >= 32)
        {
            if ((otextcol == 96) && (textbufcnt == 0))
            {
                textcol = 96;
                if (tempbuf[i] == '!')
                    textcol = 16, textbuf[textbufcnt++] = '@';
                else if (tempbuf[i] == '%')
                    textcol = 80, textbuf[textbufcnt++] = '@';
                else
                    textbuf[textbufcnt++] = tempbuf[i];
            }
            else
                textbuf[textbufcnt++] = tempbuf[i];
        }
        if (tempbuf[i] == 13)
        {
            textbuf[textbufcnt] = 0;
            textprint(180-(textbufcnt<<2), textypos+1, textcol);
            textypos += 12;
            textbufcnt = 0;
        }
        i++;
        tempbuf[i] ^= xordat;
    }
    textbuf[textbufcnt] = 0;
    textprint(180-(textbufcnt<<2), textypos+1, textcol);
    close(fil);
    return 0;
}

K_INT16 setupmouse()
{
    SDL_SetRelativeMouseMode(1);
    return(0); /* Assume mouse always exists. */
}

/* Redraw status bar... */

void statusbaralldraw()
{
    K_INT16 i, j;

    for(i=0;i<361;i++)
        screenbuffer[i+240*screenbufferwidth]=0x50;
    UploadPartialOverlay(0, 240, 361, 1);
    statusbardraw(0, 0, 32, 32, 0, 0+statusbaryoffset, statusbarback);
    statusbardraw(32, 0, 32, 32, 288, 0+statusbaryoffset, statusbarback);
    for(i=32;i<288;i+=32)
        statusbardraw(16, 0, 32, 32, i, 0+statusbaryoffset, statusbarback);
    strcpy(textbuf, "SCORE");
    textprint(3, 4+statusbaryoffset, (char)176);
    strcpy(textbuf, "TIME");
    textprint(3, 12+statusbaryoffset, (char)176);
    strcpy(textbuf, "BOARD");
    textprint(3, 20+statusbaryoffset, (char)176);
    textbuf[0] = ':', textbuf[1] = 0;
    textprint(41, 4+statusbaryoffset, (char)176);
    textprint(33, 12+statusbaryoffset, (char)176);
    textprint(41, 20+statusbaryoffset, (char)176);
    strcpy(textbuf, "LIFE");
    textprint(96, 4+statusbaryoffset, (char)176);
    strcpy(textbuf, "WEAPON");
    textbuf[6] = 0;
    textprint(272, 3+statusbaryoffset, (char)176);
    if (hiscorenamstat == 1)
    {
        for(i=0;i<9;i++)
            textbuf[i] = hiscorenam[i];
        textbuf[9] = 0;
        textprint(70, 20+statusbaryoffset, (char)176);
    }
    statusbardraw(50, 30, 7, 7, 282, 12+statusbaryoffset, statusbarinfo);
    statusbardraw(50, 37, 7, 7, 282, 21+statusbaryoffset, statusbarinfo);
    statusbardraw(57, 30, 7, 7, 306, 12+statusbaryoffset, statusbarinfo);
    statusbardraw(57, 37, 7, 7, 306, 21+statusbaryoffset, statusbarinfo);
    statusbardraw(36, 37, 7, 7, 104, 12+statusbaryoffset, statusbarinfo);
    statusbardraw(43, 37, 7, 7, 136, 12+statusbaryoffset, statusbarinfo);
    statusbardraw(32, 28, 1, 8, 94, 4+statusbaryoffset, statusbarinfo);
    statusbardraw(32, 28, 1, 8, 94, 12+statusbaryoffset, statusbarinfo);
    oldlife = 0;
    drawlife();
    oldlife = 4095;
    drawlife();
    drawscore(scorecount);
    drawtime(scoreclock);
    textbuf[0] = ((boardnum+1)/10)+48;
    textbuf[1] = ((boardnum+1)%10)+48;
    textbuf[2] = 0;
    if (textbuf[0] == 48)
        textbuf[0] = 32;
    textprint(46, 20+statusbaryoffset, (char)176);
    if (vidmode == 1)
    {
        statusbardraw(0, 32, 20, 32, -20, 0+statusbaryoffset, statusbarback);
        statusbardraw(0, 32, 20, 32, 320, 0+statusbaryoffset, statusbarback);
    }
    textbuf[0] = 9, textbuf[1] = 0;
    textprint(96, 12+statusbaryoffset, (char)0);
    textbuf[0] = lifevests+48, textbuf[1] = 0;
    textprint(96, 12+statusbaryoffset, (char)176);
    textbuf[0] = 9, textbuf[1] = 0;
    textprint(296, 21+statusbaryoffset, (char)0);
    textbuf[0] = lightnings+48, textbuf[1] = 0;
    textprint(296, 21+statusbaryoffset, (char)176);
    textbuf[0] = 9, textbuf[1] = 0;
    textprint(272, 12+statusbaryoffset, (char)0);
    textbuf[0] = firepowers[0]+48, textbuf[1] = 0;
    textprint(272, 12+statusbaryoffset, (char)176);
    textbuf[0] = 9, textbuf[1] = 0;
    textprint(272, 21+statusbaryoffset, (char)0);
    textbuf[0] = firepowers[1]+48, textbuf[1] = 0;
    textprint(272, 21+statusbaryoffset, (char)176);
    textbuf[0] = 9, textbuf[1] = 0;
    textprint(296, 12+statusbaryoffset, (char)0);
    textbuf[0] = firepowers[2]+48, textbuf[1] = 0;
    textprint(296, 12+statusbaryoffset, (char)176);
    if (purpletime >= totalclock)
    {
        statusbardraw(0, 0, 16, 15, 159, 13+statusbaryoffset, statusbarinfo);
        if (purpletime < totalclock+3840)
        {
            i = ((3840-(int)(purpletime-totalclock))>>8);
            if ((i >= 0) && (i <= 15))
                statusbardraw(0, 30, 16, i, 159, 13+statusbaryoffset, statusbarinfo);
        }
    }
    if (greentime >= totalclock)
    {
        statusbardraw(0, 15, 16, 15, 176, 13+statusbaryoffset, statusbarinfo);
        if (greentime < totalclock+3840)
        {
            i = ((3840-(int)(greentime-totalclock))>>8);
            if ((i >= 0) && (i <= 15))
                statusbardraw(16, 30, 16, i, 176, 13+statusbaryoffset, statusbarinfo);
        }
    }
    if (capetime[0] >= totalclock)
    {
        statusbardraw(16, 0, 21, 28, 194, 2+statusbaryoffset, statusbarinfo);
        if (capetime[0] < totalclock+3072)
        {
            i = (int)((capetime[0]-totalclock)>>9);
            if ((i >= 0) && (i <= 5))
            {
                if (i == 5) statusbardraw(0, 0, 21, 28, 194, 2+statusbaryoffset, coatfade);
                if (i == 4) statusbardraw(21, 0, 21, 28, 194, 2+statusbaryoffset, coatfade);
                if (i == 3) statusbardraw(42, 0, 21, 28, 194, 2+statusbaryoffset, coatfade);
                if (i == 2) statusbardraw(0, 32, 21, 28, 194, 2+statusbaryoffset, coatfade);
                if (i == 1) statusbardraw(21, 32, 21, 28, 194, 2+statusbaryoffset, coatfade);
                if (i == 0) statusbardraw(42, 32, 21, 28, 194, 2+statusbaryoffset, coatfade);
            }
        }
    }
    if (capetime[1] >= totalclock)
    {
        statusbardraw(37, 0, 21, 28, 216, 2+statusbaryoffset, statusbarinfo);
        if (capetime[1] < totalclock+3072)
        {
            i = (int)((capetime[1]-totalclock)>>9);
            if ((i >= 0) && (i <= 5))
            {
                if (i == 5) statusbardraw(0, 0, 21, 28, 216, 2+statusbaryoffset, coatfade);
                if (i == 4) statusbardraw(21, 0, 21, 28, 216, 2+statusbaryoffset, coatfade);
                if (i == 3) statusbardraw(42, 0, 21, 28, 216, 2+statusbaryoffset, coatfade);
                if (i == 2) statusbardraw(0, 32, 21, 28, 216, 2+statusbaryoffset, coatfade);
                if (i == 1) statusbardraw(21, 32, 21, 28, 216, 2+statusbaryoffset, coatfade);
                if (i == 0) statusbardraw(42, 32, 21, 28, 216, 2+statusbaryoffset, coatfade);
            }
        }
    }
    if (keys[0] > 0)
        statusbardraw(36, 44, 14, 6, 144, 13+statusbaryoffset, statusbarinfo);
    if (keys[1] > 0)
        statusbardraw(50, 44, 14, 6, 144, 21+statusbaryoffset, statusbarinfo);
    statusbardraw(41, 28, 8, 9, 292, 20+statusbaryoffset, statusbarinfo);
    statusbardraw(43, 28, 6, 9, 300, 20+statusbaryoffset, statusbarinfo);
    statusbardraw(43, 28, 6, 9, 306, 20+statusbaryoffset, statusbarinfo);
    statusbardraw(45, 28, 5, 9, 312, 20+statusbaryoffset, statusbarinfo);
    /*if (bulchoose == 0) */
    i = 268, j = 11;
    if (bulchoose == 1) i = 268, j = 20;
    if (bulchoose == 2) i = 292, j = 11;
    statusbardraw(32, 28, 8, 9, i, j+statusbaryoffset, statusbarinfo);
    statusbardraw(34, 28, 6, 9, i+8, j+statusbaryoffset, statusbarinfo);
    statusbardraw(34, 28, 6, 9, i+14, j+statusbaryoffset, statusbarinfo);
    statusbardraw(36, 28, 5, 9, i+20, j+statusbaryoffset, statusbarinfo);
    textbuf[0] = 9, textbuf[1] = 9;
    textbuf[2] = 9, textbuf[3] = 0;
    textprint(112, 12+statusbaryoffset, (char)0);
    textbuf[0] = (coins/100)+48;
    textbuf[1] = ((coins/10)%10)+48;
    textbuf[2] = (coins%10)+48;
    textbuf[3] = 0;
    if (textbuf[0] == 48)
    {
        textbuf[0] = 32;
        if (textbuf[1] == 48)
            textbuf[1] = 32;
    }
    textprint(112, 12+statusbaryoffset, (char)176);
}

/* Draw the hiscore box to the overlay... */

void drawscorebox() {
    int x, y;
    float ox=0.0, oy=0.0, od;
    unsigned char *orgl;
    unsigned char *org;

    unsigned char *dest;

    od=64.0/320.0;

    ox=0.0;
    for(x=0;(x<320)&&(ox<64.0);x++) {
        if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
            orgl=walseg[75]+(((int)ox)<<6);
        else
            orgl=walseg[scorebox-1]+(((int)ox)<<6);
        oy=0.0;
        dest=screenbuffer+(18+x)+screenbufferwidth*20;

        for(y=0;(y<200)&&(oy<64.0);y++) {
            org=orgl+((int)oy);
            if ((*org)!=255)
                *dest=*org;

            dest+=screenbufferwidth;
            oy+=od;
        }
        ox+=od;
    }

    menuleft=18; menutop=20; menuwidth=320; menuheight=200;
    menuing=1;
}

static int default_hiscores() {
    int level, pos;
    char buf[16];
    K_INT32 score=0;

    FILE* f;
    memset(buf, 0, 16);
    sprintf(filepath, "%shiscore.dat", gameroot);
    f = fopen(filepath, "wb");
    if (!f) return 0;
    for (level = 0; level < 30; level++) {
        for (pos = 0; pos < 8; pos++) {
            if (level == 28) {
                switch (pos) {
                    case 0: strcpy(buf, "Ken S.      "); score = 1000000; break;
                    case 1: strcpy(buf, "Andy C.     "); score = 900000; break;
                    case 2: strcpy(buf, "Alan S.     "); score = 800000; break;
                    case 3: strcpy(buf, "Mahesh J.   "); score = 700000; break;
                    case 4: strcpy(buf, "Cameron A.  "); score = 600000; break;
                    case 5: memset(buf, 0, 12); score = 0;
                }
                writelong((unsigned char*)buf + 12, score);
            }
            fwrite(buf, 1, 16, f);
        }
    }
    fclose(f);
    return 1;
}

/* Hiscore sequence; check for new hiscore and display hiscore box. */

void hiscorecheck()
{
    K_INT16 i, j, k, m, inse, namexist, fil;
    K_INT32 hiscore[8], scorexist, templong;

    sprintf(filepath, "%shiscore.dat", gameroot);
    sprintf(filepathUpper, "%sHISCORE.DAT", gameroot);
    fil = open(filepath, O_RDWR|O_BINARY, 0);
    if (fil == -1)
        fil = open(filepathUpper, O_RDWR|O_BINARY, 0);
    if (fil == -1) {
        if (default_hiscores())
            fil = open(filepath, O_RDWR|O_BINARY, 0);
        if (fil == -1)
            return;
    }

    glDrawBuffer(GL_FRONT);
    wipeoverlay(0, 0, 361, statusbaryoffset);
    lseek(fil, (long)(boardnum<<7), SEEK_SET);
    read(fil, &tempbuf[0], 128);
    for(i=0;i<8;i++)
    {
//	hiscore[i]=*((K_INT32 *)(&tempbuf[i*16+12]));
        hiscore[i]=readlong(&tempbuf[i*16+12]);
    }
    /*
      for(i=lside;i<rside;i++)
      height[i] = 0;
    */

    /* Score box looks awful when interpolated, and we need it in the
       overlay for proper anti-aliasing, so software scale. */

    drawscorebox();
/*    spridraw(20, 0, 320*4, scorebox);*/
    for(i=0;i<8;i++)
        if (tempbuf[i<<4] != 0)
        {
            textbuf[0] = i+49, textbuf[1] = '.', textbuf[2] = 32, textbuf[3] = 32;
            for(j=0;j<12;j++)
                textbuf[j+4] = tempbuf[(i<<4)+j];
            textbuf[16] = 0;
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
                if (strcmp(textbuf+4, BADNAME)==0)
                    continue;
                textprint(55, 60+(i<<3)+i+1, (char)128);
            } else
                textprint(55, 60+(i<<3)+i+1, (char)130);
            setuptextbuf(hiscore[i]);
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
                textprint(215, 60+(i<<3)+i+1, (char)0);
            else
                textprint(215, 60+(i<<3)+i+1, (char)96);
        }
    sprintf(&textbuf[0], "Time penalty: 10 * ");
    templong = (scoreclock*5)/12;
    textbuf[19] = (char)((templong/10000000L)%10L)+48;
    textbuf[20] = (char)((templong/1000000L)%10L)+48;
    textbuf[21] = (char)((templong/100000L)%10L)+48;
    textbuf[22] = (char)((templong/10000L)%10L)+48;
    textbuf[23] = (char)((templong/1000L)%10L)+48;
    textbuf[24] = (char)((templong/100L)%10L)+48;
    textbuf[25] = '.';
    textbuf[26] = (char)((templong/10L)%10L)+48;
    textbuf[27] = (char)(templong%10L)+48;
    textbuf[28] = 0;
    j = 19;
    while ((textbuf[j] == 48) && (j < 24))
        j++;
    for(i=19;i<28;i++)
        textbuf[i] = textbuf[i+j-19];
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        textprint(180-(strlen(textbuf)<<2), 155+1, (char)106);
    else
        textprint(180-(strlen(textbuf)<<2), 155+1, (char)114);
    scorecount -= (scoreclock/24);
    if (scorecount < 0)
        scorecount = 0;
    setuptextbuf(scorecount);
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        textprint(215, 145+1, (char)33);
    else
        textprint(215, 145+1, (char)35);
    if ((scorecount > hiscore[7]) && (cheated == 0))
    {
        if (hiscorenamstat == 0) {
            getname();
        } else
            finalisemenu();

        if (hiscorenamstat == 0)
        {
            for(i=0;i<22;i++)
                textbuf[i] = 8;
            textbuf[22] = 0;
            textprint(180-(strlen(textbuf)<<2), 135+1, (char)1);
        }
        else
        {
            m = 0;
            while ((hiscorenam[m] != 0) && (m < 11))
                m++;
            namexist = -1;
            for(i=0;i<8;i++)
            {
                j = 0;
                for(k=0;k<=m;k++)
                    if (hiscorenam[k] != tempbuf[(i<<4)+k])
                        j = 1;
                if (j == 0)
                    namexist = i;
            }
            scorexist = scorecount;
            if (namexist != -1)
            {
                if (hiscore[namexist] > scorexist)
                    scorexist = hiscore[namexist];
                for(j=namexist;j<7;j++)
                {
                    for(k=0;k<16;k++)
                        tempbuf[(j<<4)+k]=tempbuf[((j+1)<<4)+k];
                    hiscore[j] = hiscore[j+1];
                }
                inse = 6;
            }
            else
                inse = 7;
            while ((scorexist > hiscore[inse]) && (inse >= 0))
                inse--;
            inse++;
            for(j=7;j>inse;j--)
            {
                for(k=0;k<16;k++)
                    tempbuf[(j<<4)+k]=tempbuf[((j-1)<<4)+k];
                hiscore[j] = hiscore[j-1];
            }
            for(k=0;k<12;k++)
                tempbuf[(inse<<4)+k] = hiscorenam[k];
            hiscore[inse] = scorexist;
            writelong(&tempbuf[inse*16+12], scorexist);

            wipeoverlay(0, 0, 361, statusbaryoffset);
            drawscorebox();
            if (scorecount == scorexist)
            {
                switch(inse)
                {
                    case 0: strcpy(&textbuf[0], "Congratulations!!! You're #1!"); break;
                    case 1: strcpy(&textbuf[0], "Good Job!! Second place!"); break;
                    case 2: strcpy(&textbuf[0], "Not Bad - You got third place!"); break;
                    case 3: strcpy(&textbuf[0], "Fourth place! Getting there..."); break;
                    case 4: strcpy(&textbuf[0], "Fifth place! Could be improved..."); break;
                    case 5: strcpy(&textbuf[0], "Sixth Place...  Keep trying..."); break;
                    case 6: strcpy(&textbuf[0], "Seventh? Really want your name here?"); break;
                    case 7: strcpy(&textbuf[0], "Eighth? Have a load in your pants?"); break;
                };
            }
            else
                strcpy(&textbuf[0], "Are those reflexes dying?");
            textprint(180-(strlen(textbuf)<<2), 45+1, lab3dversion == KENS_LABYRINTH_1_0  || lab3dversion == KENS_LABYRINTH_1_1 ? 64 : (char)65);
            for(i=0;i<8;i++)
                if (tempbuf[i<<4] != 0)
                {
                    textbuf[0] = i+49, textbuf[1] = '.', textbuf[2] = 32, textbuf[3] = 32;
                    for(j=0;j<12;j++)
                        textbuf[j+4] = tempbuf[(i<<4)+j];
                    textbuf[16] = 0;
                    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
                        if (strcmp(textbuf+4, BADNAME)==0)
                            continue;
                    if (i == inse)
                    {
                        textprint(55, 60+(i<<3)+i+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 0 : (char)98);
                        for(j=0;j<12;j++)
                            textbuf[j] = textbuf[j+4];
                        textprint(87, 145+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 96 : (char)98);
                    }
                    else
                        textprint(55, 60+(i<<3)+i+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 128 : (char)130);
                    setuptextbuf(hiscore[i]);
                    textprint(215, 60+(i<<3)+i+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 0 : (char)96);
                }
            sprintf(&textbuf[0], "Time penalty: 10 * ");
            textbuf[19] = (char)((templong/10000000L)%10L)+48;
            textbuf[20] = (char)((templong/1000000L)%10L)+48;
            textbuf[21] = (char)((templong/100000L)%10L)+48;
            textbuf[22] = (char)((templong/10000L)%10L)+48;
            textbuf[23] = (char)((templong/1000L)%10L)+48;
            textbuf[24] = (char)((templong/100L)%10L)+48;
            textbuf[25] = '.';
            textbuf[26] = (char)((templong/10L)%10L)+48;
            textbuf[27] = (char)(templong%10L)+48;
            textbuf[28] = 0;
            j = 19;
            while ((textbuf[j] == 48) && (j < 24))
                j++;
            for(i=19;i<28;i++)
                textbuf[i] = textbuf[i+j-19];
            textprint(180-(strlen(textbuf)<<2), 155+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 106 :114);
            setuptextbuf(scorecount);
            textprint(215, 145+1, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 33 : (char)35);
            lseek(fil, (long)(boardnum<<7), SEEK_SET);
            write(fil, &tempbuf[0], 128);
        }
    }
    else
    {
        i = rand()&3;
        if (cheated == 0)
        {
            switch(i)
            {
                case 0: strcpy(&textbuf[0], "You rot if you can't beat ");
                    j = 0;
                    while (tempbuf[(i<<4)+j] != 0)
                    {
                        textbuf[j+26] = tempbuf[(i<<4)+j];
                        j++;
                    }
                    textbuf[j+26] = 0;
                    break;
                case 1: strcpy(&textbuf[0], "Try this level again if you're good."); break;
                case 2: strcpy(&textbuf[0], "Nice job, but no score like Ken's."); break;
                case 3: strcpy(&textbuf[0], "Try playing for speed next time."); break;
            };
        }
        else
        {
            switch(i)
            {
                case 0: strcpy(&textbuf[0], "Try playing for real next time."); break;
                case 1: strcpy(&textbuf[0], "Don't touch those cheat keys!"); break;
                case 2: strcpy(&textbuf[0], "Cheating doesn't pay."); break;
                case 3: strcpy(&textbuf[0], "You can't cheat a score."); break;
            };
        }
        textprint(180-(strlen(textbuf)<<2), 45+1, (char)161);
    }
    close(fil);
    sprintf(&textbuf[0], "Press any key to continue.");
    textprint(180-(strlen(textbuf)<<2), 135+1, (char)65);
    finalisemenu();
    glFlush();
    glDrawBuffer(GL_BACK);
    while ((getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
           (bstatus == 0)) {
        PollInputs();
        SDL_Delay(10);
    }
}

/* Convert integer to string. */

void setuptextbuf(K_INT32 templong)
{
    int i;

    textbuf[0] = (char)+48;
    textbuf[1] = (char)+48;
    textbuf[2] = (char)+48;
    textbuf[3] = (char)((templong/10000000L)%10L)+48;
    textbuf[4] = (char)((templong/1000000L)%10L)+48;
    textbuf[5] = (char)((templong/100000L)%10L)+48;
    textbuf[6] = (char)((templong/10000L)%10L)+48;
    textbuf[7] = (char)((templong/1000L)%10L)+48;
    textbuf[8] = (char)((templong/100L)%10L)+48;
    textbuf[9] = (char)((templong/10L)%10L)+48;
    textbuf[10] = (char)(templong%10L)+48;
    i = 0;
    while ((textbuf[i] == 48) && (i < 10))
        textbuf[i++] = 32;
    textbuf[11] = 0;
}

/* Get player's name... */

void getname()
{
    int ch;
    K_INT16 i, j;

    /* Apparently, the program is supposed to remember the name you used last
       time you played and allow you to modify that.

       In practice, this code is so buggy and useless that I think it's best to
       just leave it out. */

/*    if (namrememberstat == 0)
      {*/
    for(j=0;j<16;j++)
        hiscorenam[j] = 0;
    for(j=0;j<12;j++)
        textbuf[j] = 8;
    textbuf[12] = 0;
    textprint(94, 145+1, (char)0);
    j = 0;
/*    }
      else
      {
      for(j=0;j<12;j++)
      textbuf[j] = hiscorenam[j];
      textbuf[12] = 0;
      j = 12;
      while ((hiscorenam[j] == 0) && (j > 0))
      j--;
      textprint(94, 125+1, (char)97);
      }*/

    ch = 0;
    finalisemenu();
    glDrawBuffer(GL_FRONT);
    sprintf(&textbuf[0], "Please type your name!");

    textprint(180-(strlen(textbuf)<<2), 135+1, (char)161);
    ch = 0;
    clearkeydefstat(ACTION_MENU);
    clearkeydefstat(ACTION_MENU_CANCEL);

    // We have no keyboard on the Switch so we will get the Switch username
    #ifdef __SWITCH__
    getUsername();
    #else
    int uni;
    SDL_StartTextInput();
    while ((ch != 13) && (ch != 27))
    {
        while ((uni=getkeypress(&ch)) == 0)
        {
            if (getkeydefstatlock(ACTION_MENU_CANCEL) || getkeydefstatlock(ACTION_MENU)) {
                ch = 27;
                break;
            }
            textbuf[0] = 95;
            textbuf[1] = 0;
            textprint(94+(j<<3), 145, (char)97);
            glFlush();
            SDL_Delay(10); /* Just to avoid soaking all CPU. */
            textbuf[0] = 8;
            textbuf[1] = 0;
            textprint(94+(j<<3), 145, (char)0);
            glFlush();
            SDL_Delay(10); /* Just to avoid soaking all CPU. */
        }
        if (uni == 1) {
            if (ch == SDLK_DELETE)
            {
                hiscorenam[j] = ch;
                for(j=0;j<16;j++)
                    hiscorenam[j] = 0;
                for(j=0;j<12;j++)
                    textbuf[j] = 8;
                textbuf[12] = 0;
                textprint(94, 145+1, (char)0);
                j = 0;
                ch = 0;
            }
            if ((ch == SDLK_BACKSPACE) && (j > 0))
            {
                j--, hiscorenam[j] = 0;
                textbuf[0] = ch;
                textbuf[1] = 0;
                textprint(94+(j<<3), 145+1, (char)0);
            }
        } else {
            if ((ch >= 32) && (ch <= 127) && (j < 12))
            {
                textbuf[0] = ch;
                textbuf[1] = 0;
                textprint(94+(j<<3), 145+1, (char)97);
                hiscorenam[j] = ch;
                if ((ch != 32) || (j > 0))
                    j++;
            }
        }
    }
    SDL_StopTextInput();
    setnewkeystatus(SDLK_ESCAPE, 0);
    setnewkeystatus(SDLK_RETURN, 0);

    for(i=0;i<256;i++)
        keystatus[i] = 0;
    hiscorenam[j] = 0;
    i = j-1;
    while ((hiscorenam[i] == 32) && (i >= 0))
        hiscorenam[i--] = 0;
    #endif
    if ((hiscorenam[0] == 0) || (ch == 27))
    {
        hiscorenamstat = 0;
        hiscorenam[0] = 0;
    }
    else
    {
        if (hiscorenam[0] == 0)
            hiscorenamstat = 0;
        else
        {
            hiscorenamstat = 1;
            for(i=0;i<9;i++)
                textbuf[i] = hiscorenam[i];
            textbuf[9] = 0;
            if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)
                textprint(70, 20+statusbaryoffset, (char)177);
        }
    }
}

/* Draw score... */

void drawscore(K_INT32 thescore)
{
    K_INT16 i;

    for(i=0;i<6;i++)
        textbuf[i] = 9;
    textbuf[6] = 0;
    textprint(46, 4+statusbaryoffset, (char)0);
    textbuf[0] = (char)((thescore/100000L)%10L)+48;
    textbuf[1] = (char)((thescore/10000L)%10L)+48;
    textbuf[2] = (char)((thescore/1000L)%10L)+48;
    textbuf[3] = (char)((thescore/100L)%10L)+48;
    textbuf[4] = (char)((thescore/10L)%10L)+48;
    textbuf[5] = (char)(thescore%10L)+48;
    textbuf[6] = 0;
    i = 0;
    while ((textbuf[i] == 48) && (i < 5))
        textbuf[i++] = 32;
    textprint(46, 4+statusbaryoffset, (char)192);
}

/* Draw time... */

void drawtime(K_INT32 thetime)
{
    K_INT16 i;

    for(i=0;i<7;i++)
        textbuf[i] = 9;
    textbuf[7] = 0;
    textprint(38, 12+statusbaryoffset, (char)0);
    thetime = thetime/240;
    textbuf[0] = (char)((thetime/1000000L)%10L)+48;
    textbuf[1] = (char)((thetime/100000L)%10L)+48;
    textbuf[2] = (char)((thetime/10000L)%10L)+48;
    textbuf[3] = (char)((thetime/1000L)%10L)+48;
    textbuf[4] = (char)((thetime/100L)%10L)+48;
    textbuf[5] = (char)((thetime/10L)%10L)+48;
    textbuf[6] = (char)(thetime%10L)+48;
    textbuf[7] = 0;
    i = 0;
    while ((textbuf[i] == 48) && (i < 6))
        textbuf[i++] = 32;
    textprint(38, 12+statusbaryoffset, (char)192);
}

/* Precalculated BMP header; RGB, 24-bit. */

unsigned char BMPHeader[54]={0x42, 0x4d, 0, 0, 0, 0, 0, 0,
                             0, 0, 0x36, 0, 0, 0, 0x28, 0,
                             0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 1, 0, 0x18, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0, 0, 0,
                             0, 0, 0, 0, 0, 0};

/* Write a DWORD (32-bit int) to BMP header. */

void bmpheaderwrite(int offset, K_UINT32 value) {
    int a;

    for(a=0;a<4;a++)
        BMPHeader[offset+a]=(value>>(a<<3))&255;
}

/* Save screen to capturxx.bmp, where xx counts up from 00. */

void screencapture()
{
    char filename[15];

    unsigned char *screen=malloc(3*screenwidth*screenheight);

    char success=0;
    int file;

    K_UINT32 size=54+3*screenwidth*screenheight;

    strcpy(filename, "capturxx.bmp");
    filename[6] = (capturecount/10)+48;
    filename[7] = (capturecount%10)+48;
    capturecount++;
    if (capturecount==100)
        capturecount=0; /* Just to keep the file names sane. */

    bmpheaderwrite(2, size);

    bmpheaderwrite(0x12, screenwidth);
    bmpheaderwrite(0x16, screenheight);

    if (screen!=NULL) {
        glReadPixels(0, 0, screenwidth, screenheight, GL_BGR, GL_UNSIGNED_BYTE,
                     screen);
        strcpy(filepath, gameroot);
        strcat(filepath, filename);
        unlink(filepath);
        file=open(filepath, O_CREAT|O_WRONLY|O_BINARY,
                  S_IREAD|S_IWRITE|S_IRGRP|S_IROTH);
        if (file!=-1) {
            if (write(file, BMPHeader, 54)==54) {
                if (write(file, screen, size-54)==size-54) {
                    if (close(file)!=-1)
                        success=1;
                }
            }
        }
        free(screen);
    }

    if (success)
        ksayui(7);
    else
        ksayui(8);
}

/* Draw main menu. Separated to eliminate need for saving screen contents. */

void drawmainmenu() {
    K_INT16 n;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;

    n += 30;

    drawmenu(192, 140, menu);
    strcpy(&textbuf[0], "New game");
    n += 12; textprint(131, n, 32);
    strcpy(&textbuf[0], "Load game");
    n += 12; textprint(131, n, 32);
    strcpy(&textbuf[0], "Save game");
    n += 12; textprint(131, n, 32);
    strcpy(&textbuf[0], "Return to game");
    n += 12; textprint(131, n, 32);
    strcpy(&textbuf[0], "Setup");
    n += 12; textprint(131, n, 126);
    strcpy(&textbuf[0], "Help");
    n += 12; textprint(131, n, 126);
    strcpy(&textbuf[0], "Story");
    n += 12; textprint(131, n, 126);
    strcpy(&textbuf[0], "Copyright notice");
    n += 12; textprint(131, n, 126);
    strcpy(&textbuf[0], "Credits");
    n += 12; textprint(131, n, 126);
    strcpy(&textbuf[0], "Exit");
    n += 12; textprint(131, n, 126);
    finalisemenu();
}

/* Main menu... */

K_INT16 mainmenu()
{
    K_INT16 j, k, done;

    spriteyoffset=0;

    ksayui(27);
    fade(63);
    if (sortcnt == -1) {
        /* Emulating the original a bit too closely, this... */

        glDrawBuffer(GL_BACK);
        spriteyoffset=20;
        drawintroduction();
        spriteyoffset=0;
        SDL_GL_SwapWindow(mainwindow);
    } else {
        glDrawBuffer(GL_FRONT);
        ShowStatusBar();
    }

    glDrawBuffer(GL_FRONT);

    done = 0;
    if (sortcnt != -1)
    {
        wipeoverlay(0, 0, 361, statusbaryoffset);
    }
    j = scrsize;
    if (j < 18000)
        j = 18000;
    if (vidmode == 1)
        j = 21600;

    drawmainmenu();

    while ((mainmenuplace >= 0) && (done == 0))
    {
        if ((mainmenuplace = getselection(88, 41, mainmenuplace, 10)) >= 0)
        {
            if (mainmenuplace == MAINMENU_NEWGAME)
                if ((k = newgamemenu()) >= 0)
                {
                    done = 1;
                    if ((k == 1) && (numboards < 20))
                    {
                        ksayui(12);
                        done = 0;
                    }
                    else if ((k == 2) && (numboards < 30))
                    {
                        ksayui(12);
                        done = 0;
                    }
                }
            if (mainmenuplace == MAINMENU_LOADGAME)
                if (loadsavegamemenu(1) >= 0)
                    done = 1;
            if (mainmenuplace == MAINMENU_SAVEGAME)
            {
                if (sortcnt == -1)
                    ksayui(12);
                else
                {
                    if (loadsavegamemenu(2) >= 0)
                        done = 1;
                }
            }
            if (mainmenuplace == MAINMENU_RETURN)
                mainmenuplace = (-mainmenuplace)-1;
            if (mainmenuplace == MAINMENU_SETUP) {
                setupmenu(1);
                savesettings();
            }
            if (mainmenuplace == MAINMENU_HELP) helpmenu();
            if (mainmenuplace == MAINMENU_STORY) bigstorymenu();
            if (mainmenuplace == MAINMENU_COPYRIGHT) orderinfomenu();
            if (mainmenuplace == MAINMENU_CREDITS) creditsmenu();
            if (mainmenuplace == MAINMENU_EXIT) done = areyousure();
            if (done == 0)
            {
                /* Redraw whatever was beneath the menu. Double buffer to
                   avoid annoying flicker. */
                if (sortcnt == -1) {
                    spriteyoffset=20;
                    glDrawBuffer(GL_BACK);
                    checkGLStatus();
                    kgif(1);
                    drawintroduction();
                    spriteyoffset=0;
                    drawmainmenu();
                    checkGLStatus();
                    SDL_GL_SwapWindow(mainwindow);
                    glDrawBuffer(GL_FRONT);
                }
                else {
                    glDrawBuffer(GL_BACK);
                    checkGLStatus();
                    wipeoverlay(0, 0, 361, statusbaryoffset);
                    statusbaralldraw();
                    if (compass>0) showcompass(ang);
                    picrot(posx, posy, posz, ang);
                    drawmainmenu();
                    checkGLStatus();
                    SDL_GL_SwapWindow(mainwindow);
                    glDrawBuffer(GL_FRONT);
                }
            }
        }
    }
    if (sortcnt != -1) {
        linecompare(statusbar);
        wipeoverlay(0, 0, 361, statusbaryoffset);
    }
    else
        kgif(1);
    j = mainmenuplace;
    if (mainmenuplace < 0)
        mainmenuplace = (-mainmenuplace)-1;
    glDrawBuffer(GL_BACK);
    return(j);
}

/* Get a selection from a menu with totselectors choices, defaulting to
   nowselector, at screen position (xoffs, yoffs). */

K_INT16 getselection(K_INT16 xoffs, K_INT16 yoffs, K_INT16 nowselector,
                     K_INT16 totselectors)
{
    K_INT16 animater6, n, esckeystate;
    int mousx, mousy;
    K_INT16 bstatus, obstatus;

    glDrawBuffer(GL_FRONT);
    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    clearkeydefstat(ACTION_MENU);
    clearkeydefstat(ACTION_MENU_CANCEL);
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_UP1);
    clearkeydefstat(ACTION_MENU_UP2);
    clearkeydefstat(ACTION_MENU_DOWN1);
    clearkeydefstat(ACTION_MENU_DOWN2);

    animater6 = 0;
    esckeystate = 0;
    bstatus = 1;
    obstatus = 1;
    mousx = 0;
    mousy = 0;
    while (esckeystate == 0)
    {
        PollInputs();
        animater6++;
        if (animater6 == 6)
            animater6 = 0;

        SDL_Delay(10); /* Let's not soak up all CPU... */

        if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
            statusbardraw(16+(animater6/2)*16, 0, 15, 15, xoffs+20-n, nowselector*12+yoffs+n-1, 85);
        } else {
            if (animater6 < 3)
                statusbardraw(20+animater6*14, 32, 13, 13, xoffs+20-n, nowselector*12+yoffs+n-1+1, statusbarback);
            else
                statusbardraw(20+(animater6-3)*14, 46, 13, 13, xoffs+20-n, nowselector*12+yoffs+n-1+1, statusbarback);
        }
        obstatus = bstatus;
        if (moustat == 0) {
            bstatus=readmouse(&mousx, &mousy);
        }

        if (repeatkeydef(ACTION_MENU_UP1) || repeatkeydef(ACTION_MENU_UP2) || (mousy < -128))
        {
            if (mousy < -128)
                mousy += 128;
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
                wipeoverlay(xoffs+39-n, nowselector*12+yoffs+n-1, 15, 15);
            else
                statusbardraw(16, 15, 13, 13, xoffs+20-n, nowselector*12+yoffs+n-1+1, menu);
            nowselector--;
            ksayui(27);
            if (nowselector < 0)
                nowselector = totselectors-1;
        }

        if (repeatkeydef(ACTION_MENU_DOWN1) || repeatkeydef(ACTION_MENU_DOWN2) || (mousy > 128))
        {
            if (mousy > 128)
                mousy -= 128;
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
                wipeoverlay(xoffs+39-n, nowselector*12+yoffs+n-1, 15, 15);
            else
                statusbardraw(16, 15, 13, 13, xoffs+20-n, nowselector*12+yoffs+n-1+1, menu);
            nowselector++;
            ksayui(27);
            if (nowselector == totselectors)
                nowselector = 0;
        }
        if (getkeydefstatlock(ACTION_MENU) || getkeydefstatlock(ACTION_MENU_CANCEL))
            esckeystate |= 1;
        if (getkeydefstatlock(ACTION_MENU_SELECT1) || getkeydefstatlock(ACTION_MENU_SELECT2) || getkeydefstatlock(ACTION_MENU_SELECT3))
            esckeystate |= 2;

        if ((obstatus == 0) && (bstatus > 0))
            esckeystate |= (bstatus^3);
        glFlush();
        /*SDL_GL_SwapWindow(mainwindow);*/
    }
    ksayui(27);
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
        wipeoverlay(xoffs+39-n, nowselector*12+yoffs+n-1, 15, 15);
    else
        statusbardraw(36-n, 15, 13, 13, xoffs+20-n, nowselector*12+yoffs+n-1+1, menu);
    if ((esckeystate&2) > 0)
        return(nowselector);
    else
        return((-nowselector)-1);
}

void finalisemenu() {
    menuing=0;
    UploadPartialOverlay(menuleft, menutop, menuwidth, menuheight);
}

/* Draw menu background and frame... */

void drawmenu(K_INT16 xsiz, K_INT16 ysiz, K_INT16 walnume)
{
    K_INT16 ycent, i, j, x1, y1, x2, y2;
    unsigned char *buf;

    if (vidmode == 0)
        ycent = 100;
    else
        ycent = 120;

    if (vidmode == 0)
        x1 = 180-(xsiz>>1);
    else
        x1 = 160-(xsiz>>1);
    y1 = ycent-(ysiz>>1)-1;
    x2 = x1+xsiz-16;
    y2 = y1+ysiz-16;

    menuleft=x1+20;
    menutop=y1+1+visiblescreenyoffset+spriteyoffset;
    menuwidth=xsiz;
    menuheight=ysiz;
    menuing=1;

    if (ysiz>240) {menutop=0; menuheight=240;}

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        wipeoverlay(menuleft, menutop, menuwidth, menuheight);
        buf=screenbuffer+(screenbufferwidth*menutop+(menuleft));
        for(i=0;i<menuwidth;i++)
            *(buf++)=239;
        buf=screenbuffer+(screenbufferwidth*(menutop+menuheight-1)+menuleft);
        for(i=0;i<menuwidth;i++)
            *(buf++)=239;
        buf=screenbuffer+(screenbufferwidth*menutop+(menuleft));
        for(i=0;i<menuwidth;i++)
            *(buf+=screenbufferwidth)=239;
        buf=screenbuffer+(screenbufferwidth*menutop+(menuleft+menuwidth-1));
        for(i=0;i<menuwidth;i++)
            *(buf+=screenbufferwidth)=239;
        UploadPartialOverlay(menuleft, menutop, menuwidth, menuheight);

        return;
    }

    if (ysiz<=240) {
        statusbardraw(0, 0, 16, 16, x1, y1+1, walnume);
        statusbardraw(48, 0, 16, 16, x2, y1+1, walnume);
        statusbardraw(0, 48, 16, 16, x1, y2+1, walnume);
        statusbardraw(48, 48, 16, 16, x2, y2+1, walnume);
        for(i=x1+16;i<x2;i+=16)
        {
            statusbardraw(16, 0, 16, 16, i, y1+1, walnume);
            statusbardraw(16, 48, 16, 16, i, y2+1, walnume);
        }
    } else {
        y1=-17; y2=239;
    }

    for(j=y1+16;j<y2;j+=16)
    {
        statusbardraw(0, 16, 16, 16, x1, j+1, walnume);
        statusbardraw(48, 16, 16, 16, x2, j+1, walnume);
    }
    for(i=x1+16;i<x2;i+=16)
        for(j=y1+16;j<y2;j+=16)
            statusbardraw(16, 16, 16, 16, i, j+1, walnume);
}

/* Show credits. */

void creditsmenu()
{
    K_INT16 n;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    drawmenu(320, 176, menu);
    strcpy(&textbuf[0], "Credits");
    textprint(149, 20+n+1, 32);
    loadstory(-1);
    finalisemenu();
    pressakey();
}

/* Show story for episode... */

void bigstorymenu()
{
    K_INT16 i, j, k, nowenterstate, lastenterstate, quitstat, bstatus, obstatus;

    // if (vidmode == 0)
    //     n = 0;
    // else
    //     n = 20;
    if (boardnum < 10) j = -32, k = -27;
    else if (boardnum < 20) j = -26, k = -24;
    else j = -23, k = -22;
    quitstat = 0;
    bstatus = 1;
    obstatus = 1;
    i = j;
    while (quitstat == 0)
    {
        drawmenu(304, 192, menu);
        loadstory(i);
        finalisemenu();
        nowenterstate = 15;
        lastenterstate = 15;
        glFlush();
        while ((nowenterstate <= lastenterstate) && (bstatus <= obstatus))
        {
            PollInputs();
            lastenterstate = nowenterstate;
            nowenterstate  = repeatkeydef(ACTION_MENU_CANCEL);
            nowenterstate |= repeatkeydef(ACTION_MENU);
            nowenterstate |= repeatkeydef(ACTION_MENU_UP1)<<1;
            nowenterstate |= repeatkeydef(ACTION_MENU_UP2)<<1;
            nowenterstate |= repeatkeydef(ACTION_MENU_LEFT1)<<1;
            nowenterstate |= repeatkeydef(ACTION_MENU_LEFT2)<<1;
            nowenterstate |= repeatkeydef(ACTION_MENU_DOWN1)<<2;
            nowenterstate |= repeatkeydef(ACTION_MENU_DOWN2)<<2;
            nowenterstate |= repeatkeydef(ACTION_MENU_RIGHT1)<<2;
            nowenterstate |= repeatkeydef(ACTION_MENU_RIGHT2)<<2;
            nowenterstate |= repeatkeydef(ACTION_MENU_SELECT1)<<3;
            nowenterstate |= repeatkeydef(ACTION_MENU_SELECT2)<<3;
            nowenterstate |= repeatkeydef(ACTION_MENU_SELECT3)<<3;
            obstatus = bstatus;
            if (moustat == 0) {
                bstatus=readmouse(NULL, NULL);
            }
            SDL_Delay(10);
        }
        if ((nowenterstate & 2) == 2 && (i > j))
            i--;
        if ((nowenterstate & 4) == 4 && (i < k))
            i++;
        quitstat = nowenterstate & 1;
        if ((nowenterstate & 8) || (bstatus > obstatus))
        {
            bstatus = 1;
            obstatus = 1;
            i++;
            if (i > k)
                quitstat = 1;
        }
        ksayui(27);
    }
}

/* Quit (Y/N)? */

K_INT16 areyousure()
{
    K_INT16 i, n;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    drawmenu(224, 64, menu);
    strcpy(&textbuf[0], "Really want to quit?");
    textprint(99, 84+n+1, 112);
    strcpy(&textbuf[0], "Yes");
    textprint(105, 96+n+1, 32);
    strcpy(&textbuf[0], "No");
    textprint(105, 108+n+1, 32);
    finalisemenu();
    i = getselection(60, 95, 0, 2);
    if (i == 0)
        return(1);
    else
        return(0);
}

/* Help screen. */

void helpmenu()
{
    K_INT16 n;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    drawmenu(256, 176, menu);
    loadstory(-15);
    strcpy(&textbuf[0], "Help");
    textprint(161, 18+n+1, 32);
    finalisemenu();
    pressakey();
}

/* Soda menu. */

void sodamenu()
{
    K_INT32 ototclocker;
    K_INT16 n, valid;

    wipeoverlay(0, 0, 361, statusbaryoffset);
    glDrawBuffer(GL_FRONT);
    ototclocker = totalclock;
    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    ksayui(27);
    drawmenu(256, 160, menu);
    if (boardnum < 10)
        loadstory(-34);
    else
        loadstory(-33);
    statusbardraw(0, 0, 12, 36, 85-n, 49+n+1, sodapics);
    statusbardraw(12, 0, 12, 36, 85-n, 85+n+1, sodapics);
    statusbardraw(24, 0, 12, 36, 85-n, 121+n+1, sodapics);
    statusbardraw(36, 0, 12, 12, 85-n, 157+n+1, sodapics);
    valid = 0;
    finalisemenu();
    while (valid == 0)
    {
        sodaplace = getselection(46, 49, sodaplace, 10);
        if (sodaplace >= 0)
        {
            valid = 1;
            if ((sodaplace == 0) && (coins < 1)) valid = 0;
            if ((sodaplace == 1) && (coins < 2)) valid = 0;
            if ((sodaplace == 2) && (coins < 2)) valid = 0;
            if ((sodaplace == 3) && (coins < 5)) valid = 0;
            if ((sodaplace == 4) && ((coins < 5) || (boardnum < 10))) valid = 0;
            if ((sodaplace == 5) && (coins < 75)) valid = 0;
            if ((sodaplace == 6) && (coins < 100)) valid = 0;
            if ((sodaplace == 7) && (coins < 150)) valid = 0;
            if ((sodaplace == 8) && ((coins < 200) || (boardnum < 10))) valid = 0;
            if ((sodaplace == 9) && (coins < 250)) valid = 0;
            if (valid == 0)
                ksayui(12);
        }
        else
            valid = 1;
    }
    if ((sodaplace >= 0) && (valid == 1))
    {
        ksay(24);
        switch(sodaplace)
        {
            case 0:
                coins--;
                life += 320;
                if (life > 4095)
                    life = 4095;
                drawlife();
                break;
            case 1:
                coins -= 2;
                if (purpletime < totalclock)
                    purpletime = totalclock+9600;
                else purpletime += 9600;
                statusbardraw(0, 0, 16, 15, 159, 13+statusbaryoffset, statusbarinfo);
                break;
            case 2:
                coins -= 2;
                if (greentime < totalclock)
                    greentime = totalclock + 9600;
                else greentime += 9600;
                statusbardraw(0, 15, 16, 15, 176, 13+statusbaryoffset, statusbarinfo);
                break;
            case 3:
                coins -= 5;
                if (capetime[0] < totalclock)
                    capetime[0] = totalclock + 7200;
                else
                    capetime[0] += 7200;
                statusbardraw(16, 0, 21, 28, 194, 2+statusbaryoffset, statusbarinfo);
                break;
            case 4:
                coins -= 5;
                if (capetime[1] < totalclock)
                    capetime[1] = totalclock + 4800;
                else
                    capetime[1] += 4800;
                statusbardraw(37, 0, 21, 28, 216, 2+statusbaryoffset, statusbarinfo);
                break;
            case 5:
                coins -= 75;
                lightnings++;
                if (lightnings > 6)
                    lightnings = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(296, 21+statusbaryoffset, (char)0);
                textbuf[0] = lightnings+48, textbuf[1] = 0;
                textprint(296, 21+statusbaryoffset, (char)176);
                break;
            case 6:
                coins -= 100;
                firepowers[0]++;
                if (firepowers[0] > 6)
                    firepowers[0] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(272, 12+statusbaryoffset, (char)0);
                textbuf[0] = firepowers[0]+48, textbuf[1] = 0;
                textprint(272, 12+statusbaryoffset, (char)176);
                break;
            case 7:
                coins -= 150;
                firepowers[1]++;
                if (firepowers[1] > 6)
                    firepowers[1] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(272, 21+statusbaryoffset, (char)0);
                textbuf[0] = firepowers[1]+48, textbuf[1] = 0;
                textprint(272, 21+statusbaryoffset, (char)176);
                break;
            case 8:
                coins -= 200;
                firepowers[2]++;
                if (firepowers[2] > 6)
                    firepowers[2] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(296, 12+statusbaryoffset, (char)0);
                textbuf[0] = firepowers[2]+48, textbuf[1] = 0;
                textprint(296, 12+statusbaryoffset, (char)176);
                break;
            case 9:
                coins -= 250;
                compass = 1;
                break;
        }
        textbuf[0] = 9, textbuf[1] = 9;
        textbuf[2] = 9, textbuf[3] = 0;
        textprint(112, 12+statusbaryoffset, (char)0);
        textbuf[0] = (coins/100)+48;
        textbuf[1] = ((coins/10)%10)+48;
        textbuf[2] = (coins%10)+48;
        textbuf[3] = 0;
        if (textbuf[0] == 48)
        {
            textbuf[0] = 32;
            if (textbuf[1] == 48)
                textbuf[1] = 32;
        }
        textprint(112, 12+statusbaryoffset, (char)176);
    }
    if (sodaplace < 0)
    {
        sodaplace = (-sodaplace)-1;
        ksayui(26);
    }
    totalclock = ototclocker;
    SDL_LockMutex(timermutex);
    clockspeed = 0;
    SDL_UnlockMutex(timermutex);
    glDrawBuffer(GL_BACK);
    wipeoverlay(0, 0, 361, statusbaryoffset);
    linecompare(statusbar);
}

/* New credits instead of ordering info. */

void orderinfomenu() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
           "\"Ken's Labyrinth\"");
    textprint(30, 48, 80);
    strcpy(textbuf, "Copyright (c) 1992-1993 Ken Silverman");

    textprint(30, 58, 32);

    strcpy(textbuf,
           "\"LAB3D/SDL\" conversion");
    textprint(30, 76, 80);

    /* Fonts only have 7-bit ASCII, and my surname needs a character not in
       this set. Fake the dots. */

    strcpy(textbuf,
           ".");
    textprint(260, 80, 32);
    textprint(264, 80, 32);
    strcpy(textbuf,
           "Copyright (c) 2002-2004 Jan Lonnberg");
    textprint(30, 86, 32);

    strcpy(textbuf,
           "Copyright (c) 2013-2015 Jared Stafford");
    textprint(30, 96, 32);

    strcpy(textbuf,
           "Copyright (c) 2019 Cameron Armstrong");
    textprint(30, 106, 32);

    strcpy(textbuf,
            "http://icculus.org/LAB3D");
    textprint(30, 116, 48);

    strcpy(textbuf,
           "gitlab.com/jspenguin/lab3d-sdl");
    textprint(30, 126, 48);

    strcpy(textbuf,
            "github.com/sacredbanana/lab3d-sdl");
    textprint(30, 136, 48);

    strcpy(textbuf,
           "See readme.txt for license");
    textprint(30, 151, 96);

    strcpy(textbuf,
           "Ken Silverman's official web site:");
    textprint(30, 166, 0);

    strcpy(textbuf,
           "http://www.advsys.net/ken");
    textprint(30, 176, 48);

    finalisemenu();
    pressakey();

}

/* Save/load game selector. */

K_INT16 loadsavegamemenu(K_INT16 whichmenu)
{
    char filename[20];
    K_INT16 fil, i, j, k, n;
    K_INT32 templong;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    drawmenu(320, 160, menu);
    if (whichmenu == 1)
    {
        strcpy(&textbuf[0], "Load game");
        textprint(137, 26+n+1, 32);
    }
    else
    {
        strcpy(&textbuf[0], "Save game");
        textprint(137, 26+n+1, 112);
    }
    strcpy(&textbuf[0], "#: Name:       Board: Score: Time:");
    textprint(55, 52+n+1, 48);
    if (gameheadstat == 0)
    {
        for(j=0;j<8;j++)
        {
            sprintf(filename, "SAVGAME%d.DAT", j);
            sprintf(filepathUpper, "%s%s", gameroot, filename);
            if((fil=open(filepathUpper, O_RDONLY|O_BINARY, 0))!=-1)
            {
                gamexist[j] = 1;
                read(fil, &gamehead[j][0], 27);
                close(fil);
            }
            else {
                sprintf(filename, "savgame%d.dat", j);
                sprintf(filepath, "%s%s", gameroot, filename);
                if((fil=open(filepath, O_RDONLY|O_BINARY, 0))!=-1)
                {
                    gamexist[j] = 1;
                    read(fil, &gamehead[j][0], 27);
                    close(fil);
                }
                else
                    gamexist[j] = 0;
            }
        }
        gameheadstat = 1;
    }
    j = 0;
    for(i=70+n;i<166+n;i+=12)
    {
        if (gamexist[j] == 1)
        {
            textbuf[0] = j+49, textbuf[1] = 32, textbuf[2] = 32;
            for(k=0;k<12;k++)
            {
                textbuf[k+3] = gamehead[j][k];
                if (textbuf[k+3] == 0)
                    textbuf[k+3] = 32;
            }
            textbuf[15] = 32;
            textbuf[16] = ((gamehead[j][17]+1)/10)+48;
            if (textbuf[16] == 48)
                textbuf[16] = 32;
            textbuf[17] = ((gamehead[j][17]+1)%10)+48;
            textbuf[18] = 32;
            textbuf[19] = 32;
            textbuf[20] = 32;
            k = j*27;
            templong=readlong(&gamehead[j][19]);
//	    templong=*((K_INT32 *)(&gamehead[j][19]));

            textbuf[21] = (char)((templong/100000L)%10L)+48;
            textbuf[22] = (char)((templong/10000L)%10L)+48;
            textbuf[23] = (char)((templong/1000L)%10L)+48;
            textbuf[24] = (char)((templong/100L)%10L)+48;
            textbuf[25] = (char)((templong/10L)%10L)+48;
            textbuf[26] = (char)(templong%10L)+48;
            textbuf[27] = 32;
            k = 21;
            while ((textbuf[k] == 48) && (k < 26))
                textbuf[k++] = 32;
            k = j*27;
            templong=readlong(&gamehead[j][23]);
//	    templong=*((K_INT32 *)(&gamehead[j][23]));

            templong /= 240;
            textbuf[28] = (char)((templong/10000L)%10L)+48;
            textbuf[29] = (char)((templong/1000L)%10L)+48;
            textbuf[30] = (char)((templong/100L)%10L)+48;
            textbuf[31] = (char)((templong/10L)%10L)+48;
            textbuf[32] = (char)(templong%10L)+48;
            textbuf[33] = 0;
            k = 28;
            while ((textbuf[k] == 48) && (k < 32))
                textbuf[k++] = 32;
            textprint(56, i-1+1, 30);
            textprint(55, i-1+1, 32);
        }
        else
        {
            textbuf[0] = j+49;
            textbuf[1] = 0;
            textprint(56, i-1+1, 28);
            textprint(55, i-1+1, 30);
        }
        j++;
    }
    finalisemenu();
    if (whichmenu == 1)
    {
        do
        {
            loadsavegameplace = getselection(16, 67, loadsavegameplace, 8);
        }
        while ((loadsavegameplace >= 0) && (gamexist[loadsavegameplace] == 0));
        j = loadsavegameplace;
        if ((loadsavegameplace < 0) || (gamexist[loadsavegameplace] == 0))
            loadsavegameplace = (-loadsavegameplace)-1;
    }
    else
    {
        loadsavegameplace = getselection(16, 67, loadsavegameplace, 8);
        j = loadsavegameplace;
        if (loadsavegameplace < 0)
            loadsavegameplace = (-loadsavegameplace)-1;
    }
    return(j);
}

/* Choose episode... */

K_INT16 newgamemenu()
{
    K_INT16 j, n;

    if (vidmode == 0)
        n = 0;
    else
        n = 20;
    drawmenu(288, 64, menu);
    strcpy(&textbuf[0], "New game");
    textprint(137, 74+n+1, 112);
    strcpy(&textbuf[0], "Episode 1: Search for Sparky");
    textprint(67, 88+n+1, 32);
    if (numboards >= 20) j = 32; else j = 28;
    strcpy(&textbuf[0], "Episode 2: Sparky's Revenge");
    textprint(67, 100+n+1, ((char)j));
    if (numboards >= 30) j = 32; else j = 28;
    strcpy(&textbuf[0], "Episode 3: Find the Way Home");
    textprint(67, 112+n+1, ((char)j));
    if (newgameplace < 0) newgameplace = 0;
    if (newgameplace > 2) newgameplace = 2;
    finalisemenu();
    newgameplace = getselection(28, 87, newgameplace, 3);
    if ((newgameplace == 1) && (numboards < 20))
        return(newgameplace);
    if ((newgameplace == 2) && (numboards < 30))
        return(newgameplace);
    if (newgameplace<0) {
        newgameplace=(-newgameplace)-1;
        return -1;
    }
    drawmenu(288, 64, menu);
    strcpy(&textbuf[0], "New game");
    textprint(137, 74+n+1, 112);
    strcpy(&textbuf[0], "Easy: Don't touch me.");
    textprint(67, 92+n+1, 32);
    strcpy(&textbuf[0], "Hard: OUCH!");
    textprint(67, 104+n+1, 32);
    finalisemenu();
    if (skilevel < 0) skilevel = 0;
    if (skilevel > 1) skilevel = 1;
    skilevel = getselection(28, 91, skilevel, 2);
    if (skilevel<0) {
        skilevel=(-skilevel)-1;
        return -1;
    }
    j = newgameplace;
    if (newgameplace < 0)
        newgameplace = (-newgameplace)-1;
    return(j);
}

/* Wait for keypress (or mouse click). */

void pressakey()
{
    K_INT16 bstatus, obstatus;

    glFlush();

    bstatus = 1;
    obstatus = 1;
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    clearkeydefstat(ACTION_MENU_CANCEL);
    clearkeydefstat(ACTION_MENU);
    while (repeatkeydef(ACTION_MENU_SELECT1) == 0 &&
           repeatkeydef(ACTION_MENU_SELECT2) == 0 &&
           repeatkeydef(ACTION_MENU_SELECT3) == 0 &&
           repeatkeydef(ACTION_MENU_CANCEL) == 0 &&
           repeatkeydef(ACTION_MENU) == 0 &&
           (bstatus <= obstatus))
    {
        PollInputs();
        obstatus = bstatus;
        if (moustat == 0) {
            bstatus=readmouse(NULL, NULL);
        }
    }
    ksayui(27);
}

/* Update slot machine... */

void copyslots(K_INT16 slotnum)
{
    K_INT16 i, j, k;
    unsigned char *l;
    unsigned char *RGBATexture=malloc(64*64*4);

    l = walseg[slotinfo-1];
    for(i=0;i<3;i++)
        for(j=0;j<8;j++)
        {
            k = ((slotpos[i]+j)&63)+(((slotpos[i]+j)&64)<<3)+(i<<10);
            tempbuf[(i<<8)+(j<<3)]=l[k];
            tempbuf[(i<<8)+(j<<3)+1]=l[k+64];
            tempbuf[(i<<8)+(j<<3)+2]=l[k+128];
            tempbuf[(i<<8)+(j<<3)+3]=l[k+192];
            tempbuf[(i<<8)+(j<<3)+4]=l[k+256];
            tempbuf[(i<<8)+(j<<3)+5]=l[k+320];
            tempbuf[(i<<8)+(j<<3)+6]=l[k+384];
            tempbuf[(i<<8)+(j<<3)+7]=l[k+448];
        }
    l = walseg[slotnum-1];
    for(i=0;i<3;i++)
        for(j=0;j<8;j++)
        {
            k = 1296+(((i<<3)+i)<<6)+j;
            l[k]=tempbuf[(i<<8)+(j<<3)];
            l[k+64]=tempbuf[(i<<8)+(j<<3)+1];
            l[k+128]=tempbuf[(i<<8)+(j<<3)+2];
            l[k+192]=tempbuf[(i<<8)+(j<<3)+3];
            l[k+256]=tempbuf[(i<<8)+(j<<3)+4];
            l[k+320]=tempbuf[(i<<8)+(j<<3)+5];
            l[k+384]=tempbuf[(i<<8)+(j<<3)+6];
            l[k+448]=tempbuf[(i<<8)+(j<<3)+7];
        }
    glBindTexture(GL_TEXTURE_2D, texName[slotnum-1]);
    checkGLStatus();

    TextureConvert(l, RGBATexture, bmpkind[slotnum]);

    BuildMipmaps((Uint32*)RGBATexture, 64, 64, 1, 10);
    checkGLStatus();
    free(RGBATexture);
}

/* Update map position... */

void youarehere()
{

    walseg[map-1][yourhereoldpos]=board[0][yourhereoldpos];

    yourhereoldpos = ((posx>>10)<<6)+(posy>>10);

    walseg[map-1][yourhereoldpos]=255;
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

/* Get keypress in Unicode form. */

Uint16 getkeypress(int* key) {
    SDL_Event event;

    while(SDL_PollEvent(&event))
    {
        ProcessEvent(&event);
        switch(event.type)
        {
            case SDL_KEYDOWN:
                *key = event.key.keysym.sym;
                return 1;
            case SDL_TEXTINPUT:
                *key = event.text.text[0];
                return 2;
            default:
                break;
        }
    }
    if (quitgame) quit();
    return 0;
}

void FindJoysticks() {
    #ifdef __SWITCH__
    cur_joystick_index = 0;
    cur_controller = SDL_GameControllerOpen(0);
    cur_controller_index = 0;
    joystat = 1;
    return;
    #endif

    int i;
    if (!joyenable) return;
    for (i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            fprintf(stderr, "Controller at %d\n", i);
            if (!cur_controller) {
                cur_controller = SDL_GameControllerOpen(i);
                cur_controller_index = i;
            }
        } else {
            fprintf(stderr, "Joystick at %d\n", i);
            if (!cur_joystick) {
                cur_joystick = SDL_JoystickOpen(i);
                cur_joystick_index = i;
            }
        }
    }
    fprintf(stderr, "Controller index = %d\n", cur_controller_index);
    fprintf(stderr, "Joystick index = %d\n", cur_joystick_index);
    joystat = (cur_joystick || cur_controller) ? 0 : 1;
}

/* Poll for keyboard input or quit command. */

void ProcessEvent(SDL_Event* event) {
    int sk, scan;
    
    switch(event->type)
    {
        case SDL_QUIT:
            quitgame=1;
            break;
        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                window_in_focus = 1;
            } else if (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                window_in_focus = 0;
            } else if (event->window.event == SDL_WINDOWEVENT_EXPOSED) {
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                ShowPartialOverlay(0, 0, screenbufferwidth, screenbufferheight, 0);
            }
            break;
        case SDL_JOYDEVICEADDED:
        case SDL_CONTROLLERDEVICEADDED:
            FindJoysticks();
            break;
        case SDL_JOYDEVICEREMOVED:
            if (event->jdevice.which == cur_joystick_index) {
                cur_joystick_index = -1;
                SDL_JoystickClose(cur_joystick);
                cur_joystick = NULL;
            }
            FindJoysticks();
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            if (event->cdevice.which == cur_controller_index) {
                cur_controller_index = -1;
                SDL_GameControllerClose(cur_controller);
                cur_controller = NULL;
            }
            FindJoysticks();
            break;
        case SDL_KEYDOWN:
            sk=event->key.keysym.sym;
            scan = get_pckey(sk);
            if (scan>=0) {
                keystatus[scan] = 1;
            }
            setnewkeystatus(sk, 1);
            break;
        case SDL_KEYUP:
            sk=event->key.keysym.sym;
            scan = get_pckey(sk);
            if (scan >= 0) {
                keystatus[scan] = 0;
            }
            setnewkeystatus(sk, 0);
            break;
        default:
            break;
    }
}

void PollInputs() {
    SDL_Event event;
    while(SDL_PollEvent(&event))
    {
        ProcessEvent(&event);
    }
    if (quitgame) quit();

    // If Nintendo Switch, poll to see if user has changed mode from portable/docked
    #ifdef __SWITCH__
    configureResolution();
    hidReset();
    hidScanInput();
    #endif
}

/* Read mouse position (into x and y; NULL to ignore) and return buttons
   (1=left, 2=right, 4=middle). */

unsigned char readmouse(int *x, int *y) {
    int tx, ty;
    unsigned char bstatus=SDL_GetRelativeMouseState(&tx, &ty);

    /* Swap middle and right mouse buttons to match Microsoft style. */

    if (x!=NULL) *x+=5*tx;
    if (y!=NULL) *y+=5*ty;

    return ((bstatus&4)>>1)|(bstatus&1)|(bstatus&2)<<1;
}

void quit() {

    savesettings();

    if (demorecording) {
        demofile_close(demorecording);
        demorecording = NULL;
    }

    if (demoplaying) {
        demofile_close(demoplaying);
        demoplaying = NULL;
    }

    /* Start by demolishing all other threads... */

    SDL_UnlockMutex(timermutex); /* Just in case we have it... */
    SDL_UnlockMutex(soundmutex); /* Just in case we have it... */

    musicoff();

    if (speechstatus >= 2) {
        /* SDL is very careful to allow the sound thread to stop. Good for
           us. */

        SDL_CloseAudio();
        free(SoundBuffer);
    }

    free(screenbuffer);

    if (quitgame == 2)
        fprintf(stderr, "Error #3:  Invalid saved game.");
    if (numboards < 30)
    {

        /* Print shareware message:
           Dump 16K of segment walseg[endtext-1] to text mode screen,
           move cursor to start of line 0x16 (0 is top).

           Removed to avoid confusing people (and to save me the trouble
           of parsing a CGA colour text screen). */

    }
    free((void *)note);
    free(lzwbuf);
    free(lzwbuf2);
    if (convwalls > 0) free(pic);

    if (cur_joystick)
        SDL_JoystickClose(cur_joystick);

    if (cur_controller)
        SDL_GameControllerClose(cur_controller);

    #ifdef __SWITCH__
    romfsExit();
    #endif
    SDL_VideoQuit();
    SDL_Quit();

    exit(0);
}

#ifndef log2
#define log2(a) (log(a)/log(2))
#endif

void randominsts()
{
    long i, j, k;
    float f;

    if (channels == 2)
    {
        j = (rand()&2)-1; k = 0;
        for(i=0;i<9;i++)
        {
            if ((i == 0) || (chantrack[i] != chantrack[i-1]))
            {
                f = (float)rand()/(float)RAND_MAX;
                if (j > 0)
                {
                    //lvol[i] < rvol[i]
                    lvol[i] = log2(f+1);
                    rvol[i] = log2(3-f);
                    lplc[i] = rand()&255;
                    rplc[i] = 0;
                }
                else
                {
                    //lvol[i] > rvol[i]
                    lvol[i] = log2(3-f);
                    rvol[i] = log2(f+1);
                    lplc[i] = 0;
                    rplc[i] = rand()&255;
                }
                j = -j;
                if (((drumstat&32) == 0) || (i < 6)) k++;
            }
            else
            {
                lvol[i] = lvol[i-1]; rvol[i] = rvol[i-1];
                lplc[i] = lplc[i-1]; rplc[i] = rplc[i-1];
            }
        }
        if (k < 2)  //If only 1 source, force it to be in center
        {
            if (drumstat&32) i = 5; else i = 8;
            for(;i>=0;i--)
            {
                lvol[i] = rvol[i] = 1;
                lplc[i] = rplc[i] = 0;
            }
        }
    }
}

void drawinputbox() {
    int k;

    menuing=1; menuleft=84; menutop=127;
    menuwidth=192; menuheight=64;
    statusbardraw(0, 0, 57, 64, 84-20, 127, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 76 : scorebox);
    statusbardraw(7, 0, 39, 64, 141-20, 127, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 76 : scorebox);
    statusbardraw(7, 0, 39, 64, 180-20, 127, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 76 : scorebox);
    statusbardraw(7, 0, 57, 64, 219-20, 127, lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 76 : scorebox);
    for(k=0;k<22;k++)
        textbuf[k] = 8;
    textbuf[22] = 0;
    textprint(91, 145+1, (char)0);
    textbuf[1] = 0;
    textprint(261, 145+1, (char)0);
}

#ifdef __SWITCH__
void setMesaConfig() {
    // Uncomment below to disable error checking and save CPU time (useful for production):
    // setenv("MESA_NO_ERROR", "1", 1);

    // Uncomment below to enable Mesa logging:
    setenv("EGL_LOG_LEVEL", "debug", 1);
    setenv("MESA_VERBOSE", "all", 1);
    setenv("NOUVEAU_MESA_DEBUG", "1", 1);

    // Uncomment below to enable shader debugging in Nouveau:
    setenv("NV50_PROG_OPTIMIZE", "0", 1);
    setenv("NV50_PROG_DEBUG", "1", 1);
    setenv("NV50_PROG_CHIPSET", "0x120", 1);
}

//-----------------------------------------------------------------------------
// nxlink support
//-----------------------------------------------------------------------------

static int s_nxlinkSock = -1;

void initNxLink()
{
    if (R_FAILED(socketInitializeDefault()))
        return;

    s_nxlinkSock = nxlinkStdio();
    if (s_nxlinkSock >= 0)
        TRACE("printf output now goes to nxlink server");
    else
        socketExit();
}

void deinitNxLink()
{
    if (s_nxlinkSock >= 0)
    {
        close(s_nxlinkSock);
        socketExit();
        s_nxlinkSock = -1;
    }
}

void userAppInit()
{
    initNxLink();
}

void userAppExit()
{
    deinitNxLink();
}

void getUsername()
{
    Result rc=0;

    u128 userID=0;
    bool account_selected=0;
    AccountProfile profile;
    AccountUserData userdata;
    AccountProfileBase profilebase;

    memset(&userdata, 0, sizeof(userdata));
    memset(&profilebase, 0, sizeof(profilebase));

    strncpy(hiscorenam, "Ken", sizeof(hiscorenam));

    rc = accountInitialize();
    if (R_FAILED(rc)) {
        TRACE("accountInitialize() failed: 0x%x\n", rc);
    }

    if (R_SUCCEEDED(rc)) {
        rc = accountGetActiveUser(&userID, &account_selected);

        if (R_FAILED(rc)) {
            TRACE("accountGetActiveUser() failed: 0x%x\n", rc);
        }
        else if(!account_selected) {
            TRACE("No user is currently selected.\n");
            rc = -1;
        }

        if (R_SUCCEEDED(rc)) {
            TRACE("Current userID: 0x%lx 0x%lx\n", (u64)(userID>>64), (u64)userID);

            rc = accountGetProfile(&profile, userID);

            if (R_FAILED(rc)) {
                TRACE("accountGetProfile() failed: 0x%x\n", rc);
            }
        }

        if (R_SUCCEEDED(rc)) {
            rc = accountProfileGet(&profile, &userdata, &profilebase);//userdata is otional, see libnx acc.h.

            if (R_FAILED(rc)) {
                TRACE("accountProfileGet() failed: 0x%x\n", rc);
            }

            if (R_SUCCEEDED(rc)) {
                strncpy(hiscorenam, profilebase.username, sizeof(hiscorenam)-1);//Even though profilebase.username usually has a NUL-terminator, don't assume it does for safety.

                TRACE("Username: %s\n", hiscorenam);
            }

            accountProfileClose(&profile);
        }
    }

accountExit();
}
#endif
