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

        PL_LockTimer();
        oclockspeed=clockspeed;

        clearkeydefstat(ACTION_MENU);

        PL_SwapBuffers();

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
                PL_UnlockTimer();
                PL_Delay(10);
                PL_LockTimer();
            }
            oclockspeed+=12;

            if (!((lab3dversion==KENS_LABYRINTH_1_0 
            || lab3dversion==KENS_LABYRINTH_1_1)|j)) {
                PL_UnlockTimer();
                R_ClearScreen();
                visiblescreenyoffset=(l/90)-20;
                ShowPartialOverlay(20,20+visiblescreenyoffset,320,200,0);
                PL_SwapBuffers();
                PL_LockTimer();
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
            PL_UnlockTimer();
            fade(64+i);
            R_ClearScreen();
            if ((lab3dversion==KENS_LABYRINTH_1_0 
            || lab3dversion==KENS_LABYRINTH_1_1)|j)
                visiblescreenyoffset=0;
            else
                visiblescreenyoffset=(l/90)-20;
            ShowPartialOverlay(20,20+visiblescreenyoffset,320,200,0);
            PL_SwapBuffers();
            PL_LockTimer();

            while(clockspeed<oclockspeed+4) {
                PL_UnlockTimer();
                PL_Delay(10);
                PL_LockTimer();
            }
            oclockspeed+=4;
        }
        PL_UnlockTimer();
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
            fatal_error("boards.dat not found.");
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
            fatal_error("boards.kzp not found.");
        }
    }
    if (!introskip)
        musicoff();
}

void initvideo()
{
    time_t tnow;

    walltol=32;
    neardist=16;
    vidmode = 1; /* Force fake 360x240 mode. */

    time(&tnow);
    srand((unsigned int)tnow);

    /* Open the display.  PL_OpenVideo() picks up screenwidth/screenheight,
       corrects them to what was actually obtained and calls
       configure_screen_size() for us. */
    if (PL_OpenVideo() != 0)
        fatal_error("Video mode set failed.");

    largescreentexture = R_WantsLargeOverlayTexture();

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
#ifdef PLATFORM_AMIGA
    /* The software renderer composites straight from the 8 bit overlay, so
       the 32 bit shadow copy the GL path uploads from is not needed. */
    screenbuffer32=NULL;
#else
    screenbuffer32=malloc((size_t)screenbufferwidth*screenbufferheight*4);
#endif

    linecompare(479);

    if (screenbuffer==NULL) {
        fatal_error("Could not create screen buffer");
    }

    fprintf(stderr,"Allocating screen buffer textures...\n");
    R_InitOverlay();
}

void freememory()
{
    PL_CloseAudio();
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

}

