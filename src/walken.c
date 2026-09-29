/*
 * Walken: the 1992 pre-release of Ken's Labyrinth.
 *
 * This is a port of Ken Silverman's WALKEN.C (16 bit real mode DOS, MSC
 * 6.00a) to the LAB3D/SDL engine, in the same way oldlab3d.c ports v1.x.  The
 * game logic follows the original closely, down to the order things happen
 * in a frame; what changes is everything that touched the hardware:
 *
 *   - Mode X page flipping and the VGA line compare split become picrot()
 *     and the 8 bit overlay, which holds the status bar during the game and
 *     stands in for video memory during the intro.
 *   - Sound Blaster DMA, Adlib and MPU-401 port writes become ksay() and
 *     loadmusic(), which know how to read Walken's loose .WAV and .KSM files.
 *   - The keyboard interrupt becomes the port's configurable actions, so the
 *     mouse, joysticks and game controllers work as they do in v1.x.
 *
 * Walken's data differs from v1.x:
 *
 *   WALLS.KZP   80 flag bytes (bit 0: solid, bit 1: sprite), then 80 walls of
 *               64x64 texels, column major, as (value, run length) byte pairs.
 *   BOARDS.DAT  64x64 bytes per board.  Bit 7 of a cell turns a wall into one
 *               you can walk through; it is kept as WALKEN_SECRET here, above
 *               the tile number, so the renderer reads boards unchanged.
 *   *.WAV       11025Hz 8 bit mono sound effects.
 *   *.KSM       one song per board, plus INTRO.KSM.
 *
 * Deliberate differences from the original, all to keep it playable at
 * modern frame rates: damage from monsters and fans is charged per tick
 * rather than per frame, a sound that repeats every frame while the player
 * stands in a fan (or keeps pressing a button with nothing to use it on)
 * waits for its previous copy to finish instead of piling up, and a monster's
 * dying explosion stays up for 16 ticks rather than for one frame.
 */

#include "lab3d.h"
#include <ctype.h>

/* Walls whose credit goes to Ken (1) or Andy (2) in the intro. */
static const char credits[WALKEN_NUMWALLS+1] =
{
    0,
    1,1,1,1,1,1,1,1,1,2,1,1,1,1,2,2,2,2,1,1,
    1,2,2,2,2,2,0,0,0,0,1,1,1,0,1,1,1,1,1,1,
    1,1,1,1,2,2,2,2,1,1,1,2,2,2,2,2,2,2,2,2,
    1,1,1,0,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,0
};

/* Sound effects, in the order walkenloadsounds() packs them. */
enum {
    SND_BLOWUP, SND_BOUNCE, SND_BULLSEYE, SND_CONGRATS, SND_DEATH, SND_FALL,
    SND_GETSTUFF, SND_HITANDY, SND_HITFAN, SND_NOTNOW, SND_OUCH, SND_SHOOT,
    SND_UNLOCK, SND_WELCOME, SND_COUNT
};

static const char *soundnames[SND_COUNT] = {
    "BLOWUP", "BOUNCE", "BULLSEYE", "CONGRATS", "DEATH", "FALL",
    "GETSTUFF", "HITANDY", "HITFAN", "NOTNOW", "OUCH", "SHOOT",
    "UNLOCK", "WELCOME"
};

/* Length of each effect in ticks, and when a repeating one may start again. */
static K_INT32 soundticks[SND_COUNT], soundfree[SND_COUNT];

/* WALKEN.C's passthrough[] (non-zero for a wall that stops you) and its
   sprite borders, which are indexed from 0 where the renderer's are indexed
   from 1. */
static unsigned char solid[WALKEN_NUMWALLS+1];
static K_INT16 wlborder[128], wrborder[128];

static K_INT16 youbulnum;       /* the player's bullets in flight          */
static K_INT16 hurtticks;       /* contact damage not yet charged          */

/* Board access that wraps like the 16 bit original instead of leaving the
   array. */
#define B(x,y) board[(x)&63][(y)&63]
#define TAN(a) tantable[(a)&1023]
#define W16(v) ((K_UINT16)(v))

static int issolid(K_INT16 b) {
    if (b & WALKEN_SECRET)
        return 0;
    return solid[b&1023];
}

/* long * long as a 16 bit compiler did it: wrapping at 32 bits. */
static K_INT32 mul32(K_INT32 a, K_INT32 b) {
    return (K_INT32)((K_UINT32)a * (K_UINT32)b);
}

/* ------------------------------------------------------------ loading */

void walkenpalette(void)
{
    int i, c1 = 0, c2 = 0, c3 = 0;

    /* A 6x7x6 colour cube, then four whites.  The sprites and the overlay
       share it, as they did on the one VGA DAC. */
    for(i=0;i<252;i++)
    {
        palette[i*3] = (c1<<6)/6;
        palette[i*3+1] = (c2<<6)/7;
        palette[i*3+2] = (c3<<6)/6;
        c1++;
        if (c1 == 6)
        {
            c1 = 0;
            c2++;
            if (c2 == 7)
            {
                c2 = 0;
                c3++;
            }
        }
    }
    for(i=252*3;i<768;i++)
        palette[i] = 63;
    memcpy(spritepalette, palette, 768);
    settransferpalette();
}

void walkenloadwalls(void)
{
    unsigned char flags[WALKEN_NUMWALLS], pair[2];
    unsigned char *walsegg;
    K_INT16 i, j, fil, cnt, n, minfilt, magfilt;

    sprintf(filepath, "%swalls.kzp", gameroot);
    sprintf(filepathUpper, "%sWALLS.KZP", gameroot);
    if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
        ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
        fatal_error("Can't find walls.kzp.");

    if (read(fil, flags, WALKEN_NUMWALLS) != WALKEN_NUMWALLS)
        fatal_error("Error in walls.kzp.");

    /* Whatever the launcher's version left behind must not show through. */
    memset(bmpkind, 0, sizeof(bmpkind));
    memset(wallheader, 0, sizeof(wallheader));
    memset(shadow2, 0, sizeof(shadow2));
    for(i=0;i<numsplits;i++)
        splitTexNum[i] = -1;

    solid[0] = 0;
    for(i=0;i<WALKEN_NUMWALLS;i++)
    {
        solid[i+1] = (flags[i]&1);
        bmpkind[i+1] = 1+((flags[i]&2)>>1);
    }

    minfilt = default_wallparam.minfilt;
    magfilt = default_wallparam.magfilt;
    if (fullfilter == GL_NEAREST)
        minfilt = magfilt = GL_NEAREST;

    for(i=0;i<WALKEN_NUMWALLS;i++)
    {
        PL_PumpClock();
        walsegg = walseg[i];

        /* Each pair is a colour and a run length. */
        cnt = 0;
        while (cnt < 4096)
        {
            if (read(fil, pair, 2) != 2)
                fatal_error("walls.kzp is truncated at wall %d.", i+1);
            n = pair[1];
            if (n > 4096-cnt)
                n = 4096-cnt;
            memset(walsegg+cnt, pair[0], n);
            cnt += n;
        }

        if (bmpkind[i+1] == 2)
        {
            j = 0;
            while ((j < 4096) && (walsegg[j] == 255))
                j++;
            wlborder[i] = (j&0xfc0);
            lborder[i+1] = (j&0xfc0);
            j = 4095;
            while ((j > 0) && (walsegg[j] == 255))
                j--;
            wrborder[i] = (j&0xfc0);
            rborder[i+1] = (j&0xfc0)+64;
        }
        else
        {
            wlborder[i] = 0;
            wrborder[i] = 4095;
            lborder[i+1] = 0;
            rborder[i+1] = 4096;
        }

        walltexcoord[i][0] = 0.0;
        walltexcoord[i][1] = 1.0;
        R_LoadWallTexture(i, walsegg, bmpkind[i+1],
                          (bmpkind[i+1] < 2) ? 1 : 0, minfilt, magfilt);
    }
    close(fil);
}

