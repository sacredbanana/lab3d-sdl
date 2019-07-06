#include "lab3d.h"
#include "adlibemu.h"

/* Size of sounds.kzp should be this in normal cases. */
// SND_FILE_LENGTH (lab3dversion==2?196423:(lab3dversion?309037:294352))

/* Initialise the game: set up most I/O and data... */

void initialize()
{
    K_INT16 i, j, k, oclockspeed;
    K_UINT16 l;
    struct stat fstats;

    statusbaryoffset=250;
    spriteyoffset=0;
    ingame=0;
    inlauncher =0;
    mixing=0;
    menuing=0;
    g_stereo_sep = 768;

    visiblescreenyoffset=0;

    cur_joystick_index = -1;
    cur_controller_index = -1;

    FindJoysticks();

    SDL_JoystickEventState(1);
    FindJoysticks();

    fprintf(stderr,"Loading intro music...\n");
    saidwelcome = 0;
    if (!introskip) {
        loadmusic("BEGIN");
        musicon();
    }
    clockspeed = 0;
    slottime = 0;
    owecoins = 0;
    owecoinwait = 0;
    slotpos[0] = 0;
    slotpos[1] = 0;
    slotpos[2] = 0;
    clockspeed = 0;
    skilevel = 0;

    initgraphics();

    if (!introskip)
    {
        /* Big scrolly picture... */
        i=0;
        j=0;
        l=0;

        if (lab3dversion==KENS_LABYRINTH_2_0 || lab3dversion==KENS_LABYRINTH_2_1) {
            fade(0);
            j = kgif(2);
            if (j)
                kgif(0);

            fade(63);
            l = 25200;
            i = 1;
        }

        SDL_LockMutex(timermutex);
        oclockspeed=clockspeed;

        clearkeydefstat(ACTION_MENU);

        SDL_GL_SwapWindow(mainwindow);

        while ((getkeydefstatlock(ACTION_MENU) == 0) &&
                (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
                (bstatus == 0) &&
                (clockspeed < (((lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)|j)?3840:7680)))
        {
            if (i == 0)
            { 
                l += 90;
                if (l >= 25200)
                {
                    i = 1;
                    l = 25200;
                }
            }
            if (i == 1)
            {
                l -= 90;
                if (l > 32768)
                {
                    l = 0;
                    i = 0;
                }
            }

            while(clockspeed<oclockspeed+12) {
                SDL_UnlockMutex(timermutex);
                SDL_Delay(10);
                SDL_LockMutex(timermutex);
            }
            oclockspeed+=12;

            if (!((lab3dversion==KENS_LABYRINTH_1_0 
            || lab3dversion==KENS_LABYRINTH_1_1)|j)) {
                SDL_UnlockMutex(timermutex);
                glClearColor(0,0,0,0);
                glClear(GL_COLOR_BUFFER_BIT);
                visiblescreenyoffset=(l/90)-20;
                ShowPartialOverlay(20,20+visiblescreenyoffset,320,200,0);
                SDL_GL_SwapWindow(mainwindow);
                SDL_LockMutex(timermutex);
            }
            PollInputs();
            bstatus = 0;
            if (moustat == 0)
            {
                bstatus=readmouse(NULL, NULL);
            }
        }
        oclockspeed=clockspeed;
        for(i=63;i>=0;i-=4)
        {
            SDL_UnlockMutex(timermutex);
            fade(64+i);
            glClearColor(0,0,0,0);
            glClear(GL_COLOR_BUFFER_BIT);
            if ((lab3dversion==KENS_LABYRINTH_1_0 
            || lab3dversion==KENS_LABYRINTH_1_1)|j)
                visiblescreenyoffset=0;
            else
                visiblescreenyoffset=(l/90)-20;
            ShowPartialOverlay(20,20+visiblescreenyoffset,320,200,0);
            SDL_GL_SwapWindow(mainwindow);
            SDL_LockMutex(timermutex);

            while(clockspeed<oclockspeed+4) {
                SDL_UnlockMutex(timermutex);
                SDL_Delay(10);
                SDL_LockMutex(timermutex);
            }
            oclockspeed+=4;
        }
        SDL_UnlockMutex(timermutex);
    }
    
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
    hiscorenamstat = 0;

    /* Shareware/registered check... */
    sprintf(filepath, "%sboards.dat", gameroot);
    sprintf(filepathUpper, "%sBOARDS.DAT", gameroot);
    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        if (((i = open(filepath,O_BINARY|O_RDONLY,0)) != -1)||
            ((i = open(filepathUpper,O_BINARY|O_RDONLY,0)) != -1)) {
            fstat(i, &fstats);
            numboards = (int)(fstats.st_size>>13);
            fprintf(stderr, "Detected %d boards.\n", numboards);
            close(i);
        } else {
            fprintf(stderr,"boards.dat not found.\n");
            SDL_Quit();
            exit(1);
        }
    } else {
        sprintf(filepath, "%sboards.kzp", gameroot);
        sprintf(filepathUpper, "%sBOARDS.KZP", gameroot);
        if (((i = open(filepath,O_RDONLY|O_BINARY,0)) != -1)||
            ((i = open(filepathUpper,O_RDONLY|O_BINARY,0)) != -1)) {
            readLE16(i,&boleng[0],30*4);
            numboards = 30;
            if ((boleng[40]|boleng[41]) == 0)
                numboards = 20;
            if ((boleng[20]|boleng[21]) == 0)
                numboards = 10;
            close(i);
        } else {
            fprintf(stderr,"boards.kzp not found.\n");
            SDL_Quit();
            exit(1);
        }
    }
    if (!introskip)
        musicoff();
}

