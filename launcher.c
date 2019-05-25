#include "lab3d.h"

static void draw_gamelaunchermenu(void) {
    int n = 18;
    drawmenu(360,240,menu);

    n = 18;
    strcpy(textbuf,"LAB3D/SDL game launcher menu");
    textprint(81,16,126);

    strcpy(textbuf,"Ken's Labyrinth 1.0 (Shareware)");
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Ken's Labyrinth 1.1 (Full)");
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Ken's Labyrinth 2.0 (Shareware)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Ken's Labyrinth 2.1 (Full)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Music: ");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Effects: ");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Sound channels: ");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Music channels: ");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Cheats: ");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Sound block size: ");
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Texture colour depth: ");
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"View: ");
    n += 12; textprint(51,n,lab3dversion?32:34);
    strcpy(textbuf,"Exit");
    n += 12; textprint(51,n,lab3dversion?128:130);

    n = 220;

    strcpy(textbuf,"Use cursor keys / left stick to select.");
    textprint(31,n,lab3dversion?32:34);

    finalisemenu();
}

void gamelaunchermenu() {
    int done=0,sel=0;

    statusbaryoffset=250;


    while(!done) {
        draw_gamelaunchermenu();

        if ((sel = getselection(12,7,sel,14)) < 0)
            done=1;
        else {
                switch(sel) {
                    case 0:
                        break;
                    case 1:
                        break;
                    case 2:
                        break;
                    case 3:
                        break;
                    case 4:
                        break;
                    case 5:
                        break;
                    case 6:
                        break;
                    case 7:
                        break;
                    case 8:
                        break;
                    case 9:
                        break;
                    case 10:
                        break;
                    case 11:
                        break;
                    case 12:
                        break;
                    case 13: ;
                        done=1;
                        break;
                }
            }
    }
}