/* Read one .WAV file, returning its samples (or NULL) and their length. */
static unsigned char *loadwav(const char *name, long *length)
{
    unsigned char header[12], *data;
    K_UINT32 chunk;
    FILE *f;

    *length = 0;
    sprintf(filepath, "%s%s.WAV", gameroot, name);
    if ((f = fopen(filepath, "rb")) == NULL) {
        int i;
        sprintf(filepath, "%s%s.wav", gameroot, name);
        for (i = strlen(gameroot); filepath[i] != '.'; i++)
            filepath[i] = tolower((unsigned char)filepath[i]);
        if ((f = fopen(filepath, "rb")) == NULL) {
            fprintf(stderr, "Missing sound %s.WAV\n", name);
            return NULL;
        }
    }

    if (fread(header, 1, 12, f) != 12 || memcmp(header, "RIFF", 4) ||
        memcmp(header+8, "WAVE", 4)) {
        fclose(f);
        return NULL;
    }
    /* Walk the chunks to "data"; the samples are unsigned 8 bit mono. */
    while (fread(header, 1, 8, f) == 8) {
        chunk = readlong(header+4);
        if (!memcmp(header, "data", 4)) {
            /* WALKEN.C never played more than 32000 bytes of a sound. */
            if (chunk > 32000) chunk = 32000;
            data = malloc(chunk ? chunk : 1);
            if (data)
                *length = (long)fread(data, 1, chunk, f);
            fclose(f);
            return data;
        }
        fseek(f, (chunk+1)&~1, SEEK_CUR);
    }
    fclose(f);
    return NULL;
}

/* Pack the effects into the sounds.kzp layout ksaypan() reads: a count, then
   a 4 byte offset and 2 byte length per sound, then the samples. */
unsigned char *walkenloadsounds(long *size)
{
    unsigned char *wav[SND_COUNT], *out;
    long length[SND_COUNT], offset;
    int i;

    offset = 2+6*SND_COUNT;
    for (i = 0; i < SND_COUNT; i++) {
        wav[i] = loadwav(soundnames[i], &length[i]);
        soundticks[i] = (length[i]*240)/SOUNDNATIVERATE;
        soundfree[i] = 0;
        offset += length[i];
    }

    out = malloc(offset);
    if (out != NULL) {
        writeshort(out, SND_COUNT);
        offset = 2+6*SND_COUNT;
        for (i = 0; i < SND_COUNT; i++) {
            writelong(out+2+i*6, offset);
            writeshort(out+6+i*6, (K_UINT16)length[i]);
            if (length[i])
                memcpy(out+offset, wav[i], length[i]);
            offset += length[i];
        }
    }
    for (i = 0; i < SND_COUNT; i++)
        free(wav[i]);

    *size = offset;
    return out;
}

/* For sounds WALKEN.C starts again on every frame: the Sound Blaster cut the
   last copy off, where the mixer here would play them all at once. */
static void sayonce(K_INT16 snd)
{
    if ((totalclock >= soundfree[snd]) ||
        (soundfree[snd]-totalclock > soundticks[snd])) {
        soundfree[snd] = totalclock+soundticks[snd];
        ksay(snd);
    }
}

static void songname(char *ksmfile, K_INT16 num)
{
    sprintf(ksmfile, "WALSNG%02d", num);
}

/* The board as the original stored it: one byte per cell. */
static unsigned char boardbyte(K_INT16 b)
{
    return (unsigned char)((b&127)|((b&WALKEN_SECRET) ? 128 : 0));
}

/* The last wall shows the board itself, as a sort of map. */
static void updatemaptexture(void)
{
    int i;

    for(i=0;i<4096;i++)
        walseg[WALKEN_NUMWALLS-1][i] = boardbyte(board[i>>6][i&63]);
    R_UpdateWallTexture(WALKEN_NUMWALLS);
}

static void walkenloadboard(void)
{
    unsigned char cells[4096];
    K_INT16 i, j, k, fil;

    sprintf(filepath, "%sboards.dat", gameroot);
    sprintf(filepathUpper, "%sBOARDS.DAT", gameroot);
    if (((fil = open(filepath, O_RDONLY|O_BINARY, 0)) == -1)&&
        ((fil = open(filepathUpper, O_RDONLY|O_BINARY, 0)) == -1))
        fatal_error("Can't find boards.dat.");
    lseek(fil, ((long)boardnum)<<12, SEEK_SET);
    if (read(fil, cells, 4096) != 4096)
        fatal_error("Error in boards.dat.");
    close(fil);

    for(i=0;i<4096;i++)
        board[i>>6][i&63] = (cells[i]&127)|((cells[i]&128) ? WALKEN_SECRET : 0);
    updatemaptexture();

    /* The original only scanned 63x63 cells for monsters and the start. */
    mnum = 0;
    for(i=0;i<63;i++)
        for(j=0;j<63;j++)
        {
            mboard[i][j] = 0;
            k = cells[(i<<6)+j];
            if ((k == 66) || (k == 38) || (k == 54) || (k == 68))
            {
                mposx[mnum] = (i<<10)+512;
                mposy[mnum] = (j<<10)+512;
                mgolx[mnum] = mposx[mnum];
                mgoly[mnum] = mposy[mnum];
                moldx[mnum] = mposx[mnum];
                moldy[mnum] = mposy[mnum];
                mstat[mnum] = k;
                mshock[mnum] = 0;
                mshot[mnum] = 0;
                if (k == 66)
                    mshot[mnum] = 1;
                if (k == 38)
                    mshot[mnum] = 2;
                if (k == 54)
                    mshot[mnum] = 5;
                mnum++;
                mboard[i][j] = k;
                board[i][j] = 0;
            }
            if (k >= 252)
            {
                posx = (i<<10)+512;
                posy = (j<<10)+512;
                ang = ((k-252)<<9);
                startx = posx;
                starty = posy;
                startang = ang;
                board[i][j] = 0;
            }
        }
    posz = 32;
    angvel = 0;
    vel = 0;
    mxvel = 0;
    myvel = 0;
    hvel = 0;
    for(i=0;i<64;i++)
        bulstat[i] = 0;
    lastbulshoot = 0;
    bulnum = 0;
    youbulnum = 0;
    explonum = 0;
    keys[0] = 0;
    death = 63;
}

/* ------------------------------------------------------------ status bar */

/* Draw wall walnum at half size into the status bar, x pixels from its left
   edge.  Texels 252 and up are transparent. */
static void walkenstatusbardraw(K_INT16 x, K_INT16 walnum)
{
    unsigned char *pic = walseg[walnum-1], *buf, c;
    int a, b, sx = x+20;

    for(a=0;a<32;a++)
    {
        buf = screenbuffer+screenbufferwidth*statusbaryoffset+sx+a;
        for(b=0;b<32;b++)
        {
            c = pic[(a<<7)+(b<<1)];
            if (c < 252)
                *buf = c;
            buf += screenbufferwidth;
        }
    }
    UploadPartialOverlay(sx, statusbaryoffset, 32, 32);
}

/* The life meter: the top two rows of the bar, every other pair of pixels,
   white for what is left, black for what is gone. */
static void walkendrawlife(void)
{
    K_INT16 from, to, n;
    unsigned char col, *c;

    if (life < 0)
        life = 0;
    if (life > 63)
        life = 63;
    if (life == oldlife)
        return;
    if (life < oldlife)
        from = life, to = oldlife, col = 0;
    else
        from = oldlife, to = life, col = 252;

    c = screenbuffer+screenbufferwidth*statusbaryoffset+((from+13)<<2);
    for(n=from;n<to;n++,c+=4)
    {
        c[1] = c[2] = col;
        c[1+screenbufferwidth] = c[2+screenbufferwidth] = col;
    }
    UploadPartialOverlay((from+13)<<2, statusbaryoffset, (to-from)<<2, 2);
    oldlife = life;
}

static void walkenstatusbaralldraw(void)
{
    K_INT16 i;

    /* Black where the 360 pixel wide bar has no picture. */
    for(i=0;i<32;i++)
        memset(screenbuffer+screenbufferwidth*(statusbaryoffset+i), 0, 361);
    UploadPartialOverlay(0, statusbaryoffset, 361, 32);

    for(i=0;i<320;i+=32)
        walkenstatusbardraw(i, 31);
    oldlife = 0;
    i = life;
    walkendrawlife();
    oldlife = 63;
    life = i;
    walkendrawlife();
    for(i=1;i<=lifevests;i++)
        walkenstatusbardraw((i<<5), 32);
    for(i=1;i<=lightnings;i++)
        walkenstatusbardraw(305-((i<<5)-(i<<3)), 36);
    for(i=1;i<=firepowers[0];i++)
        walkenstatusbardraw(316-((i<<5)-(i<<3)), 39);
    if (keys[0] > 0)
        walkenstatusbardraw(0, 45);
}

