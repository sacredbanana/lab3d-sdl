/*
 * AmigaOS implementation of the LAB3D platform layer (include/platform.h).
 *
 * No SDL: intuition.library and graphics.library provide the display,
 * timer.device the clock, audio.device the sound (see amiga_audio.c),
 * lowlevel.library the joystick, and Intuition IDCMP the keyboard and mouse.
 *
 * The port is single threaded.  SDL drove the 240Hz game clock from a timer
 * thread and the mixer from an audio thread; here both are serviced from
 * PL_PumpClock(), which the game already calls from every wait loop through
 * PL_Delay() and PollInputs().  That removes the need for real locking, so
 * the mutex entry points are empty.
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"

/* ------------------------------------------------------------ library bases */

extern struct Library *CyberGfxBase;
extern struct Library *AslBase;

struct Library     *LowLevelBase;
struct Library     *KeymapBase;
struct Device      *TimerBase;

static struct IORequest timereq;
static struct MsgPort  *timerport;
static ULONG            eclock_freq;
static struct EClockVal start_eclock;
static int              timer_ok;

extern void amiga_audio_service(void);
extern void amiga_audio_close(void);

/* Settings owned by amiga_video.c. */
extern ULONG amiga_cfg_modeid;
extern int   amiga_cfg_width, amiga_cfg_height, amiga_cfg_depth;
extern int   amiga_cfg_scale, amiga_cfg_askmode;
void amiga_lock_mode(ULONG modeid, int w, int h, int d);

/* --------------------------------------------------------------- start/stop */

static void open_timer(void) {
    timerport = CreateMsgPort();
    if (!timerport) return;

    timereq.io_Message.mn_ReplyPort = timerport;
    if (OpenDevice(TIMERNAME, UNIT_ECLOCK, &timereq, 0) != 0)
        return;

    TimerBase = timereq.io_Device;
    eclock_freq = ReadEClock(&start_eclock);
    timer_ok = (eclock_freq != 0);
}

static void close_timer(void) {
    if (TimerBase) {
        CloseDevice(&timereq);
        TimerBase = NULL;
    }
    if (timerport) {
        DeleteMsgPort(timerport);
        timerport = NULL;
    }
    timer_ok = 0;
}

void PL_Init(void) {
    static int inited;

    if (inited) return;
    inited = 1;

    KeymapBase   = OpenLibrary("keymap.library", 36);
    AslBase      = OpenLibrary("asl.library", 38);
    CyberGfxBase = OpenLibrary("cybergraphics.library", 40);
    LowLevelBase = OpenLibrary("lowlevel.library", 40);

    if (!AslBase)
        fprintf(stderr, "asl.library V38 is required for the screen mode "
                        "requester.\n");
    if (!CyberGfxBase)
        fprintf(stderr, "No cybergraphics.library; RTG modes unavailable.\n");
    if (!LowLevelBase)
        fprintf(stderr, "No lowlevel.library; joystick support disabled.\n");

    open_timer();
}

void PL_Shutdown(void) {
    amiga_audio_close();
    amiga_video_close();
    close_timer();

    if (LowLevelBase) { CloseLibrary(LowLevelBase); LowLevelBase = NULL; }
    if (CyberGfxBase) { CloseLibrary(CyberGfxBase); CyberGfxBase = NULL; }
    if (AslBase)      { CloseLibrary(AslBase);      AslBase = NULL; }
    if (KeymapBase)   { CloseLibrary(KeymapBase);   KeymapBase = NULL; }
}

void PL_FatalBox(const char *title, const char *message) {
    struct EasyStruct es;

    es.es_StructSize   = sizeof(es);
    es.es_Flags        = 0;
    es.es_Title        = (UBYTE *)title;
    es.es_TextFormat   = (UBYTE *)"%s";
    es.es_GadgetFormat = (UBYTE *)"OK";

    if (IntuitionBase)
        EasyRequestArgs(NULL, &es, NULL, (APTR)&message);
}

/* ------------------------------------------------------------------ timing */

