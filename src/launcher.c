#include "lab3d.h"

static void draw_gamelaunchermenu(void) {
    inlauncher = 1;

    int n = 18;
    drawmenu(360,240,menu);

    n = 18;
    sprintf(textbuf,"LAB3D/SDL v%d.%d.%d game launcher menu", LAB3D_VERSION_MAJOR, LAB3D_VERSION_MINOR, LAB3D_VERSION_PATCH);
    textprint(40,16,126);

    strcpy(textbuf,"Ken's Labyrinth 2.1 (Full)");
    n += 12; textprint(51,n,34);
    strcpy(textbuf,"Ken's Labyrinth 2.0 (Shareware)");
    n += 12; textprint(51,n,34);
    strcpy(textbuf,"Ken's Labyrinth 1.1 (Full)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Ken's Labyrinth 1.0 (Shareware)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"What's New");
    n += 12; textprint(51,n,79);
    strcpy(textbuf,"Setup");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"About");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Exit");
    n += 12; textprint(51,n,96);

    n = 220;

    strcpy(textbuf,"Use cursor keys / left stick to select.");
    textprint(31,n,lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 32 : 34);

    finalisemenu();
}

void whatsnew420() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.2.0 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Added support for ARM64 based");
    textprint(30, 58, 96);

    strcpy(textbuf, "systems for Windows");
    textprint(30, 68, 96);

    strcpy(textbuf, "Fix crash when enabling sound if the");
    textprint(30, 88, 96);

    strcpy(textbuf, "game was started without sound");
    textprint(30, 98, 96);

    strcpy(textbuf, "macOS version bundled with all the");
    textprint(30, 118, 96);

    strcpy(textbuf, "required libraries and codesigned");
    textprint(30, 128, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew418() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.8 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Fix MIDI music (Windows and Linux).");
    textprint(30, 58, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew417() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.7 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Update to latest SDL2 for Windows.");
    textprint(30, 58, 96);

    strcpy(textbuf, "Ensure the display size setting reads");
    textprint(30, 78, 96);

    strcpy(textbuf, "the correct value.");
    textprint(30, 88, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew416() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.6 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Fix crash starting up Ken's Labyrinth");
    textprint(30, 58, 96);

    strcpy(textbuf, "1.x.");
    textprint(30, 68, 96);

    strcpy(textbuf, "Joycon controls work again in the");
    textprint(30, 88, 96);

    strcpy(textbuf, "latest Switch firmware.");
    textprint(30, 98, 96);

    strcpy(textbuf, "Vertical mouse movement can be");
    textprint(30, 118, 96);

    strcpy(textbuf, "switched off for Ken's Labyrinth 1.x.");
    textprint(30, 128, 96);

    strcpy(textbuf, "macOS version now saves settings.");
    textprint(30, 148, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew415() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.5 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Fix the broken input configuration");
    textprint(30, 58, 96);

    strcpy(textbuf, "menu.");
    textprint(30, 68, 96);

    strcpy(textbuf, "New setting for enabling/disabling");
    textprint(30, 88, 96);

    strcpy(textbuf, "vertical movement with the mouse.");
    textprint(30, 98, 96);

    strcpy(textbuf, "Disabled by default.");
    textprint(30, 108, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew414() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.4 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Update SDL.");
    textprint(30, 58, 96);

    strcpy(textbuf, "Fix crash on Windows.");
    textprint(30, 78, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew413() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.3 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "New port for Apple Silicon and Intel");
    textprint(30, 58, 96);

    strcpy(textbuf, "machines running macOS.");
    textprint(30, 68, 96);

    strcpy(textbuf, "Nintendo Switch version built with");
    textprint(30, 86, 96);

    strcpy(textbuf, "the latest devkitPro.");
    textprint(30, 96, 96);

    strcpy(textbuf, "Compiles for ARM based Linux.");
    textprint(30, 116, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew412() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.2 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Memory errors fixed");
    textprint(30, 58, 96);

    strcpy(textbuf, "A bug in the Ken's Labyrinth 1.x");
    textprint(30, 76, 96);

    strcpy(textbuf, "welcome screen that appears before");
    textprint(30, 86, 96);

    strcpy(textbuf, "every stage caused an access violation");
    textprint(30, 96, 96);

    strcpy(textbuf, "which depending on which platform and");
    textprint(30, 106, 96);

    strcpy(textbuf, "build you're running could cause it");
    textprint(30, 116, 96);

    strcpy(textbuf, "to crash.");
    textprint(30, 126, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();

    drawmenu(320, 172, menu);

    strcpy(textbuf, "This version also now uses double");
    textprint(30, 58, 96);

    strcpy(textbuf, "buffering everywhere instead of");
    textprint(30, 68, 96);

    strcpy(textbuf, "single buffering since this is no");
    textprint(30, 78, 96);

    strcpy(textbuf, "longer supported on modern GPUs.");
    textprint(30, 88, 96);

    strcpy(textbuf, "Nvidia cards will no longer flicker");
    textprint(30, 98, 96);

    strcpy(textbuf, "during the intro.");
    textprint(30, 108, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew411() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
        "Version 4.1.1 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "Critical fix for Switch");
    textprint(30, 58, 96);

    strcpy(textbuf, "Versions above 4.0.0 were untested");
    textprint(30, 76, 96);

    strcpy(textbuf, "on the Nintendo Switch and it crashes");
    textprint(30, 86, 96);

    strcpy(textbuf, "immediately after the game launches.");
    textprint(30, 96, 96);

    strcpy(textbuf, "Now it will correctly use a 64 bit");
    textprint(30, 106, 96);

    strcpy(textbuf, "integer pointer instead of a 32 bit");
    textprint(30, 116, 96);

    strcpy(textbuf, "one.");
    textprint(30, 126, 96);

    strcpy(textbuf, "Sorry about that!");
    textprint(30, 136, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew410() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
           "Version 4.1.0 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "New port for macOS!");
    textprint(30, 58, 96);

    strcpy(textbuf, "For the first time LAB3D/SDL");
    textprint(30, 76, 96);

    strcpy(textbuf, "has been ported to macOS.");
    textprint(30, 86, 96);

    strcpy(textbuf, "It is currently only available");
    textprint(30, 96, 96);

    strcpy(textbuf, "as an external download. Perhaps");
    textprint(30, 106, 96);

    strcpy(textbuf, "it will appear in the App Store");
    textprint(30, 116, 96);

    strcpy(textbuf, "later on if certain hurdles can be");
    textprint(30, 126, 96);

    strcpy(textbuf, "overcome including Xcode distribution");
    textprint(30, 136, 96);

    strcpy(textbuf, "issues and Apple reviewers.");
    textprint(30, 146, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew401() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
           "Version 4.0.1 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "New enhanced multiplatform building!");
    textprint(30, 58, 96);

    strcpy(textbuf, "This new version has incorporated");
    textprint(30, 76, 96);

    strcpy(textbuf, "a modern build system generator called");
    textprint(30, 86, 96);

    strcpy(textbuf, "CMake. This makes it easy to compile");
    textprint(30, 96, 96);

    strcpy(textbuf, "for both Windows using Visual Studio");
    textprint(30, 106, 96);

    strcpy(textbuf, "& Linux. For the first time LAB3D/SDL");
    textprint(30, 116, 96);

    strcpy(textbuf, "is available to 64-bit Windows and");
    textprint(30, 126, 96);

    strcpy(textbuf, "includes the latest version of SDL");
    textprint(30, 136, 96);

    strcpy(textbuf, "which includes a bugfix for multi");
    textprint(30, 146, 96);

    strcpy(textbuf, "monitor setups. This paves the way for");
    textprint(30, 156, 96);

    strcpy(textbuf, "a macOS version in the future...");
    textprint(30, 166, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnew400() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
           "Version 4.0.0 Release");
    textprint(30, 48, 80);

    strcpy(textbuf, "A brand new version has been released!");
    textprint(30, 58, 96);

    strcpy(textbuf, "This is a major release and includes");
    textprint(30, 76, 96);

    strcpy(textbuf, "a launcher to run any version of the");
    textprint(30, 86, 96);

    strcpy(textbuf, "game and 3D stereoscopic mode put");
    textprint(30, 96, 96);

    strcpy(textbuf, "into the setup menu. Stereo mode on");
    textprint(30, 106, 96);

    strcpy(textbuf, "Nintendo Switch only currently works");
    textprint(30, 116, 96);

    strcpy(textbuf, "in docked mode. When playing Ken 1.0");
    textprint(30, 126, 96);

    strcpy(textbuf, "or 1.1 on Switch, load/save your game");
    textprint(30, 136, 96);

    strcpy(textbuf, "by using -/+. The 4 save slots are");
    textprint(30, 146, 96);

    strcpy(textbuf, "L,R,ZL and ZR.");
    textprint(30, 156, 96);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void whatsnewkenslabyrinth2() {
    drawmenu(320, 172, menu);

    strcpy(textbuf,
           "Ken's Labyrinth II in development");
    textprint(30, 48, 80);

    strcpy(textbuf, "Incoming sequel to Ken's Labyrinth!");
    textprint(30, 58, 96);

    strcpy(textbuf, "After all these decades we will get");
    textprint(30, 76, 96);

    strcpy(textbuf, "to see a brand new installment to");
    textprint(30, 86, 96);

    strcpy(textbuf, "Ken's Labyrinth. Led by Ian M. Burton,");
    textprint(30, 96, 96);

    strcpy(textbuf, "he and his team are working hard to");
    textprint(30, 106, 96);

    strcpy(textbuf, "bring this project to life.");
    textprint(30, 116, 96);

    strcpy(textbuf, "Support the project on Patreon:");
    textprint(30, 136, 96);

    strcpy(textbuf, "www.patreon.com/user?u=17853987");
    textprint(30, 146, 48);

    strcpy(textbuf, "For updates on the project on Twitter:");
    textprint(30, 166, 96);

    strcpy(textbuf, "twitter.com/kenslabyrinthii");
    textprint(30, 176, 48);

    finalisemenu();
    SDL_GL_SwapWindow(mainwindow);
    pressakey();
}

void gamelaunchermenu() {
    int done=0,sel=0;

    statusbaryoffset=250;
    musicoff();
    loadmusic("LABSNG22");
    musicon();

    draw_ptr[++drawStackTopIndex] = draw_gamelaunchermenu;
 
    while(!done) {
        if ((sel = getselection(12,7,sel,8)) < 0)
            done = 1;
        else {
            switch(sel) {
                case 0:
                    /* Version 2.1 */
                    lab3dversion = KENS_LABYRINTH_2_1;
                    done = 1;
                    break;
                case 1:
                    /* Version 2.0 */
                    lab3dversion = KENS_LABYRINTH_2_0;
                    done = 1;
                    break;
                case 2:
                    /* Version 1.1 */
                    lab3dversion = KENS_LABYRINTH_1_1;
                    done = 1;
                    break;
                case 3:
                    /* Version 1.0 */
                    lab3dversion = KENS_LABYRINTH_1_0; 
                    done = 1;
                    break;
                case 4:
                    whatsnew420();
                    whatsnew418();
                    whatsnew417();
                    whatsnew416();
                    whatsnew415();
                    whatsnew414();
                    whatsnew413();
                    whatsnew412();
                    whatsnew411();
                    whatsnew410();
                    whatsnew401();
                    whatsnew400();
                    whatsnewkenslabyrinth2();
                    break;
                case 5:
                    setupmenu(0);
                    savesettings();
                    break;
                case 6:
                    orderinfomenu();
                    break;
                case 7:
                    quitgame = 1;
                    quit();
                    break;
            }
        }
    }
}
