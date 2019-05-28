#include "lab3d.h"

static void draw_gamelaunchermenu(void) {
    int n = 18;
    drawmenu(360,240,menu);

    n = 18;
    strcpy(textbuf,"LAB3D/SDL game launcher menu");
    textprint(81,16,126);

    strcpy(textbuf,"Ken's Labyrinth 2.1 (Full)");
    n += 12; textprint(51,n,34);
    strcpy(textbuf,"Ken's Labyrinth 2.0 (Shareware)");
    n += 12; textprint(51,n,34);
    strcpy(textbuf,"Ken's Labyrinth 1.1 (Full)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Ken's Labyrinth 1.0 (Shareware)");
    n += 12; textprint(51,n,64);
    strcpy(textbuf,"Setup");
    n += 12; textprint(51,n,96);
    strcpy(textbuf,"Exit");
    n += 12; textprint(51,n,96);

    n = 220;

    strcpy(textbuf,"Use cursor keys / left stick to select.");
    textprint(31,n,lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 ? 32 : 34);

    finalisemenu();
}

void gamelaunchermenu() {
    int done=0,sel=0;

    statusbaryoffset=250;

    while(!done) {
        draw_gamelaunchermenu();

        if ((sel = getselection(12,7,sel,6)) < 0)
            done = 1;
        else {
                switch(sel) {
                    case 0:
                     /* Version 2.1 */
                        lab3dversion = KENS_LABYRINTH_2_1;
                        rnumwalls = 448;
                        fprintf(stderr, "Ken's Labyrinth version 2.1 selected.\n");
                        done = 1;
                        break;
                    case 1:
                     /* Version 2.0 */
                        lab3dversion = KENS_LABYRINTH_2_0;
                        rnumwalls = 448;
                        fprintf(stderr, "Ken's Labyrinth version 2.0 selected.\n");
                        done = 1;
                        break;
                    case 2:
                     /* Version 1.1 */
                        lab3dversion = KENS_LABYRINTH_1_1;
                        rnumwalls=0xe0;
                        fprintf(stderr, "Ken's Labyrinth version 1.1 selected.\n");
                        done=1;
                        break;
                    case 3:
                        /* Version 1.0 */
                        lab3dversion = KENS_LABYRINTH_1_0; 
                        rnumwalls = 192;
                        fprintf(stderr, "Ken's Labyrinth version 1.0 selected.\n");
                        done = 1;
                        break;
                    case 4:
                        setupmenu(0);
                        break;
                    case 5:
                        quit();
                        break;
                }
            }
    }
}