#ifdef USE_OSS
#define MUSIC_SOURCES 3
#else
#ifdef WIN32
#define MUSIC_SOURCES 3
#include "objbase.h"
#include "shlobj.h"
#else
#define MUSIC_SOURCES 2
#endif
#endif

#include "lab3d.h"

static void draw_mainmenu(void);

#ifdef WIN32
#define HAVE_DESKTOP
HRESULT CreateLink(LPCSTR lpszPathObj,
                   LPSTR lpszPathLink, LPSTR lpszDesc,
                   LPSTR lpszArgs) {
    HRESULT hres;
    IShellLink* psl;
    char p[MAX_PATH];

    CoInitialize(NULL);
    hres = CoCreateInstance(&CLSID_ShellLink, NULL,
                            CLSCTX_INPROC_SERVER, &IID_IShellLink,
                            (void *)&psl);
    if (SUCCEEDED(hres)) {
        IPersistFile* ppf;

        GetCurrentDirectory(MAX_PATH, p);
        psl->lpVtbl->SetWorkingDirectory(psl, p);
        hres=psl->lpVtbl->SetPath(psl, lpszPathObj);

        psl->lpVtbl->SetArguments(psl, lpszArgs);

        psl->lpVtbl->SetDescription(psl, lpszDesc);

        hres = psl->lpVtbl->QueryInterface(psl, &IID_IPersistFile,
                                           (void *)&ppf);

        if (SUCCEEDED(hres)) {
            WORD wsz[MAX_PATH];

            fprintf(stderr,"Trying to save shortcut...\n");
            MultiByteToWideChar(CP_ACP, 0, lpszPathLink, -1,
                                wsz, MAX_PATH);

            hres = ppf->lpVtbl->Save(ppf, wsz, TRUE);
            ppf->lpVtbl->Release(ppf);
            if (SUCCEEDED(hres))
                fprintf(stderr,"Done.\n");
        }
        psl->lpVtbl->Release(psl);
    }
    CoUninitialize();
    return hres;
}

void createshortcut(char* errbuf, int errlen) {
    ITEMIDLIST *l;
    char p[MAX_PATH];
    char p2[MAX_PATH];
    char p3[MAX_PATH];
    int i;

    fprintf(stderr,"Getting desktop location.\n");
    if (SHGetSpecialFolderLocation(GetDesktopWindow(),
                                   CSIDL_DESKTOPDIRECTORY,&l)!=NOERROR)
        return;
    fprintf(stderr,"Converting...\n");
    if (SHGetPathFromIDList(l,p)!=TRUE) return;
    fprintf(stderr,"Desktop location is %s.\n",p);

    i=strlen(p);
    if (i>MAX_PATH-20) return;
    if (p[i-1]=='\\') p[i-1]=0;
    strcpy(p3,p);
    strcat(p3,"\\Ken's Labyrinth.lnk");

    fprintf(stderr,"Creating link as %s.\n",p3);

    GetCurrentDirectory(MAX_PATH,p2);

    i=strlen(p2);
    if (i>MAX_PATH-10) return;
    if (p2[i-1]=='\\') p2[i-1]=0;
    strcat(p2,"\\ken.exe");

    CreateLink(p2, p3,"Ken's Labyrinth","");

    strcpy(p3,p);
    strcat(p3,"\\Ken's Labyrinth Setup.lnk");

    CreateLink(p2, p3,"Ken's Labyrinth Setup","-setup");
}
#elif defined(linux)

#define HAVE_DESKTOP

#include <unistd.h>
#include <errno.h>

int makedirs(char* path) {
    struct stat st;
    char *s = path - 1;

    while (1) {
        while (*++s != '/') { if (!*s) return 0; }
        if (s == path)
            continue;

        *s = 0;

        if (stat(path, &st) != 0) {
            if (errno == ENOENT) {

                if (mkdir(path, 0755) != 0) {
                    *s = '/';
                    return errno;
                }
                *s = '/';
                continue;
            }
        }
        *s = '/';
        if (!S_ISDIR(st.st_mode)) {
            return ENOTDIR;
        }
    }
}

int create_desktop_file(char* path) {
    char *cwd;
    FILE* f;

    if (!(cwd = getcwd(NULL, 0)))
        return 0;

    fprintf(stderr, "Creating %s\n", path);
    f = fopen(path, "w");
    if (!f) {
        if (errno == ENOENT) {
            makedirs(path);
            f = fopen(path, "w");
        }
    }
    if (!f) {
        free(cwd);
        return 0;
    }

    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Categories=Game;ArcadeGame;\n");
    fprintf(f, "Encoding=UTF-8\n");
    fprintf(f, "Name=Ken's Labyrinth\n");
    fprintf(f, "Path=%s/\n", cwd);
    fprintf(f, "Exec=%s/ken\n", cwd);
    fprintf(f, "Icon=%s/ken.bmp\n", cwd);
    fprintf(f, "Type=Application\n");
    fclose(f);

    return 1;
}


void createshortcut(char* errbuf, int errlen) {
    char *home = getenv("HOME");
    char *deskpath;
    if (!home) {
        strncpy(errbuf, "could not get home dir", errlen);
        return;
    }
    deskpath = malloc(strlen(home) + 64);
    if (!deskpath)
        fatal_error("out of memory");

    sprintf(deskpath, "%s/Desktop/Ken's Labyrinth.desktop", home);
    create_desktop_file(deskpath);

    sprintf(deskpath, "%s/.local/share/applications/Ken's Labyrinth.desktop", home);
    create_desktop_file(deskpath);

    free(deskpath);
}
#endif

static int inputdevice=1, window_width=640, window_height=480, nearest=0;
static int music=1,sound=1,fullscr=1,cheat=0,channel=1,musicchannel=1;
static int soundblock=0,timing=0,texturedepth=1,scaling=2;
static int setup_ingame=0;

static char *keynames[ACTION_LAST]={
    "Move FORWARD",
    "Move BACKWARD",
    "Turn LEFT",
    "Turn RIGHT",
    "Move LEFT (strafe)",
    "Move RIGHT (strafe)",
    "STRAFE (walk sideways)",
    "STAND HIGH",
    "STAND LOW",
    "RUN",
    "FIRE",
    "CYCLE WEAPON",
    "FIREBALLS (red)",
    "BOUNCY-BULLETS (green)",
    "HEAT-SEEKING MISSILES",
    "UNLOCK / OPEN / CLOSE / USE",
    "CHEAT for more life",
    "RAISE / LOWER STATUS BAR",
    "PAUSE GAME",
    "MUTE KEY",
    "SHOW MENU"
};