K_UINT32 PL_GetTicks(void) {
    struct EClockVal now;
    ULONG dhi, dlo;

    if (!timer_ok)
        return 0;

    ReadEClock(&now);

    dhi = now.ev_hi - start_eclock.ev_hi;
    dlo = now.ev_lo - start_eclock.ev_lo;
    if (now.ev_lo < start_eclock.ev_lo)
        dhi--;

    /* (ticks * 1000) / freq, keeping the 64 bit numerator honest without
       needing 64 bit division: split the count into whole seconds plus a
       remainder. */
    {
        ULONG secs = 0;
        while (dhi) {                      /* each wrap of ev_lo is 2^32 ticks */
            secs += (ULONG)(4294967296.0 / eclock_freq);
            dlo  += (ULONG)(4294967296.0 - (double)(ULONG)(4294967296.0 / eclock_freq) * eclock_freq);
            dhi--;
        }
        secs += dlo / eclock_freq;
        dlo  %= eclock_freq;
        return secs * 1000 + (K_UINT32)((dlo * 1000) / eclock_freq);
    }
}

void PL_PumpClock(void) {
    updateclock();
    amiga_audio_service();
}

void PL_Delay(K_UINT32 ms) {
    K_UINT32 deadline = PL_GetTicks() + ms;

    do {
        PL_PumpClock();
    } while ((K_INT32)(PL_GetTicks() - deadline) < 0);
}

void PL_StartClock(void) { /* the main loop drives it */ }
void PL_StopClock(void)  { }

/* Single threaded: the clock and the mixer only ever run from PL_PumpClock(),
   which the game calls between frames, so there is nothing to lock out. */
void PL_LockTimer(void)   { }
void PL_UnlockTimer(void) { }
void PL_LockSound(void)   { }
void PL_UnlockSound(void) { }

/* ------------------------------------------------------------------- video */

int PL_OpenVideo(void) {
    /* Set once the player has picked a mode this run.  The launcher closes and
       reopens the display when it switches game version, and they should not
       have to answer the requester twice to get there. */
    static int mode_chosen;

    if (amiga_screen)
        return 0;       /* already up */

    if ((amiga_cfg_askmode && !mode_chosen) || amiga_cfg_modeid == INVALID_ID) {
        amiga_videomode pick;

        memset(&pick, 0, sizeof(pick));
        if (!amiga_select_screenmode(&pick)) {
            fprintf(stderr, "No screen mode chosen; exiting.\n");
            return -1;
        }
        amiga_cfg_modeid = pick.modeid;
        amiga_cfg_width  = pick.width;
        amiga_cfg_height = pick.height;
        amiga_cfg_depth  = pick.depth;
        amiga_lock_mode(pick.modeid, pick.width, pick.height, pick.depth);
    }
    mode_chosen = 1;

    memset(&amiga_mode, 0, sizeof(amiga_mode));
    amiga_mode.modeid = amiga_cfg_modeid;
    amiga_mode.width  = amiga_cfg_width;
    amiga_mode.height = amiga_cfg_height;
    amiga_mode.depth  = amiga_cfg_depth;

    if (amiga_video_open() != 0)
        return -1;

    /* The shared code works in a fixed 360x240 space on the Amiga; the
       display module handles centring and scaling. */
    screenwidth  = AMIGA_VIEW_W;
    screenheight = AMIGA_VIEW_H;
    virtualscreenwidth  = AMIGA_VIEW_W;
    virtualscreenheight = AMIGA_VIEW_H;
    aspw = 1.0;
    asph = 1.0;

    return 0;
}

void PL_CloseVideo(void) {
    amiga_video_close();
}

void PL_SwapBuffers(void) {
    amiga_blit_frame();
}

void PL_SetBrightness(double level) {
    /* Gamma is folded into the palette the next time it is loaded. */
    (void)level;
}

void PL_ShowCursor(int show) {
    (void)show;     /* the game screen never shows a pointer */
}

int PL_SaveScreenshot(const char *filename) {
    (void)filename;
    return 0;
}

void PL_GetDesktopSize(int *w, int *h) {
    if (amiga_screen) {
        *w = amiga_screen->Width;
        *h = amiga_screen->Height;
    } else {
        *w = AMIGA_VIEW_W;
        *h = AMIGA_VIEW_H;
    }
}

/* ------------------------------------------------------------------- input */

/* Amiga raw key code to platform key code.  Only the keys the game can bind
   are listed; anything else reports PLK_UNKNOWN and is ignored. */