void initmemory()
{
    K_INT16 i, walcounter;
    K_UINT16 l;
    unsigned char *v;

    memset(draw_ptr, 0, sizeof(draw_ptr) / sizeof(void*));
    drawStackTopIndex = -1;

    fprintf(stderr,"Allocating memory...\n");
    if (((lzwbuf = malloc(12304-8200)) == NULL)||
        ((lzwbuf2=malloc(8200))==NULL))
    {
        fatal_error("Error #3: Memory allocation failed.\n");
    }

    convwalls = numwalls;

    if ((pic = malloc((numwalls-initialwalls)<<12)) == NULL)
    {
        fatal_error("Error #4: This computer does not have enough memory.");
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

    soundratio = 1;
    soundratioshift = 0;
    
    mute = 0;

    firstime = 1;

    if (musicsource == MUSIC_SOURCE_MIDI) {
        fprintf(stderr,"Opening music output...\n");
        if (PL_MidiOpen() != 0) {
            fprintf(stderr,"No MIDI output available; music disabled.\n");
            musicsource = MUSIC_SOURCE_NONE;
        }
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

        PL_LockSound();
        fprintf(stderr,"Opening sound output in %s for %s sound effects...\n",
                (channels-1)?"stereo":"mono",
                soundpan?"stereo":"mono");

        samplerate = PL_OpenAudio((musicsource == MUSIC_SOURCE_ADLIB ||
                                   musicsource == MUSIC_SOURCE_ADLIB_RANDOM)
                                  ? 44100 : 11025,
                                  channels, soundblocksize);

        /*
         * Work out how far the sound buffer runs below the output rate.
         *
         * The mixer interpolates the digital sound buffer up to the output
         * rate, and the sound effects in sounds.kzp are 11025Hz, so the
         * buffer wants to run as close to 11025Hz as a power of two division
         * of the output rate allows.  At 44100Hz that is the historical
         * factor of four; on an Amiga running the device at 22050Hz it is
         * two, and at 11025Hz it is one.  Getting this wrong decimates the
         * effects - speech turns into noise - while leaving the music, which
         * is synthesised straight into the output stream, untouched.
         */
        soundratio = 1;
        soundratioshift = 0;
        if (musicsource == MUSIC_SOURCE_ADLIB ||
            musicsource == MUSIC_SOURCE_ADLIB_RANDOM) {
            while (soundratio < 4 &&
                   samplerate / (soundratio * 2) >= SOUNDNATIVERATE) {
                soundratio <<= 1;
                soundratioshift++;
            }
        }

        soundbytespertick = channels * samplerate * 2 / 240;
        soundtimerbytes = 0;

        if (musicsource == MUSIC_SOURCE_ADLIB || musicsource == MUSIC_SOURCE_ADLIB_RANDOM) {
        fprintf(stderr,"Opening Adlib emulation for %s music (%s output)...\n",
                musicpan?"stereo":"mono",(channels-1)?"stereo":"mono");
        adlibinit(samplerate,channels,2);
        adlibsetvolume(musicvolume*48);
        }

        reset_dsp();

        PL_UnlockSound();

        PL_PauseAudio(0);
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
    PL_SwapBuffers();

    if (moustat == 0)
            moustat = setupmouse();
    if (!introskip)
    {
        PL_LockTimer();
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
            PL_UnlockTimer();
            PL_Delay(10);
            PL_LockTimer();
        }
        PL_UnlockTimer();
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
    
    int isapple = 0;

    gameroot[0] = 0;
    
#ifdef __APPLE__
    isapple = 1;
    CFURLRef appUrlRef = CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("kenfiles"), NULL, NULL);
    CFStringRef filePathRef = CFURLCopyPath(appUrlRef);
    const char* filePath = CFStringGetCStringPtr(filePathRef, kCFStringEncodingUTF8);
    sprintf(gameroot, "%s", filePath);
    
    // Release references
    CFRelease(filePathRef);
    CFRelease(appUrlRef);
#endif

    // Check if the gamedata directory exists
    const char* directory = "gamedata";
    struct stat sb;
    int found_gamedata = 0;

    // First check local directory
    if (isapple || (stat(directory, &sb) == 0 && S_ISDIR(sb.st_mode))) {
        found_gamedata = 1;
    }
    
#if defined(__unix__) && !defined(__APPLE__)
    // If not found locally on Linux, check system installation path
    if (!found_gamedata) {
        const char* system_gamedata = "/usr/local/share/ken/gamedata";
        if (stat(system_gamedata, &sb) == 0 && S_ISDIR(sb.st_mode)) {
            sprintf(gameroot, "/usr/local/share/ken/");
            found_gamedata = 1;
        } else {
            // Also try /usr/share/ken/gamedata
            system_gamedata = "/usr/share/ken/gamedata";
            if (stat(system_gamedata, &sb) == 0 && S_ISDIR(sb.st_mode)) {
                sprintf(gameroot, "/usr/share/ken/");
                found_gamedata = 1;
            }
        }
    }
#endif

    if (found_gamedata) {
        legacyload = 0;
        switch (lab3dversion) {
            case KENS_LABYRINTH_1_0:
            sprintf(gameroot, "%s%s", gameroot, "gamedata/Ken1.0/");
            rnumwalls=192;
            fprintf(stderr, "Ken's Labyrinth version 1.0 selected.\n");
            break;
            case KENS_LABYRINTH_1_1:
            sprintf(gameroot, "%s%s", gameroot, "gamedata/Ken1.1/");
            rnumwalls=0xe0;
            fprintf(stderr, "Ken's Labyrinth version 1.1 selected.\n");
            break;
            case KENS_LABYRINTH_2_0:
            sprintf(gameroot, "%s%s", gameroot, "gamedata/Ken2.0/");
            rnumwalls=448;
            fprintf(stderr, "Ken's Labyrinth version 2.0 selected.\n");
            break;
            case KENS_LABYRINTH_2_1:
            sprintf(gameroot, "%s%s", gameroot, "gamedata/Ken2.1/");
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
    PL_CloseAudio();
    musicoff();
    configure();
    if (SoundFile) {
        free(SoundFile);
        SoundFile = NULL;
    }
    if (SoundBuffer) {
        free(SoundBuffer);
        SoundBuffer = NULL;
    }
    PL_MidiClose();
    initaudio();
    loadmusic(lastPlayedMusicFile);
    musicon();
}
