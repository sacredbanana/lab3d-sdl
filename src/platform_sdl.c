/*
 * SDL2 implementation of the LAB3D platform layer (see include/platform.h).
 *
 * This file is the only place in the desktop/console builds that talks to SDL.
 * It is deliberately a thin wrapper: every function does exactly what the
 * equivalent inline SDL call used to do, so the existing ports behave as
 * before.  The Amiga port supplies its own implementation of the same
 * interface in src/amiga/ and links no SDL.
 */

#include "lab3d.h"

static SDL_mutex *soundmutex, *timermutex;
static SDL_TimerID clocktimer;

/* Handles the shared code no longer needs to know about. */
static SDL_Window        *mainwindow;
static SDL_GLContext      maincontext;
static SDL_AudioDeviceID  audiodevice;
static SDL_Joystick      *cur_joystick;
static SDL_GameController*cur_controller;

/* ------------------------------------------------------------- start / stop */

void PL_Init(void) {
    soundmutex = SDL_CreateMutex();
    timermutex = SDL_CreateMutex();

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO |
             SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);

    if (SDL_GL_LoadLibrary(NULL) != 0)
        fprintf(stderr, "Could not dynamically open OpenGL library: %s\n",
                SDL_GetError());

    SDL_JoystickEventState(1);
}

void PL_Shutdown(void) {
    PL_StopClock();

    if (timermutex) {
        SDL_UnlockMutex(timermutex);   /* just in case we still hold it */
        SDL_DestroyMutex(timermutex);
        timermutex = NULL;
    }
    if (soundmutex) {
        SDL_UnlockMutex(soundmutex);
        SDL_DestroyMutex(soundmutex);
        soundmutex = NULL;
    }

    SDL_Quit();
}

void PL_FatalBox(const char *title, const char *message) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title, message, mainwindow);
}

/* ---------------------------------------------------------------- timing */

K_UINT32 PL_GetTicks(void) {
    return SDL_GetTicks();
}

void PL_Delay(K_UINT32 ms) {
    SDL_Delay(ms);
}

static Uint32 clockcallback(Uint32 interval, void *param) {
    (void)param;
    PL_LockTimer();
    updateclock();
    PL_UnlockTimer();
    return interval;
}

void PL_StartClock(void) {
    if (!clocktimer)
        clocktimer = SDL_AddTimer(4, clockcallback, NULL);
}

void PL_StopClock(void) {
    if (clocktimer) {
        SDL_RemoveTimer(clocktimer);
        clocktimer = 0;
    }
}

void PL_PumpClock(void) {
    /* A real timer thread keeps the clock moving; nothing to do here. */
}

void PL_LockTimer(void)   { if (timermutex) SDL_LockMutex(timermutex); }
void PL_UnlockTimer(void) { if (timermutex) SDL_UnlockMutex(timermutex); }
void PL_LockSound(void)   { if (soundmutex) SDL_LockMutex(soundmutex); }
void PL_UnlockSound(void) { if (soundmutex) SDL_UnlockMutex(soundmutex); }

/* ----------------------------------------------------------------- video */

static SDL_Surface *loadicon(void) {
    SDL_Surface *icon = SDL_LoadBMP("ken.bmp");
#if defined(__unix__) && !defined(__APPLE__)
    if (icon == NULL)
        icon = SDL_LoadBMP("/usr/local/share/ken/ken.bmp");
    if (icon == NULL)
        icon = SDL_LoadBMP("/usr/share/ken/ken.bmp");
#endif
    if (icon == NULL)
        fprintf(stderr, "Warning: ken.bmp (icon file) not found.\n");
    return icon;
}

int PL_OpenVideo(void) {
    SDL_Surface *icon;
    int realr, realg, realb, realz, reald = 0;

    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE, 0);