/* ------------------------------------------------------------ rendering */

/* Called by picrot_view() for the monsters in sight. */
void walkenmonsters(K_UINT16 posxs, K_UINT16 posys, K_INT16 angs)
{
    K_INT16 i, j, xc, yc;

    for(i=0;i<mnum;i++)
    {
        xc = (mposx[i]>>10);
        yc = (mposy[i]>>10);
        if (tempbuf[(xc<<6)+yc] == 0)
            continue;
        if (mboard[xc][yc] == 66)
            checkobj(mposx[i],mposy[i],posxs,posys,angs,kenpos);
        if (mboard[xc][yc] == 68)
            checkobj(mposx[i],mposy[i],posxs,posys,angs,ballpos);
        if (mboard[xc][yc] == 38)
        {
            if (mshock[i] > 0)
                checkobj(mposx[i],mposy[i],posxs,posys,angs,61);
            else
                checkobj(mposx[i],mposy[i],posxs,posys,angs,38);
        }
        if (mboard[xc][yc] == 54)
        {
            if (mshock[i] > 0)
                checkobj(mposx[i],mposy[i],posxs,posys,angs,56);
            else
            {
                /* Andy has three views, picked from where he is heading. */
                j = 54;
                if ((posxs > mposx[i]) == (posys > mposy[i]))
                {
                    if ((mgolx[i] < moldx[i]) || (mgoly[i] > moldy[i]))
                        j = 53;
                    if ((mgolx[i] > moldx[i]) || (mgoly[i] < moldy[i]))
                        j = 55;
                    if ((posxs < mposx[i]) && (posys < mposy[i]))
                        j = 108 - j;
                }
                if ((posxs > mposx[i]) != (posys > mposy[i]))
                {
                    if ((mgolx[i] < moldx[i]) || (mgoly[i] < moldy[i]))
                        j = 53;
                    if ((mgolx[i] > moldx[i]) || (mgoly[i] > moldy[i]))
                        j = 55;
                    if ((posxs > mposx[i]) && (posys < mposy[i]))
                        j = 108 - j;
                }
                checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
            }
        }
    }
}

/* WALKEN.C stepped its animations once a frame; here they step at the same
   rate the v1.x port uses, whatever the frame rate. */
static void walkenanimate(void)
{
    static K_INT16 spareframes = 0;
    K_INT16 i;

    spareframes += clockspd;
    i = (spareframes/TICKS_PER_SPRITE_FRAME)%12;
    spareframes %= TICKS_PER_SPRITE_FRAME;
    while (i-- > 0)
    {
        kenpos++;
        if (kenpos == 68)
            kenpos = 65;
        ballpos++;
        if (ballpos == 72)
            ballpos = 68;
        fanpos++;
        if (fanpos == 52)
            fanpos = 49;
    }
}

static void explode(K_UINT16 x, K_UINT16 y, K_UINT16 walnum)
{
    if (explonum < 16)
        addexplosion(x, y, walnum);
}

/* ------------------------------------------------------------ the intro */

/*
 * The intro drew into two pages of video memory and moved the display
 * start and the line compare split between them.  Here the overlay stands in
 * for video memory: rows 0-239 are the page at offset 0 and rows 240-479 the
 * page at offset 21600.  The last row the display starts at and the split
 * are passed to showvram().
 */

#define VRAMROWS 480

static void vramclear(int row, int rows)
{
    int i;

    for(i=row;i<row+rows;i++)
        memset(screenbuffer+screenbufferwidth*i, 0, 360);
}

/* WALKEN.C's spridraw() into video memory: wall walnum scaled to siz by
   siz pixels with its top left corner at (x,y) on the page at `row`. */
static void vramsprite(int row, int x, int y, int siz, K_INT16 walnum)
{
    const unsigned char *pic = walseg[walnum-1];
    unsigned char *buf, c;
    int a, b, u, v;

    if (siz <= 0)
        return;
    for(a=0;a<siz;a++)
    {
        if ((x+a < 0) || (x+a >= 360))
            continue;
        u = ((a<<7)+64)/(siz<<1);
        for(b=0;b<siz;b++)
        {
            if ((y+b < 0) || (y+b >= 240))
                continue;
            v = ((b<<7)+64)/(siz<<1);
            c = pic[(u<<6)+v];
            if (c < 252) {
                buf = screenbuffer+screenbufferwidth*(row+y+b)+x+a;
                *buf = c;
            }
        }
    }
}

/* Show 240 rows from video memory row `start`, with the display going back
   to row 0 from screen row `split` down, as the VGA line compare did. */
static void showvram(int start, int split)
{
    if (split < 0) split = 0;
    if (split > 240) split = 240;
#ifndef PLATFORM_AMIGA
    /* Textures only hold what was last sent, and the background colour
       pulses, so send everything on show. */
    if (split > 0)
        UploadPartialOverlay(0, start, 360, split);
    if (split < 240)
        UploadPartialOverlay(0, 0, 360, 240-split);
#endif
    R_ClearScreen();
    visiblescreenyoffset = start;
    if (split > 0)
        ShowPartialOverlay(0, start, 360, split, 0);
    visiblescreenyoffset = -split;
    if (split < 240)
        ShowPartialOverlay(0, 0, 360, 240-split, 0);
    visiblescreenyoffset = 0;
    PL_SwapBuffers();
}

static void creditspage(int row, int who, K_INT16 walnum)
{
    int j, x = 20, y = 20;

    vramsprite(row, 256+x, 136+y, 64, walnum);
    for(j=1;j<WALKEN_NUMWALLS;j++)
        if (credits[j] == who)
        {
            vramsprite(row, x, y, 32, j);
            x += 32;
            if (x >= 320)
            {
                x -= 320;
                y += 32;
            }
        }
}