static const PL_Keycode rawkeymap[128] = {
    /* 0x00 */ PLK_BACKQUOTE, PLK_1, PLK_2, PLK_3, PLK_4, PLK_5, PLK_6, PLK_7,
    /* 0x08 */ PLK_8, PLK_9, PLK_0, PLK_MINUS, PLK_EQUALS, PLK_BACKSLASH,
               PLK_UNKNOWN, PLK_KP_0,
    /* 0x10 */ PLK_q, PLK_w, PLK_e, PLK_r, PLK_t, PLK_y, PLK_u, PLK_i,
    /* 0x18 */ PLK_o, PLK_p, PLK_LEFTBRACKET, PLK_RIGHTBRACKET, PLK_UNKNOWN,
               PLK_KP_1, PLK_KP_2, PLK_KP_3,
    /* 0x20 */ PLK_a, PLK_s, PLK_d, PLK_f, PLK_g, PLK_h, PLK_j, PLK_k,
    /* 0x28 */ PLK_l, PLK_SEMICOLON, PLK_QUOTE, PLK_HASH, PLK_UNKNOWN,
               PLK_KP_4, PLK_KP_5, PLK_KP_6,
    /* 0x30 */ PLK_LESS, PLK_z, PLK_x, PLK_c, PLK_v, PLK_b, PLK_n, PLK_m,
    /* 0x38 */ PLK_COMMA, PLK_PERIOD, PLK_SLASH, PLK_UNKNOWN, PLK_KP_PERIOD,
               PLK_KP_7, PLK_KP_8, PLK_KP_9,
    /* 0x40 */ PLK_SPACE, PLK_BACKSPACE, PLK_TAB, PLK_KP_ENTER, PLK_RETURN,
               PLK_ESCAPE, PLK_DELETE, PLK_UNKNOWN,
    /* 0x48 */ PLK_UNKNOWN, PLK_UNKNOWN, PLK_KP_MINUS, PLK_UNKNOWN,
               PLK_UP, PLK_DOWN, PLK_RIGHT, PLK_LEFT,
    /* 0x50 */ PLK_F1, PLK_F2, PLK_F3, PLK_F4, PLK_F5, PLK_F6, PLK_F7, PLK_F8,
    /* 0x58 */ PLK_F9, PLK_F10, PLK_KP_DIVIDE, PLK_KP_MULTIPLY,
               PLK_UNKNOWN, PLK_UNKNOWN, PLK_KP_PLUS, PLK_INSERT,
    /* 0x60 */ PLK_LSHIFT, PLK_RSHIFT, PLK_CAPSLOCK, PLK_LCTRL, PLK_LALT,
               PLK_RALT, PLK_LGUI, PLK_APPLICATION,
    /* 0x68 */ PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN,
               PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN,
    /* 0x70 */ PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN,
               PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN,
    /* 0x78 */ PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN,
               PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN, PLK_UNKNOWN
};

/* Relative mouse motion accumulated between readmouse() calls. */
static int mouse_dx, mouse_dy;
static unsigned char mouse_buttons;
static int text_input_on;

/* Joystick state, refreshed once per PL_PollEvent() sweep. */
static ULONG joystate, joystate_old;
static int   joy_present;

#define JOYPORT 1

static void poll_joystick(void) {
    ULONG s;

    if (!LowLevelBase) { joy_present = 0; return; }

    s = ReadJoyPort(JOYPORT);
    switch (s & JP_TYPE_MASK) {
    case JP_TYPE_GAMECTLR:
    case JP_TYPE_JOYSTK:
        joy_present = 1;
        joystate_old = joystate;
        joystate = s;
        break;
    default:
        joy_present = 0;
        joystate_old = joystate = 0;
        break;
    }
}

/* Two axes and seven buttons, presented the way the binding UI expects. */
int PL_HaveJoystick(void)   { return joy_present; }
int PL_HaveController(void) { return 0; }

int PL_JoystickGetAxis(int axis) {
    if (!joy_present) return 0;
    if (axis == 0)
        return (joystate & JPF_JOY_LEFT)  ? -32768
             : (joystate & JPF_JOY_RIGHT) ?  32767 : 0;
    if (axis == 1)
        return (joystate & JPF_JOY_UP)    ? -32768
             : (joystate & JPF_JOY_DOWN)  ?  32767 : 0;
    return 0;
}

static const ULONG joybuttonbit[7] = {
    JPF_BUTTON_RED, JPF_BUTTON_BLUE, JPF_BUTTON_YELLOW, JPF_BUTTON_GREEN,
    JPF_BUTTON_FORWARD, JPF_BUTTON_REVERSE, JPF_BUTTON_PLAY
};

int PL_JoystickGetButton(int button) {
    if (!joy_present || button < 0 || button > 6) return 0;
    return (joystate & joybuttonbit[button]) ? 1 : 0;
}