static char* action_enum_names[ACTION_LAST] = {
    "forward",
    "backward",
    "left",
    "right",
    "moveleft",
    "moveright",
    "strafe",
    "standhigh",
    "standlow",
    "run",
    "fire",
    "nextweapon",
    "fireballs",
    "bouncy",
    "heat",
    "use",
    "cheat",
    "status",
    "pause",
    "mute",
    "menu",
    "menu_up1",
    "menu_up2",
    "menu_down1",
    "menu_down2",
    "menu_left1",
    "menu_left2",
    "menu_right1",
    "menu_right2",
    "menu_select1",
    "menu_select2",
    "menu_select3",
    "menu_cancel",
    "rewind",
    "advance"
};

static int action_key_default[ACTION_LAST]={
    /* ACTION_FORWARD      */  SDLK_UP,
    /* ACTION_BACKWARD     */  SDLK_DOWN,
    /* ACTION_LEFT         */  SDLK_LEFT,
    /* ACTION_RIGHT        */  SDLK_RIGHT,
    /* ACTION_MOVELEFT     */  SDLK_COMMA,
    /* ACTION_MOVERIGHT    */  SDLK_PERIOD,
    /* ACTION_STRAFE       */  SDLK_RCTRL,
    /* ACTION_STANDHIGH    */  SDLK_a,
    /* ACTION_STANDLOW     */  SDLK_z,
    /* ACTION_RUN          */  SDLK_LSHIFT,
    /* ACTION_FIRE         */  SDLK_LCTRL,
    /* ACTION_NEXTWEAPON   */  SDLK_TAB,
    /* ACTION_FIREBALLS    */  SDLK_F1,
    /* ACTION_BOUNCY       */  SDLK_F2,
    /* ACTION_HEAT         */  SDLK_F3,
    /* ACTION_USE          */  SDLK_SPACE,
    /* ACTION_CHEAT        */  SDLK_BACKSPACE,
    /* ACTION_STATUS       */  SDLK_RETURN,
    /* ACTION_PAUSE        */  SDLK_p,
    /* ACTION_MUTE         */  SDLK_m,
    /* ACTION_MENU         */  SDLK_ESCAPE,
    /* ACTION_MENU_UP1     */  SDLK_UP,
    /* ACTION_MENU_UP2     */  SDLK_KP_8,
    /* ACTION_MENU_DOWN1   */  SDLK_DOWN,
    /* ACTION_MENU_DOWN2   */  SDLK_KP_2,
    /* ACTION_MENU_LEFT1   */  SDLK_LEFT,
    /* ACTION_MENU_LEFT2   */  SDLK_KP_2,
    /* ACTION_MENU_RIGHT1  */  SDLK_RIGHT,
    /* ACTION_MENU_RIGHT2  */  SDLK_KP_6,
    /* ACTION_MENU_SELECT1 */  SDLK_RETURN,
    /* ACTION_MENU_SELECT2 */  SDLK_KP_ENTER,
    /* ACTION_MENU_SELECT3 */  SDLK_SPACE,
    /* ACTION_MENU_CANCEL  */  SDLK_ESCAPE,
    /* ACTION_REWIND       */  SDLK_r,
    /* ACTION_ADVANCE      */  SDLK_n
};

static int action_joystick_default[ACTION_LAST]={
    /* ACTION_FORWARD      */  1 | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_BACKWARD     */  1 | JOY_FLAG_AXIS,
    /* ACTION_LEFT         */  0 | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_RIGHT        */  0 | JOY_FLAG_AXIS,
    /* ACTION_MOVELEFT     */  ACTION_UNBOUND,
    /* ACTION_MOVERIGHT    */  ACTION_UNBOUND,
    /* ACTION_STRAFE       */  ACTION_UNBOUND,
    /* ACTION_STANDHIGH    */  ACTION_UNBOUND,
    /* ACTION_STANDLOW     */  ACTION_UNBOUND,
    /* ACTION_RUN          */  ACTION_UNBOUND,
    /* ACTION_FIRE         */  0,
    /* ACTION_NEXTWEAPON   */  2,
    /* ACTION_FIREBALLS    */  ACTION_UNBOUND,
    /* ACTION_BOUNCY       */  ACTION_UNBOUND,
    /* ACTION_HEAT         */  ACTION_UNBOUND,
    /* ACTION_USE          */  1,
    /* ACTION_CHEAT        */  ACTION_UNBOUND,
    /* ACTION_STATUS       */  ACTION_UNBOUND,
    /* ACTION_PAUSE        */  ACTION_UNBOUND,
    /* ACTION_MUTE         */  ACTION_UNBOUND,
    /* ACTION_MENU         */  3,
    /* ACTION_MENU_UP1     */  1 | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_MENU_UP2     */  ACTION_UNBOUND,
    /* ACTION_MENU_DOWN1   */  1 | JOY_FLAG_AXIS,
    /* ACTION_MENU_DOWN2   */  ACTION_UNBOUND,
    /* ACTION_MENU_LEFT1   */  0 | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_MENU_LEFT2   */  ACTION_UNBOUND,
    /* ACTION_MENU_RIGHT1  */  0 | JOY_FLAG_AXIS,
    /* ACTION_MENU_RIGHT2  */  ACTION_UNBOUND,
    /* ACTION_MENU_SELECT1 */  0,
    /* ACTION_MENU_SELECT2 */  ACTION_UNBOUND,
    /* ACTION_MENU_SELECT3 */  ACTION_UNBOUND,
    /* ACTION_MENU_CANCEL  */  1,
    /* ACTION_REWIND       */  ACTION_UNBOUND,
    /* ACTION_ADVANCE      */  ACTION_UNBOUND
};

