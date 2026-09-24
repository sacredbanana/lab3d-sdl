#ifndef LAB3D_PLATFORM_H
#define LAB3D_PLATFORM_H

/*
 * LAB3D/SDL platform abstraction.
 *
 * Everything the game needs from the host - video output, timing, audio,
 * keyboard/mouse/joystick input and the few odds and ends SDL used to supply -
 * goes through the PL_* interface declared here.  There are two
 * implementations:
 *
 *   src/platform_sdl.c   thin wrappers over SDL2, used by every desktop and
 *                        console port.  Behaviour is identical to the old
 *                        direct SDL calls.
 *   src/amiga/            native AmigaOS code.  The Amiga build links no SDL
 *                        at all; intuition.library, graphics.library,
 *                        cybergraphics.library, audio.device, lowlevel.library
 *                        and timer.device do the work instead.
 *
 * Key codes are numerically identical to SDL2's on every platform so that
 * lab3d.cfg stays portable between them.
 */

#ifdef __AMIGA__
#define PLATFORM_AMIGA 1
#else
#define PLATFORM_SDL 1
#endif

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ types */

typedef uint32_t K_UINT32;
typedef uint16_t K_UINT16;
typedef int32_t  K_INT32;
typedef int16_t  K_INT16;

typedef int32_t  PL_Keycode;

/* ----------------------------------------------------------- byte ordering */

#define PL_LIL_ENDIAN 1234
#define PL_BIG_ENDIAN 4321

#if defined(__BIG_ENDIAN__) || \
    (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define PL_BYTEORDER PL_BIG_ENDIAN
#else
#define PL_BYTEORDER PL_LIL_ENDIAN
#endif

#define PL_Swap16(X) ((K_UINT16)((((K_UINT16)(X)) << 8) | (((K_UINT16)(X)) >> 8)))
#define PL_Swap32(X) ((K_UINT32)((((K_UINT32)(X)) << 24) |            \
                                 ((((K_UINT32)(X)) << 8) & 0x00ff0000U) | \
                                 ((((K_UINT32)(X)) >> 8) & 0x0000ff00U) | \
                                 (((K_UINT32)(X)) >> 24)))

#define PL_Swap64(X) ((uint64_t)((((uint64_t)(X)) << 56) |                   \
                                 ((((uint64_t)(X)) << 40) & 0x00ff000000000000ULL) | \
                                 ((((uint64_t)(X)) << 24) & 0x0000ff0000000000ULL) | \
                                 ((((uint64_t)(X)) <<  8) & 0x000000ff00000000ULL) | \
                                 ((((uint64_t)(X)) >>  8) & 0x00000000ff000000ULL) | \
                                 ((((uint64_t)(X)) >> 24) & 0x0000000000ff0000ULL) | \
                                 ((((uint64_t)(X)) >> 40) & 0x000000000000ff00ULL) | \
                                 (((uint64_t)(X)) >> 56)))

#if PL_BYTEORDER == PL_LIL_ENDIAN
#define PL_SwapLE16(X) (X)
#define PL_SwapLE32(X) (X)
#define PL_SwapLE64(X) (X)
#else
#define PL_SwapLE16(X) PL_Swap16(X)
#define PL_SwapLE32(X) PL_Swap32(X)
#define PL_SwapLE64(X) PL_Swap64(X)
#endif

/* ----------------------------------------------------------------- key IDs */

