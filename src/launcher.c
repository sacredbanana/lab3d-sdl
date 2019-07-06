#include "lab3d.h"

static void draw_gamelaunchermenu(void) {
    inlauncher = 1;

    glDrawBuffer(GL_FRONT);
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

void whatsnew1() {
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
    pressakey();

}

void whatsnew2() {
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
    pressakey();

}

void gamelaunchermenu() {
    int done=0,sel=0;

    statusbaryoffset=250;
    musicoff();
    loadmusic("LABSNG22");
    musicon();

    while(!done) {
        draw_gamelaunchermenu();

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
                        done=1;
                        break;
                    case 3:
                        /* Version 1.0 */
                        lab3dversion = KENS_LABYRINTH_1_0; 
                        done = 1;
                        break;
                    case 4:
                        whatsnew1();
                        whatsnew2();
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