static int action_controller_default[ACTION_LAST]={
    /* ACTION_FORWARD      */  SDL_CONTROLLER_AXIS_LEFTY | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_BACKWARD     */  SDL_CONTROLLER_AXIS_LEFTY | JOY_FLAG_AXIS,
    /* ACTION_LEFT         */  SDL_CONTROLLER_AXIS_RIGHTX | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_RIGHT        */  SDL_CONTROLLER_AXIS_RIGHTX | JOY_FLAG_AXIS,
    /* ACTION_MOVELEFT     */  SDL_CONTROLLER_AXIS_LEFTX | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_MOVERIGHT    */  SDL_CONTROLLER_AXIS_LEFTX | JOY_FLAG_AXIS,
    /* ACTION_STRAFE       */  ACTION_UNBOUND,
    /* ACTION_STANDHIGH    */  SDL_CONTROLLER_BUTTON_DPAD_UP,
    /* ACTION_STANDLOW     */  SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    /* ACTION_RUN          */  ACTION_UNBOUND,
    /* ACTION_FIRE         */  SDL_CONTROLLER_AXIS_TRIGGERRIGHT | JOY_FLAG_AXIS,
    /* ACTION_NEXTWEAPON   */  SDL_CONTROLLER_BUTTON_X,
    /* ACTION_FIREBALLS    */  ACTION_UNBOUND,
    /* ACTION_BOUNCY       */  ACTION_UNBOUND,
    /* ACTION_HEAT         */  ACTION_UNBOUND,
    /* ACTION_USE          */  SDL_CONTROLLER_BUTTON_A,
    /* ACTION_CHEAT        */  ACTION_UNBOUND,
    /* ACTION_STATUS       */  SDL_CONTROLLER_BUTTON_Y,
    /* ACTION_PAUSE        */  ACTION_UNBOUND,
    /* ACTION_MUTE         */  ACTION_UNBOUND,
    /* ACTION_MENU         */  SDL_CONTROLLER_BUTTON_START,
    /* ACTION_MENU_UP1     */  SDL_CONTROLLER_BUTTON_DPAD_UP,
    /* ACTION_MENU_UP2     */  SDL_CONTROLLER_AXIS_LEFTY | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_MENU_DOWN1   */  SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    /* ACTION_MENU_DOWN2   */  SDL_CONTROLLER_AXIS_LEFTY | JOY_FLAG_AXIS,
    /* ACTION_MENU_LEFT1   */  SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    /* ACTION_MENU_LEFT2   */  SDL_CONTROLLER_AXIS_LEFTX | JOY_FLAG_AXIS | JOY_FLAG_NEG,
    /* ACTION_MENU_RIGHT1  */  SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    /* ACTION_MENU_RIGHT2  */  SDL_CONTROLLER_AXIS_LEFTX | JOY_FLAG_AXIS,
    /* ACTION_MENU_SELECT1 */  SDL_CONTROLLER_BUTTON_A,
    /* ACTION_MENU_SELECT2 */  SDL_CONTROLLER_AXIS_TRIGGERRIGHT | JOY_FLAG_AXIS,
    /* ACTION_MENU_SELECT3 */  ACTION_UNBOUND,
    /* ACTION_MENU_CANCEL  */  SDL_CONTROLLER_BUTTON_B,
    /* ACTION_REWIND       */  SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    /* ACTION_ADVANCE      */  SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
};

static int action_group_movement[] = {
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
    -1
};

static int action_group_weapons[] = {
    ACTION_FIRE,
    ACTION_NEXTWEAPON,
    ACTION_FIREBALLS,
    ACTION_BOUNCY,
    ACTION_HEAT,
    -1
};
static int action_group_actions[] = {
    ACTION_USE,
    ACTION_CHEAT,
    ACTION_STATUS,
    ACTION_PAUSE,
    ACTION_MUTE,
    ACTION_MENU,
    -1
};

static char *okmenu[] = { "OK" };

static char *inputdevicemenu[4] = {
    "Keyboard only",
    "Keyboard + mouse",
    "Keyboard + joystick",
    "Keyboard + mouse + joystick"
};

static char *configureinputmenu[4] = {
    "Configure Keyboard",
    "Configure Joystick",
    "Configure Controller",
    "Return to setup menu"
};

static char *inputgroupsmenu[4] = {
    "Movement",
    "Weapons",
    "Actions",
    "Return to input menu"
};

static char *fullscreenmenu[2] = {
    "Windowed",
    "Fullscreen"
};

static char *filtermenu[3] = {
    "Anisotropic filtering",
    "Trilinear filtering",
    "No filtering"
};

static char *musicmenu[3] = {
    "No music",
    "Adlib emulation",
    "General MIDI",
};

static char *soundmenu[2] = {
    "No sound",
    "Digital sound effects"
};

static char *channelmenu[2] = {
    "Mono",
    "Stereo"
};

static char *cheatmenu[3] = {
    "No cheats",
    "LSHIFT-RSHIFT",
    "LSHIFT-LCTRL"
};

static char *soundblockmenu[10] = {
    "Default (11.6 ms)",
    "1.5 ms",
    "2.9 ms",
    "5.8 ms",
    "11.6 ms",
    "23.2 ms",
    "46.4 ms",
    "92.9 ms",
    "185.8 ms",
    "371.5 ms"
};

static char *timingmenu[2] = {
    "System timer",
    "Sound output"
};

static char *texturedepthmenu[3] = {
    "Driver default",
    "32 bit",
    "16 bit"
};

static char *scalingtypemenu[4] = {
    "Fill screen (4:3 view)",
    "Integer scale (4:3 view)",
    "Fill screen (square pixels)",
    "Integer scale (square pixels)"
};

static void makeupper(char* txt) {
    while (*txt) { *txt = toupper(*txt); txt++; }
}

int selectionmenu(int alts,char *titles[], int *value, const char* menutitle) {
    int i;
    int j=12*alts+24;
    int ofs = 0;
    if (menutitle) {
        ofs = 8;
        j += 16;
    }

    drawmenu(304,j,menu);
    if (menutitle) {
        strcpy(textbuf, menutitle);
        textprint(180-(strlen(textbuf)<<2), 112 - 6 * alts, 0);
    }
    for(i=0;i<alts;i++) {
        strcpy(textbuf,titles[i]);
        textprint(71,120 - 6 * alts + 12 * i + ofs, lab3dversion?32:34);
    }

    finalisemenu();

    if (j>240)
        i=getselection(28, 97 - 6*alts + ofs, *value, alts);
    else
        i=getselection(28, 99 - 6*alts + ofs, *value, alts);

    if (i>=0) *value=i;
    return i;
}
/*
int resolutionmenu(int alts,int start,char titles[][30],int def) {
    int i;
    int j=12*alts+24;
    char t[12];

    def-=start;
    if (def<0) def=0;
    if (def>=alts) def=0;

    drawmenu(304,j,menu);

    for(i=0;i<alts;i++) {
        strcpy(textbuf,titles[i]);
        textprint(71,120-6*alts+12*i,lab3dversion?32:34);
    }

    finalisemenu();

    i=getselection(28,99-6*alts,def,alts);

    if (i>=0) return i; else return -1;
}
*/
int getnumber(void) {
    int ch, uni;
    char buf[10];
    K_INT16 i,j;

    for(j=0;j<12;j++)
        textbuf[j] = 8;
    textbuf[12] = 0;
    textprint(94,145+1,(char)0);
    j = 0;
    buf[0]=0;
    ch = 0;
    SDL_StartTextInput();
    while ((ch != 13) && (ch != 27))
    {
        while ((uni=getkeypress(&ch)) == 0)
        {
            textbuf[0] = 95;
            textbuf[1] = 0;
            textprint(94+(j<<3),145,(char)97);
            glFlush();
            SDL_Delay(10); /* Just to avoid soaking all CPU. */
            textbuf[0] = 8;
            textbuf[1] = 0;
            textprint(94+(j<<3),145,(char)0);
            glFlush();
            SDL_Delay(10); /* Just to avoid soaking all CPU. */
        }
        if (uni==1) {
            if (ch == SDLK_DELETE)
            {
                buf[j] = ch;
                for(j=0;j<10;j++)
                    buf[j] = 0;
                for(j=0;j<12;j++)
                    textbuf[j] = 8;
                textbuf[12] = 0;
                textprint(94,145+1,(char)0);
                j = 0;
                ch = 0;
            }
            if ((ch == SDLK_BACKSPACE) && (j > 0))
            {
                j--, buf[j] = 0;
                textbuf[0] = ch;
                textbuf[1] = 0;
                textprint(94+(j<<3),145+1,(char)0);
            }
        } else {
            if ((ch >= 48) && (ch <= 57) && (j < 4))
            {
                textbuf[0] = ch;
                textbuf[1] = 0;
                textprint(94+(j<<3),145+1,(char)97);
                buf[j] = ch;
                if ((ch != 32) || (j > 0))
                    j++;
            }
        }
    }
    SDL_StopTextInput();
    for(i=0;i<256;i++)
        keystatus[i] = 0;
    if (ch==27) return -1;
    return strtol(buf,NULL,10);
}