int PL_ControllerGetAxis(int axis)     { (void)axis;   return 0; }
int PL_ControllerGetButton(int button) { (void)button; return 0; }

void PL_OpenJoysticks(void) {
    if (!joyenable || !LowLevelBase) {
        joystat = 1;
        return;
    }
    /* Ask for game controller reports so CD32 pads give us all the buttons. */
    SetJoyPortAttrs(JOYPORT, SJA_Type, SJA_TYPE_AUTOSENSE, TAG_END);
    poll_joystick();
    cur_joystick_index = joy_present ? 0 : -1;
    cur_controller_index = -1;
    joystat = joy_present ? 0 : 1;
}

void PL_CloseJoysticks(void) {
    joy_present = 0;
    cur_joystick_index = -1;
}

/* Pending synthetic events produced from joystick state changes. */
static PL_Event joyqueue[16];
static int joyqhead, joyqtail;

static void joyqueue_push(const PL_Event *e) {
    int next = (joyqhead + 1) & 15;
    if (next == joyqtail) return;
    joyqueue[joyqhead] = *e;
    joyqhead = next;
}

static void make_joy_events(void) {
    ULONG changed = joystate ^ joystate_old;
    PL_Event e;
    int i;

    if (!changed) return;

    memset(&e, 0, sizeof(e));
    e.which = 0;

    for (i = 0; i < 7; i++) {
        if ((changed & joybuttonbit[i]) && (joystate & joybuttonbit[i])) {
            e.type = PL_JOYBUTTONDOWN;
            e.button = i;
            joyqueue_push(&e);
        }
    }

    if (changed & (JPF_JOY_LEFT | JPF_JOY_RIGHT)) {
        e.type = PL_JOYAXISMOTION;
        e.axis = 0;
        e.value = PL_JoystickGetAxis(0);
        joyqueue_push(&e);
    }
    if (changed & (JPF_JOY_UP | JPF_JOY_DOWN)) {
        e.type = PL_JOYAXISMOTION;
        e.axis = 1;
        e.value = PL_JoystickGetAxis(1);
        joyqueue_push(&e);
    }
}

/* Turn a RAWKEY into printable text, for the high score name entry. */
static int rawkey_to_text(UWORD code, UWORD qualifier, APTR iaddress,
                          char *out, int outlen) {
    struct InputEvent ie;
    char buf[8];
    int n;

    if (!KeymapBase) return 0;

    ie.ie_NextEvent    = NULL;
    ie.ie_Class        = IECLASS_RAWKEY;
    ie.ie_SubClass     = 0;
    ie.ie_Code         = code;
    ie.ie_Qualifier    = qualifier;
    ie.ie_EventAddress = iaddress;

    n = MapRawKey(&ie, (STRPTR)buf, sizeof(buf), NULL);
    if (n <= 0) return 0;
    if (n > outlen - 1) n = outlen - 1;

    /* Only pass through printable characters. */
    if ((unsigned char)buf[0] < 32) return 0;

    memcpy(out, buf, n);
    out[n] = 0;
    return 1;
}

