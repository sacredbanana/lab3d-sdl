#include "buildconfig.h"

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#endif

#include <stdint.h>
#include <stdlib.h>

#if !defined(KEN_64BIT)
#define INT_TYPE int
#else
#define INT_TYPE int64_t
#endif

#ifndef WIN32
#ifdef _WIN32
#define WIN32
#endif
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#ifdef _MSC_VER
#include <windows.h>
#else
/* No point in including windows.h just to get this. Saves a lot of bloat. */
#include <windef.h>
#endif
#endif

#include "SDL.h"

#ifdef __SWITCH__
#include "glad.h"
#include <switch.h>
#else
#include <SDL_opengl.h>
#ifdef __APPLE__
#include <OpenGL/glu.h>
#else
#include <GL/glu.h>
#endif
#endif

#ifndef GL_BGR
/* MSVC compatibility hack (some versions have out-of-date OpenGL headers). */
#define GL_BGR GL_BGR_EXT
#endif

#ifdef WIN32
#include "mmsystem.h"
#define S_IRGRP 0
#define S_IROTH 0
#else
#ifdef USE_OSS
#include "linux/soundcard.h"
#endif
#include <sys/ioctl.h>
/* WIN32 needs this flag to avoid data corruption, POSIX doesn't have it at
   all. So we define it ourselves on non-Windows systems. Yes, I know this is
   messy. */
#define O_BINARY 0
#endif