void customresolution(void) {
    int x,y;

    drawinputbox();
    finalisemenu();
    sprintf(&textbuf[0],"Enter screen width:");
    textprint(180-(strlen(textbuf)<<2),135+1,(char)161);
    x=getnumber();
    if (x>0) {
        drawinputbox();
        finalisemenu();
        sprintf(&textbuf[0],"Enter screen height:");
        textprint(180-(strlen(textbuf)<<2),135+1,(char)161);
        y=getnumber();
        if (y>0) {
            window_width = x;
            window_height = y;
        }
    }
}

void setupinputdevices(void) {
    selectionmenu(4, inputdevicemenu, &inputdevice, "Select input devices");
}

void setupsetfullscreen(void) {
    selectionmenu(2, fullscreenmenu, &fullscr, NULL);
}

void setupsetfiltering(void) {
    selectionmenu(3,filtermenu,&nearest, "Filtering");
}

void setupsetmusic(void) {
    selectionmenu(MUSIC_SOURCES,musicmenu,&music, "Music");
}

void setupsetsound(void) {
    selectionmenu(2,soundmenu,&sound, "Sound");
}

void setupcheatmenu(void) {
    selectionmenu(3,cheatmenu,&cheat, "Cheat keys");
}

void setupsetsoundchannels(void) {
    selectionmenu(2,channelmenu,&channel, "Sound channels");
}

void setupsetmusicchannels(void) {
    selectionmenu(2,channelmenu,&musicchannel, "Music channels");
}

void setupsoundblockmenu(void) {
    selectionmenu(10,soundblockmenu,&soundblock, "Sound buffer");
}

void setuptimingmenu(void) {
    selectionmenu(2,timingmenu,&timing, "Timing");
}

void setuptexturedepthmenu(void) {
    selectionmenu(3,texturedepthmenu,&texturedepth, "Texture depth");
}

void setupscalingmodemenu(void) {
    selectionmenu(4,scalingtypemenu,&scaling, "Scaling");
}

typedef struct {
    const char* title;
    void(*get_action_name)(char* txt, int len, int action);
    int (*select)(SDL_Event* e, int action);
    void (*draw_instructions)(const char* txt);
} input_configuration_method;

static void key_get_action_name(char* txt, int len, int action) {
    strncpy(txt, SDL_GetKeyName(action_key[action]), len);
}

static int key_select(SDL_Event* event, int action) {
    switch(event->type) {
        case SDL_KEYDOWN:
            action_key[action] = event->key.keysym.sym;
            return 1;
        default:
            break;
    }
    return 0;
}

static void joy_get_action_name(char* txt, int len, int action) {
    int def = action_joystick[action];
    if (def == ACTION_UNBOUND) {
        strncpy(txt, "None", len);
    } else if (def & JOY_FLAG_AXIS) {
        *txt++ = def & JOY_FLAG_NEG ? '-' : '+';
        def &= JOY_MASK;
        if (def == 0) {
            strncpy(txt, "X", len - 1);
        } else if (def == 1) {
            strncpy(txt, "Y", len - 1);
        } else {
            snprintf(txt, len-1, "A%d", def);
        }
    } else {
        snprintf(txt, len, "Button %d", def & JOY_MASK);
    }
}

static int joy_select(SDL_Event* event, int action) {
    switch(event->type) {
        case SDL_JOYBUTTONDOWN:
            if (event->jbutton.which == cur_joystick_index) {
                action_joystick[action] = event->jbutton.button;
                return 1;
            }
            break;
        case SDL_JOYAXISMOTION:
            if (event->jaxis.which == cur_joystick_index) {
                if (abs(event->jaxis.value) > 24000) {
                    int ax = event->jaxis.axis | JOY_FLAG_AXIS;
                    if (event->jaxis.value < 0) {
                        ax |= JOY_FLAG_NEG;
                    }
                    action_joystick[action] = ax;
                    return 1;
                }
            }
            break;
        case SDL_KEYDOWN:
            if (event->key.keysym.sym == SDLK_BACKSPACE) {
                action_joystick[action] = ACTION_UNBOUND;
            }
            return 1;
        default:
            break;
    }
    return 0;
}

static void ctrl_get_action_name(char* txt, int len, int action) {
    int def = action_controller[action];
    const char* cptext;
    if (def == ACTION_UNBOUND) {
        strncpy(txt, "None", len);
    } else if (def & JOY_FLAG_AXIS) {
        *txt++ = def & JOY_FLAG_NEG ? '-' : '+';
        def &= JOY_MASK;
        cptext = SDL_GameControllerGetStringForAxis(def);
        if (cptext) {
            strncpy(txt, cptext, len);
            makeupper(txt);
        } else {
            snprintf(txt, len, "Axis %d", def);
        }
    } else {
        cptext = SDL_GameControllerGetStringForButton(def);
        if (cptext) {
            strncpy(txt, cptext, len);
            makeupper(txt);
        } else {
            snprintf(txt, len, "Button %d", def);
        }
    }
}

static int ctrl_select(SDL_Event* event, int action) {
    switch(event->type) {
        case SDL_CONTROLLERBUTTONDOWN:
            if (event->jbutton.which == cur_controller_index) {
                action_controller[action] = event->cbutton.button;
                return 1;
            }
            break;
        case SDL_CONTROLLERAXISMOTION:
            if (event->caxis.which == cur_controller_index) {
                if (abs(event->caxis.value) > 12000) {
                    int ax = event->caxis.axis | JOY_FLAG_AXIS;
                    if (event->caxis.value < 0) {
                        ax |= JOY_FLAG_NEG;
                    }
                    action_controller[action] = ax;
                    return 1;
                }
            }
            break;
        case SDL_KEYDOWN:
            if (event->key.keysym.sym == SDLK_BACKSPACE) {
                action_controller[action] = ACTION_UNBOUND;
            }
            return 1;
        default:
            break;
    }
    return 0;
}

static void key_instruction(const char* inst) {
    drawmenu(304,48,menu);
    strcpy(textbuf,"Press key for action:");
    textprint((360-(8*strlen(textbuf)))/2,((240-48)/2)+12+0*12,lab3dversion?32:34);
    strcpy(textbuf,inst);
    textprint((360-(8*strlen(textbuf)))/2,((240-48)/2)+12+1*12,0);
    finalisemenu();
    glFlush();
}