#ifdef PLATFORM_SDL
#include "SDL.h"
#define PLK_0                  SDLK_0
#define PLK_1                  SDLK_1
#define PLK_2                  SDLK_2
#define PLK_3                  SDLK_3
#define PLK_4                  SDLK_4
#define PLK_5                  SDLK_5
#define PLK_6                  SDLK_6
#define PLK_7                  SDLK_7
#define PLK_8                  SDLK_8
#define PLK_9                  SDLK_9
#define PLK_a                  SDLK_a
#define PLK_AMPERSAND          SDLK_AMPERSAND
#define PLK_APPLICATION        SDLK_APPLICATION
#define PLK_ASTERISK           SDLK_ASTERISK
#define PLK_AT                 SDLK_AT
#define PLK_b                  SDLK_b
#define PLK_BACKQUOTE          SDLK_BACKQUOTE
#define PLK_BACKSLASH          SDLK_BACKSLASH
#define PLK_BACKSPACE          SDLK_BACKSPACE
#define PLK_c                  SDLK_c
#define PLK_CAPSLOCK           SDLK_CAPSLOCK
#define PLK_CARET              SDLK_CARET
#define PLK_CLEAR              SDLK_CLEAR
#define PLK_COLON              SDLK_COLON
#define PLK_COMMA              SDLK_COMMA
#define PLK_d                  SDLK_d
#define PLK_DELETE             SDLK_DELETE
#define PLK_DOLLAR             SDLK_DOLLAR
#define PLK_DOWN               SDLK_DOWN
#define PLK_e                  SDLK_e
#define PLK_END                SDLK_END
#define PLK_EQUALS             SDLK_EQUALS
#define PLK_ESCAPE             SDLK_ESCAPE
#define PLK_EXCLAIM            SDLK_EXCLAIM
#define PLK_f                  SDLK_f
#define PLK_F1                 SDLK_F1
#define PLK_F10                SDLK_F10
#define PLK_F11                SDLK_F11
#define PLK_F12                SDLK_F12
#define PLK_F13                SDLK_F13
#define PLK_F14                SDLK_F14
#define PLK_F15                SDLK_F15
#define PLK_F2                 SDLK_F2
#define PLK_F3                 SDLK_F3
#define PLK_F4                 SDLK_F4
#define PLK_F5                 SDLK_F5
#define PLK_F6                 SDLK_F6
#define PLK_F7                 SDLK_F7
#define PLK_F8                 SDLK_F8
#define PLK_F9                 SDLK_F9
#define PLK_g                  SDLK_g
#define PLK_GREATER            SDLK_GREATER
#define PLK_h                  SDLK_h
#define PLK_HASH               SDLK_HASH
#define PLK_HOME               SDLK_HOME
#define PLK_i                  SDLK_i
#define PLK_INSERT             SDLK_INSERT
#define PLK_j                  SDLK_j
#define PLK_k                  SDLK_k
#define PLK_KP_0               SDLK_KP_0
#define PLK_KP_1               SDLK_KP_1
#define PLK_KP_2               SDLK_KP_2
#define PLK_KP_3               SDLK_KP_3
#define PLK_KP_4               SDLK_KP_4
#define PLK_KP_5               SDLK_KP_5
#define PLK_KP_6               SDLK_KP_6
#define PLK_KP_7               SDLK_KP_7
#define PLK_KP_8               SDLK_KP_8
#define PLK_KP_9               SDLK_KP_9
#define PLK_KP_DIVIDE          SDLK_KP_DIVIDE
#define PLK_KP_ENTER           SDLK_KP_ENTER
#define PLK_KP_MINUS           SDLK_KP_MINUS
#define PLK_KP_MULTIPLY        SDLK_KP_MULTIPLY
#define PLK_KP_PERIOD          SDLK_KP_PERIOD
#define PLK_KP_PLUS            SDLK_KP_PLUS
#define PLK_l                  SDLK_l
#define PLK_LALT               SDLK_LALT
#define PLK_LCTRL              SDLK_LCTRL
#define PLK_LEFT               SDLK_LEFT
#define PLK_LEFTBRACKET        SDLK_LEFTBRACKET
#define PLK_LEFTPAREN          SDLK_LEFTPAREN
#define PLK_LESS               SDLK_LESS
#define PLK_LGUI               SDLK_LGUI
#define PLK_LSHIFT             SDLK_LSHIFT
#define PLK_m                  SDLK_m
#define PLK_MINUS              SDLK_MINUS
#define PLK_MODE               SDLK_MODE
#define PLK_n                  SDLK_n
#define PLK_NUMLOCKCLEAR       SDLK_NUMLOCKCLEAR
#define PLK_o                  SDLK_o
#define PLK_p                  SDLK_p
#define PLK_PAGEDOWN           SDLK_PAGEDOWN
#define PLK_PAGEUP             SDLK_PAGEUP
#define PLK_PAUSE              SDLK_PAUSE
#define PLK_PERIOD             SDLK_PERIOD
#define PLK_PLUS               SDLK_PLUS
#define PLK_PRINTSCREEN        SDLK_PRINTSCREEN
#define PLK_q                  SDLK_q
#define PLK_QUESTION           SDLK_QUESTION
#define PLK_QUOTE              SDLK_QUOTE
#define PLK_QUOTEDBL           SDLK_QUOTEDBL
#define PLK_r                  SDLK_r
#define PLK_RALT               SDLK_RALT
#define PLK_RCTRL              SDLK_RCTRL
#define PLK_RETURN             SDLK_RETURN
#define PLK_RIGHT              SDLK_RIGHT
#define PLK_RIGHTBRACKET       SDLK_RIGHTBRACKET
#define PLK_RIGHTPAREN         SDLK_RIGHTPAREN
#define PLK_RSHIFT             SDLK_RSHIFT
#define PLK_s                  SDLK_s
#define PLK_SCROLLLOCK         SDLK_SCROLLLOCK
#define PLK_SEMICOLON          SDLK_SEMICOLON
#define PLK_SLASH              SDLK_SLASH
#define PLK_SPACE              SDLK_SPACE
#define PLK_t                  SDLK_t
#define PLK_TAB                SDLK_TAB
#define PLK_u                  SDLK_u
#define PLK_UNDERSCORE         SDLK_UNDERSCORE
#define PLK_UNKNOWN            SDLK_UNKNOWN
#define PLK_UP                 SDLK_UP
#define PLK_v                  SDLK_v
#define PLK_w                  SDLK_w
#define PLK_x                  SDLK_x
#define PLK_y                  SDLK_y
#define PLK_z                  SDLK_z
#else
#define PLK_0                  0x00000030
#define PLK_1                  0x00000031
#define PLK_2                  0x00000032
#define PLK_3                  0x00000033
#define PLK_4                  0x00000034
#define PLK_5                  0x00000035
#define PLK_6                  0x00000036
#define PLK_7                  0x00000037
#define PLK_8                  0x00000038
#define PLK_9                  0x00000039
#define PLK_a                  0x00000061
#define PLK_AMPERSAND          0x00000026
#define PLK_APPLICATION        0x40000065
#define PLK_ASTERISK           0x0000002a
#define PLK_AT                 0x00000040
#define PLK_b                  0x00000062
#define PLK_BACKQUOTE          0x00000060
#define PLK_BACKSLASH          0x0000005c
#define PLK_BACKSPACE          0x00000008
#define PLK_c                  0x00000063
#define PLK_CAPSLOCK           0x40000039
#define PLK_CARET              0x0000005e
#define PLK_CLEAR              0x4000009c
#define PLK_COLON              0x0000003a
#define PLK_COMMA              0x0000002c
#define PLK_d                  0x00000064
#define PLK_DELETE             0x0000007f
#define PLK_DOLLAR             0x00000024
#define PLK_DOWN               0x40000051
#define PLK_e                  0x00000065
#define PLK_END                0x4000004d
#define PLK_EQUALS             0x0000003d
#define PLK_ESCAPE             0x0000001b
#define PLK_EXCLAIM            0x00000021
#define PLK_f                  0x00000066
#define PLK_F1                 0x4000003a
#define PLK_F10                0x40000043
#define PLK_F11                0x40000044
#define PLK_F12                0x40000045
#define PLK_F13                0x40000068
#define PLK_F14                0x40000069
#define PLK_F15                0x4000006a
#define PLK_F2                 0x4000003b
#define PLK_F3                 0x4000003c
#define PLK_F4                 0x4000003d
#define PLK_F5                 0x4000003e
#define PLK_F6                 0x4000003f
#define PLK_F7                 0x40000040
#define PLK_F8                 0x40000041
#define PLK_F9                 0x40000042
#define PLK_g                  0x00000067
#define PLK_GREATER            0x0000003e
#define PLK_h                  0x00000068
#define PLK_HASH               0x00000023
#define PLK_HOME               0x4000004a
#define PLK_i                  0x00000069
#define PLK_INSERT             0x40000049
#define PLK_j                  0x0000006a
#define PLK_k                  0x0000006b
#define PLK_KP_0               0x40000062
#define PLK_KP_1               0x40000059
#define PLK_KP_2               0x4000005a
#define PLK_KP_3               0x4000005b
#define PLK_KP_4               0x4000005c
#define PLK_KP_5               0x4000005d
#define PLK_KP_6               0x4000005e
#define PLK_KP_7               0x4000005f
#define PLK_KP_8               0x40000060
#define PLK_KP_9               0x40000061
#define PLK_KP_DIVIDE          0x40000054
#define PLK_KP_ENTER           0x40000058
#define PLK_KP_MINUS           0x40000056
#define PLK_KP_MULTIPLY        0x40000055
#define PLK_KP_PERIOD          0x40000063
#define PLK_KP_PLUS            0x40000057
#define PLK_l                  0x0000006c
#define PLK_LALT               0x400000e2
#define PLK_LCTRL              0x400000e0
#define PLK_LEFT               0x40000050
#define PLK_LEFTBRACKET        0x0000005b
#define PLK_LEFTPAREN          0x00000028
#define PLK_LESS               0x0000003c
#define PLK_LGUI               0x400000e3
#define PLK_LSHIFT             0x400000e1
#define PLK_m                  0x0000006d
#define PLK_MINUS              0x0000002d
#define PLK_MODE               0x40000101
#define PLK_n                  0x0000006e
#define PLK_NUMLOCKCLEAR       0x40000053
#define PLK_o                  0x0000006f
#define PLK_p                  0x00000070
#define PLK_PAGEDOWN           0x4000004e
#define PLK_PAGEUP             0x4000004b
#define PLK_PAUSE              0x40000048
#define PLK_PERIOD             0x0000002e
#define PLK_PLUS               0x0000002b
#define PLK_PRINTSCREEN        0x40000046
#define PLK_q                  0x00000071
#define PLK_QUESTION           0x0000003f
#define PLK_QUOTE              0x00000027
#define PLK_QUOTEDBL           0x00000022
#define PLK_r                  0x00000072
#define PLK_RALT               0x400000e6
#define PLK_RCTRL              0x400000e4
#define PLK_RETURN             0x0000000d
#define PLK_RIGHT              0x4000004f
#define PLK_RIGHTBRACKET       0x0000005d
#define PLK_RIGHTPAREN         0x00000029
#define PLK_RSHIFT             0x400000e5
#define PLK_s                  0x00000073
#define PLK_SCROLLLOCK         0x40000047
#define PLK_SEMICOLON          0x0000003b
#define PLK_SLASH              0x0000002f
#define PLK_SPACE              0x00000020
#define PLK_t                  0x00000074
#define PLK_TAB                0x00000009
#define PLK_u                  0x00000075
#define PLK_UNDERSCORE         0x0000005f
#define PLK_UNKNOWN            0x00000000
#define PLK_UP                 0x40000052
#define PLK_v                  0x00000076
#define PLK_w                  0x00000077
#define PLK_x                  0x00000078
#define PLK_y                  0x00000079
#define PLK_z                  0x0000007a
#endif