void initvideo()
{
    K_INT16 i, j, k;
    walltol=32;
    neardist=16;
    vidmode = 1; /* Force fake 360x240 mode. */
    time_t tnow;

    time(&tnow);
    srand((unsigned int)tnow);

    int realr,realg,realb,realz,reald = 0;
    SDL_Surface *icon;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE,8);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,24);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_RED_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_GREEN_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_BLUE_SIZE,0);
    SDL_GL_SetAttribute(SDL_GL_ACCUM_ALPHA_SIZE,0);

    SDL_ShowCursor(0);

    icon = SDL_LoadBMP("ken.bmp");
    if (icon == NULL) {
        fprintf(stderr,"Warning: ken.bmp (icon file) not found.\n");
    }

    fprintf(stderr,"Activating video...\n");

    if (mainwindow != NULL) {
        #ifndef __SWITCH__
        fatal_error("window already created (init)");
        #endif
    } else {
        if (fullscreen) {
            mainwindow = SDL_CreateWindow("Ken's Labyrinth", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                      0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_OPENGL);
        } else {
            mainwindow = SDL_CreateWindow("Ken's Labyrinth", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      screenwidth, screenheight, SDL_WINDOW_OPENGL);
        }
    }

    if (mainwindow == NULL) {
        fatal_error("Video mode set failed.");
    }

    SDL_GetWindowSize(mainwindow, &screenwidth, &screenheight);
    configure_screen_size();
    fprintf(stderr,"True size: %dx%d\n", screenwidth, screenheight);

    if (icon != NULL)
        SDL_SetWindowIcon(mainwindow, icon);

    maincontext = SDL_GL_CreateContext(mainwindow);
 
    if (maincontext == NULL) {
        fatal_error("Could not create GL context.");
    }

    #ifdef __SWITCH__
    gladLoadGL();
    #endif

    SDL_GL_GetAttribute(SDL_GL_RED_SIZE,&realr);
    SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE,&realg);
    SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE,&realb);
    SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE,&realz);
    SDL_GL_GetAttribute(SDL_GL_DOUBLEBUFFER,&reald);

    fprintf(stderr,"GL Vendor: %s\n",glGetString(GL_VENDOR));
    fprintf(stderr,"GL Renderer: %s\n",glGetString(GL_RENDERER));
    fprintf(stderr,"GL Version: %s\n",glGetString(GL_VERSION));
    //fprintf(stderr,"GL Extensions: %s\n",glGetString(GL_EXTENSIONS));

    #ifndef __SWITCH__
    fprintf(stderr,"GLU Version: %s\n",gluGetString(GLU_VERSION));
    fprintf(stderr,"GLU Extensions: %s\n",gluGetString(GLU_EXTENSIONS));

    if (reald==0) {
        fatal_error("Double buffer not available.");
    }
    #endif

    fprintf(stderr,
            "Opened GL at %d/%d/%d (R/G/B) bits, %d bit depth buffer.\n",
            realr,realg,realb,realz);

    if (realz<24) {
        walltol=256;
        neardist=128;
    }

    SDL_SetWindowBrightness(mainwindow, gammalevel);

    largescreentexture = 1;

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
        fatal_error("Could not create screen buffer");
    }

    fprintf(stderr,"Allocating screen buffer texture...\n");
    if (largescreentexture) {
        glGenTextures(1,&screenbuffertexture);
    } else {
        glGenTextures(72,screenbuffertextures);
    }
}

void freememory()
{
    SDL_CloseAudio();
    free(lzwbuf);
    free(lzwbuf2);
    free(pic);
    free(note);
    free(SoundFile);
    free(SoundBuffer);
    free(screenbuffer);
    free(screenbuffer32);

    lzwbuf = NULL;
    lzwbuf2 = NULL;
    pic = NULL;
    note = NULL;
    SoundFile = NULL;
    SoundBuffer = NULL;
    screenbuffer = NULL;
    screenbuffer32 = NULL;

    #ifndef __SWITCH__
    mainwindow = NULL;
    maincontext = NULL;
    #endif
}