static void joy_instruction(const char* inst) {
    drawmenu(304,72,menu);
    strcpy(textbuf,"Select button or axis for");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+0*12,lab3dversion?32:34);
    strcpy(textbuf,inst);
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+1*12,0);
    strcpy(textbuf,"or press any key to cancel;");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+2*12,lab3dversion?32:34);

    strcpy(textbuf,"press BACKSPACE to delete");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+3*12,lab3dversion?32:34);
    finalisemenu();
    glFlush();
}

static void ctrl_instruction(const char* inst) {
    drawmenu(304,72,menu);
    strcpy(textbuf,"Select button or stick for");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+0*12,lab3dversion?32:34);
    strcpy(textbuf,inst);
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+1*12,0);
    strcpy(textbuf,"or press any key to cancel;");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+2*12,lab3dversion?32:34);

    strcpy(textbuf,"press BACKSPACE to delete");
    textprint((360-(8*strlen(textbuf)))/2,((240-72)/2)+12+3*12,lab3dversion?32:34);
    finalisemenu();
    glFlush();
}

input_configuration_method icm_key = { "Configure Keyboard", key_get_action_name, key_select, key_instruction };
input_configuration_method icm_joy = { "Configure Joystick", joy_get_action_name, joy_select, joy_instruction };
input_configuration_method icm_ctrl = { "Configure Controller", ctrl_get_action_name, ctrl_select, ctrl_instruction };

void setupsetinputgroup(int *group, input_configuration_method* meth) {
    int i = 0, j, quitf = 0;
    SDL_Event event;
    int cnt;

    while(!quitf) {
        drawmenu(360, 240, menu);
        cnt = 0;
        for(j = 0; group[j] != -1; j++) {
            strcpy(textbuf, keynames[group[j]]);
            textprint(31, 13 + 12 * j, lab3dversion ? 32 : 34);
            meth->get_action_name(textbuf, 11, group[j]);
            textbuf[11] = 0;
            textprint(261, 13 + 12 * j, lab3dversion ? 32 : 34);
            cnt++;
        }
        strcpy(textbuf, "Return");
        textprint(31, 13 + 12 * j, lab3dversion ? 32 : 34);

        strcpy(textbuf,"Use cursor keys / left stick to select.");
        textprint(23,208,lab3dversion?32:34);

        strcpy(textbuf,"Enter/A btn to change, ESC/B to return.");
        textprint(23,220,lab3dversion?32:34);
        finalisemenu();
        i = getselection(-12, -9, i, cnt + 1);
        if (i<0) quitf=1;
        else if (i>=cnt) quitf=1;
        else {
            meth->draw_instructions(keynames[group[i]]);
            j = 1;
            while(j) {
                while(SDL_PollEvent(&event))
                {
                    ProcessEvent(&event);
                    if (quitgame) quit();
                    if (meth->select(&event, group[i])) {
                        j = 0;
                        break;
                    }
                }
                SDL_Delay(10);
            }
        }
    }
    draw_mainmenu();
}

void setupsetinput(input_configuration_method* meth) {
    int a=0, quit=0;
    while (!quit) {
        a = selectionmenu(4,inputgroupsmenu,&a, meth->title);
        switch(a) {
            case 0:
                setupsetinputgroup(action_group_movement, meth);
                break;
            case 1:
                setupsetinputgroup(action_group_weapons, meth);
                break;
            case 2:
                setupsetinputgroup(action_group_actions, meth);
                break;
            default:
                quit=1;
                break;
        }
    }

}

void setupconfigureinput(void) {
    int a=0, quit=0;
    while (!quit) {
        a = selectionmenu(4,configureinputmenu,&a, "Configure input devices");
        switch(a) {
            case 0:
                setupsetinput(&icm_key);
                break;
            case 1:
                setupsetinput(&icm_joy);
                break;
            case 2:
                setupsetinput(&icm_ctrl);
                break;
            default:
                quit=1;
                break;
        }
    }
}

static void draw_mainmenu(void) {
    int n = 18;
    drawmenu(360,240,menu);

    if (setup_ingame) {
        n = 6;
    } else {
        n = 18;
        strcpy(textbuf,"LAB3D/SDL setup menu");
        textprint(81,16,126);
    }

    strcpy(textbuf,"Input: ");
    strcat(textbuf,inputdevicemenu[inputdevice]);
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Configure Input");
    n += 12; textprint(51,n,lab3dversion?32:34);
    sprintf(textbuf,"Window size: %dx%d", window_width,
            window_height);
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Display type: ");
    strcat(textbuf,fullscreenmenu[fullscr]);
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Filtering: ");
    strcat(textbuf,filtermenu[nearest]);
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Music: ");
    strcat(textbuf,musicmenu[music]);
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Effects: ");
    strcat(textbuf,soundmenu[sound]);
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Sound channels: ");
    strcat(textbuf,channelmenu[channel]);
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Music channels: ");
    strcat(textbuf,channelmenu[musicchannel]);
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Cheats: ");
    strcat(textbuf,cheatmenu[cheat]);
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Sound block size: ");
    strcat(textbuf,soundblockmenu[soundblock]);
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Texture colour depth: ");
    strcat(textbuf,texturedepthmenu[texturedepth]);
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"View: ");
    strcat(textbuf,scalingtypemenu[scaling]);
    n += 12; textprint(51,n,lab3dversion?32:34);
#ifdef HAVE_DESKTOP
    n += 12; strcpy(textbuf,"Create desktop shortcuts");
    textprint(51,n,96);
#endif
    if (setup_ingame)
        strcpy(textbuf,"Return");
    else
        strcpy(textbuf,"Exit setup");

    n += 12; textprint(51,n,lab3dversion?128:130);

    n = 220;
    if (setup_ingame) {
        strcpy(textbuf,"Some settings may require restart.");
        textprint(31,n,lab3dversion?32:34);
    } else {
        strcpy(textbuf,"Use cursor keys / left stick to select.");
        textprint(31,n,lab3dversion?32:34);
    }

    finalisemenu();
}