/* ----------------------------------------------------------------- events */

enum {
    PL_NOEVENT = 0,
    PL_QUIT,
    PL_KEYDOWN,
    PL_KEYUP,
    PL_TEXTINPUT,
    PL_JOYAXISMOTION,
    PL_JOYBUTTONDOWN,
    PL_JOYDEVICEADDED,
    PL_JOYDEVICEREMOVED,
    PL_CONTROLLERAXISMOTION,
    PL_CONTROLLERBUTTONDOWN,
    PL_CONTROLLERDEVICEADDED,
    PL_CONTROLLERDEVICEREMOVED,
    PL_FOCUSGAINED,
    PL_FOCUSLOST,
    PL_EXPOSED
};

typedef struct {
    int         type;
    PL_Keycode  key;        /* PL_KEYDOWN / PL_KEYUP                       */
    char        text[8];    /* PL_TEXTINPUT                                */
    int         which;      /* device index for the joystick/pad events    */
    int         axis;       /* PL_*AXISMOTION                              */
    int         value;      /* PL_*AXISMOTION                              */
    int         button;     /* PL_*BUTTONDOWN                              */
} PL_Event;

/* Game controller axes and buttons.  Values match SDL_GameController* so that
   saved bindings carry across platforms. */
#define PL_CONTROLLER_AXIS_INVALID        (-1)
#define PL_CONTROLLER_AXIS_LEFTX          0
#define PL_CONTROLLER_AXIS_LEFTY          1
#define PL_CONTROLLER_AXIS_RIGHTX         2
#define PL_CONTROLLER_AXIS_RIGHTY         3
#define PL_CONTROLLER_AXIS_TRIGGERLEFT    4
#define PL_CONTROLLER_AXIS_TRIGGERRIGHT   5
#define PL_CONTROLLER_AXIS_MAX            6