static void walkenintroduction(void)
{
    unsigned char grey[3];
    K_INT16 lasti, i, j, steps, n, elapsed, frac;
    int start, split, lc;

    fade(63);
    ingame = 0;
    settransferpalette();
    vramclear(0, VRAMROWS);
    loadmusic("INTRO");
    PL_LockTimer();
    clockspeed = 0;
    PL_UnlockTimer();
    totalclock = 0;
    frac = 0;
    musicon();
    ksay(SND_WELCOME);

    lasti = 0;
    i = 0;
    start = 0;
    split = 240;
    lc = 511;
    bstatus = 0;
    while ((newkeystatus(PLK_ESCAPE) == 0) &&
           (newkeystatus(PLK_SPACE) == 0) &&
           (newkeystatus(PLK_RETURN) == 0) &&
           (getkeydefstatlock(ACTION_MENU) == 0) &&
           (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
           (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
           (bstatus == 0) && (quitgame == 0))
    {
        PollInputs();
        if (moustat == 0)
            bstatus = readmouse(NULL, NULL);

        /* The original waited for each vertical retrace; at 60Hz that is
           four ticks a frame. */
        PL_LockTimer();
        while (clockspeed < 4) {
            PL_UnlockTimer();
            PL_Delay(10);
            PL_LockTimer();
        }
        elapsed = clockspeed;
        totalclock += clockspeed;
        clockspeed = 0;
        PL_UnlockTimer();
        if (totalclock > 5760)
            totalclock -= 5760;

        /* What happened once a frame then happens once every 4 ticks. */
        frac += elapsed;
        steps = frac>>2;
        frac &= 3;
        if (steps > 8)
            steps = 8;

        /* The background pulses. */
        grey[0] = grey[1] = grey[2] = (unsigned char)(((int)labs((totalclock%120)-60))>>3);
        R_SetOverlayPaletteRange(0, 1, grey);

        lasti = i;
        i = (K_INT16)(totalclock>>2);
        if ((i >= 0) && (i < 240))
        {
            if (lasti >= 240)
            {
                vramclear(0, VRAMROWS);
                start = 0;
                lc = 511;
            }
            vramsprite(0, 180-(i>>1), 120-(i>>1), i, 64);
        }
        if ((i >= 240) && (i < 480))
        {
            if (lasti < 240)
            {
                vramsprite(240, 80, 20, 200, 5);
                start = 0;
            }
        }
        if ((i >= 480) && (i < 720))
        {
            start += steps;
            if (start > 240)
                start = 240;
        }
        if ((i >= 720) && (i < 960))
        {
            if (lasti < 720)
            {
                start = 240;
                ksay(SND_BLOWUP);
                vramsprite(240, 80, 20, 200, 44);
            }
            for(n=0;n<steps;n++)
            {
                j = rand()&127;
                vramsprite(240, rand()%(360-j), rand()%(240-j), j, 44);
            }
        }
        if ((i >= 960) && (i < 1200))
        {
            if (lasti < 960)
            {
                vramclear(0, 240);
                creditspage(0, 1, 76);
                lc = 480;
            }
            lc -= 4*steps;
            if (lc <= 0)
                lc = 1;
        }
        if ((i >= 1200) && (i < 1440))
        {
            if (lasti < 1200)
            {
                vramclear(240, 240);
                start = 240;
                creditspage(240, 2, 77);
                lc = 1;
            }
            if (lc < 512)
                lc += 4*steps;
            if (lc >= 512)
                lc = 511;
        }
        split = (lc+1)>>1;
        showvram(start, split);
    }
    setnewkeystatus(PLK_ESCAPE, 0);
    setnewkeystatus(PLK_SPACE, 0);
    setnewkeystatus(PLK_RETURN, 0);
    clearkeydefstat(ACTION_MENU);
    clearkeydefstat(ACTION_MENU_CANCEL);
    clearkeydefstat(ACTION_MENU_SELECT1);
    clearkeydefstat(ACTION_MENU_SELECT2);
    clearkeydefstat(ACTION_MENU_SELECT3);
    musicoff();

    /* Set up a new game. */
    grey[0] = grey[1] = grey[2] = 0;
    R_SetOverlayPaletteRange(0, 1, grey);
    ingame = 1;
    settransferpalette();
    wipeoverlay(0, 0, 361, statusbaryoffset);
    R_ClearScreen();
    SetVisibleScreenOffset(0);

    dside = 240;
    halfheight = 120;
    boardnum = 0;
    walkenloadboard();
    sortcnt = 0;
    life = 63;
    death = 63;
    lifevests = 0;
    lightnings = 0;
    firepowers[0] = 0;
    fanpos = 49;
    kenpos = 66;
    ballpos = 68;
    rogermode = 0;
    hurtticks = 0;
    loadmusic("WALSNG00");
    musicon();
    PL_LockTimer();
    clockspeed = 0;
    PL_UnlockTimer();
    totalclock = 0;
    clockspd = 0;
    statusbar = 415;
    statusbargoal = statusbar;
    linecompare(statusbar);
    walkenstatusbaralldraw();
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
}

/* ------------------------------------------------------------ saved games */

/* SAVGAME0.DAT to SAVGAME7.DAT, laid out byte for byte as the DOS version
   wrote them, so its saved games load here and the other way round. */

static int opensaved(K_INT16 gamenum, int write)
{
    int fil;

    sprintf(filepathUpper, "%sSAVGAME%d.DAT", gameroot, gamenum);
    sprintf(filepath, "%ssavgame%d.dat", gameroot, gamenum);
    if (write) {
        unlink(filepath);
        return open(filepathUpper, O_BINARY|O_CREAT|O_WRONLY|O_TRUNC,
                    S_IWRITE|S_IREAD|S_IRGRP|S_IROTH);
    }
    if ((fil = open(filepathUpper, O_BINARY|O_RDONLY, 0)) == -1)
        fil = open(filepath, O_BINARY|O_RDONLY, 0);
    return fil;
}

static void walkensavegame(K_INT16 gamenum)
{
    unsigned char cells[4096], bytes[512];
    K_INT16 i, clk = clockspd;
    K_UINT32 nowlong = nownote;
    int fil;

    if ((fil = opensaved(gamenum, 1)) == -1)
        return;
    for(i=0;i<4096;i++)
        cells[i] = boardbyte(board[i>>6][i&63]);
    write(fil, cells, 4096);
    write(fil, &mboard[0][0], 4096);
    writeLE16(fil, &boardnum, 2);
    writeLE16(fil, &life, 2);
    writeLE16(fil, &death, 2);
    writeLE16(fil, &lifevests, 2);
    writeLE16(fil, &lightnings, 2);
    writeLE16(fil, &firepowers[0], 2);
    writeLE16(fil, &keys[0], 2);
    writeLE16(fil, &fanpos, 2);
    writeLE16(fil, &kenpos, 2);
    writeLE16(fil, &ballpos, 2);
    writeLE16(fil, &rogermode, 2);
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
    writeLE16(fil, &hvel, 2);
    writeLE16(fil, &oldposx, 2);
    writeLE16(fil, &oldposy, 2);
    writeLE16(fil, &youbulnum, 2);
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
    for(i=0;i<mnum;i++)
        bytes[i] = (unsigned char)mstat[i];
    write(fil, bytes, mnum);
    writeLE16(fil, &mshock[0], mnum<<1);
    write(fil, &mshot[0], mnum);
    writeLE32(fil, &totalclock, 4);
    PL_LockSound();
    writeLE32(fil, &musicstatus, 4);
    writeLE16(fil, &clk, 2);
    writeLE32(fil, &count, 4);
    writeLE32(fil, &countstop, 4);
    writeLE32(fil, &nowlong, 4);
    writeLE32(fil, &chanage[0], 18<<2);
    write(fil, &chanfreq[0], 18);
    PL_UnlockSound();
    close(fil);
}

static K_INT16 walkenloadgame(K_INT16 gamenum)
{
    unsigned char cells[4096], bytes[512];
    char ksmfile[16];
    K_INT16 i, clk;
    K_UINT32 music, cnt, cntstop, nowlong, age[18];
    unsigned char freq[18];
    int fil;

    if ((fil = opensaved(gamenum, 0)) == -1)
        return(-1);
    read(fil, cells, 4096);
    for(i=0;i<4096;i++)
        board[i>>6][i&63] = (cells[i]&127)|((cells[i]&128) ? WALKEN_SECRET : 0);
    read(fil, &mboard[0][0], 4096);
    readLE16(fil, &boardnum, 2);
    readLE16(fil, &life, 2);
    readLE16(fil, &death, 2);
    readLE16(fil, &lifevests, 2);
    readLE16(fil, &lightnings, 2);
    readLE16(fil, &firepowers[0], 2);
    readLE16(fil, &keys[0], 2);
    readLE16(fil, &fanpos, 2);
    readLE16(fil, &kenpos, 2);
    readLE16(fil, &ballpos, 2);
    readLE16(fil, &rogermode, 2);
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
    readLE16(fil, &hvel, 2);
    readLE16(fil, &oldposx, 2);
    readLE16(fil, &oldposy, 2);
    readLE16(fil, &youbulnum, 2);
    readLE16(fil, &bulnum, 2);
    if ((bulnum < 0) || (bulnum > 32))
        bulnum = 0;
    readLE16(fil, &bulang[0], bulnum<<1);
    readLE16(fil, &bulkind[0], bulnum<<1);
    readLE16(fil, &bulx[0], bulnum<<1);
    readLE16(fil, &buly[0], bulnum<<1);
    readLE32(fil, &bulstat[0], bulnum<<2);
    readLE32(fil, &lastbulshoot, 4);
    readLE16(fil, &mnum, 2);
    if (mnum > 512)
        mnum = 0;
    readLE16(fil, &mposx[0], mnum<<1);
    readLE16(fil, &mposy[0], mnum<<1);
    readLE16(fil, &mgolx[0], mnum<<1);
    readLE16(fil, &mgoly[0], mnum<<1);
    readLE16(fil, &moldx[0], mnum<<1);
    readLE16(fil, &moldy[0], mnum<<1);
    read(fil, bytes, mnum);
    for(i=0;i<mnum;i++)
        mstat[i] = bytes[i];
    readLE16(fil, &mshock[0], mnum<<1);
    read(fil, &mshot[0], mnum);
    readLE32(fil, &totalclock, 4);
    readLE32(fil, &music, 4);
    readLE16(fil, &clk, 2);
    readLE32(fil, &cnt, 4);
    readLE32(fil, &cntstop, 4);
    readLE32(fil, &nowlong, 4);
    readLE32(fil, &age[0], 18<<2);
    read(fil, freq, 18);
    close(fil);

    /* Pick the song up where it was. */
    musicoff();
    songname(ksmfile, boardnum);
    loadmusic(ksmfile);
    musicon();
    if (music != 0)
    {
        PL_LockSound();
        if (nowlong < numnotes)
        {
            count = cnt;
            countstop = cntstop;
            nownote = (K_UINT16)nowlong;
            memcpy(chanage, age, sizeof(age));
            memcpy(chanfreq, freq, sizeof(freq));
        }
        PL_UnlockSound();
    }

    updatemaptexture();
    explonum = 0;
    mxvel = 0;
    myvel = 0;
    hurtticks = 0;

    /* A game saved in 320x200 mode had its status bar 80 lines higher. */
    if (statusbargoal < 400)
    {
        statusbar += 80;
        statusbargoal += 80;
    }
    linecompare(statusbar);
    walkenstatusbaralldraw();
    return(0);
}

/* Show the LOAD or SAVE picture and wait for a slot, 1 to 8.  Returns the
   slot, or -1 if the player backed out. */
static K_INT16 pickslot(K_INT16 walnum)
{
    K_INT16 slot = -1;
#ifdef __SWITCH__
    K_UINT32 switchKeyPressed;
#endif

    picrot(posx,posy,posz,ang);
    spridraw((int)180-64,(int)halfheight-64,(int)128<<2,walnum);
    PL_SwapBuffers();
    while ((slot < 0) && (newkeystatus(PLK_ESCAPE) == 0) &&
           (newkeystatus(PLK_SPACE) == 0) &&
           (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) && (quitgame == 0))
    {
        PollInputs();
#ifdef __SWITCH__
        padUpdate(&pad);
        switchKeyPressed = padGetButtonsDown(&pad);
        if (switchKeyPressed & HidNpadButton_L) slot = 0;
        if (switchKeyPressed & HidNpadButton_ZL) slot = 1;
        if (switchKeyPressed & HidNpadButton_R) slot = 2;
        if (switchKeyPressed & HidNpadButton_ZR) slot = 3;
#endif
        if (newkeystatus(PLK_1)) slot = 0, setnewkeystatus(PLK_1, 0);
        if (newkeystatus(PLK_2)) slot = 1, setnewkeystatus(PLK_2, 0);
        if (newkeystatus(PLK_3)) slot = 2, setnewkeystatus(PLK_3, 0);
        if (newkeystatus(PLK_4)) slot = 3, setnewkeystatus(PLK_4, 0);
        if (newkeystatus(PLK_5)) slot = 4, setnewkeystatus(PLK_5, 0);
        if (newkeystatus(PLK_6)) slot = 5, setnewkeystatus(PLK_6, 0);
        if (newkeystatus(PLK_7)) slot = 6, setnewkeystatus(PLK_7, 0);
        if (newkeystatus(PLK_8)) slot = 7, setnewkeystatus(PLK_8, 0);
        PL_Delay(10);
    }
    /* Escape backs out here; it must not go on to end the game. */
    setnewkeystatus(PLK_ESCAPE, 0);
    setnewkeystatus(PLK_SPACE, 0);
    clearkeydefstat(ACTION_MENU);
    return slot;
}

/* ------------------------------------------------------------ the game */

/* `continuous` is for damage taken every frame something touches you. */
static void hurt(K_INT16 damage, K_INT16 snd, int continuous)
{
    life -= damage;
    if (life <= 0)
    {
        life = 0;
        walkendrawlife();
        death = 62;
        angvel = (rand()&32)-16;
        ksay(SND_DEATH);
        musicoff();
    }
    else
    {
        walkendrawlife();
        angvel = (rand()&32)-16;
        if (continuous)
            sayonce(snd);
        else
            ksay(snd);
    }
}

static void removebullet(K_INT16 i)
{
    bulnum--;
    if (bulkind[i] == 1)
        youbulnum--;
    bulx[i] = bulx[bulnum];
    buly[i] = buly[bulnum];
    bulang[i] = bulang[bulnum];
    bulstat[i] = bulstat[bulnum];
    bulkind[i] = bulkind[bulnum];
}

static void removemonster(K_INT16 k)
{
    mboard[moldx[k]>>10][moldy[k]>>10] = 0;
    mboard[mgolx[k]>>10][mgoly[k]>>10] = 0;
    mnum--;
    moldx[k] = moldx[mnum];
    moldy[k] = moldy[mnum];
    mposx[k] = mposx[mnum];
    mposy[k] = mposy[mnum];
    mgolx[k] = mgolx[mnum];
    mgoly[k] = mgoly[mnum];
    mstat[k] = mstat[mnum];
    mshot[k] = mshot[mnum];
    mshock[k] = mshock[mnum];
}

/* A cheat key: both shifts plus a letter, once per press. */
static int cheatkey(int cheatkeysdown, int scan)
{
    static unsigned char held[256];
    int now = cheatkeysdown && (keystatus[scan] > 0);
    int fire = now && !held[scan];

    held[scan] = (unsigned char)now;
    return fire;
}

void walkenmain(void)
{
    char ksmfile[16];
    K_INT16 i, j, k, m, x, y, contact;
    int cheatvest, cheatlightning, cheatfire, cheatkey_, cheatlife, cheatboard;
    K_UINT16 l, newx, newy, plcx, plcy;
    K_INT32 tanz;
    int cheatkeysdown, fire, unlock;

    clockspd = 0;
    musicoff();
    SetVisibleScreenOffset(0);
    walkenintroduction();

    while ((getkeydefstat(ACTION_MENU) == 0) && (quitgame == 0))
    {
        PollInputs();

        /* WALKEN.C's cheats needed no password: both shift keys and a
           letter. */
        cheatkeysdown = (keystatus[42] > 0) && (keystatus[54] > 0);

        if (death < 63)
        {
            fade(death);
            posz+=2;
            if (posz > 64)
                posz = 64;
            if (angvel < 0)
                angvel -= 20;
            if (angvel > 0)
                angvel += 20;
            ang = (ang+angvel)&2047;
            death -= 2;
            if (death <= 0)
            {
                death = 0;
                fade(63);
                if (lifevests > 0)
                {
                    death = 63;
                    life = 63;
                    walkenstatusbardraw((lifevests<<5),31);
                    walkendrawlife();
                    lifevests--;
                    musicon();
                    posz = 32;
                    posx = startx;
                    posy = starty;
                    ang = startang;
                    angvel = 0;
                    vel = 0;
                    mxvel = 0;
                    myvel = 0;
                    hvel = 0;
                }
                else
                    walkenintroduction();
            }
        }

        for(i=0;i<explonum;i++)
            if (totalclock > (K_INT32)explotime[i])
            {
                explonum--;
                explotime[i] = explotime[explonum];
                explox[i] = explox[explonum];
                exploy[i] = exploy[explonum];
                explostat[i] = explostat[explonum];
                i--;
            }

        for(i=0;i<bulnum;i++)
        {
            if (bulkind[i] == 1)
                checkobj(bulx[i],buly[i],posx,posy,ang,39);
            if (bulkind[i] == 2)
                checkobj(bulx[i],buly[i],posx,posy,ang,57);
            if (bulkind[i] == 1)
                if (bulstat[i]+(120+(lightnings<<5)) < (K_UINT32)totalclock)
                    removebullet(i);
            if (bulkind[i] == 2)
                if (bulstat[i]+240 < (K_UINT32)totalclock)
                    removebullet(i);
        }
        walkenanimate();
        picrot(posx,posy,posz,ang);
        sortcnt = 0;

        PL_LockSound();
        PL_LockTimer();

        /* Speed cap at 2 ticks/frame (about 120 fps), as for v1.x. */
        if ((musicstatus == 1) && (clockspeed >= 0) && (clockspeed < 2)) {
            PL_UnlockSound();
            while(clockspeed<2) {
                PL_Delay(0); /* Give other threads a chance. */
                PollInputs();
                updateclock();
            }
            PL_LockSound();
        }

        if (musicstatus!=1)
            PL_Delay(10); /* Just to prevent insane speeds... */

        clockspd=clockspeed;
        if (clockspd>48) clockspd=48; /* Prevent total insanity if game
                                         is suspended. */
        clockspeed=0;
        PL_UnlockTimer();
        PL_UnlockSound();

        mousx = mousy = 0;
        bstatus = 0;
        if ((moustat == 0) && (death == 63))
        {
            bstatus = readmouse(&mousx, &mousy);
            if (!mouseverticalmovement)
                mousy = 0;
        }

        if (newkeystatus(PLK_r) > 0)
            rogermode = rogermode ^ 1;

        /* Show or hide the status bar. */
        if (statusbar == statusbargoal)
        {
            if (getkeydefstat(ACTION_STATUS) > 0)
            {
                if (lastbarchange == 0)
                    statusbargoal = (479+415)-statusbar;
                lastbarchange = 1;
            }
            else
                lastbarchange = 0;
        }
        else
        {
            if (statusbargoal < statusbar)
            {
                statusbar -= (clockspd>>1);
                if (statusbar <= statusbargoal)
                    statusbar = statusbargoal;
            }
            else
            {
                statusbar += (clockspd>>1);
                if (statusbar >= statusbargoal)
                    statusbar = statusbargoal;
            }
            linecompare(statusbar);
        }

        fire = (getkeydefstat(ACTION_FIRE) > 0) || ((bstatus&1) > 0);
        if (fire && (death == 63))
        {
            if ((youbulnum < firepowers[0]) && (lastbulshoot+240-(lightnings<<5) < (K_UINT32)totalclock))
            {
                bulx[bulnum] = posx;
                buly[bulnum] = posy;
                bulang[bulnum] = ang;
                bulstat[bulnum] = totalclock;
                bulkind[bulnum] = 1;
                lastbulshoot = totalclock;
                youbulnum++;
                bulnum++;
                ksay(SND_SHOOT);
            }
            if ((firepowers[0] == 0) && (lastshoot == 0))
                ksay(SND_NOTNOW);
        }
        lastshoot = fire;

        for(i=0;i<bulnum;i++)
        {
            x = (int)((clockspd*sintable[(bulang[i]+512)&2047])>>13);
            y = (int)((clockspd*sintable[bulang[i]])>>13);
            for(m=0;m<4;m++)
                if (bulstat[i] > 0)
                {
                    bulx[i] += x;
                    buly[i] += y;
                    if ((bulkind[i] == 2) && (death == 63) && (m < 4))
                    {
                        l = labs((long)bulx[i]-(long)posx)+labs((long)buly[i]-(long)posy);
                        if (l < 768)
                        {
                            hurt(1+((768-(int)l)>>6), SND_OUCH, 0);
                            removebullet(i);
                            m = 4;
                        }
                    }
                    j = mboard[bulx[i]>>10][buly[i]>>10];
                    if ((j > 0) && (j != 68) && (bulkind[i] == 1) && (m < 4))
                    {
                        l = 0;
                        for(k=0;k<mnum;k++)
                            if (labs((long)bulx[i]-(long)mposx[k])+labs((long)buly[i]-(long)mposy[k]) < 768)
                            {
                                mshot[k]--;
                                if (mshot[k] > 0)
                                {
                                    if (mstat[k] == 54)
                                        mshock[k] += 30;
                                    else
                                        mshock[k] += 60;
                                    l |= 2;
                                }
                                if (mshot[k] == 0)
                                {
                                    explode(mposx[k],mposy[k],44);
                                    removemonster(k);
                                    l |= 1;
                                }
                            }
                        if (l > 0)
                        {
                            removebullet(i);
                            m = 4;
                            if (death == 63)
                            {
                                if ((l&1) > 0)
                                    ksay(SND_BLOWUP);
                                if ((l&2) > 0)
                                    ksay(SND_HITANDY);
                            }
                        }
                    }
                    j = board[bulx[i]>>10][buly[i]>>10];
                    if (bmpkind[j&1023] != 0)
                    {
                        if ((bulkind[i] == 1) && (j == 5) && (m < 4) && (death == 63))
                        {
                            /* Shooting Ken's portrait hurts. */
                            hurt(1, SND_OUCH, 0);
                            removebullet(i);
                            m = 4;
                        }
                        if ((bulkind[i] == 1) && (j == 52) && (death == 63))
                            ksay(SND_BULLSEYE);
                        /* WALKEN.C looked the borders up one wall out; so
                           does this. */
                        if (issolid(j) && (j != 40) && (m < 4))
                            if ((bmpkind[j&1023] == 1) || ((((bulx[i]&1023)<<2) > wlborder[j&127]) && (((bulx[i]&1023)<<2) < wrborder[j&127]) && (((buly[i]&1023)<<2) > wlborder[j&127]) && (((buly[i]&1023)<<2) < wrborder[j&127])))
                            {
                                removebullet(i);
                                m = 4;
                            }
                        if ((j == 40) && (m < 4))
                        {
                            /* The net bounces bullets back. */
                            bulang[i] = ((bulang[i]+1024)&2047);
                            while ((board[bulx[i]>>10][buly[i]>>10]&1023) == 40)
                            {
                                bulx[i] -= x;
                                buly[i] -= y;
                            }
                            m = 4;
                            if (bulkind[i] == 1)
                            {
                                if (death == 63)
                                    ksay(SND_BOUNCE);
                                bulstat[i] = totalclock;
                            }
                        }
                    }
                }
        }

        /* Contact damage is charged per tick (the original charged
           1+clockspeed/8 a frame, which depends on the frame rate). */
        hurtticks += clockspd;
        contact = hurtticks>>2;
        hurtticks &= 3;

        for(i=0;i<mnum;i++)
            if (labs((long)mposx[i]-(long)posx)+labs((long)mposy[i]-(long)posy) < 16384)
            {
                j = 0;
                if (mstat[i] == 66)                 //Ken speed
                    j = (clockspd<<2)+(clockspd<<1);
                if (mstat[i] == 38)                 //Green monster speed
                    j = (clockspd<<3)+(clockspd<<1);
                if (mstat[i] == 54)                 //Andy speed
                    j = (clockspd<<3)+(clockspd<<2);
                if (mstat[i] == 68)                 //Ball speed
                    j = (clockspd<<3);

                if (mshock[i] > 0)
                {
                    mshock[i] -= clockspd;
                    if (mshock[i] < 0)
                        mshock[i] = 0;
                }
                else
                {
                    if (mgolx[i] > mposx[i])
                    {
                        mposx[i] += j;
                        if (mposx[i] > mgolx[i])
                            mposx[i] = mgolx[i];
                    }
                    if (mgolx[i] < mposx[i])
                    {
                        mposx[i] -= j;
                        if (mposx[i] < mgolx[i])
                            mposx[i] = mgolx[i];
                    }
                    if (mgoly[i] > mposy[i])
                    {
                        mposy[i] += j;
                        if (mposy[i] > mgoly[i])
                            mposy[i] = mgoly[i];
                    }
                    if (mgoly[i] < mposy[i])
                    {
                        mposy[i] -= j;
                        if (mposy[i] < mgoly[i])
                            mposy[i] = mgoly[i];
                    }
                }
                if ((labs((long)posx-(long)mposx[i])+labs((long)posy-(long)mposy[i]) < 768) &&
                    (death == 63) && (mshock[i] == 0) && (contact > 0))
                    hurt(contact, SND_OUCH, 1);
                if ((mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]) && (mshock[i] == 0))
                {
                    if (board[mposx[i]>>10][mposy[i]>>10] == 73)
                    {
                        /* Into the hole. */
                        if (mstat[i] == 68)
                            explode(mposx[i],mposy[i],72);
                        removemonster(i);
                        ksay(SND_FALL);
                    }
                    else
                    {
                        x = (mposx[i]>>10);
                        y = (mposy[i]>>10);
                        if ((rand()&1) == 0)
                        {
                            if ((posx < mposx[i]) && (!issolid(B(x-1,y))) && (B(x-1,y) != 23) && ((B(x-1,y)&WALKEN_SECRET) == 0) && (mboard[(x-1)&63][y] == 0))
                                mgolx[i] = mposx[i]-1024;
                            if ((posx > mposx[i]) && (!issolid(B(x+1,y))) && (B(x+1,y) != 23) && ((B(x+1,y)&WALKEN_SECRET) == 0) && (mboard[(x+1)&63][y] == 0))
                                mgolx[i] = mposx[i]+1024;
                        }
                        else
                        {
                            if ((posy < mposy[i]) && (!issolid(B(x,y-1))) && (B(x,y-1) != 23) && ((B(x,y-1)&WALKEN_SECRET) == 0) && (mboard[x][(y-1)&63] == 0))
                                mgoly[i] = mposy[i]-1024;
                            if ((posy > mposy[i]) && (!issolid(B(x,y+1))) && (B(x,y+1) != 23) && ((B(x,y+1)&WALKEN_SECRET) == 0) && (mboard[x][(y+1)&63] == 0))
                                mgoly[i] = mposy[i]+1024;
                        }
                        mboard[moldx[i]>>10][moldy[i]>>10] = 0;
                        mboard[mposx[i]>>10][mposy[i]>>10] = (unsigned char)mstat[i];
                        mboard[mgolx[i]>>10][mgoly[i]>>10] = (unsigned char)mstat[i];
                        moldx[i] = mposx[i];
                        moldy[i] = mposy[i];
                    }
                }
                j = rand()&2047;
                if (mstat[i] == 38)
                    j &= 1023;
                if (mstat[i] == 54)
                    j &= 511;
                if ((mstat[i] != 68) && (j < clockspd) && (bulnum < 32) && (mshock[i] == 0))
                {
                    /* Aim roughly at the player. */
                    bulx[bulnum] = mposx[i];
                    buly[bulnum] = mposy[i];
                    k = 512;
                    if (mposx[i] != posx)
                    {
                        tanz = (K_INT32)((K_UINT32)((((K_INT32)mposy[i]-(K_INT32)posy)<<12)/((K_INT32)mposx[i]-(K_INT32)posx))<<4);
                        if (tanz < 0)
                            k = 768;
                        else
                            k = 256;
                        for (m=128;m>0;m>>=1)
                        {
                            if (tantable[k] < tanz)
                                k += m;
                            else
                                k -= m;
                        }
                    }
                    if (mposy[i] > posy)
                        k += 1024;
                    bulang[bulnum] = ((k+2016+(rand()&63))&2047);
                    bulstat[bulnum] = totalclock;
                    if (mstat[i] == 54)
                        bulstat[bulnum] += 240;
                    bulkind[bulnum] = 2;
                    bulnum++;
                }
            }

        if (death == 63)
        {
            if (getkeydefstat(ACTION_LEFT) && (angvel > -40))
                angvel -= 14;
            if (getkeydefstat(ACTION_RIGHT) && (angvel < 40))
                angvel += 14;
        }
        if (angvel < 0)
        {
            angvel += 8;
            if (angvel > 0)
                angvel = 0;
        }
        if (angvel > 0)
        {
            angvel -= 8;
            if (angvel < 0)
                angvel = 0;
        }
        if (mousx != 0)
        {
            mousx += (mousx>>1);
            if (mousx < -24)
                mousx = -24;
            if (mousx > 24)
                mousx = 24;
            mxvel += mousx;
            if (mxvel+angvel < -40)
                mxvel = -40-angvel;
            if (mxvel+angvel > 40)
                mxvel = 40-angvel;
        }
        if (mxvel < 0)
        {
            mxvel += 8;
            if (mxvel > 0)
                mxvel = 0;
        }
        if (mxvel > 0)
        {
            mxvel -= 8;
            if (mxvel < 0)
                mxvel = 0;
        }
        if ((rogermode > 0) && (death == 63) && (getkeydefstat(ACTION_LEFT) == 0) && (getkeydefstat(ACTION_RIGHT) == 0))
            angvel = 0;
        ang = (ang+2048+(((angvel+mxvel)*clockspd)>>3))&2047;

        /* A and Z fly up and down. */
        if ((getkeydefstat(ACTION_STANDHIGH) > 0) && (hvel > -5) && (death == 63))
            hvel-=2;
        if ((getkeydefstat(ACTION_STANDLOW) > 0) && (hvel < 5) && (death == 63))
            hvel+=2;
        if (hvel < 0)
            hvel++;
        if (hvel > 0)
            hvel--;
        posz += ((hvel*clockspd)>>3);
        if (posz < 8)
        {
            posz = 8;
            hvel = 0;
        }
        if (posz > 56)
        {
            posz = 56;
            hvel = 0;
        }

        /* Keys open every lock next to you, and are never used up. */
        unlock = (getkeydefstat(ACTION_USE) > 0) || ((bstatus&2) > 0);
        if (unlock && (death == 63))
        {
            if (keys[0] > 0)
            {
                x = (posx>>10);
                y = (posy>>10);
                if (B(x-1,y) == 21)
                {
                    B(x-1,y) = 0;
                    ksay(SND_UNLOCK);
                }
                if (B(x+1,y) == 21)
                {
                    B(x+1,y) = 0;
                    ksay(SND_UNLOCK);
                }
                if (B(x,y-1) == 21)
                {
                    B(x,y-1) = 0;
                    ksay(SND_UNLOCK);
                }
                if (B(x,y+1) == 21)
                {
                    B(x,y+1) = 0;
                    ksay(SND_UNLOCK);
                }
            }
            if ((keys[0] == 0) && (lastunlock == 0))
                ksay(SND_NOTNOW);
        }
        lastunlock = unlock;

        maxvel = 150;
        if (getkeydefstat(ACTION_RUN) > 0)
            maxvel = 300;
        if (death == 63)
        {
            if (getkeydefstat(ACTION_FORWARD))
            {
                vel += 40;
                if (vel > maxvel)
                    vel = maxvel;
            }
            if (getkeydefstat(ACTION_BACKWARD))
            {
                vel -= 40;
                if (vel < -maxvel)
                    vel = -maxvel;
            }
        }
        if (vel < 0)
        {
            vel += 25;
            if (vel > 0)
                vel = 0;
        }
        if (vel > 0)
        {
            vel -= 25;
            if (vel < 0)
                vel = 0;
        }
        if (myvel < 0)
        {
            myvel += 25;
            if (myvel > 0)
                myvel = 0;
        }
        if (myvel > 0)
        {
            myvel -= 25;
            if (myvel < 0)
                myvel = 0;
        }
        if (mousy != 0)
        {
            myvel = -(mousy<<3);
            if (myvel < -300)
                myvel = -300;
            if (myvel > 300)
                myvel = 300;
            if (myvel+vel < -300)
                myvel = -300-vel;
            if (myvel+vel > 300)
                myvel = 300-vel;
        }

        /* Move and slide along walls.  The arithmetic is 16 bit unsigned,
           as it was in the original. */
        if ((vel+myvel) != 0)
        {
            oldposx = posx;
            oldposy = posy;
            posx += (int)(((long)(vel+myvel)*clockspd*sintable[(ang+512)&2047])>>19);
            posy += (int)(((long)(vel+myvel)*clockspd*sintable[ang])>>19);
            newx = posx, newy = posy;
            if ((W16(newx-128)&0xfc00) < (W16(oldposx-128)&0xfc00))
            {
                plcx = (W16(oldposx-128)&0xfc00)+128;
                plcy = W16(oldposy+(mul32((K_INT32)oldposx-(K_INT32)plcx,TAN(ang))>>16));
                if (issolid(B((plcx>>10)-1,W16(plcy-128)>>10)))
                    posx = (W16(oldposx-128)&0xfc00) + 128;
                if (issolid(B((plcx>>10)-1,W16(plcy+128)>>10)))
                    posx = (W16(oldposx-128)&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy-128)>>10)))
                    posy = (oldposy&0xfc00) + 128;
                if (issolid(B(W16(posx+128)>>10,W16(posy-128)>>10)))
                    posy = (oldposy&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy+128)>>10)))
                    posy = (oldposy&0xfc00) + 895;
                if (issolid(B(W16(posx+128)>>10,W16(posy+128)>>10)))
                    posy = (oldposy&0xfc00) + 895;
            }
            if ((W16(newx+128)&0xfc00) > (W16(oldposx+128)&0xfc00))
            {
                plcx = (W16(oldposx+128)&0xfc00)+895;
                plcy = W16(oldposy+(mul32((K_INT32)oldposx-(K_INT32)plcx,TAN(ang))>>16));
                if (issolid(B((plcx>>10)+1,W16(plcy-128)>>10)))
                    posx = (W16(oldposx+128)&0xfc00) + 895;
                if (issolid(B((plcx>>10)+1,W16(plcy+128)>>10)))
                    posx = (W16(oldposx+128)&0xfc00) + 895;
                if (issolid(B(W16(posx-128)>>10,W16(posy-128)>>10)))
                    posy = (oldposy&0xfc00) + 128;
                if (issolid(B(W16(posx+128)>>10,W16(posy-128)>>10)))
                    posy = (oldposy&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy+128)>>10)))
                    posy = (oldposy&0xfc00) + 895;
                if (issolid(B(W16(posx+128)>>10,W16(posy+128)>>10)))
                    posy = (oldposy&0xfc00) + 895;
            }
            if ((W16(newy-128)&0xfc00) < (W16(oldposy-128)&0xfc00))
            {
                plcy = (W16(oldposy-128)&0xfc00)+128;
                plcx = W16(oldposx+(mul32((K_INT32)oldposy-(K_INT32)plcy,TAN(2560-ang))>>16));
                if (issolid(B(W16(plcx-128)>>10,(plcy>>10)-1)))
                    posy = (W16(oldposy-128)&0xfc00) + 128;
                if (issolid(B(W16(plcx+128)>>10,(plcy>>10)-1)))
                    posy = (W16(oldposy-128)&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy-128)>>10)))
                    posx = (oldposx&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy+128)>>10)))
                    posx = (oldposx&0xfc00) + 128;
                if (issolid(B(W16(posx+128)>>10,W16(posy-128)>>10)))
                    posx = (oldposx&0xfc00) + 895;
                if (issolid(B(W16(posx+128)>>10,W16(posy+128)>>10)))
                    posx = (oldposx&0xfc00) + 895;
            }
            if ((W16(newy+128)&0xfc00) > (W16(oldposy+128)&0xfc00))
            {
                plcy = (W16(oldposy+128)&0xfc00)+895;
                plcx = W16(oldposx+(mul32((K_INT32)oldposy-(K_INT32)plcy,TAN(2560-ang))>>16));
                if (issolid(B(W16(plcx-128)>>10,(plcy>>10)+1)))
                    posy = (W16(oldposy+128)&0xfc00) + 895;
                if (issolid(B(W16(plcx+128)>>10,(plcy>>10)+1)))
                    posy = (W16(oldposy+128)&0xfc00) + 895;
                if (issolid(B(W16(posx-128)>>10,W16(posy-128)>>10)))
                    posx = (oldposx&0xfc00) + 128;
                if (issolid(B(W16(posx-128)>>10,W16(posy+128)>>10)))
                    posx = (oldposx&0xfc00) + 128;
                if (issolid(B(W16(posx+128)>>10,W16(posy-128)>>10)))
                    posx = (oldposx&0xfc00) + 895;
                if (issolid(B(W16(posx+128)>>10,W16(posy+128)>>10)))
                    posx = (oldposx&0xfc00) + 895;
            }
        }

        /* Whatever is in the cell you are standing in. */
        cheatvest = cheatkey(cheatkeysdown, 18);        /* E */
        cheatlightning = cheatkey(cheatkeysdown, 38);   /* L */
        cheatfire = cheatkey(cheatkeysdown, 33);        /* F */
        cheatkey_ = cheatkey(cheatkeysdown, 37);        /* K */
        cheatlife = cheatkey(cheatkeysdown, 31);        /* S */
        cheatboard = cheatkey(cheatkeysdown, 48);       /* B */
        x = (posx>>10);
        y = (posy>>10);
        i = (board[x][y]&1023);
        if ((i == 32) || cheatvest)
        {
            lifevests++;
            if (lifevests > 4)
                lifevests = 4;
            if (i == 32)
                board[x][y] = 0;
            if (death == 63)
                ksay(SND_GETSTUFF);
            walkenstatusbardraw((lifevests<<5),32);
        }
        if ((i == 36) || cheatlightning)
        {
            lightnings++;
            if (lightnings > 6)
                lightnings = 6;
            if (i == 36)
                board[x][y] = 0;
            if (death == 63)
                ksay(SND_GETSTUFF);
            walkenstatusbardraw(305-((lightnings<<5)-(lightnings<<3)),36);
        }
        if ((i == 39) || cheatfire)
        {
            firepowers[0]++;
            if (firepowers[0] > 6)
                firepowers[0] = 6;
            if (i == 39)
                board[x][y] = 0;
            if (death == 63)
                ksay(SND_GETSTUFF);
            walkenstatusbardraw(316-((firepowers[0]<<5)-(firepowers[0]<<3)),39);
        }
        if ((i == 45) || cheatkey_)
        {
            keys[0]++;
            if (i == 45)
                board[x][y] = 0;
            if (death == 63)
                ksay(SND_GETSTUFF);
            walkenstatusbardraw(0,45);
        }
        if ((i == 46) || (i == 59) || (i == 60) || cheatlife)
        {
            life += 5;
            if (i == 46)
                life += 5;
            if (i == 60)
                life += 15;
            if (life > 63)
                life = 63;
            walkendrawlife();
            if ((i == 46) || (i == 59) || (i == 60))
                board[x][y] = 0;
            if (death == 63)
                ksay(SND_GETSTUFF);
        }
        if ((i == 63) || cheatboard)
        {
            /* The stairs lead to the next board. */
            ksay(SND_CONGRATS);
            musicoff();
            if (boardnum < numboards-1)
            {
                boardnum++;
                walkenstatusbardraw(0,31);
                walkenloadboard();
                songname(ksmfile, boardnum);
                loadmusic(ksmfile);
                musicon();
                PL_LockTimer();
                clockspeed = 0;
                PL_UnlockTimer();
                clockspd = 0;
            }
            else
                break;          /* that was the last board */
        }
        if ((i == 49) && (death == 63) && (contact > 0))
            hurt(contact, SND_HITFAN, 1);
        if ((i == 73) && (abs(512-(posx&1023))+abs(512-(posy&1023)) < 384) && (death == 63))
        {
            life = 0;
            walkendrawlife();
            death = 62;
            angvel = 0;
            ksay(SND_FALL);
            musicoff();
        }

        if (!cheatkeysdown)
        {
#ifdef __SWITCH__
            K_UINT32 switchKeyPressed;

            padUpdate(&pad);
            switchKeyPressed = padGetButtonsDown(&pad);
            if ((switchKeyPressed & HidNpadButton_Plus) || (getkeydefstat(ACTION_OLD_SAVE) > 0))
#else
            if (getkeydefstat(ACTION_OLD_SAVE) > 0)
#endif
            {
                if ((j = pickslot(79)) >= 0)
                    walkensavegame(j);
                clearkeydefstat(ACTION_OLD_SAVE);
                clockspd = 0;
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                PL_LockTimer();
                clockspeed = 0;
                PL_UnlockTimer();
            }
#ifdef __SWITCH__
            if ((switchKeyPressed & HidNpadButton_Minus) || (getkeydefstat(ACTION_OLD_LOAD) > 0))
#else
            if (getkeydefstat(ACTION_OLD_LOAD) > 0)
#endif
            {
                if ((j = pickslot(78)) >= 0)
                    walkenloadgame(j);
                clearkeydefstat(ACTION_OLD_LOAD);
                clockspd = 0;
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                PL_LockTimer();
                clockspeed = 0;
                PL_UnlockTimer();
            }
        }
        if (keystatus[88] > 0)
            screencapture();

#ifdef __SWITCH__ /* The home button quits on Switch. */
        clearkeydefstat(ACTION_MENU);
#endif
        totalclock += clockspd;
        PL_SwapBuffers();
    }
    musicoff();
}