void setupmenu(int ingame) {
    int quit=0,sel=0,j;
    char errbuf[64];
    setup_ingame = ingame;

    while(!quit) {
        draw_mainmenu();
#ifdef HAVE_DESKTOP
        if ((sel = getselection(12,7 + (ingame ? -12 : 0),sel,15)) < 0)
#else
        if ((sel = getselection(12,7 + (ingame ? -12 : 0),sel,14)) < 0)
#endif
                quit=1;
            else {
                switch(sel) {
                    case 0:
                        setupinputdevices();
                        break;
                    case 1:
                        setupconfigureinput();
                        break;
                    case 2:
                        customresolution();
                        break;
                    case 3:
                        setupsetfullscreen();
                        break;
                    case 4:
                        setupsetfiltering();
                        break;
                    case 5:
                        setupsetmusic();
                        break;
                    case 6:
                        setupsetsound();
                        break;
                    case 7:
                        setupsetsoundchannels();
                        break;
                    case 8:
                        setupsetmusicchannels();
                        break;
                    case 9:
                        setupcheatmenu();
                        break;
                    case 10:
                        setupsoundblockmenu();
                        break;
                    case 11:
                        setuptexturedepthmenu();
                        break;
                    case 12:
                        setupscalingmodemenu();
                        break;
                    case 13:
#ifdef HAVE_DESKTOP
                        errbuf[0] = 0;
                        createshortcut(errbuf, sizeof(errbuf) - 1);
                        if (!errbuf[0]) {
                            strcpy(errbuf, "Shortcuts created.");
                        }
                        j = 0;
                        selectionmenu(1, okmenu, &j, errbuf);
                        break;
                    case 14:
#endif
                        quit=1;
                        break;
                }
            }
    }
}

void configure(void) {
    window_in_focus = 1;

    screenwidth = window_width;
    screenheight = window_height;
    fullscreen = fullscr;
    fullfilter = nearest == 2 ? GL_NEAREST : GL_LINEAR_MIPMAP_LINEAR;
    partialfilter=nearest == 2 ? GL_NEAREST : GL_LINEAR;

    anisotropic = (nearest==0);

    speechstatus = sound?2:0;
    switch(music) {
        case 2:
            musicsource=1;
            break;
        case 1:
            musicsource=2;
            break;
        default:
            musicsource=-1;
            break;
    }
    moustat = ((3-inputdevice)&1);
    joyenable = inputdevice & 2 ? 1 : 0;

    cheatenable=cheat;
    if (channel||musicchannel)
        channels=2;
    else
        channels=1;

    soundpan=channel;
    musicpan=musicchannel;

    soundblocksize=/* channels* */
        ((musicsource==2)?SOUNDBLOCKSIZE44KHZ:SOUNDBLOCKSIZE11KHZ);
    if (soundblock>0) {
        soundblocksize>>=4;
        soundblocksize<<=soundblock;
    }
    soundtimer=0;
    switch(texturedepth) {
        case 1:
            colourformat=GL_RGBA8;
            break;
        case 2:
            colourformat=GL_RGBA4;
            break;
        default:
            colourformat=GL_RGBA;
    }
}

void configure_screen_size(void) {
    int div1,div2;
    aspw=1.0;
    asph=1.0;
    switch(scaling) {
        case 1:
        case 3:
            div1=screenwidth/320;
            div2=screenheight/200;

            if (div2<div1) div1=div2;
            if (div1<1) {
                fprintf(stderr,
                        "Warning: resolution must be 320x200 or more"
                        " for integer scaling.\n");
                virtualscreenwidth=360;
                virtualscreenheight=240;
            } else {
                virtualscreenwidth=screenwidth/div1;
                virtualscreenheight=screenheight/div1;
            }
            if (scaling==3) {
                if (screenwidth*3>screenheight*4)
                    aspw=((((double)screenwidth*3.0))/(((double)screenheight)*4.0));
                else
                    asph=((((double)screenheight)*4.0))/(((double)screenwidth*3.0));
    	    }
            break;
        case 0:
            virtualscreenwidth=360;
            virtualscreenheight=240;
            break;
        default:
            if (screenwidth*3>screenheight*4) {
                aspw=((((double)screenwidth*3.0))/(((double)screenheight)*4.0));
                virtualscreenwidth=360.0*aspw;
                virtualscreenheight=240;
    	    } else {
                asph=((((double)screenheight)*4.0))/(((double)screenwidth*3.0));
                virtualscreenwidth=360;
                virtualscreenheight=240.0*asph;
    	    }
    }
}

void load_default_settings(void) {
    int i;
    channels=2; musicvolume=64; soundvolume=64; gammalevel=1.0;
    i=0;

    for(i=0;i<ACTION_LAST;i++) {
        action_key[i] = action_key_default[i];
        action_joystick[i] = action_joystick_default[i];
        action_controller[i] = action_controller_default[i];
    }

    if (lab3dversion) {
        action_key[ACTION_OLD_LOAD]=SDLK_l;
        action_key[ACTION_OLD_SAVE]=SDLK_s;
    }
}

typedef struct {
    FILE* input;
    char buf[256];
} loadsettings_data;

typedef struct _setting_t {
    const char* name;
    int(*load)(const char* key, char* val, struct _setting_t* set);
    int(*save)(const char* key, FILE* f, struct _setting_t* set);
    void *p1, *p2;
    int i1, i2;
} setting_t;

typedef struct {
    const char* name;
    setting_t* settings;
} setting_section_t;

static int _parse_int(const char *svalue, int* p) {
    char* endptr;
    long val;
    val = strtol(svalue, &endptr, 0);
    if (*svalue == 0 || *endptr != 0) {
        return 1;
    }
    *p = val;
    return 0;
}

static int _load_int(const char* key, char* svalue, setting_t* set) {
    return _parse_int(svalue, (int*)set->p1);
}

static int _save_int(const char* key, FILE* f, setting_t* set) {
    fprintf(f, "%s = %d\n", key, *((int*)set->p1));
    return 0;
}

static int _load_float(const char* key, char* svalue, setting_t* set) {
    char* endptr;
    double val;
    val = strtod(svalue, &endptr);
    if (*svalue == 0 || *endptr != 0) {
        return 1;
    }
    *((double*)set->p1) = val;
    return 0;
}

static int _save_float(const char* key, FILE* f, setting_t* set) {
    fprintf(f, "%s = %f\n", key, *((double*)set->p1));
    return 0;
}

static int _load_enum(const char* key, char* svalue, setting_t* set) {
    int val = get_enum(svalue, (enumpair*)set->p2);
    if (val == -1) {
        return _parse_int(svalue, (int*)set->p1);
    }
    *((int*)set->p1) = val;
    return 0;
}

static int _save_enum(const char* key, FILE* f, setting_t* set) {
    enumpair* cur = (enumpair*)set->p2;
    while (cur->name) {
        if (cur->value == *((int*)set->p1)) {
            fprintf(f, "%s = %s\n", key, cur->name);
            return 0;
        }
        cur++;
    }
    return _save_int(key, f, set);
}

static int _string_or_int(char* val, char** strval, int* ival) {
    char *end = val + strlen(val) - 1;
    if (end > val && *val == '"' && *end == '"') {
        *end-- = 0;
        *strval = val + 1;
        return 1;
    } else if (_parse_int(val, ival) == 0) {
        return 2;
    }
    return 0;
}