#ifdef __SWITCH__
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
#endif

    SDL_ShowCursor(0);

    icon = loadicon();

    fprintf(stderr, "Activating video...\n");

    if (mainwindow == NULL) {
        if (fullscreen) {
            mainwindow = SDL_CreateWindow("Ken's Labyrinth",
                                          SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 0, 0,
                                          SDL_WINDOW_FULLSCREEN_DESKTOP |
                                          SDL_WINDOW_OPENGL);
        } else {
            mainwindow = SDL_CreateWindow("Ken's Labyrinth",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          screenwidth, screenheight,
                                          SDL_WINDOW_OPENGL);
        }
        if (mainwindow == NULL)
            fatal_error("Video mode set failed.");
        SDL_SetWindowGrab(mainwindow, SDL_TRUE);
    }

    SDL_GetWindowSize(mainwindow, &screenwidth, &screenheight);
    configure_screen_size();
    fprintf(stderr, "True size: %dx%d\n", screenwidth, screenheight);

    if (icon != NULL) {
        SDL_SetWindowIcon(mainwindow, icon);
        SDL_FreeSurface(icon);
    }

    maincontext = SDL_GL_CreateContext(mainwindow);
    SDL_GL_SetSwapInterval(1);   /* 0 = off, 1 = vsync, -1 = adaptive */

    if (maincontext == NULL)
        fatal_error("Could not create GL context.");

#ifdef __SWITCH__
    gladLoadGL();
#endif

    SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &realr);
    SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &realg);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &realb);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &realz);
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER, &reald);

    fprintf(stderr, "GL Vendor: %s\n", glGetString(GL_VENDOR));
    fprintf(stderr, "GL Renderer: %s\n", glGetString(GL_RENDERER));
    fprintf(stderr, "GL Version: %s\n", glGetString(GL_VERSION));

#ifndef __SWITCH__
    fprintf(stderr, "GLU Version: %s\n", gluGetString(GLU_VERSION));
    fprintf(stderr, "GLU Extensions: %s\n", gluGetString(GLU_EXTENSIONS));

    if (reald == 0)
        fatal_error("Double buffer not available.");
#endif

    fprintf(stderr,
            "Opened GL at %d/%d/%d (R/G/B) bits, %d bit depth buffer.\n",
            realr, realg, realb, realz);

    if (realz < 24) {
        walltol = 256;
        neardist = 128;
    }

    PL_SetBrightness(gammalevel);
    return 0;
}

void PL_CloseVideo(void) {
    if (mainwindow) {
        SDL_DestroyWindow(mainwindow);
        mainwindow = NULL;
    }
    if (maincontext) {
        SDL_GL_DeleteContext(maincontext);
        maincontext = NULL;
    }
}

void PL_SwapBuffers(void) {
    SDL_GL_SwapWindow(mainwindow);
}

void PL_SetBrightness(double level) {
    if (mainwindow)
        SDL_SetWindowBrightness(mainwindow, (float)level);
}

void PL_ShowCursor(int show) {
    SDL_ShowCursor(show);
}

int PL_SaveScreenshot(const char *filename) {
    /* The GL renderer grabs the framebuffer itself; see screencapture(). */
    (void)filename;
    return 0;
}

void PL_GetDesktopSize(int *w, int *h) {
    SDL_Rect bounds;

    if (SDL_GetDisplayBounds(0, &bounds) != 0) {
        *w = 640; *h = 480;
        return;
    }
    *w = bounds.w;
    *h = bounds.h;
}

/* ----------------------------------------------------------------- input */

