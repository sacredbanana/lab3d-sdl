#define MAIN
#include <sys/stat.h>

#include "lab3d.h"
#include "adlibemu.h"
#include "math.h"
#include "SDL_main.h"

static demo_vardef_t demovars[] = {
    DEMO_VAR(boardnum),
    DEMO_VAR(scorecount),
    DEMO_VAR(scoreclock),
    { "board", &board[0][0], 2, 4096 },
    DEMO_VAR(skilevel),
    DEMO_VAR(life),
    DEMO_VAR(death),
    DEMO_VAR(lifevests),
    DEMO_VAR(lightnings),
    DEMO_ARRAY(firepowers),
    DEMO_VAR(bulchoose),
    DEMO_ARRAY(keys),
    DEMO_VAR(coins),
    DEMO_VAR(compass),
    DEMO_VAR(cheated),
    DEMO_VAR(statusbar),
    DEMO_VAR(statusbargoal),
    DEMO_VAR(posx),
    DEMO_VAR(posy),
    DEMO_VAR(posz),
    DEMO_VAR(ang),
    DEMO_VAR(startx),
    DEMO_VAR(starty),
    DEMO_VAR(startang),
    DEMO_VAR(angvel),
    DEMO_VAR(vel),
    DEMO_VAR(mxvel),
    DEMO_VAR(myvel),
    DEMO_VAR(svel),
    DEMO_VAR(hvel),
    DEMO_VAR(oldposx),
    DEMO_VAR(oldposy),
    DEMO_VAR(bulnum),
    DEMO_ARRAY(bulang),
    DEMO_ARRAY(bulkind),
    DEMO_ARRAY(bulx),
    DEMO_ARRAY(buly),
    DEMO_ARRAY(bulstat),
    DEMO_VAR(lastbulshoot),
    DEMO_VAR(mnum),
    DEMO_VAR(bossmonster),
    DEMO_ARRAY(mposx),
    DEMO_ARRAY(mposy),
    DEMO_ARRAY(mgolx),
    DEMO_ARRAY(mgoly),
    DEMO_ARRAY(moldx),
    DEMO_ARRAY(moldy),
    DEMO_ARRAY(mstat),
    DEMO_ARRAY(mshock),
    DEMO_ARRAY(mshot),
    DEMO_VAR(doorx),
    DEMO_VAR(doory),
    DEMO_VAR(doorstat),
    DEMO_VAR(numwarps),
    DEMO_VAR(justwarped),
    DEMO_ARRAY(xwarp),
    DEMO_ARRAY(ywarp),
    DEMO_VAR(totalclock),
    DEMO_VAR(ototclock),
    DEMO_VAR(purpletime),
    DEMO_VAR(greentime),
    DEMO_ARRAY(capetime),
    DEMO_VAR(fadewarpval),
    DEMO_VAR(fadehurtval),
    DEMO_VAR(slottime),
    DEMO_ARRAY(slotpos),
    DEMO_VAR(owecoins),
    DEMO_VAR(owecoinwait),
    DEMO_ARRAY(hiscorenam),
    DEMO_VAR(hiscorenamstat),
    DEMO_VAR(explonum),
    DEMO_ARRAY(explox),
    DEMO_ARRAY(exploy),
    DEMO_ARRAY(explotime),
    DEMO_ARRAY(explostat),
    DEMO_VAR(psoundnum),
    DEMO_ARRAY(psounds),
    DEMO_ARRAY(psoundpan),
    { NULL }
};

unsigned char slotable[3][16] =
{
    {5,2,4,5,3,0,4,1,2,4,5,3,5,4,1,3},
    {4,2,5,4,3,1,5,0,3,5,4,2,1,4,5,3},
    {4,2,0,4,5,4,1,3,1,5,4,3,2,5,3,5}
};

void delmonster(int i) {
    mnum--;
    if (i + 1 == bossmonster) bossmonster = 0;
    board[moldx[i]>>10][moldy[i]>>10] &= 0xbfff;
    board[mgolx[i]>>10][mgoly[i]>>10] &= 0xbfff;
    moldx[i] = moldx[mnum];
    moldy[i] = moldy[mnum];
    mposx[i] = mposx[mnum];
    mposy[i] = mposy[mnum];
    mgolx[i] = mgolx[mnum];
    mgoly[i] = mgoly[mnum];
    mstat[i] = mstat[mnum];
    mshot[i] = mshot[mnum];
    mshock[i] = mshock[mnum];
}

void debuginfo(void) {
    int x = 0, y = 2, bultime, ptime, gtime, graytime, bluetime;
    int accelf;
    int accels;
    int accelt;
    if (newkeystatus(SDLK_F11)) {
        if (!(showdebug & 2))
            showdebug ^= 3;
    } else {
        showdebug &= 1;
    }
    if (!showdebug)
        return;
    accelf = (getkeypressure(ACTION_FORWARD, 16384, 32767) - getkeypressure(ACTION_BACKWARD, 16384, 32767)) * 256 / 32767.0;
    accels = (getkeypressure(ACTION_MOVELEFT, 16384, 32767) - getkeypressure(ACTION_MOVERIGHT, 16384, 32767)) * 256 / 32767.0;
    accelt = (getkeypressure(ACTION_RIGHT, 16384, 32767) - getkeypressure(ACTION_LEFT, 16384, 32767)) * 36 / 32767.0;

#define dbg(v) snprintf(textbuf, sizeof(textbuf), "%-12s: %d\n", #v, v); textprint(x,y,(char)96); y += 10
    mixing=1;
    wipeoverlay(0,0,361,statusbaryoffset);
    snprintf(textbuf, sizeof(textbuf), "%02d:%04d %02d:%04d %4d %3d:%03d\n",
             posx>>10, posx&1023, posy>>10, posy&1023, ang&2047,
             scoreclock / 240, scoreclock % 240);
    textprint(x,y,(char)96); y += 10;

    snprintf(textbuf, sizeof(textbuf), "%4d    %4d    %4d\n",
             vel, svel, angvel);
    textprint(x,y,(char)96); y += 10;

    snprintf(textbuf, sizeof(textbuf), "%4d    %4d    %4d\n",
             accelf, accels, accelt);
    textprint(x,y,(char)96); y += 10;

    bultime = (lastbulshoot+240-(firepowers[bulchoose]<<5)) - totalclock;
    if (bultime < 0) bultime = 0;

    ptime = purpletime > totalclock ? totalclock - purpletime : 0;
    gtime = greentime > totalclock ? totalclock - greentime : 0;
    graytime = capetime[0] > totalclock ? totalclock - capetime[0] : 0;
    bluetime = capetime[1] > totalclock ? totalclock - capetime[1] : 0;

    snprintf(textbuf, sizeof(textbuf), "%3d %5d %5d %5d %5d\n",
             bultime, ptime/24, gtime/24, graytime/24, bluetime/24);
    textprint(x,y,(char)96); y += 10;

    strcpy(textbuf, "+");
    textprint(360/2 - 4, 120, (char)96);

    mixing=0;
#undef dbg
}

void drawvolumebar(int vol,int type,float level) {
    if (level>0.5) level=0.5;
    glEnable(GL_BLEND);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 360.0, -15+30*type, 225+30*type, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glBegin(GL_QUADS);
    glColor4f(0,0,0,level);
    glVertex2s(96,110);
    glVertex2s(96,130);
    glColor4f(0.25,0.25,0.25,level);
    glVertex2s(224,130);
    glVertex2s(224,110);
    if (type)
        glColor4f(0,0,255,level);
    else
        glColor4f(255,0,0,level);
    glVertex2s(96,110);
    glVertex2s(96,130);
    glVertex2s(96+(vol>>1),130);
    glVertex2s(96+(vol>>1),110);
    glEnd();
    glDisable(GL_BLEND);
    checkGLStatus();
}

static int playdemo(demofile_t* demoplaying, demofile_t* demorecording, int rewinding) {
    char ksmfile[15];
    double basetime = SDL_GetTicks();
    double ctime;
    int democlock = 0;
    double demomsclock = 0;
    fade(63);
    // int cf = 0;
    // int done = 0;
    int pausemode = rewinding, oldpause = 0, pause = 0;
    int oboardnum = rewinding ? boardnum : -1;
    int advance_pressed = 1, use_pressed = 1; //rewind_pressed = 1
    K_UINT32 opsoundnum = 0xFFFFFFFF;
    unsigned char opsounds[16], opsoundpan[16];
    int rewindable = demofile_rewindable(demoplaying);

    if (demorecording) {
        rewindable &= demofile_rewindable(demoplaying);
    }

    while (1) {
        double accelf = 0.0;
        int td, dir = 1;
        PollInputs();

        if (getkeydefstatlock(ACTION_MENU) || getkeydefstatlock(ACTION_MENU_CANCEL)) {
            return 1;
        }

        if (getkeydefstat(ACTION_REWIND)) {
            if (!advance_pressed) {
                return 0;
            }
        }

        if (getkeydefstat(ACTION_ADVANCE)) {
            pause = 1;
        } else {

            if (getkeydefstat(ACTION_USE) && !use_pressed) {
                use_pressed = 1;
                pausemode ^= 1;
            }

            dir = 1;
            accelf = (getkeypressure(ACTION_FORWARD, 16384, 32767) - getkeypressure(ACTION_BACKWARD, 16384, 32767))/ 32767.0;

            if (pausemode) {
                pause = 1;
                if (fabs(accelf) >= 0.1) {
                    pause = 0;
                }
            } else {
                pause = 0;
                accelf *= 20;
            }
            accelf *= 1 + (getkeypressure(ACTION_FIRE, 32767, 32767) * 19.0 / 32767.0);

            if (accelf < 0 /*|| (rewinding && accelf == 0)*/) {
                if (!rewindable) {
                    accelf = 0;
                    pause = 1;
                } else
                    dir = -1;
                accelf = -accelf;
            }

            accelf = pausemode ? 1.0 / accelf : 1.0 / (1 + accelf);

        }
        if (!getkeydefstat(ACTION_REWIND))
            advance_pressed = 0;
        if (!getkeydefstat(ACTION_USE))
            use_pressed = 0;

        if (pause != oldpause) {
            oldpause = pause;
            if (pause) {
                basetime -= SDL_GetTicks();
            } else {
                basetime += SDL_GetTicks();
            }
        }

        ctime = ((double)SDL_GetTicks()) - basetime;
        /*printf("%lf %lf %lf %d\n", ctime, demomsclock, accelf, pause);*/
        if (!pause) {
            while (demomsclock < ctime) {
                td = demofile_advance(demoplaying, dir);
                if (dir == -1) {
                    memcpy(opsounds, psounds, 16);
                    memcpy(opsoundpan, psoundpan, 16);
                }
                demofile_update_vars(demoplaying);
                if (dir == -1 && posx == 0 && posy == 0 && posz == 0) {
                    demofile_advance(demoplaying, 1);
                    demofile_update_vars(demoplaying);
                    td = 0;
                }

                if (accelf >= (1.0/15.0) && psoundnum != opsoundnum && opsoundnum != 0xFFFFFFFF) {
                    if (psoundnum > opsoundnum) {
                        if (opsoundnum + 16 < psoundnum)
                            opsoundnum = psoundnum - 16;
                        for (;opsoundnum < psoundnum; opsoundnum++) {
                            if (debugmode)
                                printf("fsnd %d: %d\n", opsoundnum, psounds[opsoundnum & 15]);
                            ksaypan(psounds[opsoundnum & 15], psoundpan[opsoundnum & 15], 1);
                        }
                    } else {
                        if (opsoundnum > psoundnum + 16)
                            opsoundnum = psoundnum + 16;
                        for (;opsoundnum > psoundnum; opsoundnum--) {
                            if (debugmode)
                                printf("bsnd %d: %d\n", opsoundnum, psounds[opsoundnum & 15]);
                            ksaypan(opsounds[(opsoundnum - 1) & 15], opsoundpan[(opsoundnum - 1) & 15], 1);
                        }
                    }
                }
                opsoundnum = psoundnum;

                if (td >= 0)
                    democlock += td * dir;
                /*fprintf(stderr, "%5d %d %d %d %d\n", democlock, td, posx, posy, ang);*/
                if (demorecording && td >= 0) {
                    if (dir == -1) {
                        demofile_advance(demorecording, -1);
                    } else {
                        demofile_write_frame(demorecording, td);
                    }
                }

                if (td <= 0) {
                    basetime = SDL_GetTicks() - 1;
                    demomsclock = 0;
                    ctime = 0;
                    break;
                }

                demomsclock += td * (1000.0/240.0) * accelf;
            }
            if (ctime > 0) {
                ctime += (1000.0/60.0);
                double waitfor = demomsclock;
                if (waitfor > ctime)
                    waitfor = ctime;
                while ((ctime = ((double)SDL_GetTicks()) - basetime) < waitfor)
                    SDL_Delay((int)(waitfor - ctime));
            }
        }

        if (ototclock <= 0)
            ototclock = totalclock;

        if (boardnum != oboardnum) {
            if (oboardnum != -1)
                musicoff();
            sprintf(ksmfile, "LABSNG%02d", boardnum);
            loadmusic(ksmfile);
            musicon();
            oboardnum = boardnum;
        }


        /* All of these increment at 15fps */
        int tcshf = totalclock >> 4;
        animate2 = tcshf & 1;
        animate3 = tcshf % 3;
        animate4 = tcshf & 3;
        animate8 = tcshf & 7;
        animate10 = tcshf % 10;
        animate11 = tcshf % 11;
        animate15 = tcshf % 15;
        oscillate3 = tcshf & 3;
        if (oscillate3 == 3) oscillate3 = 0;
        oscillate5 = tcshf & 7;
        if (oscillate5 > 4) oscillate5 = 8 - oscillate5;

        /* These two increment as fast as the game can display - just assume 60fps for now */
        tcshf = totalclock >> 2;
        animate6 = totalclock % 6;
        animate7 = totalclock % 7;

        wipeoverlay(0,0,361,statusbaryoffset);
        if (slottime > 0) {
            copyslots(slotto+1);
        } else {
            copyslots(slotto);
        }
        if (death == 4095)
        {
            if (fadehurtval > 0) {
                fade(fadehurtval+128);
            } else if (fadewarpval < 63) {
                fade(fadewarpval);
            } else {
                fade(63);
            }
        } else {
            fade(death>>6);
        }

        picrot(posx, posy, posz, ang);
        debuginfo();
        statusbaralldraw();
        SDL_GL_SwapWindow(mainwindow);
    }
}