static int _load_key(const char* key, char* val, setting_t* set) {
    char *strval;
    int keycode;
    int action = set->i1;
    switch (_string_or_int(val, &strval, &keycode)) {
        case 0:
            return 1;
        case 1:
            keycode = SDL_GetKeyFromName(strval);
            if (keycode == SDLK_UNKNOWN) keycode = -1;
            break;
        case 2:
            break;
    }
    action_key[action] = keycode;
    return 0;
}

static int _save_key(const char* key, FILE* f, setting_t* set) {
    int action = set->i1;
    int keycode = action_key[action];
    const char *txt;
    if (keycode == -1) {
        txt = "";
    } else {
        txt = SDL_GetKeyName(keycode);
        if (!*txt) {
            fprintf(f, "%s = %d\n", key, keycode);
            return 0;
        }
    }
    fprintf(f, "%s = \"%s\"\n", key, txt);
    return 0;
}

static int _load_joyaction(const char* key, char* val, setting_t* set) {
    char *strval;
    int flags = 0, which = 0, aval = ACTION_UNBOUND;
    int action = set->i1;
    int controller = set->i2;
    switch (_string_or_int(val, &strval, &which)) {
        case 0:
            return 1;
        case 1:
            if (*strval) {
                if (toupper(strval[0]) == 'A') {
                    if (strval[1] == '+') {
                        flags = JOY_FLAG_AXIS;
                    } else if (strval[1] == '-') {
                        flags = JOY_FLAG_AXIS | JOY_FLAG_NEG;
                    }
                }
                if (flags) {
                    which = SDL_CONTROLLER_AXIS_INVALID;
                    if (controller)
                        which = SDL_GameControllerGetAxisFromString(strval + 2);
                    if (which == SDL_CONTROLLER_AXIS_INVALID)
                        if (_parse_int(strval + 2, &which) != 0)
                            return 1;
                } else {
                    which = SDL_CONTROLLER_BUTTON_INVALID;
                    if (controller)
                        which = SDL_GameControllerGetButtonFromString(strval);
                    if (which == SDL_CONTROLLER_BUTTON_INVALID)
                        if (_parse_int(strval, &which) != 0)
                            return 1;
                }
                if (which & ~JOY_MASK)
                    return 1;

                aval = flags | which;
            }

            break;
        case 2:
            break;
    }
    if (controller) {
        action_controller[action] = aval;
    } else {
        action_joystick[action] = aval;
    }
    return 0;
}

static int _save_joyaction(const char* key, FILE* f, setting_t* set) {
    int action = set->i1;
    int controller = set->i2;
    int val, which;
    const char *txt = NULL;
    char txtbuf[32];

    val = controller ? action_controller[action] : action_joystick[action];
    which = val & JOY_MASK;

    if (val == ACTION_UNBOUND) {
        strcpy(txtbuf, "\"\"");
    } else {
        if (val & JOY_FLAG_AXIS) {
            char neg = val & JOY_FLAG_NEG ? '-' : '+';
            if (controller) {
                txt = SDL_GameControllerGetStringForAxis(which);
                if (txt && *txt) {
                    snprintf(txtbuf, 32, "\"A%c%s\"", neg, txt);
                }
            }
            if (!txt || !*txt) {
                snprintf(txtbuf, 32, "\"A%c%d\"", neg, which);
            }
        } else {
            if (controller) {
                txt = SDL_GameControllerGetStringForButton(which);
                if (txt && *txt) {
                    snprintf(txtbuf, 32, "\"%s\"", txt);
                }
            }
            if (!txt || !*txt) {
                snprintf(txtbuf, 32, "\"%d\"", which);
            }
        }
    }
    makeupper(txtbuf);
    fprintf(f, "%s = %s\n", key, txtbuf);
    return 0;
}


static int _save_blankline(const char* key, FILE* f, setting_t* set) {
    fprintf(f, "\n");
    return 0;
}

#define INTSETTING(name, gvar) { #name, _load_int, _save_int, &gvar }
#define XINTSETTING(name, gvar) { #name, _load_int, NULL, &gvar }
#define FLOATSETTING(name, gvar) { #name, _load_float, _save_float, &gvar }
#define ENUMSETTING(name, values, gvar) { #name, _load_enum, _save_enum, &gvar, values }

static setting_t video_settings[] = {
    INTSETTING(fullscreen, fullscr),
    INTSETTING(width, window_width),
    INTSETTING(height, window_height),
    INTSETTING(filtering, nearest),
    INTSETTING(scaling, scaling),
    FLOATSETTING(brightness, gammalevel),
    INTSETTING(texturedepth, texturedepth),
    { NULL }
};

static setting_t sound_settings[] = {
    INTSETTING(enable, sound),
    INTSETTING(volume, soundvolume),
    INTSETTING(stereo, channel),
    INTSETTING(buffer, soundblock),
    { NULL }
};

static setting_t music_settings[] = {
    INTSETTING(enable, music),
    INTSETTING(volume, musicvolume),
    INTSETTING(stereo, musicchannel),
    { NULL }
};

static setting_t input_settings[] = {
    INTSETTING(inputdevice, inputdevice),
    INTSETTING(cheatkeymode, cheat),
    { NULL }
};

static setting_t key_settings[ACTION_LAST+1];
static setting_t joystick_settings[ACTION_LAST+1];
static setting_t controller_settings[ACTION_LAST+1];

static setting_section_t sections[] = {
    { "Video", video_settings },
    { "Sound", sound_settings },
    { "Music", music_settings },
    { "Input", input_settings },
    { "Keyboard", key_settings },
    { "Joystick", joystick_settings },
    { "Controller", controller_settings },
    { NULL }
};

void loadsettings(void) {
    FILE* input;
    int newformat = 0;
    char buf[256];
    char *key, *val;
    int curline = 0;
    #ifdef __SWITCH__
    const char* filename = "settings_switch.ini";
    #else
    const char* filename = "settings.ini";
    #endif

    setting_section_t* cursection = NULL;
    setting_t* cursetting = NULL;

    load_default_settings();

    input = fopen(filename, "r");

    if (input == NULL)
        setup();

    if (!key_settings[0].name) {
        int i;
        for (i = 0; i < ACTION_LAST; i++) {
            key_settings[i].name = joystick_settings[i].name =
                controller_settings[i].name = action_enum_names[i];
            key_settings[i].i1 = joystick_settings[i].i1 =
                controller_settings[i].i1 = i;
            key_settings[i].load = _load_key;
            key_settings[i].save = _save_key;
            joystick_settings[i].load = _load_joyaction;
            joystick_settings[i].save = _save_joyaction;
            controller_settings[i].load = _load_joyaction;
            controller_settings[i].save = _save_joyaction;
            controller_settings[i].i2 = 1;
        }
    }
    while (read_ini(input, buf, sizeof(buf), &key, &val, &curline)) {
        if (!val) {
            newformat = 1;
            cursection = sections;
            while (1) {
                if (!cursection->name) {
                    fatal_error("%s:%d: Invalid config section: %s", filename, curline, key);
                    return;
                }
                if (strcasecmp(key, cursection->name) == 0)
                    break;
                cursection++;
            }
        } else {
            if (!cursection) {
                fatal_error("%s:%d: Config value outside of section: %s", filename, curline, key);
            }
            cursetting = cursection->settings;
            while (1) {
                if (!cursetting->name) {
                    fatal_error("%s:%d: Unknown config setting in section %s: %s", filename, curline, cursection->name, key);
                }
                if (strcasecmp(key, cursetting->name) == 0) {
                    int rv = cursetting->load(key, val, cursetting);
                    if (rv != 0) {
                        fatal_error("%s:%d: Invalid value for %s: %s", filename, curline, cursetting->name, val);
                    }
                    break;
                }
                cursetting++;
            }
        }
    }
    if (!newformat) {
        setup();
    }
    fclose(input);
}