#define PL_CONTROLLER_BUTTON_INVALID      (-1)
#define PL_CONTROLLER_BUTTON_A            0
#define PL_CONTROLLER_BUTTON_B            1
#define PL_CONTROLLER_BUTTON_X            2
#define PL_CONTROLLER_BUTTON_Y            3
#define PL_CONTROLLER_BUTTON_BACK         4
#define PL_CONTROLLER_BUTTON_GUIDE        5
#define PL_CONTROLLER_BUTTON_START        6
#define PL_CONTROLLER_BUTTON_LEFTSTICK    7
#define PL_CONTROLLER_BUTTON_RIGHTSTICK   8
#define PL_CONTROLLER_BUTTON_LEFTSHOULDER 9
#define PL_CONTROLLER_BUTTON_RIGHTSHOULDER 10
#define PL_CONTROLLER_BUTTON_DPAD_UP      11
#define PL_CONTROLLER_BUTTON_DPAD_DOWN    12
#define PL_CONTROLLER_BUTTON_DPAD_LEFT    13
#define PL_CONTROLLER_BUTTON_DPAD_RIGHT   14
#define PL_CONTROLLER_BUTTON_MAX          15

/* ------------------------------------------------------------- start / stop */

void        PL_Init(void);
void        PL_Shutdown(void);
void        PL_FatalBox(const char *title, const char *message);