int PL_PollEvent(PL_Event *event) {
    struct IntuiMessage *msg;

    memset(event, 0, sizeof(*event));

    if (joyqtail != joyqhead) {
        *event = joyqueue[joyqtail];
        joyqtail = (joyqtail + 1) & 15;
        return 1;
    }

    if (!amiga_window)
        return 0;

    while ((msg = (struct IntuiMessage *)GetMsg(amiga_window->UserPort))) {
        ULONG  cls   = msg->Class;
        UWORD  code  = msg->Code;
        UWORD  qual  = msg->Qualifier;
        WORD   mx    = msg->MouseX;
        WORD   my    = msg->MouseY;
        APTR   iaddr = msg->IAddress;
        int    handled = 0;

        ReplyMsg((struct Message *)msg);

        switch (cls) {
        case IDCMP_RAWKEY:
            if (code & IECODE_UP_PREFIX) {
                event->type = PL_KEYUP;
                event->key  = rawkeymap[code & 0x7f];
            } else {
                event->type = PL_KEYDOWN;
                event->key  = rawkeymap[code & 0x7f];

                if (text_input_on) {
                    PL_Event te;
                    memset(&te, 0, sizeof(te));
                    te.type = PL_TEXTINPUT;
                    if (rawkey_to_text(code, qual, iaddr, te.text,
                                       sizeof(te.text)))
                        joyqueue_push(&te);
                }
            }
            if (event->key == PLK_UNKNOWN)
                break;      /* nothing useful; look at the next message */
            handled = 1;
            break;

        case IDCMP_MOUSEMOVE:
            mouse_dx += mx;
            mouse_dy += my;
            break;

        case IDCMP_MOUSEBUTTONS:
            switch (code) {
            case SELECTDOWN: mouse_buttons |=  1; break;
            case SELECTUP:   mouse_buttons &= ~1; break;
            case MENUDOWN:   mouse_buttons |=  2; break;
            case MENUUP:     mouse_buttons &= ~2; break;
            case MIDDLEDOWN: mouse_buttons |=  4; break;
            case MIDDLEUP:   mouse_buttons &= ~4; break;
            default: break;
            }
            break;

        case IDCMP_ACTIVEWINDOW:
            event->type = PL_FOCUSGAINED;
            handled = 1;
            break;

        case IDCMP_INACTIVEWINDOW:
            event->type = PL_FOCUSLOST;
            handled = 1;
            break;

        default:
            break;
        }

        if (handled)
            return 1;
    }

    poll_joystick();
    make_joy_events();

    if (joyqtail != joyqhead) {
        *event = joyqueue[joyqtail];
        joyqtail = (joyqtail + 1) & 15;
        return 1;
    }

    return 0;
}

void PL_PumpEvents(void) {
    PL_Event e;
    while (PL_PollEvent(&e))
        ProcessEvent(&e);
}

unsigned char PL_GetRelativeMouseState(int *x, int *y) {
    if (x) *x = mouse_dx;
    if (y) *y = mouse_dy;
    mouse_dx = mouse_dy = 0;
    return mouse_buttons;
}

void PL_SetRelativeMouseMode(int enabled) {
    (void)enabled;      /* the backdrop window is opened with WA_DELTAMOVE */
}

void PL_StartTextInput(void) { text_input_on = 1; }
void PL_StopTextInput(void)  { text_input_on = 0; }

/* -------------------------------------------------------------- key names */

typedef struct { PL_Keycode key; const char *name; } keyname_t;

static const keyname_t keynametab[] = {
#include "amiga/keynames.h"
    { 0, NULL }
};

const char *PL_GetKeyName(PL_Keycode key) {
    static char buf[16];
    const keyname_t *k;

    for (k = keynametab; k->name; k++)
        if (k->key == key)
            return k->name;

    if (key >= 32 && key < 127) {
        buf[0] = (char)toupper((int)key);
        buf[1] = 0;
        return buf;
    }
    return "";
}

PL_Keycode PL_GetKeyFromName(const char *name) {
    const keyname_t *k;

    if (!name || !*name) return PLK_UNKNOWN;

    for (k = keynametab; k->name; k++)
        if (strcasecmp(k->name, name) == 0)
            return k->key;

    if (name[1] == 0)
        return (PL_Keycode)tolower((int)(unsigned char)name[0]);

    return PLK_UNKNOWN;
}

/* Game controllers do not exist on the Amiga; the joystick path covers CD32
   pads.  These still have to answer so that a config file written on a PC
   round-trips without losing its controller bindings. */
const char *PL_ControllerAxisName(int axis)     { (void)axis;   return NULL; }
const char *PL_ControllerButtonName(int button) { (void)button; return NULL; }
int PL_ControllerAxisFromName(const char *name)   { (void)name; return -1; }
int PL_ControllerButtonFromName(const char *name) { (void)name; return -1; }

/* --------------------------------------------------------------------- MIDI */

int  PL_MidiOpen(void)  { return -1; }   /* no MIDI output on this port */
void PL_MidiClose(void) { }
void PL_MidiWrite(const unsigned char *data, int len) { (void)data; (void)len; }

/* --------------------------------------------------------------- rectangles */

int PL_IntersectRect(const PL_Rect *a, const PL_Rect *b, PL_Rect *out) {
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = (a->x + a->w) < (b->x + b->w) ? (a->x + a->w) : (b->x + b->w);
    int y1 = (a->y + a->h) < (b->y + b->h) ? (a->y + a->h) : (b->y + b->h);

    if (x1 <= x0 || y1 <= y0)
        return 0;

    out->x = x0; out->y = y0;
    out->w = x1 - x0; out->h = y1 - y0;
    return 1;
}