#ifdef DEBUG_BUFFERS
#ifdef MAIN
void SDL_GL_SwapBuffersDebug() {
    SDL_GL_SwapBuffers();
    glClearColor(255, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
}
#endif
#define SDL_GL_SwapBuffers SDL_GL_SwapBuffersDebug
#endif

/* Assume that SDL has fixed data types... */

typedef Uint32 K_UINT32;
typedef Uint16 K_UINT16;
typedef Sint32 K_INT32;
typedef Sint16 K_INT16;

#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <time.h>
#ifndef _MSC_VER
#include <unistd.h>
#endif

#include "demo.h"

/* Some (obsolete) port numbers... */

#define dataport 0x330
#define statport 0x331

/* Some other constants... */

#define numwalls 448
#define numoptions 4

#define initialwalls 0
#define gifbuflen 4096

/* Some texture names... */

//SORTWALLSTART
#define fountain 12        //l = 3
#define safe 16            //l = 3
#define clock 20
#define introstoryback 32
#define kenface 74
#define kenfaceouch 75
#define andy 76
#define andygone 77
#define map (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 80 : 78)
#define invisible (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 18 : 79)
#define goldlock 80
#define silverlock 81
#define doorside1 84
#define doorside2 84
#define doorside5 85
#define doorside3 86
#define doorside4 87
#define door1 88           //l = 6
#define door2 94           //l = 6
#define door3 100           //l = 8
#define door4 108          //l = 7
#define door5 115          //l = 8
#define bricksfull 130
#define brickshalf 131
#define slotto 134
#define slotup 135
#define slotinfo 137
#define soda 138
#define stairs 141
#define exitsign 143       //l = 2
#define tentacles 145      //l = 2
#define stairtop 151
#define minicolumn 152     //l = 4
#define tablecandle 163    //l = 2
#define target 166
#define net 167
#define fan 168            //l = 3
#define warp 170           //l = 2
#define coin 171
#define diamond 172
#define diamonds3 173
#define fries 174
#define emptyfries 175
#define meal 176
#define emptymeal 177
#define firstaid 178
#define emptyfirst 179
#define purple 180
#define emptypurple 181
#define green 182
#define emptygreen 183
#define gray 184
#define blue 185
#define grayblue 186
#define emptycoat 187
#define all4coats 188
#define emptyall4 189
#define goldkey 190
#define silverkey 191
#define emptykey 192
#define bul1get 193
#define bul1fly 194
#define bul2get 197
#define bul2fly 198
#define bul3get 200
#define bul3fly 201        //l = 6
#define bul3halfly 207     //l = 2
#define emptybulstand 209
#define lightning 210
#define extralife 211
#define getcompass 212
#define miniexplosion 213
#define explosion 214
#define hive 215
#define hivetohoney 216    //l = 6
#define honey 222
#define monbat 223         //l = 3
#define monske 226         //l = 3
#define monken 229         //l = 5
#define monwit 234
#define monbee 239         //l = 10
#define monspi 245         //l = 10
#define mongr2 251         //l = 18
#define mongre 262         //l = 6
#define monear 268         //l = 5
#define monand 273         //l = 5
#define monali 278         //l = 2
#define monbal 280         //l = 5
#define hole 285
#define monhol 286         //l = 4
#define mongho 290         //l = 3
#define monrob 293         //l = 10
#define monro2 303         //l = 8
#define mondog 311         //l = 15
#define monzor 341         //l = 10
#define monke2 356         //l = 14
#define monan2 370         //l = 9
#define monan3 376         //l = 9
#define bul4fly 385
#define bul5fly 386        //l = 2
#define bul6fly 388        //l = 4
#define bul7fly 390        //l = 3
#define bul8fly 393        //l = 4
#define bul10fly 395       //l = 3
#define bul9fly 397
#define bul11fly 398       //l = 7
#define monmum 407         //l = 8
#define skilblank 415
#define skilevel1 416
#define skilevel2 417
#define sodapics 418
#define copyright 419
#define labysign 420
#define congratsign 423
#define earth 424
#define youcheated 428
#define gameover 429
#define sharewaremessage 430
#define compassplc 431     //l = 4
#define statusbarback 435
#define statusbarinfo 436
#define coatfade 437
#define scorebox 438
#define menu 439
#define textwall 440       //l = 2
#define episodesign1off 442
#define episodesign2off 443
#define episodesign3off 444
#define episodesign1on 445
#define episodesign2on 446
#define episodesign3on 447
#define endtext 448
//SORTWALLEND
#ifndef __SWITCH__
#define HidNpadButton_Plus 0
#define HidNpadButton_Minus 0
#define HidNpadButton_L 0
#define HidNpadButton_ZL 0
#define HidNpadButton_R 0
#define HidNpadButton_ZR 0
#endif

#ifndef ENABLE_NXLINK
#define TRACE(fmt,...) ((void)0)
#else
#include <unistd.h>
#define TRACE(fmt,...) printf("%s: " fmt "\n", __PRETTY_FUNCTION__, ## __VA_ARGS__)
#endif

#ifdef _MSC_VER 
#define S_ISDIR(mode)  (((mode) & S_IFMT) == S_IFDIR)
#define strncasecmp _strnicmp
#define strcasecmp _stricmp
#endif

enum {
    ACTION_FORWARD,
    ACTION_BACKWARD,
    ACTION_LEFT,
    ACTION_RIGHT,
    ACTION_MOVELEFT,
    ACTION_MOVERIGHT,
    ACTION_STRAFE,
    ACTION_STANDHIGH,
    ACTION_STANDLOW,
    ACTION_RUN,
    ACTION_FIRE,
    ACTION_NEXTWEAPON,
    ACTION_FIREBALLS,
    ACTION_BOUNCY,
    ACTION_HEAT,
    ACTION_USE,
    ACTION_CHEAT,
    ACTION_OLD_SAVE = ACTION_CHEAT,
    ACTION_STATUS,
    ACTION_PAUSE,
    ACTION_MUTE,
    ACTION_OLD_LOAD = ACTION_MUTE,
    ACTION_MENU,
    ACTION_MENU_UP1,
    ACTION_MENU_UP2,
    ACTION_MENU_DOWN1,
    ACTION_MENU_DOWN2,
    ACTION_MENU_LEFT1,
    ACTION_MENU_LEFT2,
    ACTION_MENU_RIGHT1,
    ACTION_MENU_RIGHT2,
    ACTION_MENU_SELECT1,
    ACTION_MENU_SELECT2,
    ACTION_MENU_SELECT3,
    ACTION_MENU_CANCEL,
    ACTION_REWIND,
    ACTION_ADVANCE,
    ACTION_LAST,
    ACTION_CONFIG_LAST = ACTION_MENU + 1
};

enum {
    MAINMENU_NEWGAME,
    MAINMENU_LOADGAME,
    MAINMENU_SAVEGAME,
    MAINMENU_RETURN,
    MAINMENU_SETUP,
    MAINMENU_HELP,
    MAINMENU_STORY,
    MAINMENU_COPYRIGHT,
    MAINMENU_CREDITS,
    MAINMENU_EXIT,
    MAINMENU_LAST
};

enum {
    MUSIC_SOURCE_NONE,
    MUSIC_SOURCE_MIDI,
    MUSIC_SOURCE_ADLIB,
    MUSIC_SOURCE_ADLIB_RANDOM
};

typedef enum {
    DIRECTION_NONE,
    DIRECTION_UP,
    DIRECTION_DOWN
} Direction;

#define KEY_REPEAT_START 250
#define KEY_REPEAT 80


/* Global variables are defined in lab3d.c and extern in all other modules. */

#define TICKS_PER_SPRITE_FRAME 8

#ifdef MAIN
#define EXTERN
#else
#define EXTERN extern
#endif

/* Global variables that actually get initialised here... */

#ifdef MAIN
EXTERN unsigned char bultype[26] =
{ 0,1,1,1,1,2,1,2,1,2,1,2,1,2,1,2,2,2,2,2,2,2,2,1,2,1 };
EXTERN unsigned char paldef[16][3] =
{
        {0,30,63},{28,34,60},{0,50,20},{41,52,28},{63,63,25},{63,63,63},
        {63,20,20},{63,0,63},
        {63,48,27},{63,40,25},{63,48,48},{45,63,45},{55,55,63},{63,40,63},
        {63,30,20},{55,25,30}
};

EXTERN unsigned char opaldef[16][3] =
{
  {0,30,63},{28,34,60},{0,50,20},{15,60,30},{63,63,25},{63,63,63},
  {63,20,20},{63,0,63},
  {63,32,0},{63,40,25},{63,48,48},{45,63,45},{0,0,63},{63,40,63},
  {63,30,20},{63,63,63}
};

EXTERN K_UINT16 pcfreq[63] =
{
        0,
        65,69,73,78,82,87,92,98,104,110,117,123,
        131,139,147,156,165,175,185,196,208,220,233,247,
        262,277,294,311,330,349,370,392,415,440,466,494,
        523,554,587,622,659,698,740,784,831,880,932,988,
        1047,1109,1175,1245,1319,1397,1480,1568,1661,1760,1864,1976,
        2094
};
EXTERN K_UINT16 adlibfreq[63] =
{
        0,
        2390,2411,2434,2456,2480,2506,2533,2562,2592,2625,2659,2695,
        3414,3435,3458,3480,3504,3530,3557,3586,3616,3649,3683,3719,
        4438,4459,4482,4504,4528,4554,4581,4610,4640,4673,4707,4743,
        5462,5483,5506,5528,5552,5578,5605,5634,5664,5697,5731,5767,
        6486,6507,6530,6552,6576,6602,6629,6658,6688,6721,6755,6791,
        7510
};
EXTERN K_UINT16 firstime = 1, quitgame = 0;
EXTERN K_INT16 midiinst = 0, cheatenable = 0, capturecount = 0;
EXTERN K_INT16 mainmenuplace = 0, loadsavegameplace = 0, gameheadstat = 0;
EXTERN K_INT16 newgameplace = 0, sodaplace = 0;
#else
EXTERN unsigned char paldef[16][3];
EXTERN unsigned char opaldef[16][3];
EXTERN K_UINT16 pcfreq[63];
EXTERN K_UINT16 adlibfreq[63];
EXTERN K_UINT16 firstime, quitgame;
EXTERN K_INT16 midiinst, cheatenable, capturecount;
EXTERN K_INT16 mainmenuplace, loadsavegameplace, gameheadstat;
EXTERN K_INT16 newgameplace, sodaplace;
#endif

/* Amount of split textures (transitions). */

#define numsplits 7

/* Graphics stuff... */
EXTERN unsigned char* lzwbuf, * pic, * walseg[numwalls + 1];
EXTERN K_UINT16* lzwbuf2;
EXTERN GLuint texName[numwalls + 1];
EXTERN GLuint gameoversprite;
EXTERN GLuint splitTexName[numsplits][2];
EXTERN K_INT16 splitTexNum[numsplits];
EXTERN K_INT16 lborder[numwalls + 1], rborder[numwalls + 1];
EXTERN K_INT16 boleng[30 * 2], tileng[numwalls + 1];
EXTERN K_INT32 tioffs[numwalls + 1], walrec[numwalls + 1];
EXTERN K_INT16 numcells, totalsortcnt, sortcnt;
EXTERN K_UINT16 sortx[512], sorty[512];
EXTERN K_INT16 sorti[512], sortbnum[512];
EXTERN char wallheader[numwalls + 1], bmpkind[numwalls + 1];

/* A stack of function pointers for drawing the menu */
EXTERN void (*draw_ptr[4])(void);
EXTERN K_UINT16 drawStackTopIndex;

EXTERN struct {
    int alts;
    char** titles;
    int* value;
    const char* menutitle;
} selectionMenuStruct;

EXTERN K_INT16 whichmenu;

/*
   Why not just skip over the functions that draw the
   intro? Because they do important initialization stuff.
   Sigh.
*/

EXTERN char introskip;

/* Monster stuff... */

EXTERN K_UINT16 mposx[512], mposy[512], mnum, bossmonster;
EXTERN K_UINT16 mgolx[512], mgoly[512], mrotbuf[2048];
EXTERN K_UINT16 moldx[512], moldy[512];
EXTERN K_INT16 mshock[512], mstat[512];

/* Screen stuff and a lot of other things... */

EXTERN K_INT16 vidmode, dside, scrsize;
EXTERN K_INT16 board[64][64], videotype, mute, numboards, skilevel;
EXTERN unsigned char option[numoptions], keydefs[ACTION_LAST];
EXTERN unsigned char tempbuf[4096], storybuf[4096], palette[768], mshot[512];
EXTERN char hiscorenam[16], hiscorenamstat, namrememberstat;
EXTERN K_INT16 cliptowall;
EXTERN K_UINT16 explox[16];
EXTERN K_UINT16 exploy[16];
EXTERN K_UINT32 explotime[16];
EXTERN K_UINT16 explostat[16];
EXTERN K_UINT16 explonum;

EXTERN unsigned char psounds[16];
EXTERN unsigned char psoundpan[16];
EXTERN K_UINT32 psoundnum;

EXTERN char showdebug;

EXTERN demofile_t* demoplaying, * demorecording;

/* Temporary table... */
EXTERN K_INT16 lincalc[360];

EXTERN K_INT32 tantable[1025], sintable[2050];
EXTERN K_INT16 radarang[360];
EXTERN K_UINT16 halfheight;
EXTERN K_UINT16 times90[240], less64inc[64];
EXTERN K_INT16 moustat;
EXTERN int mousx, mousy, bstatus;
EXTERN K_INT16 joyx1, joyy1, joyx2, joyy2, joyx3, joyy3, joystat;
EXTERN unsigned char readch, oldreadch, keystatus[256];
EXTERN volatile K_INT16 clockspeed;
EXTERN K_INT16 slottime, slotpos[3], owecoins, owecoinwait;
EXTERN K_INT16 statusbar, statusbargoal, doorx, doory, doorstat;
EXTERN K_INT16 fadehurtval, fadewarpval;
EXTERN K_INT32 ototclock, totalclock, purpletime, greentime, capetime[2];
EXTERN K_INT32 scoreclock, scorecount;
EXTERN char textbuf[52];
EXTERN K_INT16 musicsource, midiscrap;
EXTERN K_UINT32 musicstatus, count, countstop;
EXTERN K_UINT16 samplerate;
EXTERN K_UINT16 numnotes, speed, drumstat, numchans, nownote;
EXTERN K_UINT32* note;
EXTERN K_UINT32 chanage[18];
EXTERN unsigned char inst[256][11], databuf[512];
EXTERN K_INT32 gminst[256];
EXTERN unsigned char chanfreq[18], chantrack[18];
EXTERN unsigned char trinst[16], trquant[16], trchan[16];
EXTERN unsigned char trprio[16], trvol[16];
EXTERN K_UINT16 ksayfreq;
EXTERN unsigned char speechstatus;
EXTERN K_INT16 boardnum, oldlife, life, death;
EXTERN K_INT16 lifevests, lightnings, firepowers[3], keys[2];
EXTERN K_INT16 compass, cheated, coins, waterstat;
EXTERN K_INT16 animate2, animate3, animate4, oscillate3, oscillate5;
EXTERN K_INT16 animate6, animate7, animate8, animate10;
EXTERN K_INT16 animate11, animate15;
EXTERN char xwarp[16], ywarp[16], numwarps, justwarped;
EXTERN K_INT16 bulnum, bulang[64], bulkind[64], bulchoose;
EXTERN K_UINT16 bulx[64], buly[64];
EXTERN K_UINT32 bulstat[64], lastbulshoot, lastbarchange;
EXTERN K_UINT16 startx, starty, posx, posy, oldposx, oldposy;
EXTERN K_INT16 ang, startang, angvel, yourhereoldpos;
EXTERN K_INT16 vel, mxvel, myvel, svel, maxvel;
EXTERN K_INT16 posz, hvel, lastunlock, lastshoot, saidwelcome;
EXTERN K_UINT16 convavailpages, convwalls;
EXTERN unsigned char gamehead[8][27], gamexist[8];
EXTERN K_INT16 legacyload;
EXTERN char gameroot[512], filepath[1024], filepathUpper[512];
EXTERN K_INT16 texturecreationneeded;
EXTERN K_INT16 stereo;

/* SDL timer... */

EXTERN SDL_TimerID timer;

/* Overlay buffer... */
EXTERN unsigned char* screenbuffer;
EXTERN uint32_t* screenbuffer32;
EXTERN int screenbufferwidth, screenbufferheight;
EXTERN int screenwidth, screenheight;
EXTERN float virtualscreenwidth, virtualscreenheight;
EXTERN GLuint screenbuffertexture; /* One big texture. */
EXTERN GLuint screenbuffertextures[72]; /* 72 small textures. */
EXTERN int largescreentexture;
EXTERN int walltol, neardist;

/* Nintendo Switch globals, defines and functions... */
#ifdef __SWITCH__
EXTERN PadState pad;
EXTERN Result rc, rc2;
EXTERN HiddbgHdlsHandle HdlsHandle;
EXTERN HiddbgHdlsDeviceInfo device;
EXTERN HiddbgHdlsState state;
EXTERN HiddbgHdlsSessionId session_id;
EXTERN u8 keyBuffer[256];

void configureResolution();
void setMesaConfig();
void initNxLink();
void deinitNxLink();
void userAppInit();
void userAppExit();
void getUsername();

/* libGLU reimplementation functions */
void normalize(float v[3]);
void cross(float v1[3], float v2[3], float result[3]);
void __gluMakeIdentityf(GLfloat m[16]);
void GLAPI
gluLookAt(GLdouble eyex, GLdouble eyey, GLdouble eyez, GLdouble centerx,
    GLdouble centery, GLdouble centerz, GLdouble upx, GLdouble upy,
    GLdouble upz);
#endif

/* MIDI stuff... */
#ifdef WIN32
EXTERN HMIDIOUT sequencerdevice;
#endif
#ifdef USE_OSS
EXTERN int sequencerdevice;
EXTERN int nrmidis;
#endif

/* Palette for graphics uploads... */
EXTERN unsigned char spritepalette[768];

EXTERN K_INT16 fadelevel, ingame, inlauncher;

void oldmain(void);
void UploadOverlay(void);
void UploadPartialOverlay(int x, int y, int w, int h);
void initialize();
void initmemory();
void initvideo();
void initaudio();
void initgraphics();
void inittablesandsettings();
void initgameversion();
void freememory();
void clearimgcache();
void resetaudio();
void gamelaunchermenu();
K_INT16 vline(K_INT16, K_INT16, K_INT16, K_INT16, K_INT16);
void update_bulrot(K_UINT16, K_UINT16);
void picrot(K_UINT16, K_UINT16, K_INT16, K_INT16);
void spridraw(K_INT16, K_INT16, K_INT16, K_INT16);
void pictur(K_INT16, K_INT16, K_INT16, K_INT16, K_INT16);
void doordraw(K_UINT16 x, K_UINT16 y, K_INT16 walnume, K_UINT16 posxs,
    K_UINT16 posys);
void statusbardraw(K_UINT16, K_UINT16, K_UINT16, K_UINT16, K_UINT16,
    K_UINT16, K_INT16);
void loadboard();
void loadtables();
K_INT16 ksay(K_UINT16);
K_INT16 ksayui(K_UINT16);
K_INT16 ksaypan(K_UINT16 filenum, K_UINT16 pan, int ui);
void checkobj(K_UINT16, K_UINT16, K_UINT16, K_UINT16, K_INT16, K_INT16);
void addexplosion(K_UINT16 x, K_UINT16 y, K_UINT16 stat);
void linecompare(K_UINT16);
void drawmeter(int life, int oldlife, int x, int y);
void drawlife();
void loadwalls(int replace);
K_INT16 loadgame(K_INT16);
K_INT16 savegame(K_INT16);
void introduction(K_INT16);
K_INT16 loadmusic(char*);
void outdata(unsigned char, unsigned char, unsigned char);
void musicon();
void musicoff();
void setinst(unsigned char, K_INT16, unsigned char, unsigned char,
    unsigned char, unsigned char, unsigned char, unsigned char,
    unsigned char, unsigned char,
    unsigned char, unsigned char, unsigned char);
void setmidiinsts();
void checkhitwall(K_UINT16, K_UINT16, K_UINT16, K_UINT16);
void fade(K_INT16);
void showcompass(K_INT16);
K_INT16 kgif(K_INT16);
void setgamevideomode();
void textprint(K_INT16, K_INT16, char);
K_INT16 loadstory(K_INT16);
void displaystory(K_INT16);
K_INT16 setupmouse();
void setupmenu(int ingame);
void statusbaralldraw();
void hiscorecheck();
void setuptextbuf(K_INT32);
void getname();
void drawscore(K_INT32);
void drawtime(K_INT32);
void screencapture();
K_INT16 mainmenu();
K_INT16 getselection(K_INT16, K_INT16, K_INT16, K_INT16);
void drawselectionmenu();
void drawmenu(K_INT16, K_INT16, K_INT16);
void finalisemenu();
void creditsmenu();
void helpmenu();
void orderinfomenu();
K_INT16 loadsavegamemenu();
K_INT16 newgamemenu();
void pressakey();
void wingame(K_INT16);
void winallgame();
K_INT16 areyousure();
void copyslots(K_INT16);
void youarehere();
void bigstorymenu();
void sodamenu();
Uint32 tickhandler(Uint32 interval, void* param);
void ksmhandler();
void SetVisibleScreenOffset(K_UINT16 offset);
int ClipToBuffer(int* sx, int* sy, int* w, int* h);
void ShowPartialOverlay(int x, int y, int w, int h, int statusbar);
void PollInputs();
void FindJoysticks();
void ProcessEvent(SDL_Event* event);
void checkGLStatus();
void floorsprite(K_UINT16 x, K_UINT16 y, K_INT16 walnume);
void flatsprite(K_UINT16 x, K_UINT16 y, K_INT16 ang, K_INT16 playerang,
    K_INT16 walnume);

typedef struct {
    SDL_Keycode key;
    int scan;
} pckeymap_t;

typedef struct {
    char* name;
    int value;
} enumpair;

#ifdef MAIN

/* SDL to PC key mapping table... */
pckeymap_t PCkey[] = {
    { SDLK_BACKSPACE, 14 },
    { SDLK_TAB, 15 },
    { SDLK_CLEAR, 251 },
    { SDLK_RETURN, 28 },
    { SDLK_PAUSE, 0 },
    { SDLK_ESCAPE, 1 },
    { SDLK_SPACE, 57 },
    { SDLK_EXCLAIM, 2 },
    { SDLK_QUOTEDBL, 40 },
    { SDLK_HASH, 4 },
    { SDLK_DOLLAR, 5 },
    { SDLK_AMPERSAND, 8 },
    { SDLK_QUOTE, 40 },
    { SDLK_LEFTPAREN, 10 },
    { SDLK_RIGHTPAREN, 11 },
    { SDLK_ASTERISK, 9 },
    { SDLK_PLUS, 13 },
    { SDLK_COMMA, 51 },
    { SDLK_MINUS, 12 },
    { SDLK_PERIOD, 52 },
    { SDLK_SLASH, 53 },
    { SDLK_0, 11 },
    { SDLK_1, 2 },
    { SDLK_2, 3 },
    { SDLK_3, 4 },
    { SDLK_4, 5 },
    { SDLK_5, 6 },
    { SDLK_6, 7 },
    { SDLK_7, 8 },
    { SDLK_8, 9 },
    { SDLK_9, 10 },
    { SDLK_COLON, 39 },
    { SDLK_SEMICOLON, 39 },
    { SDLK_LESS, 51 },
    { SDLK_EQUALS, 13 },
    { SDLK_GREATER, 52 },
    { SDLK_QUESTION, 53 },
    { SDLK_AT, 3 },
    { SDLK_LEFTBRACKET, 26 },
    { SDLK_BACKSLASH, 43 },
    { SDLK_RIGHTBRACKET, 27 },
    { SDLK_CARET, 7 },
    { SDLK_UNDERSCORE, 12 },
    { SDLK_BACKQUOTE, 41 },
    { SDLK_a, 30 },
    { SDLK_b, 48 },
    { SDLK_c, 46 },
    { SDLK_d, 32 },
    { SDLK_e, 18 },
    { SDLK_f, 33 },
    { SDLK_g, 34 },
    { SDLK_h, 35 },
    { SDLK_i, 23 },
    { SDLK_j, 36 },
    { SDLK_k, 37 },
    { SDLK_l, 38 },
    { SDLK_m, 50 },
    { SDLK_n, 49 },
    { SDLK_o, 24 },
    { SDLK_p, 25 },
    { SDLK_q, 16 },
    { SDLK_r, 19 },
    { SDLK_s, 31 },
    { SDLK_t, 20 },
    { SDLK_u, 22 },
    { SDLK_v, 47 },
    { SDLK_w, 17 },
    { SDLK_x, 45 },
    { SDLK_y, 21 },
    { SDLK_z, 44 },
    { SDLK_DELETE, 83 },
    { SDLK_KP_0, 82 },
    { SDLK_KP_1, 79 },
    { SDLK_KP_2, 80 },
    { SDLK_KP_3, 81 },
    { SDLK_KP_4, 75 },
    { SDLK_KP_5, 76 },
    { SDLK_KP_6, 77 },
    { SDLK_KP_7, 71 },
    { SDLK_KP_8, 72 },
    { SDLK_KP_9, 73 },
    { SDLK_KP_PERIOD, 83 },
    { SDLK_KP_DIVIDE, 224 },
    { SDLK_KP_MULTIPLY, 55 },
    { SDLK_KP_MINUS, 74 },
    { SDLK_KP_PLUS, 78 },
    { SDLK_KP_ENTER, 224 },
    { SDLK_UP, 200 },
    { SDLK_DOWN, 208 },
    { SDLK_RIGHT, 205 },
    { SDLK_LEFT, 203 },
    { SDLK_INSERT, 210 },
    { SDLK_HOME, 199 },
    { SDLK_END, 207 },
    { SDLK_PAGEUP, 201 },
    { SDLK_PAGEDOWN, 209 },
    { SDLK_F1, 59 },
    { SDLK_F2, 60 },
    { SDLK_F3, 61 },
    { SDLK_F4, 62 },
    { SDLK_F5, 63 },
    { SDLK_F6, 64 },
    { SDLK_F7, 65 },
    { SDLK_F8, 66 },
    { SDLK_F9, 67 },
    { SDLK_F10, 68 },
    { SDLK_F11, 87 },
    { SDLK_F12, 88 },
    { SDLK_F13, 236 },
    { SDLK_F14, 237 },
    { SDLK_F15, 238 },
    { SDLK_NUMLOCKCLEAR, 69 },
    { SDLK_CAPSLOCK, 58 },
    { SDLK_SCROLLLOCK, 70 },
    { SDLK_RSHIFT, 54 },
    { SDLK_LSHIFT, 42 },
    { SDLK_RCTRL, 157 },
    { SDLK_LCTRL, 29 },
    { SDLK_RALT, 184 },
    { SDLK_LALT, 56 },
    { SDLK_MODE, 184 },
    { SDLK_APPLICATION, 184 },
    { SDLK_PRINTSCREEN, 183 },
    { SDLK_PAUSE, 0 },
    { 0, -1 },
};

#else
extern pckeymap_t PCkey[];
#endif

/* Sound data. */
EXTERN unsigned char* SoundFile;

/* Sound output buffer... */
EXTERN K_INT16* SoundBuffer;
EXTERN int FeedPoint;

/* SDL Audio Device ID */
EXTERN SDL_AudioDeviceID audiodevice;

/* Last played Music file */
EXTERN char lastPlayedMusicFile[12];

/* Various graphics kludges. */
EXTERN int statusbaryoffset, spriteyoffset, visiblescreenyoffset, mixing;
EXTERN int statusbaryvisible;

/* Faster menu drawing... */
EXTERN int menuleft, menutop, menuwidth, menuheight, menuing;

/* Fade level... */
EXTERN GLfloat redfactor, bluefactor, greenfactor;

/* Shall we go fullscreen? */
EXTERN int fullscreen;

/* Filtering modes. */
EXTERN GLint fullfilter, partialfilter, anisotropic;

/* Joystick device. */
EXTERN SDL_Window* mainwindow;
EXTERN int window_in_focus;

EXTERN SDL_GameController* cur_controller;
EXTERN int cur_controller_index;
EXTERN unsigned int cur_controller_axis_active;

EXTERN SDL_Joystick* cur_joystick;
EXTERN int cur_joystick_index;
EXTERN int cur_joystick_num_axes;
EXTERN unsigned int cur_joystick_axis_active;

EXTERN int joyenable;

EXTERN SDL_GLContext* maincontext;

void DumpSound(unsigned char* sound, K_UINT16 leng, K_UINT32 playpoint, int pos);
void AudioCallback(void* userdata, Uint8* stream, int len);
void TextureConvert(unsigned char* from, unsigned char* to, K_INT16 type);
Uint16 getkeypress();
void drawtooverlay(K_UINT16 picx, K_UINT16 picy, int w,
    int h, int x, int y, K_INT16 walnum,
    unsigned char coloff);
void wipeoverlay(K_UINT16 x, K_UINT16 y, K_UINT16 w, K_UINT16 h);
void settransferpalette();
void setdarkenedpalette();
void updateoverlaypalette(K_UINT16 start, K_UINT16 amount, unsigned char* cols);
void TransitionTexture(int left, int texture, int right);

void process_sdl_event(SDL_Event* e);
unsigned char readmouse(int* x, int* y);
void quit();
void loadsettings();
void savesettings();
void configure();
void configure_screen_size();
void reset_dsp();
void updatemap();
void updategameover();
void ShowStatusBar();
void setup();
void drawinputbox();
int getkeydefstat(int keydef);
int getkeydefstatlock(int keydef);
void clearkeydefstat(int keydef);
int getkeypressure(int keydef, int pressval, int runpressval);
void fatal_error(const char* fmt, ...);
int newkeystatus(int key);
void setnewkeystatus(int key, int val);
int get_pckey(SDL_Keycode key);
int read_ini(FILE* input, char* buf, int buflen, char** keyp, char** valp, int* linep);
int get_enum(char* str, enumpair* cur);
int smooth_input(int, int, int, int);
void setup_stereo(int);

EXTERN SDL_mutex* soundmutex, * timermutex;

#define JOY_FLAG_NEG  0x40000000
#define JOY_FLAG_AXIS 0x20000000
#define JOY_MASK 0x1FFFFFFF
#define ACTION_UNBOUND -1

EXTERN int g_stereo_sep;

EXTERN int action_key[ACTION_LAST];
EXTERN int action_joystick[ACTION_LAST];
EXTERN int action_controller[ACTION_LAST];

EXTERN K_UINT32 action_repeat_lock[ACTION_LAST];

EXTERN int numkeyspressed;
EXTERN int keyspressed[128];

EXTERN int musicvolume, soundvolume;
EXTERN int channels;
K_INT16 ksaystereo(K_UINT16 filenum, K_UINT16 x, K_UINT16 y);
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define readLE16 read
#define readLE32 read
#define writeLE16 write
#define writeLE32 write
#else
ssize_t readLE16(int fd, void* buf, size_t count);
ssize_t readLE32(int fd, void* buf, size_t count);
ssize_t writeLE16(int fd, void* buf, size_t count);
ssize_t writeLE32(int fd, void* buf, size_t count);
#endif
EXTERN double gammalevel;


/* Data from wallparam.ini */

EXTERN int shadow2[numwalls]; // Renamed to avoid conflict with shadow in CoreFoundation
EXTERN double walltexcoord[numwalls][2];

#if SDL_BYTE_ORDER==SDL_LITTLE_ENDIAN
#define RED_SHIFT 0
#define GREEN_SHIFT 8
#define BLUE_SHIFT 16
#define ALPHA_SHIFT 24
#define REDCMP(X) ((X)&255)
#define GREENCMP(X) (((X)>>8)&255)
#define BLUECMP(X) (((X)>>16)&255)
#define ALPHACMP(X) (((X)>>24)&255)
#else
#define RED_SHIFT 24
#define GREEN_SHIFT 16
#define BLUE_SHIFT 8
#define ALPHA_SHIFT 0
#define REDCMP(X) (((X)>>24)&255)
#define GREENCMP(X) (((X)>>16)&255)
#define BLUECMP(X) (((X)>>8)&255)
#define ALPHACMP(X) ((X)&255)
#endif


/* Size of block for sound device. */
#define SOUNDBLOCKSIZE11KHZ 256
#define SOUNDBLOCKSIZE44KHZ 1024
EXTERN int soundblocksize;

/* Experimental sound buffer timing method variables. */
EXTERN int soundtimer, soundbytespertick, soundtimerbytes;

/* Sound effect and music pan enable toggles. */

EXTERN int soundpan, musicpan;

/* Colour depth. */
EXTERN GLint colourformat;

/* Toggle extreme debug output (slows things down if everything works).*/
EXTERN int debugmode;

/* LAB3D version to emulate (0 = v2.1 (Epic reg.), 1 = v1.1 (AdvSys reg.)). */
EXTERN enum {
    KENS_LABYRINTH_1_0,
    KENS_LABYRINTH_1_1,
    KENS_LABYRINTH_2_0,
    KENS_LABYRINTH_2_1
} lab3dversion;

/* Things that aren't constants anymore really... */
EXTERN int rnumwalls;

/* Monster board (v1.x only)... */
EXTERN unsigned char mboard[64][64];

/* Some v1.x flags... */
EXTERN K_INT16 fanpos, kenpos, kenpos2, ballpos, heatpos, warpos, rogermode;

/* Read clock speed. */
EXTERN K_INT16 clockspd;

void updateclock(void);

/* Aspect ratio correction factors for ingame view (extra width and height). */
EXTERN GLdouble aspw, asph;