/* --------------------------------------------------------------- timing */

K_UINT32    PL_GetTicks(void);
void        PL_Delay(K_UINT32 ms);

/* The game clock used to be advanced by an SDL timer thread.  PL_StartClock()
   arranges for updateclock() to be called at 240Hz by whatever mechanism the
   platform provides; PL_PumpClock() gives single-threaded platforms a chance
   to catch up and is a no-op where a real timer thread exists. */
void        PL_StartClock(void);
void        PL_StopClock(void);
void        PL_PumpClock(void);

/* Critical sections shared with the audio and timer callbacks. */
void        PL_LockTimer(void);
void        PL_UnlockTimer(void);
void        PL_LockSound(void);
void        PL_UnlockSound(void);

/* ---------------------------------------------------------------- video */

/* Bring up the display.  Returns 0 on success. */
int         PL_OpenVideo(void);
void        PL_CloseVideo(void);
void        PL_SwapBuffers(void);
void        PL_SetBrightness(double level);
void        PL_ShowCursor(int show);
int         PL_SaveScreenshot(const char *filename);
void        PL_GetDesktopSize(int *w, int *h);

/* --------------------------------------------------------------- input */

int         PL_PollEvent(PL_Event *event);
void        PL_PumpEvents(void);
unsigned char PL_GetRelativeMouseState(int *x, int *y);
void        PL_SetRelativeMouseMode(int enabled);
void        PL_StartTextInput(void);
void        PL_StopTextInput(void);
const char *PL_GetKeyName(PL_Keycode key);
PL_Keycode  PL_GetKeyFromName(const char *name);
const char *PL_ControllerAxisName(int axis);
const char *PL_ControllerButtonName(int button);
int         PL_ControllerAxisFromName(const char *name);
int         PL_ControllerButtonFromName(const char *name);

void        PL_OpenJoysticks(void);
void        PL_CloseJoysticks(void);

/* Direct polling, used by the action-to-device mapping in getkeypressure(). */
int         PL_HaveJoystick(void);
int         PL_HaveController(void);
int         PL_JoystickGetAxis(int axis);
int         PL_JoystickGetButton(int button);
int         PL_ControllerGetAxis(int axis);
int         PL_ControllerGetButton(int button);

/* --------------------------------------------------------------- audio */

/* Requests `freq` Hz, `channels` channel signed 16 bit output delivered by
   repeated calls to AudioCallback().  Returns the rate actually obtained. */
int         PL_OpenAudio(int freq, int channels, int samples);
void        PL_CloseAudio(void);
void        PL_PauseAudio(int paused);

/* Resample 8 bit unsigned mono PCM.  Returns the number of output samples and
   writes at most `outmax` bytes. */
int         PL_ResampleU8(const unsigned char *in, int inlen, int infreq,
                          unsigned char *out, int outmax, int outfreq);

/* --------------------------------------------------------------- MIDI */

int         PL_MidiOpen(void);
void        PL_MidiClose(void);
void        PL_MidiWrite(const unsigned char *data, int len);

/* ------------------------------------------------------------ rectangles */

typedef struct { int x, y, w, h; } PL_Rect;
int         PL_IntersectRect(const PL_Rect *a, const PL_Rect *b, PL_Rect *out);

#endif /* LAB3D_PLATFORM_H */