int main(int argc,char **argv)
{
    char ksmfile[15], hitnet, cheatkeysdown, won;
    K_INT16 i, j, jj, k, m=0, n=0, x, y, brd0, brd1, brd2, brd3, incenter=0;
    K_UINT16 l, newx, newy, oposx, oposy, plcx, plcy,inhibitrepeat=0;
    K_INT32 templong;

    K_INT32 standvel=0;
    K_INT32 dfwdvel=0, dsidevel=0, dturnvel=0, dstandvel=0;

    K_INT16 bx; /* Converted from asm. */
    K_UINT32 frames=0,timeused=0;
    K_INT16 soundvolumevisible=0,musicvolumevisible=0;
    int fil;
    int cmd_loadgame = 0;

    #if defined(__SWITCH__) && defined(ENABLE_NXLINK)
    // Set mesa configuration (useful for debugging)
    setMesaConfig();
    #endif

    for(i=1;i<argc;i++) {
        if ((strcmp(argv[i],"-V")==0)||(strcmp(argv[i],"-version")==0)) {
            printf("LAB3D/SDL version %d.%d.%d\n", LAB3D_VERSION_MAJOR, LAB3D_VERSION_MINOR, LAB3D_VERSION_PATCH);
            return 0;
        }
        else if ((strcmp(argv[i],"-h")==0)||(strcmp(argv[i],"-help")==0)) {
            printf("Command line options:\n");
            printf(" -h, -help                Print this help.\n");
            printf(" -V, -version             Print version.\n");
            printf(" -res <w> <h> <virtual w> <virtual h>\n");
            printf("                          Set screen resolution.  Default: 800 600 360 240\n");
            printf(" -asp <aspw> <asph>       Aspect ratio correction.  Default: 1.0 1.0\n");
            printf(" -win                     Start in window mode.\n");
            printf(" -fullscreen              Start in fullscreen mode.\n");
            printf(" -nearest                 Use nearest neighbor texture resize filter.\n");
            printf(" -trilinear               Use trilinear texture resize filter.\n");
            printf(" -stereo                  Enable stereoscopic view.\n");
            printf(" -gmmusic                 Use General MIDI for music.\n");
            printf(" -admusic                 Use Adlib emulation for music.\n");
            printf(" -nomusic                 Don't play music.\n");
            printf(" -sound                   Enable sound.\n");
            printf(" -nosound                 Disable sound.\n");
            printf(" -skip                    Skip intro sequence and start a new game right away.\n");
            printf(" -load <slot>             Load a saved game from one of the 8 available slots.\n");
            printf(" -record <slot> <dem>     Load a game (slot 1-8) and record a demo.\n");
            printf("                          If <slot> is set to 0 a new game will be started.\n");
            printf(" -recordx <slot> <dem>    Same as -record but saves an uncompressed demo file.\n");
            printf(" -play <dem>              Play a demo file.\n");
            printf(" -setup                   Start in settings menu.\n");
            printf(" -v, -debug               Verbose output.\n");
            return 0;
        }
    }

    clockspd=0;

    soundmutex = SDL_CreateMutex();
    timermutex = SDL_CreateMutex();

    /* Initialisation... */

    /* Initialise SDL; */

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO |
             SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER);

    #ifdef __SWITCH__
    romfsInit();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    #endif

    if (SDL_GL_LoadLibrary(NULL) != 0) {
        TRACE("Could not dynamically open OpenGL library: %s", SDL_GetError());
        SDL_Quit();
    }

    if (strlen(argv[0])>=5) {
        if (strcmp(argv[0]+strlen(argv[0])-5,"setup")==0)
            setup();
    }

    debugmode=0;

    for(i=1;i<argc;i++) {
        if ((strcmp(argv[i],"-res")==0)&&(i+4<argc)) {
            screenwidth=atoi(argv[++i]);
            screenheight=atoi(argv[++i]);
            virtualscreenwidth=atof(argv[++i]);
            virtualscreenheight=atof(argv[++i]);

            if ((screenwidth<=0)||(screenheight<=0)) {
                fprintf(stderr,"Invalid screen resolution; using default.\n");
                screenwidth=800; screenheight=600;
            }
            if ((virtualscreenwidth<320.0)||(virtualscreenheight<200.0)) {
                fprintf(stderr,"Invalid 2D resolution; using default.\n");
                virtualscreenwidth=360; virtualscreenheight=240;
            }
        }
        else if ((strcmp(argv[i],"-asp")==0)&&(i+2<argc)) {
            aspw=atof(argv[++i]);
            asph=atof(argv[++i]);

            if ((aspw<=0.1)||(asph<=0.1)) {
                fprintf(stderr,"Invalid aspect ratio correction; using default.\n");
                aspw=1.0; asph=1.0;
            }
        }
        else if (strcmp(argv[i],"-skip")==0) {
            introskip = 1;
        }
        else if ((strcmp(argv[i],"-load")==0)&&(i+1<argc)) {
            j = atoi(argv[++i]);

            if (j > 0 && j <= 8) {
                cmd_loadgame = j;
                introskip = 1;
            }
        }
        else if (strcmp(argv[i],"-stereo")==0) {
            stereo = 1;
        }
        else if (((k=(strcmp(argv[i],"-recordx")==0)) ||
                  (strcmp(argv[i],"-record")==0))
                 &&(i+2<argc)) {
            printf("k=%d\n",k);
            j = atoi(argv[++i]);

            if (j > 0 && j <= 8) {
                cmd_loadgame = j;
                introskip = 1;
            }

            demorecording = demofile_open(argv[++i], demovars, 1, k);
        }
        else if ((strcmp(argv[i],"-play")==0)&&(i+1<argc)) {
            demoplaying = demofile_open(argv[++i], demovars, 0, 0);
            if (!demoplaying)
                fatal_error("failed to load demo");
            introskip = 1;
        }
        else if (strcmp(argv[i],"-win")==0) fullscreen=0;
        else if (strcmp(argv[i],"-fullscreen")==0) fullscreen=1;
        else if (strcmp(argv[i],"-nearest")==0) {
            fullfilter=partialfilter=GL_NEAREST;
        }
        else if (strcmp(argv[i],"-trilinear")==0) {
            fullfilter=GL_LINEAR_MIPMAP_LINEAR;
            partialfilter=GL_LINEAR;
        }
        else if (strcmp(argv[i],"-gmmusic")==0)
            musicsource = MUSIC_SOURCE_MIDI;
        else if (strcmp(argv[i],"-admusic")==0)
            musicsource = MUSIC_SOURCE_ADLIB;
        else if (strcmp(argv[i],"-admusicran")==0)
            musicsource = MUSIC_SOURCE_ADLIB_RANDOM;
        else if (strcmp(argv[i],"-nomusic")==0)
            musicsource = MUSIC_SOURCE_NONE;
        else if (strcmp(argv[i],"-sound")==0)
            speechstatus=2;
        else if (strcmp(argv[i],"-nosound")==0)
            speechstatus=0;
        else if (strcmp(argv[i],"-setup")==0)
            setup();
        else if ((strcmp(argv[i],"-debug")==0) ||
                 (strcmp(argv[i],"-v")==0))
            debugmode=1;
    }

    lab3dversion = KENS_LABYRINTH_2_1;
    initgameversion();
    inittablesandsettings();
    initvideo();
    initmemory();
    initaudio();

    if (!legacyload) {
        loadmusic("BEGIN");
        musicon();
    }
    initgraphics();

    if (!legacyload) {
        gamelaunchermenu();
        musicoff();
        #if !defined(__SWITCH__)
        SDL_DestroyWindow(mainwindow);
        SDL_GL_DeleteContext(maincontext);
        #endif
        freememory();
        clearimgcache();
        initgameversion();
        inittablesandsettings();
        initvideo();
        initmemory();
        resetaudio();
    }

    initialize();
    
    if (argc >= 2)
    {
        for(i=0;i<8;i++) {
            ksmfile[i] = argv[argc-1][i];
            if (ksmfile[i]==0) break;
        }
        i = 0;
        if (strcmp(ksmfile,"cheaton")==0) {
            fprintf(stderr,"Alternate cheat mode on.\n");
            cheatenable=2;
        }
        while ((ksmfile[i] != 0) && (i < 8))
        {
            ksmfile[i] = (ksmfile[i]^(i<<1))+7;
            i++;
        }
        if (i == 7)
        {
            ksmfile[7] = 0;
            /* Obfuscated text translates as "snausty". */
            if (strcmp(ksmfile,"zslz\202\205|") == 0) {
                cheatenable = 1;
                fprintf(stderr,"Cheats enabled.\n");
            }
        }
    }

    if (stereo)
        setup_stereo(stereo);
        
    /* Fork v1.1 off to its own main function here. */

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
        oldmain();
        quit();
        return 0;
    }

    

    /* Introduction... */
    kgif(1);
    introduction(0);

    if (cmd_loadgame != 0) {
        loadgame(cmd_loadgame);
    }

    introskip = 0;

    won = 0;
    cliptowall=1;

    if (demoplaying) {
        int rc = playdemo(demoplaying, demorecording, 0);
        if (rc == 0) {
            clockspeed = 0;
            goto demo_continue_entry;
        } else {
            quit();
        }
    }

    clockspeed = 0;
    /* Main game loop starts here... */

    while (quitgame == 0)
    {
        PollInputs();
        if (death < 4095)
        {
            /* Die... */

            fade(death>>6);
            posz+=2;
            if (posz > 64)
                posz = 64;
            if (angvel < 0)
            {
                angvel -= 8;
                if (angvel < -32)
                    angvel = -32;
            }
            if (angvel > 0)
            {
                angvel += 8;
                if (angvel > 32)
                    angvel = 32;
            }
            ang = (ang+angvel)&2047;
            death -= (((((K_INT16)(totalclock-ototclock))<<6)&0xffc0)+64);
            if (death <= 0)
                death = 0;
            if (death == 0)
            {
                /* Dead... */
                if (lifevests > 0)
                {
                    /* Respawn... */
                    death = 4095;
                    life = 4095;
                    purpletime = totalclock-1;
                    greentime = totalclock-1;
                    capetime[0] = totalclock-1;
                    capetime[1] = totalclock-1;
                    statusbardraw(15,13,16,15,159,13+statusbaryoffset,statusbarback);
                    statusbardraw(16,13,16,15,176,13+statusbaryoffset,statusbarback);
                    statusbardraw(16,2,21,28,194,2+statusbaryoffset,statusbarback);
                    statusbardraw(16,2,21,28,216,2+statusbaryoffset,statusbarback);
                    drawlife();
                    lifevests--;
                    textbuf[0] = 9, textbuf[1] = 0;
                    textprint(96,12+statusbaryoffset,(char)0);
                    textbuf[0] = lifevests+48, textbuf[1] = 0;
                    textprint(96,12+statusbaryoffset,(char)176);
                    musicon();
                    posz = 32;
                    posx = startx;
                    posy = starty;
                    youarehere();
                    ang = startang;
                    angvel = 0;
                    vel = 0;
                    mxvel = 0;
                    myvel = 0;
                    svel = 0;
                    hvel = 0;
                    fade(63);
                }
                else
                {
                    /* Game over, man! */

                    fade(0);

                    glClearColor(0,0,0,0);
                    glClear(GL_COLOR_BUFFER_BIT);

                    ingame=0;
                    fade(fadewarpval);
                    pictur(180,halfheight,144<<2,0,gameover);
                    SDL_GL_SwapWindow(mainwindow);

                    SDL_Delay(1000);

                    for(i=fadewarpval;i>=0;i-=2)
                    {
                        glClear(GL_COLOR_BUFFER_BIT);
                        fade(i);
                        pictur(180,halfheight,144<<2,0,gameover);
                        SDL_GL_SwapWindow(mainwindow);
                        SDL_Delay(20);
                    }
                    fade(63);
                    kgif(1);
                    introduction(1);
                }
            }
        }

        for(i=0;i<explonum;i++)
        {
            if (totalclock>explotime[i]) {
                explonum--;
                explotime[i]=explotime[explonum];
                explox[i]=explox[explonum];
                exploy[i]=exploy[explonum];
                explostat[i]=explostat[explonum];
                i--;
                continue;
            }
        }

        /* Find bullets... */

        for(i=0;i<bulnum;i++)
        {
            if (bulstat[i] < totalclock)
            {
                bulnum--;
                bulx[i] = bulx[bulnum];
                buly[i] = buly[bulnum];
                bulang[i] = bulang[bulnum];
                bulstat[i] = bulstat[bulnum];
                bulkind[i] = bulkind[bulnum];
            }
        }

        /* Draw scene... */

        /* Status bar toggled? */

        if (statusbar == statusbargoal)
        {
            if (getkeydefstat(ACTION_STATUS) > 0)
            {
                if (lastbarchange == 0)
                {
                    if (vidmode == 0)
                        statusbargoal = (399+335)-statusbar;
                    else
                        statusbargoal = (479+415)-statusbar;
                    if (statusbargoal > statusbar)
                        scrsize += 2880;
                    if (vidmode == 1)
                    {
/*			statusbar = statusbargoal;
                        linecompare(statusbar);*/
                        if (statusbar == 415)
                        {
                            scrsize = 18720;
//			    statusbaralldraw();
                        }
                        else
                            scrsize = 21600;
                    }
                }
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
                {
                    statusbar = statusbargoal;
                    scrsize -= 2880;
                }
            }
            else
            {
                statusbar += (clockspd>>1);
                if (statusbar >= statusbargoal)
                    statusbar = statusbargoal;
            }
            linecompare(statusbar);
        }

        update_bulrot(posx, posy);

        if (demorecording) {
            /*int timediff = totalclock-ototclock;
              if (timediff > 4) timediff = 4;*/
            demofile_write_frame(demorecording, clockspd);
            if (demofile_rewindable(demorecording) && getkeydefstat(ACTION_REWIND)) {
                playdemo(demorecording, NULL, 1);
                clockspeed = 4;
            }
        }

        picrot(posx,posy,posz,ang);
        debuginfo();

    demo_continue_entry:

        if ((death<4095)&&(lifevests == 0))
        {
/*		for(i=lside;i<rside;i++)
                height[i] = 0;*/

            /* Draw GAME OVER sign... */

            x = 180 + ((K_INT16)(sintable[(2048-death)&2047]>>10));
            y = halfheight + ((K_INT16)(sintable[death&2047]>>11));

            ingame=0;
            j=fadelevel;
            fade(fadewarpval);
            pictur(x,y,(144-(death>>5))<<2,(death&2047)^2047,gameover);
            fade(j);
            ingame=1;

        }
        sortcnt = 0;
        SDL_LockMutex(soundmutex);
        SDL_LockMutex(timermutex);

        /* Speed cap at 2 ticks/frame (about 120 fps). */
        if ((musicstatus == 1) && (clockspeed >= 0) && (clockspeed < (demorecording ? 4 : 2))) {
            SDL_UnlockMutex(soundmutex);
            while(clockspeed < (demorecording ? 4 : 2)) {
                SDL_Delay(0); /* Give other threads a chance. */
                updateclock();
            }
            SDL_LockMutex(soundmutex);
        }
        if (musicstatus!=1)
            SDL_Delay(10); /* Just to prevent insane speeds... */

        clockspd=clockspeed;

        if (clockspd>240) clockspd=240; /* Prevent total insanity if game
                                           is suspended. */
        clockspeed=0;
        SDL_UnlockMutex(timermutex);
        SDL_UnlockMutex(soundmutex);

        frames++;
        timeused+=clockspd;

        /* Update animation frame counts... */

        if ((totalclock^ototclock) > 15)
        {
            animate2 = (animate2^1);
            animate3++;
            if (animate3 == 3)
                animate3 = 0;
            animate4 = (animate4+1)&3;
            switch(oscillate3)
            {
                case 0: oscillate3 = 1; break;
                case 1: oscillate3 = 2; break;
                case 2: oscillate3 = 1025; break;
                case 1025: oscillate3 = 0; break;
            }
            switch(oscillate5)
            {
                case 0: oscillate5 = 1; break;
                case 1: oscillate5 = 2; break;
                case 2: oscillate5 = 3; break;
                case 3: oscillate5 = 4; break;
                case 4: oscillate5 = 1027; break;
                case 1027: oscillate5 = 1026; break;
                case 1026: oscillate5 = 1025; break;
                case 1025: oscillate5 = 0; break;
            }
            animate8++;
            if (animate8 == 8)
                animate8 = 0;
            animate10++;
            if (animate10 == 10)
                animate10 = 0;
            animate11++;
            if (animate11 == 11)
                animate11 = 0;
            animate15++;
            if (animate15 == 15)
                animate15 = 0;
        }
        animate6++;
        if (animate6 == 6)
            animate6 = 0;
        animate7++;
        if (animate7 == 6)
            animate7 = 0;
        if (death == 4095)
        {
            /* Choose weapons... */

            if ((getkeydefstat(ACTION_FIREBALLS)|getkeydefstat(ACTION_BOUNCY)|getkeydefstat(ACTION_HEAT)|getkeydefstat(ACTION_NEXTWEAPON)) > 0)
            {
                j = bulchoose;
                if (getkeydefstat(ACTION_FIREBALLS) > 0) bulchoose = 0;
                if (getkeydefstat(ACTION_BOUNCY) > 0) bulchoose = 1;
                if (getkeydefstat(ACTION_HEAT) > 0) bulchoose = 2;
                if (getkeydefstatlock(ACTION_NEXTWEAPON) > 0) {
                    i = 2;
                    do {
                        bulchoose = (bulchoose + 1) % 3;
                    } while (firepowers[bulchoose] == 0 && --i);
                }
                if (firepowers[bulchoose] == 0)
                {
                    ksay(12);
                    bulchoose = j;
                }
                else
                {
                    /* if (j == 0)*/ x = 268, y = 11;
                    if (j == 1) x = 268, y = 20;
                    if (j == 2) x = 292, y = 11;
                    statusbardraw(16,y,25,1,x,y+statusbaryoffset,statusbarback);
                    statusbardraw(16,y+8,25,1,x,y+8+statusbaryoffset,statusbarback);
                    statusbardraw(16,y+1,1,7,x,y+1+statusbaryoffset,statusbarback);
                    statusbardraw(16,y+1,1,7,x+24,y+1+statusbaryoffset,statusbarback);
                    if (j == 1)
                        statusbardraw(41,28,1,9,292,20+statusbaryoffset,statusbarinfo);
                    if (bulchoose == 0) x = 268, y = 11;
                    if (bulchoose == 1) x = 268, y = 20;
                    if (bulchoose == 2) x = 292, y = 11;
                    statusbardraw(32,28,8,9,x,y+statusbaryoffset,statusbarinfo);
                    statusbardraw(34,28,6,9,x+8,y+statusbaryoffset,statusbarinfo);
                    statusbardraw(34,28,6,9,x+14,y+statusbaryoffset,statusbarinfo);
                    statusbardraw(36,28,5,9,x+20,y+statusbaryoffset,statusbarinfo);
                }
            }

            /* Fire... */

            if (((getkeydefstat(ACTION_FIRE) > 0) || ((bstatus&1) > 0)))
            {
                if (lastbulshoot+240-(firepowers[bulchoose]<<5) < totalclock)
                {
                    if ((firepowers[bulchoose] > 0) && (bulnum < 64))
                    {
                        bulx[bulnum] = posx;
                        buly[bulnum] = posy;
                        bulang[bulnum] = ang;
                        bulstat[bulnum] = totalclock+120+(lightnings<<5);
                        bulkind[bulnum] = bulchoose+1;
                        lastbulshoot = totalclock;
                        bulnum++;
                        if (bulchoose == 2)
                            ksay(22);
                        else
                            ksay(15);
                    }
                    else
                    {
                        if ((lastshoot == 0) && (firepowers[bulchoose] == 0))
                            ksay(12);
                    }
                }
                lastshoot = 1;
            }
            else
                lastshoot = 0;
        }

        /* Move bullets and check for hits... */

        for(i=0;i<bulnum;i++)
        {
            // x = (int)((clockspd*sintable[(bulang[i]+512)&2047])>>13);
            x=(K_INT16)((clockspd*sintable[((bulang[i]+512)&1023)])>>13);
            if ((bulang[i]+512)&1024) x=-x;
            // y = (int)((clockspd*sintable[bulang[i]])>>13);
            y=(K_INT16)((clockspd*sintable[bulang[i]&1023])>>13);
            if (bulang[i]&1024) y=-y;

            if (bulkind[i] == 15)
            {
                x -= (x>>1);
                y -= (y>>1);
            }
            if (bulkind[i] == 17)
            {
                x += (x>>1);
                y += (y>>1);
            }
            for(m=0;m<4;m++)
                if (bulstat[i] > 0)
                {
                    bulx[i] += x;
                    buly[i] += y;
                    if ((bultype[bulkind[i]] == 2) && (death == 4095) && (m < 4))
                    {
                        l = labs((K_INT32)bulx[i]-(K_INT32)posx)+labs((K_INT32)buly[i]-(K_INT32)posy);
                        if (l < 768)
                        {
                            if (greentime < totalclock)
                            {
                                if (capetime[0] < totalclock)
                                {
                                    n = 128;
                                    if (bulkind[i] == 7) n = 320;
                                    if (bulkind[i] == 9) n = 192;
                                    if ((bulkind[i] >= 15) && (bulkind[i] <= 17)) n = 320;
                                    if (bulkind[i] == 13) n = 320;
                                    if (purpletime >= totalclock)
                                        n >>= 1;
                                    life -= n;
                                    if (life <= 0)
                                    {
                                        life = 0;
                                        drawlife();
                                        death = 4094;
                                        angvel = (rand()&32)-16;
                                        ksay(5);
                                        musicoff();
                                    }
                                    else
                                    {
                                        drawlife();
                                        angvel = (rand()&32)-16;
                                        fadehurtval += 16;
                                        ksaystereo(14,bulx[i],buly[i]);
                                    }
                                }
                                bulnum--;
                                bulx[i] = bulx[bulnum];
                                buly[i] = buly[bulnum];
                                bulang[i] = bulang[bulnum];
                                bulstat[i] = bulstat[bulnum];
                                bulkind[i] = bulkind[bulnum];
                                m = 4;
                            }
                            else
                            {
                                do
                                {
                                    l = labs((K_INT32)bulx[i]-(K_INT32)posx)+labs((K_INT32)buly[i]-(K_INT32)posy);
                                    bulx[i] -= x;
                                    buly[i] -= y;
                                }
                                while (l < 768);
                                bulang[i] = ((bulang[i]+1024)&2047);
                                m = 4;
                                switch(bulkind[i])
                                {
                                    case 5: case 7: case 9: case 11: case 13:
                                    case 22: case 24: bulkind[i]++; break;
                                }
                                bulstat[i] = totalclock+240;
                            }
                        }
                    }
                    j = board[bulx[i]>>10][buly[i]>>10]&16384;
                    if ((j > 0) && (bultype[bulkind[i]] == 1) && (m < 4))
                    {
                        l = 0;
                        for(k=0;k<mnum;k++)
                            if (labs((K_INT32)bulx[i]-(K_INT32)mposx[k])+labs((K_INT32)buly[i]-(K_INT32)mposy[k]) < 768)
                            {
                                if ((mstat[k] == monan2) && (mshock[k] > 0) && (mshot[k] < 32))
                                {
                                    while (abs((K_INT16)(bulx[i]-mposx[k]))+abs((K_INT16)(buly[i]-mposy[k])) < 768)
                                    {
                                        bulx[i] -= x;
                                        buly[i] -= y;
                                    }
                                    bulang[i] = ((bulang[i]+1024)&2047);
                                    if (bulkind[i] < 5)
                                        bulkind[i] += 17;
                                    ksaystereo(2,bulx[i],buly[i]);
                                    m = 4;
                                }
                                else if ((mstat[k] != monhol) && (((mstat[k] != monke2) && (mstat[k] != monzor) && (mstat[k] != monan3)) || (mshock[k] == 0)))
                                {
                                    mshot[k]--;
                                    if ((purpletime >= totalclock) && (mshot[k] > 0))
                                        mshot[k]--;
                                    if (bulkind[i] == 3)
                                    {
                                        if (mshot[k] > 0)
                                            mshot[k]--;
                                        else
                                            l |= 4;
                                    }
                                    if (mshot[k] > 0)
                                    {
                                        j = 0;
                                        if ((mstat[k] == monan2) || (mstat[k] == monke2))
                                        {
                                            if (mshot[k] < 32)
                                                j = 64+(rand()&255);
                                            else
                                                j = 45;
                                        }
                                        switch(mstat[k])
                                        {
                                            case monrob: j = 0; break;
                                            case monzor: j = 45; break;
                                            case monand: case monali: j = 60; break;
                                            case mongre: case mongr2:
                                            case monwit: j = 80; break;
                                        }
                                        if ((mstat[k] != monbal) && (mstat[k] != mongho))
                                            l |= 2;
                                        if ((mshock[k] >= 0) && (mshock[k] < 8192))
                                        {
                                            mshock[k] += j;
                                            if (mshock[k] >= 8192)
                                                mshock[k] = 8191;
                                        }
                                    }
                                    if (mshot[k] == 0)
                                    {
                                        if (mstat[k] == hive)
                                            mshock[k] = 16384;
                                        if ((mstat[k] == monbal) || (mstat[k] == mongho) || (mstat[k] == hive) || (mstat[k] == mondog))
                                        {
                                            if (mstat[k] != hive)
                                                mshot[k] = 1;
                                            else
                                                ksaystereo(30,bulx[i],buly[i]);
                                            if (mstat[k] != mondog)
                                                l |= 4;
                                            else
                                                ksaystereo(32,bulx[i],buly[i]);
                                            bulnum--;
                                            bulx[i] = bulx[bulnum];
                                            buly[i] = buly[bulnum];
                                            bulang[i] = bulang[bulnum];
                                            bulstat[i] = bulstat[bulnum];
                                            bulkind[i] = bulkind[bulnum];
                                            m = 4;
                                        }
                                        else
                                        {
                                            l |= 1;
                                            if ((mstat[k] == monan3) || (mstat[k] == monhol))
                                                addexplosion(mposx[k],mposy[k],miniexplosion);
                                            else
                                                addexplosion(mposx[k],mposy[k],explosion);
                                            if (mstat[k] == monzor)
                                            {
                                                if (boardnum == 9)
                                                {
                                                    xwarp[2] = xwarp[1];
                                                    ywarp[2] = ywarp[1];
                                                    j = (mposx[k]>>10);
                                                    jj = (mposy[k]>>10);
                                                    l = 0;
                                                    do
                                                    {
                                                        xwarp[3] = (char)j+(rand()&31)-16;
                                                        ywarp[3] = (char)jj+(rand()&31)-16;
                                                        l++;
                                                    }
                                                    while ((board[(int)xwarp[3]][(int)ywarp[3]] != 1024) || ((abs(((K_INT16)xwarp[3])-j)+abs(((K_INT16)ywarp[3])-jj) < 16) && (l < 16)));
                                                    board[(int)xwarp[3]][(int)ywarp[3]] = warp+1024;
                                                    xwarp[1] = (char)j;
                                                    ywarp[1] = (char)jj;
                                                    board[j][jj] = warp+1024;
                                                    numwarps = 4;
                                                    mposx[mnum] = ((mposx[k]>>10)<<10)+512;
                                                    mposy[mnum] = ((mposy[k]>>10)<<10)+512;
                                                    mgolx[mnum] = mposx[mnum];
                                                    mgoly[mnum] = mposy[mnum];
                                                    moldx[mnum] = mposx[mnum];
                                                    moldy[mnum] = mposy[mnum];
                                                    mstat[mnum] = mondog;
                                                    mshock[mnum] = 0;
                                                    mshot[mnum] = 1;
                                                    mnum++;
                                                    board[mposx[k]>>10][mposy[k]>>10] |= 0x4000;
                                                    justwarped = 1;
                                                    if (cheated != 0)
                                                        board[54][20] = youcheated;
                                                }
                                                if (boardnum == 29)
                                                {
                                                    j = (mposx[k]>>10);
                                                    jj = (mposy[k]>>10);
                                                    xwarp[3] = (char)j;
                                                    ywarp[3] = (char)jj;
                                                    numwarps = 4;
                                                    board[j][jj] = warp+1024;
                                                    justwarped = 1;
                                                }
                                            }
                                            if (mstat[k] == monan2)
                                            {
                                                j = (mposx[k]>>10);
                                                jj = (mposy[k]>>10);
                                                board[j][jj] = silverkey+1024;
                                            }
                                            if (mstat[k] == monke2)
                                            {
                                                j = (mposx[k]>>10);
                                                jj = (mposy[k]>>10);
                                                board[j][jj] = goldkey+1024;
                                            }
                                            switch(mstat[k])
                                            {
                                                case monbat:
                                                case monear:
                                                case monbee: scorecount += 50; l |= 16; break;
                                                case monspi: scorecount += 50; l |= 8; break;
                                                case monken:
                                                case monro2: scorecount += 100; break;
                                                case monske:
                                                case monmum: scorecount += 100; l |= 8; break;
                                                case mongre:
                                                case mongr2:
                                                case monrob:
                                                case monwit:
                                                case monbal:
                                                case monan3: scorecount += 250; break;
                                                case monand: scorecount += 500; break;
                                                case monali: scorecount += 1000; break;
                                                case monhol: scorecount += 1500; break;
                                                case mongho: scorecount += 2500; break;
                                                case monzor:
                                                case monke2:
                                                case monan2: scorecount += 5000; break;
                                                case mondog: scorecount >>= 1; break;
                                            }
                                            drawscore(scorecount);

                                            delmonster(k);
                                        }
                                    }
                                }
                            }
                        if ((l > 0) && (m != 4))
                        {
                            if (death == 4095)
                            {
                                if ((l&1) > 0)
                                {
                                    if ((l&8) == 0)
                                    {
                                        if ((l&16) == 0)
                                            ksaystereo(1,bulx[i],buly[i]);
                                        else
                                            ksaystereo(0,bulx[i],buly[i]);
                                    }
                                    else
                                        ksaystereo(30,bulx[i],buly[i]);
                                }
                                if ((l&2) > 0)
                                    ksaystereo(8,bulx[i],buly[i]);
                            }
                            if ((l&4) == 0)
                            {
                                bulnum--;
                                bulx[i] = bulx[bulnum];
                                buly[i] = buly[bulnum];
                                bulang[i] = bulang[bulnum];
                                bulstat[i] = bulstat[bulnum];
                                bulkind[i] = bulkind[bulnum];
                            }
                            else
                                bulkind[i] = 4;
                            m = 4;
                        }
                    }
                    j = board[bulx[i]>>10][buly[i]>>10];
                    jj = (j&1023);
                    if ((bmpkind[jj] != 0) && (((bulx[i]|1023) != (posx|1023)) || ((buly[i]|1023) != (posy|1023))) && (death == 4095))
                    {
                        if ((bultype[bulkind[i]] == 1) && ((jj == kenface) || (jj == kenfaceouch)) && (m < 4))
                        {
                            if (jj == kenface)
                                board[bulx[i]>>10][buly[i]>>10] = kenfaceouch;
                            life -= 64;
                            if (life <= 0)
                            {
                                life = 0;
                                drawlife();
                                death = 4094;
                                angvel = (rand()&32)-16;
                                ksay(5);
                                musicoff();
                            }
                            else
                            {
                                drawlife();
                                angvel = (rand()&32)-16;
                                fadehurtval += 16;
                                ksaystereo(14,bulx[i],buly[i]);
                            }
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if ((bultype[bulkind[i]] == 1) && (jj == andy) && (m < 4))
                        {
                            if (mnum < 512)
                            {
                                mposx[mnum] = ((bulx[i]>>10)<<10)+512;
                                mposy[mnum] = ((buly[i]>>10)<<10)+512;
                                mgolx[mnum] = mposx[mnum];
                                mgoly[mnum] = mposy[mnum];
                                moldx[mnum] = mposx[mnum];
                                moldy[mnum] = mposy[mnum];
                                mstat[mnum] = monand;
                                mshock[mnum] = 0;
                                mshot[mnum] = 10;
                                mnum++;
                                board[bulx[i]>>10][buly[i]>>10] |= 0x4000;
                                if ((rand()&7) == 0)
                                {
                                    board[bulx[i]>>10][buly[i]>>10] &= 0xfc00;
                                    board[bulx[i]>>10][buly[i]>>10] |= andygone;
                                }
                            }
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if ((bultype[bulkind[i]] == 1) && (((j&0xfff) == target) || ((j&0xfff) == clock)))
                        {
                            if ((j&0xfff) == target) ksaystereo(3,bulx[i],buly[i]);
                            if ((j&0xfff) == clock) ksaystereo(31,bulx[i],buly[i]);
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if (((wallheader[jj]&8) == 0) && (jj != net) && (m < 4))
                        {
                            n = 0;
                            if ((bmpkind[jj] == 1) || (bmpkind[jj] == 4))
                                n = 1;
                            if ((n == 1) || ((((bulx[i]&1023)<<2) > lborder[jj]) && (((bulx[i]&1023)<<2) < (4096-lborder[jj])) && (((buly[i]&1023)<<2) > lborder[jj]) && (((buly[i]&1023)<<2) < (4096-lborder[jj]))))
                            {
                                if (((j&2048) > 0) && (bultype[bulkind[i]] == 1))
                                {
                                    ksaystereo(10,bulx[i],buly[i]);
                                    addexplosion(bulx[i],buly[i],explosion);
                                    if (jj == bricksfull)
                                        board[bulx[i]>>10][buly[i]>>10] = brickshalf+(j&0xfc00);
                                    else if ((j&1024) == 0)
                                        board[bulx[i]>>10][buly[i]>>10] = 1024;
                                    else
                                        board[bulx[i]>>10][buly[i]>>10] ^= 2048;
                                    scorecount += 750;
                                    drawscore(scorecount);
                                    bulnum--;
                                    bulx[i] = bulx[bulnum];
                                    buly[i] = buly[bulnum];
                                    bulang[i] = bulang[bulnum];
                                    bulstat[i] = bulstat[bulnum];
                                    bulkind[i] = bulkind[bulnum];
                                    m = 4;
                                }
                                if (((purpletime < totalclock) || (bulkind[i] == 2) || (bulkind[i] >= 5)) && (m < 4))
                                {
                                    if ((bulkind[i] == 2) || (bulkind[i] == 22))
                                    {
                                        while ((wallheader[board[bulx[i]>>10][buly[i]>>10]&1023]&8) == 0)
                                        {
                                            bulx[i] -= x;
                                            buly[i] -= y;
                                        }
                                        newx = bulx[i]+x;
                                        newy = buly[i]+y;
                                        oposx = bulx[i];
                                        oposy = buly[i];
                                        k = 0;
                                        if ((newx&0xfc00) < (oposx&0xfc00))
                                        {
                                            plcx = (oposx&0xfc00);
                                            plcy = oposy+(K_INT16)((((K_INT32)oposx-(K_INT32)plcx)*tantable[bulang[i]&1023])>>16);
                                            if ((wallheader[board[(plcx>>10)-1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                            if ((wallheader[board[(plcx>>10)-1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                        }
                                        if ((newx&0xfc00) > (oposx&0xfc00))
                                        {
                                            plcx = (oposx&0xfc00)+1023;
                                            plcy = oposy+(K_INT16)((((K_INT32)oposx-(K_INT32)plcx)*tantable[bulang[i]&1023])>>16);
                                            if ((wallheader[board[(plcx>>10)+1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                            if ((wallheader[board[(plcx>>10)+1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                        }
                                        if ((newy&0xfc00) < (oposy&0xfc00))
                                        {
                                            plcy = (oposy&0xfc00);
                                            plcx = oposx+(K_INT16)((((K_INT32)oposy-(K_INT32)plcy)*tantable[(2560-bulang[i])&1023])>>16);
                                            if ((wallheader[board[plcx>>10][(plcy>>10)-1]&1023]&8) == 0)
                                                k |= 2;
                                            if ((wallheader[board[plcx>>10][(plcy>>10)-1]&1023]&8) == 0)
                                                k |= 2;
                                        }
                                        if ((newy&0xfc00) > (oposy&0xfc00))
                                        {
                                            plcy = (oposy&0xfc00)+1023;
                                            plcx = oposx+(K_INT16)((((K_INT32)oposy-(K_INT32)plcy)*tantable[(2560-bulang[i])&1023])>>16);
                                            if ((wallheader[board[plcx>>10][(plcy>>10)+1]&1023]&8) == 0)
                                                k |= 2;
                                            if ((wallheader[board[plcx>>10][(plcy>>10)+1]&1023]&8) == 0)
                                                k |= 2;
                                        }
                                        if ((k&1) > 0)
                                            bulang[i] = ((3072-bulang[i])&2047);
                                        if ((k&2) > 0)
                                            bulang[i] = ((2048-bulang[i])&2047);
                                        m = 4;
                                    }
                                    else
                                    {
                                        bulnum--;
                                        bulx[i] = bulx[bulnum];
                                        buly[i] = buly[bulnum];
                                        bulang[i] = bulang[bulnum];
                                        bulstat[i] = bulstat[bulnum];
                                        bulkind[i] = bulkind[bulnum];
                                        m = 4;
                                    }
                                }
                            }
                        }
                        if ((jj == net) && (m < 4))
                        {
                            while ((board[bulx[i]>>10][buly[i]>>10]&1023) == net)
                            {
                                bulx[i] -= x;
                                buly[i] -= y;
                            }
                            bulang[i] = ((bulang[i]+1024)&2047);
                            m = 4;
                            if (bulkind[i] < 5)
                            {
                                if (death == 4095)
                                    ksaystereo(2,bulx[i],buly[i]);
                                bulkind[i] += 17;
                                bulstat[i] = totalclock+240;
                            }
                        }
                    }
                }
        }
        i = 0;				     //scan vicinity
        mrotbuf[0] = ((posx>>10)<<6)+(posy>>10);
        tempbuf[mrotbuf[0]] = 20;
        j = 1;				     //j is stop (end of nodes)

        /* This block converted from asm... Apparently, it does a sort of
           breadth-first search on neighbouring board squares up to a distance
           of 20 squares, limited by walls. Translation: it works out which
           squares are in the same room within 20 squares, and their distance
           in increasing order of distance. */

        bx=mrotbuf[i]; /* bx always seems to be the same as mrotbuf[i]. */
        while((i!=j)&&(tempbuf[bx]>0)) {
            if ((tempbuf[bx-64]==0)&&((board[0][bx-64]&0x0c00)==1024)) {
                mrotbuf[j]=bx-64;
                tempbuf[bx-64]=tempbuf[bx]-1;
                j++;
            }
            if ((tempbuf[bx+64]==0)&&((board[0][bx+64]&0x0c00)==1024)) {
                mrotbuf[j]=bx+64;
                tempbuf[bx+64]=tempbuf[bx]-1;
                j++;
            }
            if ((tempbuf[bx-1]==0)&&((board[0][bx-1]&0x0c00)==1024)) {
                mrotbuf[j]=bx-1;
                tempbuf[bx-1]=tempbuf[bx]-1;
                j++;
            }
            if ((tempbuf[bx+1]==0)&&((board[0][bx+1]&0x0c00)==1024)) {
                mrotbuf[j]=bx+1;
                tempbuf[bx+1]=tempbuf[bx]-1;
                j++;
            }
            i++;
            bx=mrotbuf[i];
        }

        /* Move monsters and allow them to fire and suchlike... */

        bossmonster = 0;

        for(i=0;i<mnum;i++)
        {
            if (mposx[i] > posx) templong = (K_INT32)(mposx[i]-posx);
            else templong = (K_INT32)(posx-mposx[i]);
            if (mposy[i] > posy) templong += (K_INT32)(mposy[i]-posy);
            else templong += (K_INT32)(posy-mposy[i]);
            if (templong < 16384)
            {
                x = (mposx[i]>>10);
                y = (mposy[i]>>10);
                switch(mstat[i])
                {
                    case mongho: j = clockspd; break;
                    case monhol:
                    case monro2: j = (clockspd<<2); break;
                    case mondog:
                        if (templong < 3072)
                            j = (clockspd<<2);
                        else if (templong < 6144)
                            j = (clockspd<<4);
                        else
                            j = (clockspd<<5)+(clockspd<<3);
                        break;
                    case monken:
                    case monmum:
                    case monske: j = (clockspd<<2)+(clockspd<<1); break;
                    case monbal:
                    case monke2:
                    case monan2: j = (clockspd<<3); break;
                    case mongre:
                    case mongr2:
                    case monrob:
                    case monwit:
                    case monear: j = (clockspd<<3)+(clockspd<<1); break;
                    case monand:
                    case monbat:
                    case monbee:
                    case monspi: j = (clockspd<<3)+(clockspd<<2); break;
                    case monali:
                    case monan3: j = (clockspd<<4); break;
                    case monzor:
                        j = (clockspd<<3);
                        if ((mshock[i]&8192) > 0)
                            j += (clockspd<<4);
                        break;
                    case hive:
                        j = 0;
                        if ((mshock[i] == 0) && (mnum < 512) && ((rand()&1023) <= clockspd))
                        {
                            mposx[mnum] = (mposx[i]&0xfc00)+512;
                            mposy[mnum] = (mposy[i]&0xfc00)+512;
                            mgolx[mnum] = mposx[mnum];
                            mgoly[mnum] = mposy[mnum];
                            moldx[mnum] = mposx[mnum];
                            moldy[mnum] = mposy[mnum];
                            mstat[mnum] = monbee;
                            mshock[mnum] = 0;
                            mshot[mnum] = 1;
                            board[x][y] |= 0x4000;
                            mnum++;
                        }
                        break;
                }
                if ((mshock[i]&8192) > 0)
                    if (mshock[i] > 8192)
                        mshock[i]--;
                if ((mshock[i]&16384) > 0)
                    if (mshock[i] < 24575)
                        mshock[i]++;
                if ((mshock[i] > 0) && (mshock[i] < 8192))
                {
                    mshock[i] -= clockspd;
                    if (mshock[i] < 0)
                        mshock[i] = 0;
                }
                if ((mshock[i] == 0) || (mstat[i] == monke2) || (mstat[i] == monzor) || (mstat[i] == monan3))
                {
                    if (mgolx[i] > mposx[i])
                    {
                        mposx[i] += j;
                        if (mposx[i] > mgolx[i]) mposx[i] = mgolx[i];
                    }
                    else if (mgolx[i] < mposx[i])
                    {
                        mposx[i] -= j;
                        if (mposx[i] < mgolx[i]) mposx[i] = mgolx[i];
                    }
                    if (mgoly[i] > mposy[i])
                    {
                        mposy[i] += j;
                        if (mposy[i] > mgoly[i]) mposy[i] = mgoly[i];
                    }
                    else if (mgoly[i] < mposy[i])
                    {
                        mposy[i] -= j;
                        if (mposy[i] < mgoly[i]) mposy[i] = mgoly[i];
                    }
                }
                if ((templong < 768) && (death == 4095) && (mstat[i] != mondog))
                {
                    if ((mstat[i] == monhol) && (mshock[i] > 0))
                    {
                        life = 0;
                        drawlife();
                        death = 4094;
                        angvel = 0;
                        ksay(6);
                        musicoff();
                    }
                    else if (capetime[1] < totalclock)
                    {
                        if (capetime[0] < totalclock)
                        {
                            if ((mstat[i] == monan2) && (mshock[i] > 0) && (mshock[i] < 8192) && (mshot[i] < 32))
                            {
                                life = 0;
                                drawlife();
                                death = 4094;
                                angvel = (rand()&32)-16;
                                ksay(5);
                                musicoff();
                            }
                            else if (mshock[i] == 0)
                            {
                                life -= 16+(clockspd<<2);
                                if (life <= 0)
                                {
                                    life = 0;
                                    drawlife();
                                    death = 4094;
                                    angvel = (rand()&32)-16;
                                    ksay(5);
                                    musicoff();
                                }
                                else
                                {
                                    drawlife();
                                    angvel = (rand()&32)-16;
                                    fadehurtval += 16;
                                    ksaystereo(14,mposx[i],mposy[i]);
                                }
                            }
                        }
                    }
                    else
                    {
                        if ((mstat[i] != monzor) && (mstat[i] != monke2) && (mstat[i] != monan2) && (mstat[i] != monan3))
                        {
                            if ((mstat[i] == monan3) || (mstat[i] == monhol))
                                addexplosion(mposx[i],mposy[i],miniexplosion);
                            else
                                addexplosion(mposx[i],mposy[i],explosion);
                            ksaystereo(30,mposx[i],mposy[i]);
                            delmonster(i);
                        }
                    }
                }
                if ((mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]) && ((mshock[i] == 0) || (mstat[i] == monke2) || (mstat[i] == monzor) || (mstat[i] == monan3)))
                {
                    if ((board[mposx[i]>>10][mposy[i]>>10]&1023) == hole)
                    {
                        if (mstat[i] == monbal)
                            checkobj(mposx[i],mposy[i],posx,posy,ang,monbal+4);
                        delmonster(i);
                        ksaystereo(6,mposx[i],mposy[i]);
                    }
                    else
                    {
                        if (mstat[i] == monhol)
                        {
                            k = 4;
                            do
                            {
                                j = (rand()&3);
                                if ((j == 0) && ((board[x-1][y]&0x4c00) == 1024))
                                    mgolx[i] = mposx[i]-1024;
                                if ((j == 1) && ((board[x+1][y]&0x4c00) == 1024))
                                    mgolx[i] = mposx[i]+1024;
                                if ((j == 2) && ((board[x][y-1]&0x4c00) == 1024))
                                    mgoly[i] = mposy[i]-1024;
                                if ((j == 3) && ((board[x][y+1]&0x4c00) == 1024))
                                    mgoly[i] = mposy[i]+1024;
                                k--;
                            }
                            while ((k > 0) && (mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]));
                            mshock[i] = 480;
                        }
                        else if (mstat[i] != hive)
                        {
                            m = (x<<6)+y;
                            n = tempbuf[m];
                            k = 4;
                            do
                            {
                                j = rand()&3;
                                switch(j)
                                {
                                    case 0:
                                        if ((tempbuf[m-64] > n) && ((board[x-1][y]&0x4c00) == 1024))
                                            mgolx[i] = mposx[i]-1024;
                                        break;
                                    case 1:
                                        if ((tempbuf[m+64] > n) && ((board[x+1][y]&0x4c00) == 1024))
                                            mgolx[i] = mposx[i]+1024;
                                        break;
                                    case 2:
                                        if ((tempbuf[m-1] > n) && ((board[x][y-1]&0x4c00) == 1024))
                                            mgoly[i] = mposy[i]-1024;
                                        break;
                                    case 3:
                                        if ((tempbuf[m+1] > n) && ((board[x][y+1]&0x4c00) == 1024))
                                            mgoly[i] = mposy[i]+1024;
                                        break;
                                }
                                k--;
                            }
                            while ((k > 0) && (mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]));
                        }
                        if ((mstat[i] == monzor) || (mstat[i] == monke2) || (mstat[i] == monan2))
                        {
                            if (mstat[i] == monzor) j = 5000;
                            if (mstat[i] == monke2) j = 4000;
                            if (mstat[i] == monan2) j = 3000;
                            if (labs((K_INT32)mposx[i]-(K_INT32)posx)+labs((K_INT32)mposy[i]-(K_INT32)posy) < j)
                            {
                                if ((rand()&1) == 0)
                                {
                                    if ((posx > mposx[i]) && ((board[x-1][y]&0x4c00) == 1024))
                                        mgolx[i] = mposx[i]-1024, mgoly[i] = mposy[i];
                                    if ((posx < mposx[i]) && ((board[x+1][y]&0x4c00) == 1024))
                                        mgolx[i] = mposx[i]+1024, mgoly[i] = mposy[i];
                                }
                                else
                                {
                                    if ((posy > mposy[i]) && ((board[x][y-1]&0x4c00) == 1024))
                                        mgoly[i] = mposy[i]-1024, mgolx[i] = mposx[i];
                                    if ((posy < mposy[i]) && ((board[x][y+1]&0x4c00) == 1024))
                                        mgoly[i] = mposy[i]+1024, mgolx[i] = mposx[i];
                                }
                            }
                        }
                        if (((mgolx[i]&0xfc00) == (posx&0xfc00)) && ((mgoly[i]&0xfc00) == (posy&0xfc00)) && ((skilevel == 0) || (mstat[i] == mondog)))
                        {
                            mgolx[i] = moldx[i];
                            mgoly[i] = moldy[i];
                        }
                        board[moldx[i]>>10][moldy[i]>>10] &= 0xbfff;
                        if (mstat[i] == 0)
                        {
                            board[mposx[i]>>10][mposy[i]>>10] &= 0xbfff;
                            board[mgolx[i]>>10][mgoly[i]>>10] &= 0xbfff;
                        }
                        else
                        {
                            board[mposx[i]>>10][mposy[i]>>10] |= 0x4000;
                            board[mgolx[i]>>10][mgoly[i]>>10] |= 0x4000;
                        }
                        moldx[i] = mposx[i];
                        moldy[i] = mposy[i];
                    }
                }
                j = rand()&2047;
                switch(mstat[i])
                {
                    case mongre:
                    case mongr2:
                    case monro2: j &= 1023; break;
                    case monand:
                    case monrob:
                    case monwit: j &= 511; break;
                    case monali:
                    case monzor:
                    case mongho:
                    case monan2:
                    case monke2:
                    case monan3: j &= 127; break;
                }
                if ((j < clockspd) && (bulnum < 64) && (mshock[i] == 0))
                {
                    if ((mstat[i] != monbal) && (mstat[i] != monhol) && (mstat[i] != monbat) && (mstat[i] != monbee) && (mstat[i] != monspi) && (mstat[i] != hive) && (mstat[i] != mondog))
                    {
                        bulx[bulnum] = mposx[i];
                        buly[bulnum] = mposy[i];
                        k = 512;
                        if (mposx[i] != posx)
                        {
                            templong = (((((K_INT32)mposy[i]-(K_INT32)posy)<<12)/((K_INT32)mposx[i]-(K_INT32)posx))<<4);
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
                        if (mposy[i] > posy)
                            k += 1024;
                        bulang[bulnum] = ((k+2016+(rand()&63))&2047);
                        bulstat[bulnum] = totalclock+240;
                        if (mstat[i] == monand) bulstat[bulnum] += 240;
                        if (mstat[i] == monali) bulstat[bulnum] += 480;
                        bulkind[bulnum] = 5;
                        if (mstat[i] == monzor) bulkind[bulnum] = 7;
                        if (mstat[i] == monali) bulkind[bulnum] = 9;
                        if (mstat[i] == monke2)
                        {
                            if (mshot[i] >= 32)
                                bulkind[bulnum] = 17;
                            else if (mshot[i] >= 16)
                                bulkind[bulnum] = 16+(rand()&1);
                            else
                                bulkind[bulnum] = 15+(rand()%3);
                        }
                        if (mstat[i] == monan2) bulkind[bulnum] = 13;
                        if (mstat[i] == monan3) bulkind[bulnum] = 11;
                        if (mstat[i] == monwit) bulkind[bulnum] = 22;
                        if ((mstat[i] == monrob) || (mstat[i] == monro2))
                            bulkind[bulnum] = 24;
                        bulnum++;
                    }
                }
                if (mstat[i] == monzor || mstat[i] == monke2 || mstat[i] == monan2) {

                    if (tempbuf[((mposx[i]>>10)<<6)|(mposy[i]>>10)]) {
                        bossmonster = i + 1;
                    }

                }
                if (mstat[i] == monzor)
                    if ((mshot[i] < 24) && (mshock[i] == 0) && ((rand()&1023) <= clockspd))
                        mshock[i] = 16384+monzor+6;
                if (mstat[i] == monke2)
                    if ((mshot[i] < 16) && (mshock[i] == 0) && ((rand()&255) <= clockspd))
                        mshock[i] = 16384+monke2+7;
                if (mstat[i] == monan3)
                    if ((mshock[i] == 0) && ((rand()&511) <= clockspd))
                        mshock[i] = 16384+monan3+3;
                if (mstat[i] == monan2)
                    if ((mshot[i] < 24) && (mnum < 512) && ((rand()&1023) <= clockspd))
                    {
                        mposx[mnum] = (mposx[i]&0xfc00)+512;
                        mposy[mnum] = (mposy[i]&0xfc00)+512;
                        mgolx[mnum] = mposx[mnum];
                        mgoly[mnum] = mposy[mnum];
                        moldx[mnum] = mposx[mnum];
                        moldy[mnum] = mposy[mnum];
                        mstat[mnum] = monan3;
                        mshock[mnum] = monan3+8192+8;
                        mshot[mnum] = 1;
                        board[mposx[mnum]>>10][mposy[mnum]>>10] |= 16384;
                        mnum++;
                    }
            }
        }
        /* Get player moves... */

        if (death == 4095)
        {
            bstatus = 0;

            dfwdvel = getkeypressure(ACTION_FORWARD, 16384, 32768) - getkeypressure(ACTION_BACKWARD, 16384, 32768);
            dsidevel = getkeypressure(ACTION_MOVELEFT, 16384, 32768) - getkeypressure(ACTION_MOVERIGHT, 16384, 32768);
            if ((getkeydefstat(ACTION_STRAFE) == 0)) {
                dturnvel = getkeypressure(ACTION_RIGHT, 32768, 32768) - getkeypressure(ACTION_LEFT, 32768, 32768);
            } else {
                dsidevel += getkeypressure(ACTION_LEFT, 16384, 32768) - getkeypressure(ACTION_RIGHT, 16384, 32768);
                dturnvel = 0;
            }
            dstandvel = getkeypressure(ACTION_STANDLOW, 16384, 32768) - getkeypressure(ACTION_STANDHIGH, 16384, 32768);

            dturnvel = dturnvel * 37 / 32768;
            dfwdvel = dfwdvel * 257 / 32768;
            dsidevel = dsidevel * 257 / 32768;
            dstandvel = dstandvel * 257 / 32768;

            if (moustat == 0) {
                mousx = mousy = 0;
                bstatus|=readmouse(&mousx, &mousy);
                dturnvel += mousx;
                if (mouseverticalmovement)
                    dfwdvel -= (mousy<<3);
            }

            if (dturnvel < -36) dturnvel = -36;
            if (dturnvel > 36) dturnvel = 36;
            if (dfwdvel < -256) dfwdvel = -256;
            if (dfwdvel > 256) dfwdvel = 256;
            if (dsidevel < -256) dsidevel = -256;
            if (dsidevel > 256) dsidevel = 256;
            if (dstandvel < -256) dstandvel = -256;
            if (dstandvel > 256) dstandvel = 256;

            j = clockspd;
            if (j > 16)
                j = 16;
            angvel = smooth_input(angvel, dturnvel, clockspd, clockspd*2);
            vel = smooth_input(vel, dfwdvel, clockspd*6, clockspd*6);
            svel = smooth_input(svel, dsidevel, clockspd*6, clockspd*6);
            standvel = smooth_input(standvel, dstandvel, clockspd*6, clockspd*6);
        }
        ang = (ang+2048+(((angvel+mxvel)*clockspd)>>3))&2047;
        if (compass > 0)
            showcompass(ang);

        hvel = standvel >> 6;
        i=((hvel*clockspd)/8);
        if ((hvel<0)&&(i==0)) i=-1;
        if ((hvel>0)&&(i==0)) i=1;
        posz += i;
        if (posz < 8)
        {
            posz = 8;
            standvel = 0;
        }
        if (posz > 56)
        {
            posz = 56;
            standvel = 0;
        }

        /* Handle action button presses... */

        waterstat = 0;
        if (((getkeydefstat(ACTION_USE) > 0) ||
             ((bstatus&2) > 0)) && (death == 4095)) {
            /*printf("use!!! ihr=%d ds=%d\n", inhibitrepeat, doorstat);*/
            if (!inhibitrepeat)
            {
                x = (posx>>10);
                y = (posy>>10);
                brd0 = board[x-1][y]&1023;
                brd1 = board[x+1][y]&1023;
                brd2 = board[x][y-1]&1023;
                brd3 = board[x][y+1]&1023;

                /* Safe... */

                if (lastunlock == 0)
                {
                    i = 0;
                    if ((brd0 == safe+1) && (((ang+512)&1024) > 0))
                        i = 1, m = x-1, n = y, brd0 = safe+2;
                    if ((brd1 == safe+1) && (((ang+512)&1024) == 0))
                        i = 1, m = x+1, n = y, brd1 = safe+2;
                    if ((brd2 == safe+1) && ((ang&1024) > 0))
                        i = 1, m = x, n = y-1, brd2 = safe+2;
                    if ((brd3 == safe+1) && ((ang&1024) == 0))
                        i = 1, m = x, n = y+1, brd3 = safe+2;
                    if (i == 1)
                    {
                        inhibitrepeat=1;
                        board[m][n] = (board[m][n]&0xfc00)+(safe+2);
                        coins+=2;
                        if (coins > 999)
                            coins = 999;
                        ksay(7);
                        scorecount += 250;
                        drawscore(scorecount);
                        textbuf[0] = 9, textbuf[1] = 9;
                        textbuf[2] = 9, textbuf[3] = 0;
                        textprint(112,12+statusbaryoffset,(char)0);
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
                        textprint(112,12+statusbaryoffset,(char)176);
                        lastunlock = 1;
                    }
                    i = 0;
                    if ((brd0 == safe) && (((ang+512)&1024) > 0))
                        i = 1, m = x-1, n = y;
                    if ((brd1 == safe) && (((ang+512)&1024) == 0))
                        i = 1, m = x+1, n = y;
                    if ((brd2 == safe) && ((ang&1024) > 0))
                        i = 1, m = x, n = y-1;
                    if ((brd3 == safe) && ((ang&1024) == 0))
                        i = 1, m = x, n = y+1;
                    if (i == 1)
                    {
                        inhibitrepeat=1;
                        ksay(16);
                        i = rand()&3;
                        if (i == 0)
                            board[m][n] = (board[m][n]&0xfc00)+(safe+1);
                        else
                            board[m][n] = (board[m][n]&0xfc00)+(safe+2);
                        lastunlock = 1;
                    }
                }

                /* Fountain... */

                i = waterstat;
                if ((brd0 == fountain) && (((ang+512)&1024) > 0))
                    waterstat = 1, lastunlock = 1;
                if ((brd1 == fountain) && (((ang+512)&1024) == 0))
                    waterstat = 1, lastunlock = 1;
                if ((brd2 == fountain) && ((ang&1024) > 0))
                    waterstat = 1, lastunlock = 1;
                if ((brd3 == fountain) && ((ang&1024) == 0))
                    waterstat = 1, lastunlock = 1;
                if (waterstat == 1)
                    if ((totalclock^ototclock) > 127)
                    {
                        if (i == 0)
                            ksay(28);
                        life += 32;
                        if (life > 4095)
                            life = 4095;
                        drawlife();
                    }

                /* Soda machine... */

                if ((coins > 0) && (lastunlock == 0))
                {
                    i = 0;
                    if (((ang+512)&1024) > 0)
                        if (brd0 == soda)
                            i = 1;
                    if (((ang+512)&1024) == 0)
                        if (brd1 == soda)
                            i = 1;
                    if ((ang&1024) > 0)
                        if (brd2 == soda)
                            i = 1;
                    if ((ang&1024) == 0)
                        if (brd3 == soda)
                            i = 1;
                    if (i == 1)
                    {
                        inhibitrepeat=1;
                        sodamenu();
                        update_bulrot(posx, posy);
                        picrot(posx,posy,posz,ang);
                        lastunlock = 1;
                        lastbarchange = 1;
                        lastshoot = 1;
                    }

                    /* Slot machine... */

                    if (slottime == 0)
                    {
                        i = 0;
                        if (((ang+512)&1024) > 0)
                            if (brd0 == slotto)
                                i = 1;
                        if (((ang+512)&1024) == 0)
                            if (brd1 == slotto)
                                i = 1;
                        if ((ang&1024) > 0)
                            if (brd2 == slotto)
                                i = 1;
                        if ((ang&1024) == 0)
                            if (brd3 == slotto)
                                i = 1;
                        if (i == 1)
                        {
                            inhibitrepeat=1;
                            ksay(24);
                            coins--;
                            lastunlock = 1;
                            textbuf[0] = 9, textbuf[1] = 9;
                            textbuf[2] = 9, textbuf[3] = 0;
                            textprint(112,12+statusbaryoffset,(char)0);
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
                            textprint(112,12+statusbaryoffset,(char)176);
                            slottime = 512+(rand()&255);
                            slotpos[0] = (rand()&127);
                            slotpos[1] = (rand()&127);
                            slotpos[2] = (rand()&127);
                        }
                    }
                }

                /* Locks... */

                i = 0;
                if ((keys[0] > 0) || (keys[1] > 0))
                {
                    if (((ang+512)&1024) > 0)
                    {
                        if ((brd0 == goldlock) && (keys[0] > 0))
                            board[x-1][y] = 1024, i = 1;
                        if ((brd0 == silverlock) && (keys[1] > 0))
                            board[x-1][y] = 1024, i = 2;
                    }
                    if (((ang+512)&1024) == 0)
                    {
                        if ((brd1 == goldlock) && (keys[0] > 0))
                            board[x+1][y] = 1024, i = 1;
                        if ((brd1 == silverlock) && (keys[1] > 0))
                            board[x+1][y] = 1024, i = 2;
                    }
                    if ((ang&1024) > 0)
                    {
                        if ((brd2 == goldlock) && (keys[0] > 0))
                            board[x][y-1] = 1024, i = 1;
                        if ((brd2 == silverlock) && (keys[1] > 0))
                            board[x][y-1] = 1024, i = 2;
                    }
                    if ((ang&1024) == 0)
                    {
                        if ((brd3 == goldlock) && (keys[0] > 0))
                            board[x][y+1] = 1024, i = 1;
                        if ((brd3 == silverlock) && (keys[1] > 0))
                            board[x][y+1] = 1024, i = 2;
                    }

                    /* End of episode 2 lock... */

                    if (i > 0)
                    {
                        inhibitrepeat=1;
                        ksay(16);
                        if ((boardnum == 19) && (i == 2))
                        {
                            if (cheated == 0)
                            {
                                wingame(2);
                                if (numboards <= 20)
                                {
                                    fade(0);
                                    kgif(1);
                                    introduction(1);
                                }
                                else
                                    won = 1;
                            }
                            else
                            {
                                oldposx += 8192;
                                posx += 8192;
                            }
                        }
                        if ((boardnum == 29) && (i == 1) && (cheated != 0))
                        {
                            oldposx += 16384;
                            posx += 16384;
                        }
                    }
                }

                /* Doors... */

                if (i == 0)
                {
                    if (doorstat == 0)
                    {
                        k = board[x-1][y];
                        if (((k&16384) == 0) && (((ang+512)&1024) > 0))
                        {
                            if (((k&1023) == door1) || ((k&1023) == door1+5))
                            {
                                doorx = x-1, doory = y, doorstat = door1;
                                if ((posx&1023) < 256)
                                    posx = (posx&0xfc00)+256;
                            }
                            if (((k&1023) == door2) || ((k&1023) == door2+5))
                            {
                                doorx = x-1, doory = y, doorstat = door2;
                                if ((posx&1023) < 256)
                                    posx = (posx&0xfc00)+256;
                            }
                            if (((k&1023) == door3) || ((k&1023) == door3+7))
                            {
                                doorx = x-1, doory = y, doorstat = door3;
                                if ((posx&1023) < 256)
                                    posx = (posx&0xfc00)+256;
                            }
                            if ((((k&1023) == door4) && (coins >= 10)) || ((k&1023) == door4+6))
                            {
                                doorx = x-1, doory = y, doorstat = door4;
                                if ((posx&1023) < 256)
                                    posx = (posx&0xfc00)+256;
                            }
                            if (((k&1023) == door5) || ((k&1023) == door5+7))
                            {
                                doorx = x-1, doory = y, doorstat = door5;
                                if ((posx&1023) < 256)
                                    posx = (posx&0xfc00)+256;
                            }
                        }
                        k = board[x+1][y];
                        if (((k&16384) == 0) && (((ang+512)&1024) == 0))
                        {
                            if (((k&1023) == door1) || ((k&1023) == door1+5))
                            {
                                doorx = x+1, doory = y, doorstat = door1;
                                if ((posx&1023) > 767)
                                    posx = (posx&0xfc00)+767;
                            }
                            if (((k&1023) == door2) || ((k&1023) == door2+5))
                            {
                                doorx = x+1, doory = y, doorstat = door2;
                                if ((posx&1023) > 767)
                                    posx = (posx&0xfc00)+767;
                            }
                            if (((k&1023) == door3) || ((k&1023) == door3+7))
                            {
                                doorx = x+1, doory = y, doorstat = door3;
                                if ((posx&1023) > 767)
                                    posx = (posx&0xfc00)+767;
                            }
                            if ((((k&1023) == door4) && (coins >= 10)) || ((k&1023) == door4+6))
                            {
                                doorx = x+1, doory = y, doorstat = door4;
                                if ((posx&1023) > 767)
                                    posx = (posx&0xfc00)+767;
                            }
                            if (((k&1023) == door5) || ((k&1023) == door5+7))
                            {
                                doorx = x+1, doory = y, doorstat = door5;
                                if ((posx&1023) > 767)
                                    posx = (posx&0xfc00)+767;
                            }
                        }
                        k = board[x][y-1];
                        if (((k&16384) == 0) && ((ang&1024) > 0))
                        {
                            if (((k&1023) == door1) || ((k&1023) == door1+5))
                            {
                                doorx = x, doory = y-1, doorstat = door1;
                                if ((posy&1023) < 256)
                                    posy = (posy&0xfc00)+256;
                            }
                            if (((k&1023) == door2) || ((k&1023) == door2+5))
                            {
                                doorx = x, doory = y-1, doorstat = door2;
                                if ((posy&1023) < 256)
                                    posy = (posy&0xfc00)+256;
                            }
                            if (((k&1023) == door3) || ((k&1023) == door3+7))
                            {
                                doorx = x, doory = y-1, doorstat = door3;
                                if ((posy&1023) < 256)
                                    posy = (posy&0xfc00)+256;
                            }
                            if ((((k&1023) == door4) && (coins >= 10)) || ((k&1023) == door4+6))
                            {
                                doorx = x, doory = y-1, doorstat = door4;
                                if ((posy&1023) < 256)
                                    posy = (posy&0xfc00)+256;
                            }
                            if (((k&1023) == door5) || ((k&1023) == door5+7))
                            {
                                doorx = x, doory = y-1, doorstat = door5;
                                if ((posy&1023) < 256)
                                    posy = (posy&0xfc00)+256;
                            }
                        }
                        k = board[x][y+1];
                        if (((k&16384) == 0) && ((ang&1024) == 0))
                        {
                            if (((k&1023) == door1) || ((k&1023) == door1+5))
                            {
                                doorx = x, doory = y+1, doorstat = door1;
                                if ((posy&1023) > 767)
                                    posy = (posy&0xfc00)+767;
                            }
                            if (((k&1023) == door2) || ((k&1023) == door2+5))
                            {
                                doorx = x, doory = y+1, doorstat = door2;
                                if ((posy&1023) > 767)
                                    posy = (posy&0xfc00)+767;
                            }
                            if (((k&1023) == door3) || ((k&1023) == door3+7))
                            {
                                doorx = x, doory = y+1, doorstat = door3;
                                if ((posy&1023) > 767)
                                    posy = (posy&0xfc00)+767;
                            }
                            if ((((k&1023) == door4) && (coins >= 10)) || ((k&1023) == door4+6))
                            {
                                doorx = x, doory = y+1, doorstat = door4;
                                if ((posy&1023) > 767)
                                    posy = (posy&0xfc00)+767;
                            }
                            if (((k&1023) == door5) || ((k&1023) == door5+7))
                            {
                                doorx = x, doory = y+1, doorstat = door5;
                                if ((posy&1023) > 767)
                                    posy = (posy&0xfc00)+767;
                            }
                        }
                        if (doorstat > 0)
                        {
                            inhibitrepeat=1;
                            if ((doorstat == door1) && ((board[doorx][doory]&1023) == door1+5))
                                doorstat = door1+5+1024;
                            if ((doorstat == door2) && ((board[doorx][doory]&1023) == door2+5))
                                doorstat = door2+5+1024;
                            if ((doorstat == door3) && ((board[doorx][doory]&1023) == door3+7))
                                doorstat = door3+7+1024;
                            if (doorstat == door4)
                            {
                                if ((board[doorx][doory]&1023) == door4+6)
                                    doorstat = door4+6+1024;
                                else
                                {
                                    coins -= 10;
                                    textbuf[0] = 9, textbuf[1] = 9;
                                    textbuf[2] = 9, textbuf[3] = 0;
                                    textprint(112,12+statusbaryoffset,(char)0);
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
                                    textprint(112,12+statusbaryoffset,(char)176);
                                }
                            }
                            if ((doorstat == door5) && ((board[doorx][doory]&1023) == door5+7))
                                doorstat = door5+7+1024;
                            if ((doorstat == door3) || (doorstat == door3+7+1024))
                                ksay(29);
                            else
                            {
                                if (doorstat < 1024)
                                    ksay(13);
                                else
                                    ksay(4);
                            }
                        }
                        if ((lastunlock == 0) && (doorstat == 0)) {
                            ksay(12);
                        }
                    }
                }
                lastunlock = 1;
            }
        }
        else {
            inhibitrepeat=0;
            lastunlock = 0;
        }

        /* Roll slot machine reels... */

        if (slottime > 0)
        {
            i = slottime;
            slottime -= clockspd;
            if (slottime >= 304)
                slotpos[0] += (clockspd>>1);
            if (slottime >= 184)
                slotpos[1] += (clockspd>>1);
            if (slottime >= 64)
                slotpos[2] += (clockspd>>1);
            if ((i >= 304) && (slottime < 304))
            {
                ksay(8);
                slotpos[0] = ((slotpos[0]+4)&0x78);
            }
            if ((i >= 184) && (slottime < 184))
            {
                ksay(8);
                slotpos[1] = ((slotpos[1]+4)&0x78);
            }
            if ((i >= 64) && (slottime < 64))
            {
                ksay(8);
                slotpos[2] = ((slotpos[2]+4)&0x78);
            }
            if (slottime <= 0)
            {
                copyslots(slotto);
                slottime = 0;
                i = slotable[0][slotpos[0]>>3];
                j = slotable[1][slotpos[1]>>3];
                k = slotable[2][slotpos[2]>>3];
                m = 0;
                if ((i == j) && (i == k))
                {
                    switch(i)
                    {
                        case 0: m = 200; break;
                        case 1: m = 25; break;
                        case 2: m = 25; break;
                        case 3: m = 8; break;
                        case 4: m = 4; break;
                        case 5: m = 4; break;
                    }
                }
                else if ((i == j) || (j == k) || (i == k))
                {
                    if (j == k)
                        i = j;
                    switch(i)
                    {
                        case 0: m = 5; break;
                        case 1: m = 2; break;
                        case 2: m = 2; break;
                        case 3: m = 2; break;
                        case 4: m = 1; break;
                        case 5: m = 1; break;
                    }
                }
                if (m > 0)
                {
                    owecoins += m;
                    owecoinwait = 0;
                }
                else if (death == 4095)
                    ksay(26);
            }
            copyslots(slotto+1);
        }

        /* Count up winnings from slot machine... */

        if (owecoins > 0)
        {
            if (owecoinwait >= 0)
            {
                owecoinwait -= clockspd;
                if (owecoinwait <= 0)
                {
                    owecoinwait = 60;
                    coins++;
                    owecoins--;
                    if (coins > 999)
                        coins = 999;
                    textbuf[0] = 9, textbuf[1] = 9;
                    textbuf[2] = 9, textbuf[3] = 0;
                    textprint(112,12+statusbaryoffset,(char)0);
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
                    textprint(112,12+statusbaryoffset,(char)176);
                    if (death == 4095)
                        ksay(25);
                }
            }
        }

        /* Update doors... */
        if (doorstat > 0 && ((totalclock^ototclock) > 7))
        {
            if (doorstat < 1024)
            {
                doorstat++;
                if (doorstat == door1+6) doorstat = 0;
                if (doorstat == door2+6) doorstat = 0;
                if (doorstat == door3+8) doorstat = 0;
                if (doorstat == door4+7) doorstat = 0;
                if (doorstat == door5+8) doorstat = 0;
            }
            else
            {
                doorstat--;
                if (doorstat == 1024+door1-1) doorstat = 0;
                if (doorstat == 1024+door2-1) doorstat = 0;
                if (doorstat == 1024+door3-1) doorstat = 0;
                if (doorstat == 1024+door4-1) doorstat = 0;
                if (doorstat == 1024+door5-1) doorstat = 0;
            }
            if (doorstat != 0)
            {
                board[doorx][doory] &= 8192;
                board[doorx][doory] |= (doorstat&1023);
                if ((wallheader[doorstat&1023]&8) > 0)
                    board[doorx][doory] |= 1024;
            }
        }

        /* Bump into walls... */

        if (((vel) != 0) || (svel != 0))
        {
            oldposx = posx;
            oldposy = posy;
            if ((vel) != 0)
            {
                posx += (K_INT16)(((K_INT32)(vel)*clockspd*sintable[(ang+512)&2047])>>19);
                posy += (K_INT16)(((K_INT32)(vel)*clockspd*sintable[ang])>>19);
                checkhitwall(oldposx,oldposy,posx,posy);
            }
            if (svel != 0)
            {
                posx += (K_INT16)(((K_INT32)svel*clockspd*sintable[ang])>>19);
                posy += (K_INT16)(((K_INT32)svel*clockspd*sintable[(ang+1536)&2047])>>19);
                checkhitwall(oldposx,oldposy,posx,posy);
            }
        }

        /* Set colour 255 of palette to ((totalclock&255)>>1)&63,
           32+the new red value (&63), the inverse green value (i.e. ^63). */

        spritepalette[765]=((totalclock&255)>>1)&63;
        spritepalette[766]=(32+spritepalette[765])&63;
        spritepalette[767]=spritepalette[766]^63;

        /* Win episode 3... */

        x = (posx>>10);
        y = (posy>>10);
        if (boardnum == 29)
            if ((x == 32) && (y == 29))
            {
                winallgame();
                fade(0);
                kgif(1);
                introduction(0);
            }

        /* Check for net hit... */

        if ((vel|svel) > 0)
        {
            hitnet = 0;
            if ((posx < oldposx) && ((board[(posx-260)>>10][y]&1023) == net)) hitnet = 1;
            if ((posx > oldposx) && ((board[(posx+260)>>10][y]&1023) == net)) hitnet = 1;
            if ((posy < oldposy) && ((board[x][(posy-260)>>10]&1023) == net)) hitnet = 1;
            if ((posy > oldposy) && ((board[x][(posy+260)>>10]&1023) == net)) hitnet = 1;
            if ((hitnet == 1) && (abs(vel)+abs(svel) > 64))
            {
                svel = -svel-svel;
                vel = -vel-vel;
                myvel = -myvel-myvel;
                if (death == 4095)
                    ksay(2);
            }
        }

        /* In centre of square (used to check for falling down holes)? */

        i = (board[x][y]&1023);
        if (i != 0)
        {
            incenter = 1;
            j = abs(512-(posx&1023));
            k = abs(512-(posy&1023));
            if ((j >= 240) || (k >= 240))
                incenter = 0;
            if ((j+k) >= 340)
                incenter = 0;
        }

        /* Check cheat keys... */

        if ((keystatus[42] > 0) && (keystatus[54] > 0) && (cheatenable == 1))
            cheatkeysdown = 1;
        else if ((keystatus[42] > 0) && (keystatus[29] > 0) && (cheatenable == 2))
            cheatkeysdown=1;
        else
            cheatkeysdown = 0;

        /* Check for goodies... */

        if ((keystatus[19] > 0) && (cheatkeysdown == 1)) {
            keystatus[19]=0;
            cheated++;
            ksay(7);
            cliptowall ^= 1;
        }


        if ((i == extralife) || ((keystatus[18] > 0) && (cheatkeysdown == 1)))
        {
            lifevests++;
            if (lifevests > 9)
                lifevests = 9;
            if (i == extralife)
            {
                board[x][y] = 1024|(board[x][y]&16384);
                scorecount += 7500;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            textbuf[0] = 9, textbuf[1] = 0;
            textprint(96,12+statusbaryoffset,(char)0);
            textbuf[0] = lifevests+48, textbuf[1] = 0;
            textprint(96,12+statusbaryoffset,(char)176);
        }
        if ((i == lightning) || ((keystatus[38] > 0) && (cheatkeysdown == 1)))
        {
            lightnings++;
            if (lightnings > 6)
                lightnings = 6;
            if (i == lightning)
            {
                board[x][y] = 1024|(board[x][y]&16384);
                scorecount += 5000;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            textbuf[0] = 9, textbuf[1] = 0;
            textprint(296,21+statusbaryoffset,(char)0);
            textbuf[0] = lightnings+48, textbuf[1] = 0;
            textprint(296,21+statusbaryoffset,(char)176);
        }
        if ((i == getcompass) || ((keystatus[32] > 0) && (cheatkeysdown == 1)))
        {
            compass = 1;
            if (i == getcompass)
            {
                board[x][y] = 1024|(board[x][y]&16384);
                scorecount += 15000;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
        }
        if (((i == bul1get) || (i == bul2get) || (i == bul3get)) || (((keystatus[33] > 0) || (keystatus[34] > 0) || (keystatus[35] > 0)) && (cheatkeysdown == 1)))
        {
            if ((i == bul1get) || ((keystatus[33] > 0) && (cheatkeysdown == 1)))
            {
                firepowers[0]++;
                if (firepowers[0] > 6)
                    firepowers[0] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(272,12+statusbaryoffset,(char)0);
                textbuf[0] = firepowers[0]+48, textbuf[1] = 0;
                textprint(272,12+statusbaryoffset,(char)176);
            }
            if ((i == bul2get) || ((keystatus[34] > 0) && (cheatkeysdown == 1)))
            {
                firepowers[1]++;
                if (firepowers[1] > 6)
                    firepowers[1] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(272,21+statusbaryoffset,(char)0);
                textbuf[0] = firepowers[1]+48, textbuf[1] = 0;
                textprint(272,21+statusbaryoffset,(char)176);
            }
            if ((i == bul3get) || ((keystatus[35] > 0) && (cheatkeysdown == 1)))
            {
                firepowers[2]++;
                if (firepowers[2] > 6)
                    firepowers[2] = 6;
                textbuf[0] = 9, textbuf[1] = 0;
                textprint(296,12+statusbaryoffset,(char)0);
                textbuf[0] = firepowers[2]+48, textbuf[1] = 0;
                textprint(296,12+statusbaryoffset,(char)176);
            }
            if ((i == bul1get) || (i == bul2get) || (i == bul3get))
            {
                board[x][y] = (emptybulstand+1024)|(board[x][y]&16384);
                scorecount += 2500;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
        }
        if ((i == diamond) || (i == diamonds3))
        {
            if (death == 4095)
                ksay(7);
            board[x][y] = 1024|(board[x][y]&16384);
            if (i == diamond)
            {
                scorecount += 500;
                coins += 5;
            }
            if (i == diamonds3)
            {
                scorecount += 1500;
                coins += 15;
            }
            if (coins > 999)
                coins = 999;
            drawscore(scorecount);
            textbuf[0] = 9, textbuf[1] = 9;
            textbuf[2] = 9, textbuf[3] = 0;
            textprint(112,12+statusbaryoffset,(char)0);
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
            textprint(112,12+statusbaryoffset,(char)176);
        }
        if ((i == coin) || ((keystatus[16] > 0) && (cheatkeysdown == 1)))
        {
            coins++;
            if (coins > 999)
                coins = 999;
            if (death == 4095)
                ksay(7);
            if (i == coin)
            {
                board[x][y] = 1024|(board[x][y]&16384);
                scorecount += 100;
                drawscore(scorecount);
            }
            else
                cheated++;
            textbuf[0] = 9, textbuf[1] = 9;
            textbuf[2] = 9, textbuf[3] = 0;
            textprint(112,12+statusbaryoffset,(char)0);
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
            textprint(112,12+statusbaryoffset,(char)176);
        }
        if ((i == goldkey) || ((keystatus[37] > 0) && (cheatkeysdown == 1)))
        {
            keys[0]++;
            if (i == goldkey)
            {
                board[x][y] = (emptykey+1024)|(board[x][y]&16384);
                scorecount += 500;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            statusbardraw(36,44,14,6,144,13+statusbaryoffset,statusbarinfo);
        }
        if ((i == silverkey) || ((keystatus[37] > 0) && (cheatkeysdown == 1)))
        {
            keys[1]++;
            if (i == silverkey)
            {
                board[x][y] = (emptykey+1024)|(board[x][y]&16384);
                scorecount += 500;
                drawscore(scorecount);
                if (death == 4095)
                    ksay(7);
            }
            else
                cheated++;
            statusbardraw(50,44,14,6,144,21+statusbaryoffset,statusbarinfo);
        }
        if (i == warp)
        {
            if (justwarped == 0)
            {
                k = -1;
                for(j=0;j<numwarps;j++)
                    if ((xwarp[j] == (char)x) && (ywarp[j] == (char)y))
                        k = j;
                if (k != -1)
                {
                    k++;
                    if (k >= numwarps)
                        k = 0;
                    if (boardnum == 29)
                        if ((k&1) == 0)
                            k ^= 2;
                    for(i=0;i<mnum;i++)
                        if (mstat[i] == mondog)
                        {
                            if (mposx[i] > posx) templong = (K_INT32)(mposx[i]-posx);
                            else templong = (K_INT32)(posx-mposx[i]);
                            if (mposy[i] > posy) templong += (K_INT32)(mposy[i]-posy);
                            else templong += (K_INT32)(posy-mposy[i]);
                            if (templong < 4096)
                            {
                                board[moldx[i]>>10][moldy[i]>>10] &= 0xbfff;
                                board[mgolx[i]>>10][mgoly[i]>>10] &= 0xbfff;
                                mposx[i] = (((K_UINT16)xwarp[k])<<10)+512;
                                mposy[i] = (((K_UINT16)ywarp[k])<<10)+512;
                                moldx[i] = mposx[i], moldy[i] = mposy[i];
                                mgolx[i] = mposx[i], mgoly[i] = mposy[i];
                                board[mposx[i]>>10][mposy[i]>>10] |= 0x4000;
                            }
                        }
                    posx = (((K_UINT16)xwarp[k])<<10)+512;
                    posy = (((K_UINT16)ywarp[k])<<10)+512;
                    x = xwarp[k];
                    y = ywarp[k];
                    i = (board[x][y]&1023);
                    if (death == 4095)
                        ksay(17);
                    fadewarpval = 8;
                }
                justwarped = 1;
                if ((boardnum == 9) && (k == 2))
                {
                    if (cheated == 0)
                    {
                        wingame(1);
                        if (numboards <= 10)
                        {
                            fade(0);
                            kgif(1);
                            introduction(1);
                        }
                        else
                            won = 1;
                    }
                    else
                    {
                        posx = (((K_UINT16)xwarp[0])<<10)+512;
                        posy = (((K_UINT16)ywarp[0])<<10)+512;
                    }
                }
            }
        }
        else
            justwarped = 0;

        /* Fade... */

        if (death == 4095)
        {
            if (fadewarpval < 63)
            {
                fadewarpval += (clockspd>>1);
                if (fadewarpval > 63)
                    fadewarpval = 63;
                fade(fadewarpval);
                fadehurtval = 0;
            }
            if (fadehurtval > 0)
            {
                fadehurtval -= (clockspd>>1);
                if (fadehurtval > 63)
                    fadehurtval = 63;
                if (fadehurtval < 0)
                    fadehurtval = 0;
                fade(fadehurtval+128);
            }
        }

        /* Check for active potions/cloaks... */

        if (purpletime >= ototclock)
        {
            if (purpletime >= totalclock)
            {
                if (purpletime < totalclock+3840)
                {
                    k = ((3840-(K_INT16)(purpletime-totalclock))>>8);
                    if ((k >= 0) && (k <= 15))
                        statusbardraw(0,30+k,16,1,159,13+k+statusbaryoffset,statusbarinfo);
                }
            }
            else
                statusbardraw(15,13,16,15,159,13+statusbaryoffset,statusbarback);
        }
        if (greentime >= ototclock)
        {
            if (greentime >= totalclock)
            {
                if (greentime < totalclock+3840)
                {
                    k = ((3840-(K_INT16)(greentime-totalclock))>>8);
                    if ((k >= 0) && (k <= 15))
                        statusbardraw(16,30+k,16,1,176,13+k+statusbaryoffset,statusbarinfo);
                }
            }
            else
                statusbardraw(16,13,16,15,176,13+statusbaryoffset,statusbarback);
        }
        if (capetime[0] >= ototclock)
        {
            if (capetime[0] >= totalclock)
            {
                if (capetime[0] < totalclock+3072)
                {
                    k = (K_INT16)((capetime[0]-totalclock)>>9);
                    if ((k >= 0) && (k <= 5))
                    {
                        if (k == 5) statusbardraw(0,0,21,28,194,2+statusbaryoffset,coatfade);
                        if (k == 4) statusbardraw(21,0,21,28,194,2+statusbaryoffset,coatfade);
                        if (k == 3) statusbardraw(42,0,21,28,194,2+statusbaryoffset,coatfade);
                        if (k == 2) statusbardraw(0,32,21,28,194,2+statusbaryoffset,coatfade);
                        if (k == 1) statusbardraw(21,32,21,28,194,2+statusbaryoffset,coatfade);
                        if (k == 0) statusbardraw(42,32,21,28,194,2+statusbaryoffset,coatfade);
                    }
                }
            }
            else
                statusbardraw(16,2,21,28,194,2+statusbaryoffset,statusbarback);
        }
        if (capetime[1] >= ototclock)
        {
            if (capetime[1] >= totalclock)
            {
                if (capetime[1] < totalclock+3072)
                {
                    k = (K_INT16)((capetime[1]-totalclock)>>9);
                    if ((k >= 0) && (k <= 5))
                    {
                        if (k == 5) statusbardraw(0,0,21,28,216,2+statusbaryoffset,coatfade);
                        if (k == 4) statusbardraw(21,0,21,28,216,2+statusbaryoffset,coatfade);
                        if (k == 3) statusbardraw(42,0,21,28,216,2+statusbaryoffset,coatfade);
                        if (k == 2) statusbardraw(0,32,21,28,216,2+statusbaryoffset,coatfade);
                        if (k == 1) statusbardraw(21,32,21,28,216,2+statusbaryoffset,coatfade);
                        if (k == 0) statusbardraw(42,32,21,28,216,2+statusbaryoffset,coatfade);
                    }
                }
            }
            else
                statusbardraw(16,2,21,28,216,2+statusbaryoffset,statusbarback);
        }

        /* Check for more goodies... */

        if ((i == purple) || ((keystatus[39] > 0) && (cheatkeysdown == 1)))
        {
            if (purpletime < totalclock)
                purpletime = totalclock + 4800;
            else
                purpletime += 4800;
            if (i == purple)
            {
                board[x][y] = (emptypurple+1024)|(board[x][y]&16384);
                scorecount += 1000;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            statusbardraw(0,0,16,15,159,13+statusbaryoffset,statusbarinfo);
        }
        if ((i == green) || ((keystatus[40] > 0) && (cheatkeysdown == 1)))
        {
            if (greentime < totalclock)
                greentime = totalclock + 4800;
            else
                greentime += 4800;
            if (i == green)
            {
                board[x][y] = (emptygreen+1024)|(board[x][y]&16384);
                scorecount += 1500;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            statusbardraw(0,15,16,15,176,13+statusbaryoffset,statusbarinfo);
        }
        if ((i == gray) || ((keystatus[36] > 0) && (cheatkeysdown == 1)))
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 7200;
            else
                capetime[0] += 7200;
            if (i == gray)
            {
                board[x][y] = (emptycoat+1024)|(board[x][y]&16384);
                scorecount += 2500;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            statusbardraw(16,0,21,28,194,2+statusbaryoffset,statusbarinfo);
        }
        if ((i == blue) || ((keystatus[30] > 0) && (cheatkeysdown == 1)))
        {
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 4800;
            else
                capetime[1] += 4800;
            if (i == blue)
            {
                board[x][y] = (emptycoat+1024)|(board[x][y]&16384);
                scorecount += 5000;
                drawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            statusbardraw(37,0,21,28,216,2+statusbaryoffset,statusbarinfo);
        }
        if (i == grayblue)
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 7200;
            else
                capetime[0] += 7200;
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 4800;
            else
                capetime[1] += 4800;
            board[x][y] = (emptycoat+1024)|(board[x][y]&16384);
            if (death == 4095)
                ksay(7);
            scorecount += 7500;
            drawscore(scorecount);
            statusbardraw(16,0,21,28,194,2+statusbaryoffset,statusbarinfo);
            statusbardraw(37,0,21,28,216,2+statusbaryoffset,statusbarinfo);
        }
        if (i == all4coats)
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 7200;
            else
                capetime[0] += 7200;
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 4800;
            else
                capetime[1] += 4800;
            if (purpletime < totalclock)
                purpletime = totalclock + 4800;
            else
                purpletime += 4800;
            if (greentime < totalclock)
                greentime = totalclock + 4800;
            else
                greentime += 4800;
            board[x][y] = (emptyall4+1024)|(board[x][y]&16384);
            if (death == 4095)
                ksay(7);
            scorecount += 10000;
            drawscore(scorecount);
            statusbardraw(0,0,16,15,159,13+statusbaryoffset,statusbarinfo);
            statusbardraw(0,15,16,15,176,13+statusbaryoffset,statusbarinfo);
            statusbardraw(16,0,21,28,194,2+statusbaryoffset,statusbarinfo);
            statusbardraw(37,0,21,28,216,2+statusbaryoffset,statusbarinfo);
        }

        /* Check for food... */

        if (life < 4095)
            if (((i == meal) || (i == honey) || (i == fries) || (i == firstaid)) || (getkeydefstat(ACTION_CHEAT) > 0) || ((keystatus[31] > 0) && (cheatkeysdown == 1)))
            {
                if ((keystatus[31] > 0) && (cheatkeysdown == 1))
                    life += 320, cheated++;
                if (getkeydefstat(ACTION_CHEAT) > 0)
                {
                    life += 320;
                    scorecount >>= 1;
                    drawscore(scorecount);
                }
                if (i == fries)
                    life += 320, board[x][y] = (emptyfries+1024)|(board[x][y]&16384);
                if (i == meal)
                    life += 640, board[x][y] = (emptymeal+1024)|(board[x][y]&16384);
                if (i == honey)
                    life += 640, board[x][y] = ((board[x][y]&16384)|1024);
                if (i == firstaid)
                    life += 1280, board[x][y] = (emptyfirst+1024)|(board[x][y]&16384);
                if (life > 4095)
                    life = 4095;
                drawlife();
                if (death == 4095)
                    ksay(7);
            }

        /* Stairs! */

        if ((i == stairs) || (won == 1) || ((keystatus[48] > 0) && (cheatkeysdown == 1)))
        {
            won = 0;
            musicoff();
            if (boardnum < numboards-1)
            {
                if (i == stairs)
                {
                    if (cheated == 0)
                        ksay(11);
                    else
                        ksay(19);
                }
                if ((boardnum&1) == 0)
                    loadmusic("CONGRAT1");
                else
                    loadmusic("CONGRAT0");
                musicon();
                if (i == stairs)
                {
                    hiscorecheck();
                    boardnum++;
                    //reviewboard();
                }
                else
                    boardnum++;
                if ((keystatus[48] > 0) && (cheatkeysdown == 1)) {
                    cheated++;
                    keystatus[48]=0; /* Crude, but effective. */
                }
                musicoff();
                loadboard();
                ksmfile[0] = 'L', ksmfile[1] = 'A', ksmfile[2] = 'B';
                ksmfile[3] = 'S', ksmfile[4] = 'N', ksmfile[5] = 'G';
                ksmfile[6] = (boardnum/10)+48, ksmfile[7] = (boardnum%10)+48;
                ksmfile[8] = 0;
                statusbardraw(16,13,14,15,144,13+statusbaryoffset,statusbarback);
                textbuf[0] = 9, textbuf[1] = 9, textbuf[2] = 0;
                textprint(46,20+statusbaryoffset,(char)0);
                textbuf[0] = ((boardnum+1)/10)+48;
                textbuf[1] = ((boardnum+1)%10)+48;
                textbuf[2] = 0;
                if (textbuf[0] == 48)
                    textbuf[0] = 32;
                textprint(46,20+statusbaryoffset,(char)176);
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                angvel = 0;
                vel = 0;
                mxvel = 0;
                myvel = 0;
                svel = 0;
                hvel = 0;
                ototclock = -1;
                scoreclock = 0;
                scorecount = 0;
                drawscore(scorecount);
                drawtime(scoreclock);
                SDL_LockMutex(timermutex);
                clockspeed = 0;
                SDL_UnlockMutex(timermutex);
            }
            else
            {
                fade(0);
                kgif(1);
                introduction(0);
            }
        }

        /* Check for fans... */

        if (death == 4095)
        {
            if ((i == fan) && (capetime[0] < totalclock) && (capetime[1] < totalclock))
            {
                life -= (clockspd<<3);
                if (life <= 0)
                {
                    life = 0;
                    drawlife();
                    death = 4094;
                    angvel = (rand()&32)-16;
                    ksay(5);
                    musicoff();
                }
                else
                {
                    drawlife();
                    angvel = (rand()&32)-16;
                    fadehurtval += 16;
                    ksay(9);
                }
            }
            if (((i&1023) == hole) && (incenter == 1))
            {
                life = 0;
                drawlife();
                death = 4094;
                angvel = 0;
                ksay(6);
                musicoff();
            }
        }

        /* Pause... */

        if (getkeydefstat(ACTION_PAUSE) > 0)
        {
            wipeoverlay(0,0,361,statusbaryoffset);
            ototclock = totalclock;
            if (fadewarpval > 16)
                for(i=63;i>=16;i-=2)
                {
                    SDL_Delay(10); /* Close enough. */
                    fade(i+64);
                    picrot(posx,posy,posz,ang);

                    if (vidmode == 0)
                        n = 17;
                    else
                        n = 37;

                    /* Yes, I know this fades out the text, too. That's how the
                       original does it. I don't know why, but I don't think
                       it's a bug... */

                    mixing=1;

                    sprintf(textbuf,"GAME PAUSED");
                    textprint(180-(strlen(textbuf)<<2),n+1,(char)0);
                    setuptextbuf((scoreclock*5)/12);
                    textbuf[12] = textbuf[11];
                    textbuf[11] = textbuf[10];
                    textbuf[10] = textbuf[9];
                    textbuf[9] = '.';
                    k = 0;
                    while ((textbuf[k] == 8) && (k < 12))
                        k++;
                    for(n=0;n<13;n++)
                        textbuf[n] = textbuf[n+k];
                    for(n=strlen(textbuf);n>=0;n--)
                        textbuf[n+6] = textbuf[n];
                    textbuf[strlen(textbuf)+6] = 0;
                    textbuf[0] = 'T';
                    textbuf[1] = 'i';
                    textbuf[2] = 'm';
                    textbuf[3] = 'e';
                    textbuf[4] = ':';
                    textbuf[5] = ' ';
                    if (vidmode == 0)
                        n = 27;
                    else
                        n = 47;
                    textprint(180-(strlen(textbuf)<<2),n+1,(char)0);

                    mixing=0;

                    SDL_GL_SwapWindow(mainwindow);
                }
            x = getkeydefstat(ACTION_PAUSE);
            y = 1;
            while ((x <= y) &&
                   (getkeydefstatlock(ACTION_MENU) == 0) &&
                   (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
                   (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
                   (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
                   (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
                   (bstatus == 0))
            {
                PollInputs();
                bstatus = 0;
                if (moustat == 0)
                {
                    bstatus=readmouse(NULL,NULL);
                }
                y = x;
                x = getkeydefstat(ACTION_PAUSE);
                SDL_Delay(10); /* Leave some CPU for the rest of us! */
            }
            if (fadewarpval > 16)
                for(i=16;i<=fadewarpval;i+=2)
                {
                    SDL_Delay(10); /* Close enough. */
                    fade(i+64);
                    picrot(posx,posy,posz,ang);
                    SDL_GL_SwapWindow(mainwindow);
                }
            wipeoverlay(0,0,361,statusbaryoffset);
            picrot(posx,posy,posz,ang);
            totalclock = ototclock;
            SDL_LockMutex(timermutex);
            clockspeed = 0;
            SDL_UnlockMutex(timermutex);
            lastunlock = 1;
            lastshoot = 1;
            lastbarchange = 1;
        }
        if (keystatus[88] > 0) {
            screencapture();
            keystatus[88]=0;
        }
        if (ototclock == 0)
        {
            /* Introduction to level... */

            fade(27);
            wipeoverlay(0,0,361,statusbaryoffset);
            picrot(posx,posy,posz,ang);
            loadmusic("DRUMSONG");
            musicon();
            for(i=0;i<16;i++)
            {
                tempbuf[i*3] = (i*63)>>4;
                tempbuf[i*3+1] = (i*63)>>4;
                tempbuf[i*3+2] = (i*63)>>4;
            }
            updateoverlaypalette(240,16,tempbuf);
            mixing=1;

            loadstory(boardnum);
            displaystory(boardnum);

            mixing=0;

            clearkeydefstat(ACTION_MENU);
            clearkeydefstat(ACTION_MENU_CANCEL);
            clearkeydefstat(ACTION_MENU_SELECT1);
            clearkeydefstat(ACTION_MENU_SELECT2);
            clearkeydefstat(ACTION_MENU_SELECT3);
            while(readmouse(NULL, NULL)!=0) {
                PollInputs();
                SDL_Delay(10);
            }

            while ((getkeydefstat(ACTION_MENU) == 0) &&
                   (getkeydefstat(ACTION_MENU_CANCEL) == 0) &&
                   (getkeydefstat(ACTION_MENU_SELECT1) == 0) &&
                   (getkeydefstat(ACTION_MENU_SELECT2) == 0) &&
                   (getkeydefstat(ACTION_MENU_SELECT3) == 0) &&
                   (bstatus == 0))
            {
                PollInputs();
                bstatus = 0;
                if (moustat == 0)
                {
                    bstatus=readmouse(NULL, NULL);
                }
                SDL_LockMutex(timermutex);
                totalclock += clockspeed;
                clockspeed = 0;
                SDL_UnlockMutex(timermutex);

                j = 63-(((K_INT16)labs((totalclock%120)-60))>>3);

                fade(27);
                picrot(posx,posy,posz,ang);

                mixing=1;
                fade(j);
                ShowPartialOverlay(0,0,360,statusbaryoffset,0);
                mixing=0;

                fade(27);

                SDL_GL_SwapWindow(mainwindow);

                SDL_LockMutex(timermutex);
                while(clockspeed<4) {
                    SDL_UnlockMutex(timermutex);
                    SDL_Delay(10);
                    SDL_LockMutex(timermutex);
                }
                SDL_UnlockMutex(timermutex);
            }
            settransferpalette();
            lastunlock = 1;
            lastshoot = 1;
            lastbarchange = 1;
            musicoff();
            clockspeed = 0;
            scoreclock = 0;
            scorecount = 0;
            drawscore(scorecount);
            drawtime(scoreclock);
            ksmfile[0] = 'L', ksmfile[1] = 'A', ksmfile[2] = 'B';
            ksmfile[3] = 'S', ksmfile[4] = 'N', ksmfile[5] = 'G';
            ksmfile[6] = (boardnum/10)+48, ksmfile[7] = (boardnum%10)+48;
            ksmfile[8] = 0;
            loadmusic(ksmfile);
            musicon();
            mixing=0;
            for(i=27;i<=63;i+=2)
            {
                SDL_LockMutex(timermutex);
                clockspeed=0;
                while(clockspeed<4) {
                    SDL_UnlockMutex(timermutex);
                    SDL_Delay(10);
                    SDL_LockMutex(timermutex);
                }
                SDL_UnlockMutex(timermutex);
                fade(i);
                picrot(posx,posy,posz,ang);
                SDL_GL_SwapWindow(mainwindow);
            }
            SDL_LockMutex(timermutex);
            clockspeed = 0;
            SDL_UnlockMutex(timermutex);
            totalclock = ototclock;
            ototclock = 1;
            wipeoverlay(0,0,361,statusbaryoffset);
            picrot(posx,posy,posz,ang);
        }

        /* Mute key... */

        if (getkeydefstat(ACTION_MUTE) > 0)
        {
            SDL_LockMutex(soundmutex); /* Paranoid, I know... */
            mute = 1 - mute;
            if ((mute == 1) && (musicsource == MUSIC_SOURCE_MIDI)) {
#ifdef WIN32
                midiOutReset(sequencerdevice);
#endif
#ifdef USE_OSS
                ioctl(sequencerdevice, SNDCTL_SEQ_PANIC);
                ioctl(sequencerdevice, SNDCTL_SEQ_RESET);
#endif
                setmidiinsts();
            }
            SDL_UnlockMutex(soundmutex);
            clearkeydefstat(ACTION_MUTE);
        }

        /* [ESCAPE] to main menu... */

        if (getkeydefstat(ACTION_MENU) == 1)
        {
            if (ototclock > 1)
            {
                SDL_GL_SwapWindow(mainwindow);
                j = mainmenu();
                if (j < MAINMENU_COPYRIGHT)
                {
                    if (j == MAINMENU_NEWGAME)
                    {
                        picrot(posx, posy, posz, ang);
                        musicoff();
                        fade(0);
                        if (newgameplace == 0) boardnum = 0;
                        if (newgameplace == 1) boardnum = 10;
                        if (newgameplace == 2) boardnum = 20;
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
                        namrememberstat = hiscorenamstat;
                        hiscorenamstat = 0;
                        hiscorenam[0] = 0;
                        SDL_LockMutex(timermutex);
                        clockspeed = 0;
                        SDL_UnlockMutex(timermutex);
                        scoreclock = 0;
                        scorecount = 0;
                        statusbaralldraw();
                    }
                    if ((j == MAINMENU_LOADGAME) && (loadsavegameplace >= 0))
                    {
                        fade(0);
                        loadgame(loadsavegameplace);
                        fade(63);
                    }
                    if ((j == MAINMENU_SAVEGAME) && (loadsavegameplace >= 0))
                    {
                        if (hiscorenamstat == 0)
                        {
                            drawinputbox();
                            getname();
                        }
                        /*if (hiscorenamstat > 0)*/
                        savegame(loadsavegameplace);
                    }
                    clearkeydefstat(ACTION_MENU);
                    lastunlock = 1;
                    lastshoot = 1;
                    lastbarchange = 1;
                }
                else
                    quitgame = 1;
                if (ototclock > 0)
                    totalclock = ototclock;
                if (vidmode == 0)
                    linecompare(statusbar);
                SDL_LockMutex(timermutex);
                clockspeed = 0;
                SDL_UnlockMutex(timermutex);
            }
            else
            {
                picrot(posx, posy, posz, ang);
                clearkeydefstat(ACTION_MENU);
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
            }
        }
        if (newkeystatus(SDLK_F5)) {
            soundvolume-=(clockspd>>1);
            if (soundvolume<0) soundvolume=0;
            soundvolumevisible=240;
        }
        if (newkeystatus(SDLK_F6)) {
            soundvolume+=(clockspd>>1);
            if (soundvolume>256) soundvolume=256;
            soundvolumevisible=240;
        }
        if (newkeystatus(SDLK_F7)) {
            musicvolume-=(clockspd>>1);
            if (musicvolume<0) musicvolume=0;
            SDL_LockMutex(soundmutex); /* Probably overkill. */
            adlibsetvolume(musicvolume*48);
            SDL_UnlockMutex(soundmutex);
            musicvolumevisible=240;
        }
        if (newkeystatus(SDLK_F8)) {
            musicvolume+=(clockspd>>1);
            if (musicvolume>256) musicvolume=256;
            SDL_LockMutex(soundmutex); /* Probably overkill. */
            adlibsetvolume(musicvolume*48);
            SDL_UnlockMutex(soundmutex);
            musicvolumevisible=240;
        }
        if (newkeystatus(SDLK_F10)) {
            gammalevel *= pow(1.01, clockspd);
            if (gammalevel>10.0) gammalevel=10.0;
            if (SDL_SetWindowBrightness(mainwindow, gammalevel) != 0)
                fprintf(stderr,"Gamma not supported.\n");
        }
        if (newkeystatus(SDLK_F9)) {
            gammalevel *= pow(1.01, -clockspd);
            if (gammalevel<0.1) gammalevel=0.1;
            if (SDL_SetWindowBrightness(mainwindow, gammalevel) != 0)
                fprintf(stderr,"Gamma not supported.\n");
        }
        if (soundvolumevisible) {
            drawvolumebar(soundvolume,0,soundvolumevisible/240.0);
            soundvolumevisible-=clockspd;
            if (soundvolumevisible<0)
                soundvolumevisible=0;
        }
        if (musicvolumevisible) {
            drawvolumebar(musicvolume,1,musicvolumevisible/240.0);
            musicvolumevisible-=clockspd;
            if (musicvolumevisible<0)
                musicvolumevisible=0;
        }

        if (ototclock <= 0)
            ototclock++;
        else
            ototclock = totalclock;
        totalclock += clockspd;
        scoreclock += clockspd;
        if ((scoreclock%240) < clockspd)
            drawtime(scoreclock);

        SDL_GL_SwapWindow(mainwindow);
    }

    /* End of main loop. End of game. Tidy up things... */

    if (frames>0)
        fprintf(stderr,"%d frames in %d ticks. Frame rate is: %f fps.\n",
                frames,timeused,((float)frames)/((float)timeused)*240.0);
    quit();
    return 0;
}