static int translate_event(SDL_Event *in, PL_Event *out) {
    memset(out, 0, sizeof(*out));

    switch (in->type) {
    case SDL_QUIT:
        out->type = PL_QUIT;
        return 1;
    case SDL_WINDOWEVENT:
        if (in->window.event == SDL_WINDOWEVENT_FOCUS_GAINED)
            out->type = PL_FOCUSGAINED;
        else if (in->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            out->type = PL_FOCUSLOST;
        else if (in->window.event == SDL_WINDOWEVENT_EXPOSED)
            out->type = PL_EXPOSED;
        else
            return 0;
        return 1;
    case SDL_KEYDOWN:
        out->type = PL_KEYDOWN;
        out->key = in->key.keysym.sym;
        return 1;
    case SDL_KEYUP:
        out->type = PL_KEYUP;
        out->key = in->key.keysym.sym;
        return 1;
    case SDL_TEXTINPUT:
        out->type = PL_TEXTINPUT;
        strncpy(out->text, in->text.text, sizeof(out->text) - 1);
        return 1;
    case SDL_JOYAXISMOTION:
        out->type = PL_JOYAXISMOTION;
        out->which = in->jaxis.which;
        out->axis = in->jaxis.axis;
        out->value = in->jaxis.value;
        return 1;
    case SDL_JOYBUTTONDOWN:
        out->type = PL_JOYBUTTONDOWN;
        out->which = in->jbutton.which;
        out->button = in->jbutton.button;
        return 1;
    case SDL_JOYDEVICEADDED:
        out->type = PL_JOYDEVICEADDED;
        out->which = in->jdevice.which;
        return 1;
    case SDL_JOYDEVICEREMOVED:
        out->type = PL_JOYDEVICEREMOVED;
        out->which = in->jdevice.which;
        return 1;
    case SDL_CONTROLLERAXISMOTION:
        out->type = PL_CONTROLLERAXISMOTION;
        out->which = in->caxis.which;
        out->axis = in->caxis.axis;
        out->value = in->caxis.value;
        return 1;
    case SDL_CONTROLLERBUTTONDOWN:
        out->type = PL_CONTROLLERBUTTONDOWN;
        out->which = in->cbutton.which;
        out->button = in->cbutton.button;
        return 1;
    case SDL_CONTROLLERDEVICEADDED:
        out->type = PL_CONTROLLERDEVICEADDED;
        out->which = in->cdevice.which;
        return 1;
    case SDL_CONTROLLERDEVICEREMOVED:
        out->type = PL_CONTROLLERDEVICEREMOVED;
        out->which = in->cdevice.which;
        return 1;
    default:
        return 0;
    }
}

int PL_PollEvent(PL_Event *event) {
    SDL_Event sdlevent;

    while (SDL_PollEvent(&sdlevent)) {
        if (translate_event(&sdlevent, event))
            return 1;
    }
    return 0;
}

void PL_PumpEvents(void) {
    SDL_PumpEvents();
}

unsigned char PL_GetRelativeMouseState(int *x, int *y) {
    return SDL_GetRelativeMouseState(x, y);
}

void PL_SetRelativeMouseMode(int enabled) {
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

void PL_StartTextInput(void) { SDL_StartTextInput(); }
void PL_StopTextInput(void)  { SDL_StopTextInput(); }

const char *PL_GetKeyName(PL_Keycode key) {
    return SDL_GetKeyName(key);
}

PL_Keycode PL_GetKeyFromName(const char *name) {
    return SDL_GetKeyFromName(name);
}

const char *PL_ControllerAxisName(int axis) {
    return SDL_GameControllerGetStringForAxis(axis);
}

const char *PL_ControllerButtonName(int button) {
    return SDL_GameControllerGetStringForButton(button);
}

int PL_ControllerAxisFromName(const char *name) {
    return SDL_GameControllerGetAxisFromString(name);
}

int PL_ControllerButtonFromName(const char *name) {
    return SDL_GameControllerGetButtonFromString(name);
}

void PL_OpenJoysticks(void) {
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

int PL_HaveJoystick(void)   { return cur_joystick != NULL; }
int PL_HaveController(void) { return cur_controller != NULL; }

int PL_JoystickGetAxis(int axis) {
    return cur_joystick ? SDL_JoystickGetAxis(cur_joystick, axis) : 0;
}

int PL_JoystickGetButton(int button) {
    return cur_joystick ? SDL_JoystickGetButton(cur_joystick, button) : 0;
}

int PL_ControllerGetAxis(int axis) {
    return cur_controller ? SDL_GameControllerGetAxis(cur_controller, axis) : 0;
}

int PL_ControllerGetButton(int button) {
    return cur_controller ? SDL_GameControllerGetButton(cur_controller, button) : 0;
}

void PL_CloseJoysticks(void) {
    if (cur_joystick) {
        SDL_JoystickClose(cur_joystick);
        cur_joystick = NULL;
        cur_joystick_index = -1;
    }
    if (cur_controller) {
        SDL_GameControllerClose(cur_controller);
        cur_controller = NULL;
        cur_controller_index = -1;
    }
}

/* ----------------------------------------------------------------- audio */

static void audiotrampoline(void *userdata, Uint8 *stream, int len) {
    AudioCallback(userdata, stream, len);
}

int PL_OpenAudio(int freq, int chans, int samples) {
    SDL_AudioSpec want, have;

    memset(&want, 0, sizeof(want));
    want.freq = freq;
    want.format = AUDIO_S16SYS;
    want.channels = chans;
    want.samples = samples;
    want.userdata = NULL;
    want.callback = audiotrampoline;

    audiodevice = SDL_OpenAudioDevice(NULL, 0, &want, &have,
                                      SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    if (audiodevice == 0) {
        TRACE("Failed to open audio: %s", SDL_GetError());
        return freq;
    }
    return have.freq;
}

void PL_CloseAudio(void) {
    if (audiodevice) {
        SDL_CloseAudioDevice(audiodevice);
        audiodevice = 0;
    }
}

void PL_PauseAudio(int paused) {
    if (audiodevice)
        SDL_PauseAudioDevice(audiodevice, paused);
}

int PL_ResampleU8(const unsigned char *in, int inlen, int infreq,
                  unsigned char *out, int outmax, int outfreq) {
    SDL_AudioCVT cvt;
    int n;

    if (SDL_BuildAudioCVT(&cvt, AUDIO_U8, 1, infreq,
                          AUDIO_U8, 1, outfreq) < 0) {
        n = inlen < outmax ? inlen : outmax;
        memcpy(out, in, n);
        return n;
    }

    cvt.len = inlen;
    cvt.buf = (Uint8 *)SDL_malloc(cvt.len * cvt.len_mult);
    if (!cvt.buf) return 0;
    memcpy(cvt.buf, in, inlen);
    SDL_ConvertAudio(&cvt);

    n = cvt.len_cvt < outmax ? cvt.len_cvt : outmax;
    memcpy(out, cvt.buf, n);
    SDL_free(cvt.buf);
    return n;
}

/* ------------------------------------------------------------------ MIDI */

int PL_MidiOpen(void) {
#ifdef WIN32
    if (midiOutOpen(&sequencerdevice, MIDI_MAPPER, (DWORD)(NULL),
                    (DWORD)(NULL), 0) != MMSYSERR_NOERROR)
        return -1;
    return 0;
#elif defined(USE_OSS)
    sequencerdevice = open("/dev/sequencer", O_WRONLY, 0);
    if (sequencerdevice < 0)
        return -1;
    if (ioctl(sequencerdevice, SNDCTL_SEQ_NRMIDIS, &nrmidis) == -1)
        return -1;
    return 0;
#else
    return -1;
#endif
}

void PL_MidiClose(void) {
#ifdef WIN32
    if (sequencerdevice != 0)
        midiOutClose(sequencerdevice);
    sequencerdevice = 0;
#endif
}

void PL_MidiWrite(const unsigned char *data, int len) {
#ifdef USE_OSS
    if (sequencerdevice >= 0)
        write(sequencerdevice, data, len);
#else
    (void)data; (void)len;
#endif
}

/* ------------------------------------------------------------- rectangles */

int PL_IntersectRect(const PL_Rect *a, const PL_Rect *b, PL_Rect *out) {
    SDL_Rect ra, rb, ro;

    ra.x = a->x; ra.y = a->y; ra.w = a->w; ra.h = a->h;
    rb.x = b->x; rb.y = b->y; rb.w = b->w; rb.h = b->h;

    if (!SDL_IntersectRect(&ra, &rb, &ro))
        return 0;

    out->x = ro.x; out->y = ro.y; out->w = ro.w; out->h = ro.h;
    return 1;
}