void initmemory()
{
    K_INT16 i, walcounter;
    K_UINT16 l;
    unsigned char *v;

    fprintf(stderr,"Allocating memory...\n");
    if (((lzwbuf = malloc(12304-8200)) == NULL)||
        ((lzwbuf2=malloc(8200))==NULL))
    {
        fatal_error("Error #3: Memory allocation failed.\n");
    }

    convwalls = numwalls;

    if ((pic = malloc((numwalls-initialwalls)<<12)) == NULL)
    {
        fprintf(stderr,
                "Error #4: This computer does not have enough memory.\n");
        SDL_Quit();
        exit(-1);
    }

    if ((note = malloc(16384)) == NULL)
    {
        fatal_error("Could not allocate memory for music");
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

    numkeyspressed=0;
}

void initaudio()
{
    FILE *file;
    struct stat fstats;
    long sndsize;
    int i;

    speed = 240;
    musicstatus=0;

    soundmutex = SDL_CreateMutex();
    timermutex = SDL_CreateMutex();
    
    mute = 0;

    firstime = 1;

    if (musicsource == MUSIC_SOURCE_MIDI) {
        fprintf(stderr,"Opening music output...\n");
#ifdef WIN32
        if ((i=midiOutOpen(&sequencerdevice,MIDI_MAPPER,(DWORD)(NULL),
                           (DWORD)(NULL),0))!=
            MMSYSERR_NOERROR) {
            fatal_error("Could not open MIDI output");
        }
#endif
#ifdef USE_OSS
        sequencerdevice=open("/dev/sequencer", O_WRONLY, 0);
        if (sequencerdevice<0) {
            fprintf(stderr,"Music failed opening /dev/sequencer.\n");
            SDL_Quit();
            exit(-1);
        }
        if (ioctl(sequencerdevice, SNDCTL_SEQ_NRMIDIS, &nrmidis) == -1) {
            fatal_error("Can't get info about midi ports!");
            SDL_Quit();
            exit(-1);
        }
#endif
    }

    if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM) {
        fprintf(stderr,"Opening Adlib emulation for %s music (%s output)...\n",
                musicpan?"stereo":"mono",(channels-1)?"stereo":"mono");
        adlibinit(44100,channels,2);
        adlibsetvolume(musicvolume*48);
    }

    if (speechstatus >= 2)
    {
        sprintf(filepath, "%ssounds.kzp", gameroot);
        sprintf(filepathUpper, "%sSOUNDS.KZP", gameroot);
        if (((i = open(filepath,O_BINARY|O_RDONLY,0)) != -1)||
            ((i = open(filepathUpper,O_BINARY|O_RDONLY,0)) != -1)) {
            fstat(i, &fstats);
            sndsize = (int)(fstats.st_size);
            fprintf(stderr, "Detected %ld byte sounds.\n", sndsize);
            close(i);
        } else sndsize=0;

        SoundFile=malloc(sndsize);

        SoundBuffer=malloc(65536*2);

        if ((SoundFile==NULL)||(SoundBuffer==NULL)) {
            fatal_error("Insufficient memory for sound.");
        }

        file=fopen(filepath,"rb");
        if (file==NULL) {
            file=fopen(filepathUpper,"rb");
        }
        if (file==NULL) {
            fatal_error("Can not find sounds.kzp.");
        }
        if (fread(SoundFile,1,sndsize,file)!=sndsize) {
            fatal_error("Error in sounds.kzp.");
        }
        fclose(file);

        SDL_LockMutex(soundmutex);
        fprintf(stderr,"Opening sound output in %s for %s sound effects...\n",
                (channels-1)?"stereo":"mono",
                soundpan?"stereo":"mono");

        SDL_AudioSpec want, have;
        want.freq = (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM) ? 44100 : 11025;
        want.format = AUDIO_S16SYS;
        want.channels = channels;
        want.samples = soundblocksize;
        want.userdata = NULL;
        want.callback = AudioCallback;
        soundbytespertick = channels * want.freq * 2 / 240;
        soundtimerbytes = 0;

        audiodevice = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_CHANNELS_CHANGE | SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
        if (audiodevice == 0) {
            TRACE("Failed to open audio: %s", SDL_GetError());
        }

        reset_dsp();

        SDL_UnlockMutex(soundmutex);

        SDL_PauseAudioDevice(audiodevice, 0);
    } else {
        if (soundtimer)
            fprintf(stderr,"Warning: no sound, using system timer.\n");
        soundtimer=0;
    }
}