void savesettings(void) {
    FILE *output = fopen("settings.ini", "w");

    setting_section_t* cursection = sections;
    setting_t* cursetting = NULL;

    if (output == NULL) return;

    for (cursection = sections; cursection->name; cursection++) {
        fprintf(output, "[%s]\n", cursection->name);
        for (cursetting = cursection->settings; cursetting->name; cursetting++) {
            if (cursetting->save) {
                cursetting->save(cursetting->name, output, cursetting);
            }
        }
        fprintf(output, "\n");
    }

    fclose(output);
}

void setup(void) {
    K_INT16 i, j, k, walcounter;
    K_UINT16 l;
    char *v;
    SDL_Surface *icon;
    SDL_Rect displaybounds;

    configure();
    statusbaryoffset=250;

    /* Display accuracy not important in setup... */

    SDL_Init(SDL_INIT_TIMER|SDL_INIT_VIDEO|
             SDL_INIT_JOYSTICK|SDL_INIT_GAMECONTROLLER);

    SDL_GetDisplayBounds(0, &displaybounds);

    SDL_JoystickEventState(1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,5);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,5);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE,0);
    SDL_ShowCursor(0);

    fprintf(stderr,"Activating video...\n");

    if (displaybounds.w >= 800 && displaybounds.h >= 640) {
        screenwidth=720; screenheight=480;
    } else {
        screenwidth=360; screenheight=240;
    }

    icon = SDL_LoadBMP("ken.bmp");
    if (icon == NULL) {
        fprintf(stderr,"Warning: ken.bmp (icon file) not found.\n");
    }

    if (mainwindow != NULL)
        fatal_error("window already created (setup)");
    mainwindow = SDL_CreateWindow("Ken's Labyrinth Setup", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  screenwidth, screenheight, SDL_WINDOW_OPENGL);

    if (mainwindow == NULL) {
        fatal_error("Video mode set failed.");
    }

    SDL_GetWindowSize(mainwindow, &screenwidth, &screenheight);

    if (icon != NULL)
        SDL_SetWindowIcon(mainwindow, icon);

    maincontext = SDL_GL_CreateContext(mainwindow);

    if (maincontext == NULL) {
        fatal_error("Could not create GL context.");
    }

    SDL_SetWindowBrightness(mainwindow, 1.0); /* Zap gamma correction. */

    virtualscreenwidth=360;
    virtualscreenheight=240;

    largescreentexture=0;

    if (largescreentexture) {
        /* One large 512x512 texture. */

        screenbufferwidth=screenbufferheight=512;
    } else {
        /* 6*11 matrix of 64x64 tiles with 1 pixel wide borders on shared
           edges. */

        screenbufferwidth=374;
        screenbufferheight=746;
    }

    screenbuffer=malloc(screenbufferwidth*screenbufferheight);
    screenbuffer32=malloc(screenbufferwidth*screenbufferheight*4);

    linecompare(479);

    if (screenbuffer==NULL) {
        fprintf(stderr,"Insufficient memory.\n");
        SDL_Quit();
        exit(-1);
    }

    fprintf(stderr,"Loading configuration file...\n");

    loadtables();
    fullfilter=partialfilter=GL_NEAREST;
    vidmode = 1;
    mute = 0;
    moustat = 1;
    joyenable = 1;

    musicsource=-1;
    speechstatus=0;

    fprintf(stderr,"Allocating memory...\n");
    if (((lzwbuf = malloc(12304-8200)) == NULL)||
        ((lzwbuf2=malloc(8200))==NULL))
    {
        fprintf(stderr,"Error #3: Memory allocation failed.\n");
        SDL_Quit();
        exit(-1);
    }

    convwalls = numwalls;

    if ((pic = malloc((numwalls-initialwalls)<<12)) == NULL)
    {
        fprintf(stderr,
                "Error #4: This computer does not have enough memory.\n");
        SDL_Quit();
        exit(-1);
    }
    walcounter = initialwalls;
    if (convwalls > initialwalls)
    {
        v = pic;
        for(i=0;i<convwalls-initialwalls;i++)
        {
            walseg[walcounter] = v;
            walcounter++;
            v += 4096;
        }
    }
    l = 0;
    for(i=0;i<240;i++)
    {
        times90[i] = l;
        l += 90;
    }
    less64inc[0] = 16384;
    for(i=1;i<64;i++)
        less64inc[i] = 16384 / i;
    for(i=0;i<256;i++)
        keystatus[i] = 0;

    cur_joystick_index = -1;
    cur_controller_index = -1;

    FindJoysticks();

    if (largescreentexture) {
        glGenTextures(1,&screenbuffertexture);
    } else {
        glGenTextures(72,screenbuffertextures);
    }

    saidwelcome = 0;
    fprintf(stderr,"Loading intro pictures...\n");

    if (lab3dversion) {
        kgif(-1);
        k=0;
        for(i=0;i<16;i++)
            for(j=1;j<17;j++)
            {
                spritepalette[k++] = (opaldef[i][0]*j)/17;
                spritepalette[k++] = (opaldef[i][1]*j)/17;
                spritepalette[k++] = (opaldef[i][2]*j)/17;
            }
        fprintf(stderr,"Loading old graphics...\n");
        loadwalls(0);
        fade(63);
        k=0;
        for(i=0;i<16;i++)
            for(j=1;j<17;j++)
            {
                palette[k++] = (opaldef[i][0]*j)/17;
                palette[k++] = (opaldef[i][1]*j)/17;
                palette[k++] = (opaldef[i][2]*j)/17;
            }
        settransferpalette();
        keynames[ACTION_OLD_LOAD] = "LOAD game";
        keynames[ACTION_OLD_SAVE] = "SAVE game";

    } else {
        /* The ingame palette is stored in this GIF! */
        kgif(1);
        memcpy(spritepalette,palette,768);

        kgif(0);
        settransferpalette();
        fprintf(stderr,"Loading graphics...\n");
        loadwalls(0);

        kgif(1);
        fade(63);
    }
    glDrawBuffer(GL_FRONT);

    setupmenu(0);

    savesettings();
    SDL_Quit();
    exit(0);
}