void initgraphics()
{
    K_INT16 i, j, k, oclockspeed;

    texturecreationneeded = 1;

    fprintf(stderr,"Loading intro pictures...\n");

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
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
    } else {
        /* The ingame palette is stored in this GIF! */
        kgif(1);
        memcpy(spritepalette,palette,768);

        /* Show the Epic Megagames logo while loading... */
        kgif(0);
        fprintf(stderr,"Loading graphics...\n");

        loadwalls(1);

        /* Ken's Labyrinth logo. */
        if (!kgif(2))
            kgif(1);

        fade(63);
    }

    k = 0;
    for(i=0;i<16;i++)
        for(j=1;j<17;j++)
        {
            palette[k++] = (paldef[i][0]*j)/17;
            palette[k++] = (paldef[i][1]*j)/17;
            palette[k++] = (paldef[i][2]*j)/17;
        }

    SetVisibleScreenOffset(0);
    SDL_GL_SwapWindow(mainwindow);

    if (moustat == 0)
            moustat = setupmouse();
    if (!introskip)
    {
        SDL_LockMutex(timermutex);
        oclockspeed = clockspeed;
        while ((getkeydefstatlock(ACTION_MENU) == 0) &&
            (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
            (bstatus == 0) &&
            (clockspeed < oclockspeed+960))
        {
            PollInputs();

            bstatus = 0;
            if (moustat == 0)
            {
                bstatus=readmouse(NULL, NULL);
            }
            SDL_UnlockMutex(timermutex);
            SDL_Delay(10);
            SDL_LockMutex(timermutex);
        }
        SDL_UnlockMutex(timermutex);
    }
}

void inittablesandsettings()
{
    fprintf(stderr,"Loading tables/settings...\n");

    loadtables();
    loadsettings();
    configure();
    configure_screen_size();
}

void initgameversion()
{
    int fil;

    // Check if the gamedata directory exists
    const char* directory = "gamedata";
    struct stat sb;

    if (stat(directory, &sb) == 0 && S_ISDIR(sb.st_mode)) {
        legacyload = 0;
        switch (lab3dversion) {
            case KENS_LABYRINTH_1_0:
            sprintf(gameroot, "%s", "gamedata/Ken1.0/");
            rnumwalls=192;
            fprintf(stderr, "Ken's Labyrinth version 1.0 selected.\n");
            break;
            case KENS_LABYRINTH_1_1:
            sprintf(gameroot, "%s", "gamedata/Ken1.1/");
            rnumwalls=0xe0;
            fprintf(stderr, "Ken's Labyrinth version 1.1 selected.\n");
            break;
            case KENS_LABYRINTH_2_0:
            sprintf(gameroot, "%s", "gamedata/Ken2.0/");
            rnumwalls=448;
            fprintf(stderr, "Ken's Labyrinth version 2.0 selected.\n");
            break;
            case KENS_LABYRINTH_2_1:
            sprintf(gameroot, "%s", "gamedata/Ken2.1/");
            rnumwalls=448;
            fprintf(stderr, "Ken's Labyrinth version 2.1 selected.\n");
            break;
        }
    } else {
        gameroot[0] = '\0';
        legacyload = 1;
        sprintf(filepath, "%send.txt", gameroot);
        sprintf(filepathUpper, "%sEND.TXT", gameroot);
        if (((fil = open(filepath,O_RDONLY|O_BINARY,0)) != -1)||
            ((fil = open(filepathUpper,O_RDONLY|O_BINARY,0)) != -1)) {
            close(fil);
            lab3dversion=KENS_LABYRINTH_1_0; /* Version 1.0 detected. */
            rnumwalls=192;
            fprintf(stderr, "Ken's Labyrinth version 1.0 detected.\n");
        } else {
            sprintf(filepath, "%sboards.dat", gameroot);
            sprintf(filepathUpper, "%sBOARDS.DAT", gameroot);
            if (((fil = open(filepath,O_RDONLY|O_BINARY,0)) != -1)||
                ((fil = open(filepathUpper,O_RDONLY|O_BINARY,0)) != -1)) {
                close(fil);
                lab3dversion=KENS_LABYRINTH_1_1; /* Version 1.1 detected. */
                rnumwalls=0xe0;
                fprintf(stderr, "Ken's Labyrinth version 1.1 detected.\n");
            } else {
                lab3dversion=KENS_LABYRINTH_2_1; /* Assuming version 2.x. */
                rnumwalls=448;
                fprintf(stderr, "Ken's Labyrinth version 2.x detected.\n");
            }
        }
    }
}

void resetaudio()
{
    SDL_CloseAudioDevice(audiodevice);
    musicoff();
    configure();
    initaudio();
    loadmusic(lastPlayedMusicFile);
    musicon();
}
