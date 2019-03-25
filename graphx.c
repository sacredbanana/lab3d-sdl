#include "lab3d.h"
#include <math.h>

static K_INT16 wallfound[64][64][4];
static char wallside[16384];
static K_INT16 wallx[16384],wally[16384];
static K_UINT16 walnum[16384];

static K_INT32 wallsfound;

static K_INT32 rayscast;


static double hitpointx,hitpointy;

static K_INT16 mapfound;
static K_INT16 gameoverfound;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EPSILON 0.0000001

static int stereo = 0;
static GLuint stereo_fbufs[2];
static GLuint stereo_tex[2];
static GLuint stereo_depth[2];

/* (x2-x1)^2+(y2-y1)^2. */

double distance2(double x1,double y1,double x2,double y2) {
    double dx=x2-x1;
    double dy=y2-y1;

    return dx*dx+dy*dy;
}

double angcan(double angle) {
    while(angle<0) angle+=M_PI*2;
    while(angle>=M_PI*2) angle-=M_PI*2;
    return angle;
}

/* Cast a ray from (posxs,posys) in direction angle, and see what it hits
   and passes through. */

K_INT16 castray(K_UINT16 posxs,K_UINT16 posys, double angle) {
    K_INT16 walx,waly,waln;
    char wals;

    double tan1,tan2;

    double y1,x2;
    K_INT16 x1,y2,x1i,y2i;
    int y1i,x2i;

    K_INT16 xdir,ydir;
    double xinc,yinc;

    int status;

    char xdet,detr;

    char cont;

    K_INT16 j,k;

    K_INT32 cx1,cy1;

    rayscast++;

    angle=angcan(angle);

    tan1=tan(0.5*M_PI-angle);
    tan2=tan(angle);

    x1=posxs>>10;
    y1=(posxs&1023)/1024.0;
    yinc=tan2;
    xdir=1;
    if ((angle>=M_PI*0.5)&&(angle<M_PI*1.5)) {
        xdir = -1;
        yinc = -yinc;
        x1 += 1; /* Note: if xdir==-1, use x1-1 for wall checks. */
        y1 = 1.0 - y1;
    }
    if (tan2<0) {
        tan2=-tan2;
    }

    y1*=tan2;

    if (!(angle>=M_PI))
        y1=-y1;
    y1+=posys/1024.0;

    y2=posys>>10;
    x2=(posys&1023)/1024.0;
    xinc=tan1;
    ydir=1;
    if (angle>=M_PI) {
        ydir = -1;
        xinc = -xinc;
        y2 += 1; /* Note: if ydir==-1, use y2-1 for wall checks. */
        x2 = 1.0 - x2;
    }
    if (tan1<0) {
        tan1=-tan1;
    }

    x2*=tan1;

    if (!((angle>=M_PI*0.5)&&(angle<M_PI*1.5)))
        x2=-x2;
    x2+=posxs/1024.0;

    x1+=xdir;
    y1+=yinc;
    x2+=xinc;
    y2+=ydir;

    x1i=x1-(xdir<0);
    x2i=x2;
    y1i=y1;
    y2i=y2-(ydir<0);

    cont=1;
    while(cont) {
        status=0;
        cont=0;

        if (fabs(tan2)<EPSILON) {
            y1i=y1;
            x1i=x1-(xdir<0);
            if ((y1i>=0)&&(y1i<64))
                while((status!=1)&&(x1i>=0)&&(x1i<64)) {
                    tempbuf[(x1i<<6)+y1i]=1;
                    status=bmpkind[board[x1i][y1i]&1023];
                    if (status!=1) {
                        x1i+=xdir;
                    }
                }
            x1=x1i+(xdir<0);
            if (status!=1) return -1;
        } else if (fabs(tan1)<EPSILON) {
            x2i=x2;
            y2i=y2-(ydir<0);
            if ((x2i>=0)&&(x2i<64))
                while((status!=256)&&(y2i>=0)&&(y2i<64)) {
                    tempbuf[(x2i<<6)+y2i]=1;
                    status=bmpkind[board[x2i][y2i]&1023]<<8;
                    if (status!=256) {
                        y2i+=ydir;
                    }
                }
            y2=y2i+(ydir<0);
            if (status!=256) return -1;
        } else {
            xdet=(tan2<tan1);
            y1i=y1; x2i=x2;

            while((status!=1)&&(status!=256)) {
/*
  printf("Player at (%f,%f):(%d,%lf)(%lf,%d), reald %lf,%lf\n",
  posxs/1024.0,posys/1024.0,x1,y1,x2,y2,
  ((double)(x1-(posxs/1024.0)))/(double)(y1-(posys/1024.0)),
  ((double)(x2-(posxs/1024.0)))/(double)(y2-(posys/1024.0)));
  printf("expd %lf,%lf\n",
  ((double)(xdir))/(double)yinc,
  ((double)xinc)/(double)(ydir));
*/
                x1i=x1-(xdir<0); y2i=y2-(ydir<0);
                if (xdet)
                    detr=((x1-x2)<0)^(xdir<0);
                else
                    detr=((y1-y2)<0)^(ydir<0);
                if (detr) {
                    if ((x1i<0)||(x1i>=64)||(y1i<0)||(y1i>=64)) break;
                    tempbuf[(x1i<<6)+y1i]=1;
                    status=bmpkind[board[x1i][y1i]&1023];
                    if (status!=1) {
                        x1+=xdir;
                        y1+=yinc;
                        y1i=y1;
                    }
                } else {
                    if ((x2i<0)||(x2i>=64)||(y2i<0)||(y2i>=64)) break;
                    tempbuf[(x2i<<6)+y2i]=1;
                    status=bmpkind[board[x2i][y2i]&1023]<<8;
                    if (status!=256) {
                        y2+=ydir;
                        x2+=xinc;
                        x2i=x2;
                    }
                }
            }
            if ((status!=1)&&(status!=256)) return -1;
        }

        if ((status&0xff)==1) {
            j = ((int)(board[x1-(xdir<0)][y1i]-1)&1023);
            if ((angle>=M_PI*0.5)&&(angle<M_PI*1.5))
                k=board[x1i+1][y1i];
            else
                k=board[x1i-1][y1i];
            if ((k&8192)==0) {
                k &= 1023;
                if (lab3dversion) {
                    if (((k >= 152) && (k <= 157)) || ((k >= 168) && (k <= 173)))
                        j = 188;
                } else {
                    if ((k >= door1) && (k <= door1+5)) j = doorside1-1;
                    if ((k >= door2) && (k <= door2+5)) j = doorside2-1;
                    if ((k >= door3) && (k <= door3+7)) j = doorside3-1;
                    if ((k >= door4) && (k <= door4+6)) j = doorside4-1;
                    if ((k >= door5) && (k <= door5+7)) j = doorside5-1;
                }
            }
            wals=(xdir<0); /* 0=west, 1=east. */
            walx=x1-(xdir<0);
            waly=y1i;
            waln=j;
            hitpointx=x1;
            hitpointy=y1;
            x1+=xdir;
            y1+=yinc;
        } else if ((status&0xff00)==256) {
            j = ((int)(board[x2i][y2-(ydir<0)]-1)&1023);
            if (angle<M_PI)
                k=board[x2i][y2i-1];
            else
                k=board[x2i][y2i+1];
            if ((k&8192) > 0)
            {
                k &= 1023;
                if (lab3dversion) {
                    if (((k >= 152) && (k <= 157)) || ((k >= 168) && (k <= 173)))
                        j = 188;
                } else {
                    if ((k >= door1) && (k <= door1+5)) j = doorside1-1;
                    if ((k >= door2) && (k <= door2+5)) j = doorside2-1;
                    if ((k >= door3) && (k <= door3+7)) j = doorside3-1;
                    if ((k >= door4) && (k <= door4+6)) j = doorside4-1;
                    if ((k >= door5) && (k <= door5+7)) j = doorside5-1;
                }
            }
            wals=2+(ydir<0);
            walx=x2i;
            waly=y2-(ydir<0);
            waln=j;
            hitpointx=x2;
            hitpointy=y2;
            x2+=xinc;
            y2+=ydir;
        } else return -1;

        if (lab3dversion==0)
            if (waterstat>0)
                if ((waln&1023)==fountain-1)
                    waln+=(animate2+1);

        if ((status&255)==1) waln|=16384;

        //	fprintf(stderr,"Ray hit at %lf,%lf\n",hitpointx,hitpointy);

        /* Keep going if ray hit a wall very close to us. */

        cx1=((K_INT32)walx)<<10;
        cy1=((K_INT32)waly)<<10;

        cont=0;

        switch(wals) {
            case 0: /* West */
                if (((cx1-posxs)<walltol)&&
                    (abs(posys-(cy1+512))<(512+walltol)))
                    cont=1;
                break;
            case 1: /* East */
                if (((posxs-(cx1+1024))<walltol)&&
                    (abs(posys-(cy1+512))<(512+walltol)))
                    cont=1;
                break;
            case 2: /* North */
                if (((cy1-posys)<walltol)&&
                    (abs(posxs-(cx1+512))<(512+walltol)))
                    cont=1;
                break;
            case 3: /* South */
                if (((posys-(cy1+1024))<walltol)&&
                    (abs(posxs-(cx1+512))<(512+walltol)))
                    cont=1;
                break;
        }

        if (wallfound[walx][waly][(int)wals]!=-1) {
            if (!cont)
                return wallfound[walx][waly][(int)wals];
            else
                continue;
        }
        wallfound[walx][waly][(int)wals]=wallsfound;

        wallx[wallsfound]=walx;
        wally[wallsfound]=waly;
        wallside[wallsfound]=wals;
        walnum[wallsfound]=waln;

        if ((waln&1023)==map-1) mapfound=1;
        if ((waln&1023)==gameover-1) gameoverfound=1;

        wallsfound++;
/*	if (cont)
        printf("Continuing...\n");*/
    }
    return wallsfound;
}

/* Binary division ray casting routine. */

void recurseray(K_UINT16 posxs,K_UINT16 posys,double angle,double la,double ra,
                double leftx,double lefty,double rightx,double righty) {

    if (angcan(ra-la)<EPSILON) return;

    if (castray(posxs,posys,angle)<0) {
        fprintf(stderr,"Warning: ray to nothing.\n");
        return;
    }
    if ((angcan(ra-la)>=M_PI/2-EPSILON)||
        (distance2(hitpointx,hitpointy,leftx,lefty)>(1.0-EPSILON)))
        recurseray(posxs,posys,(la+angle)/2.0,la,angle,
                   leftx,lefty,hitpointx,hitpointy);
    if ((angcan(ra-la)>=M_PI/2-EPSILON)||
        (distance2(hitpointx,hitpointy,rightx,righty)>(1.0-EPSILON)))
        recurseray(posxs,posys,(ra+angle)/2.0,angle, ra,
                   hitpointx,hitpointy,rightx,righty);
}

float fogcol[4]={0.5,0.5,0.5,1.0};

void update_bulrot(K_UINT16 posxs, K_UINT16 posys) {
    /* Update bullet rotations... Somewhat misplaced IMHO. */

    K_INT16 i, j, k;
    K_INT32 x1, x2, y1, y2;

    static int spareframes=0;

    if (lab3dversion) {
        spareframes+=clockspd;
        i=(spareframes/TICKS_PER_SPRITE_FRAME)%12;
        spareframes%=TICKS_PER_SPRITE_FRAME;
        for(k=0;k<i;k++) {
            heatpos = 287-heatpos;
            j = kenpos;
            if (j == 66) kenpos = 67;
            if (j == 67) kenpos = 1024+66;
            if (j == 1024+66) kenpos = 65;
            if (j == 65) kenpos = 66;
            switch(kenpos2) {
                /* Sequence is 193, 65, 66, 67, 194, 67+1024, 66+1024, 65+1024.
                   Sequence appears to match values used in v1.1; determined
                   through observation of v1.1 in slow motion. */
                case 193:
                    kenpos2=65; break;
                case 65:
                    kenpos2=66; break;
                case 66:
                    kenpos2=67; break;
                case 67:
                    kenpos2=194; break;
                case 194:
                    kenpos2=67+1024; break;
                case 67+1024:
                    kenpos2=66+1024; break;
                case 66+1024:
                    kenpos2=65+1024; break;
                case 65+1024:
                    kenpos2=193; break;
            }
            ballpos++;
            if (ballpos == 72)
                ballpos = 68;
            fanpos++;
            if (fanpos == 52)
                fanpos = 49;
            warpos++;
            if (warpos == 125)
                warpos = 123;
        }
        for(k=0;k<bulnum;k++)
            if (bulkind[k] == 7)
            {
                x1 = ((long)bulx[k]-(long)posx);
                y1 = ((long)buly[k]-(long)posy);
                if (labs(x1)+labs(y1) < 32768)
                {
                    x1 >>= 2;
                    y1 >>= 2;
                    x2 = (((x1*sintable[bulang[k]])-(y1*sintable[(bulang[k]+512)&2047]))>>16);
                    y2 = (((x1*sintable[(bulang[k]+512)&2047])+(y1*sintable[bulang[k]]))>>16);
                    if ((x2|y2) != 0)
                    {
                        j = ((x2*clockspd)<<11)/(x2*x2+y2*y2);
                        bulang[k] += j;
                    }
                }
            }
    } else {
        for(k=0;k<bulnum;k++)
            if (bulkind[k] == 7)
            {
                x1 = ((long)bulx[k]-(long)posxs);
                y1 = ((long)buly[k]-(long)posys);
                if (labs(x1)+labs(y1) < 32768)
                {
                    x1 >>= 2;
                    y1 >>= 2;
                    x2 = (((x1*sintable[bulang[k]])-(y1*sintable[(bulang[k]+512)&2047]))>>16);
                    y2 = (((x1*sintable[(bulang[k]+512)&2047])+(y1*sintable[bulang[k]]))>>16);
                    if ((x2|y2) != 0)
                    {
                        j = ((x2*clockspd)<<11)/(x2*x2+y2*y2);
                        bulang[k] += j;
                    }
                }
            }
    }
}

/* Draw an ingame view, as seen from (posxs,posys,poszs) in direction
   angs. */

static void _picrot(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs, double aspw, double asph)
{
    unsigned char shadecoffs;
    K_INT16 i, j, k, x, y;
    K_INT16 yy, temp;
    K_INT32 x1, y1, x2, y2;
    K_INT16 xc, yc;

    double hpx1,hpy1;

    GLdouble xmin,xmax,ymin,ymax;

    double angl, angr;
    double vangw;

    for(i=0;i<explonum;i++)
    {
        checkobj(explox[i],exploy[i],posxs,posys,angs,explostat[i]);
    }

    for(i=0;i<bulnum;i++)
    {
        switch(bulkind[i])
        {
            case 1: case 18:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul1fly+animate3);
                break;
            case 2: case 19:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul2fly+animate2);
                break;
            case 3: case 20:
                k = bul3fly+animate2+2;
                j = (1024+bulang[i]-angs)&2047;
                if (j < 960)
                    k -= 2;
                if (j > 1088)
                    k += 2;
                checkobj(bulx[i],buly[i],posxs,posys,angs,k);
                break;
            case 4: case 21:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul3halfly+animate2);
                break;
            case 5: case 6:
                checkobj(bulx[i],buly[i],posxs,posys,angs,bul4fly);
                break;
            case 7: case 8:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul6fly+animate2);
                break;
            case 9: case 10:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul5fly+animate2);
                break;
            case 11: case 12:
                checkobj(bulx[i],buly[i],posxs,posys,angs,bul9fly);
                break;
            case 13: case 14:
                checkobj(bulx[i],buly[i],posxs,posys,angs,bul8fly);
                break;
            case 15: case 16: case 17:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul7fly+bulkind[i]-15);
                break;
            case 22: case 23:
                checkobj(bulx[i],buly[i],posxs,posys,angs,bul10fly);
                break;
            case 24: case 25:
                checkobj(bulx[i],buly[i],posxs,posys,angs,
                         bul11fly+animate7);
                break;
        }
    }

    wallsfound=0;
    mapfound=0;
    gameoverfound=0;

    memset(tempbuf, 0, 4096);

    // ktpanda hack:
    // It always annoyed me that whenever you enter
    // a cube, its contents disappear. This makes sure
    // that whatever is in the current cube is drawn.
    tempbuf[((posxs>>10)<<6)+(posys>>10)]=1;

    memset(wallfound,255,32768);

    rayscast=0;

    vangw=atan(tan(M_PI*0.25)*aspw);

    angl=angs/1024.0*M_PI-vangw;
    angr=angs/1024.0*M_PI+vangw;

    if (castray(posxs,posys,angr)<0)
        fprintf(stderr,"Warning: ray to nothing.\n");
    hpx1=hitpointx;
    hpy1=hitpointy;

    if (castray(posxs,posys,angl)<0)
        fprintf(stderr,"Warning: ray to nothing.\n");

    if ((angcan(angr-angl)>=M_PI/2-EPSILON)||
        (distance2(hitpointx,hitpointy,hpx1,hpy1)>(1.0-EPSILON)))
        recurseray(posxs,posys,angs/1024.0*M_PI,angl,
                   angr,hitpointx,hitpointy,hpx1,hpy1);

    //    fprintf(stderr,"Rays cast: %d\n",rayscast);

    if (vidmode == 0) {
        yy = 9000;
        // endyy = 0;
    }
    else {
        yy = 10800;
        // endyy = 0;
    }

    /* These two textures change all the time, but we don't want to waste time
       uploading invisible changes... */

    if (lab3dversion==0) {
        if (mapfound)
            updatemap();
        if (gameoverfound)
            updategameover();
    }

    /* I suppose it could be faster on some systems to do tricks with the
       viewport rather than just draw everything that goes under the status
       bar. Perhaps... later. */

    glDisable(GL_LIGHTING);
    /* Draw floor and roof (save time by clearing to one of them, and drawing
       only one rectangle)... */

    if (lab3dversion)
        glClearColor( palette[0x85*3]/64.0*redfactor,
                      palette[0x85*3+1]/64.0*greenfactor,
                      palette[0x85*3+2]/64.0*bluefactor, 0 );
    else
        glClearColor( palette[0x84*3]/64.0*redfactor,
                      palette[0x84*3+1]/64.0*greenfactor,
                      palette[0x84*3+2]/64.0*bluefactor, 0 );

    glDepthMask(1);
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glDisable(GL_TEXTURE_2D);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (GLfloat)360, 0.0, (GLfloat)240, -1.0, 1.0);

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );

    glDisable(GL_DEPTH_TEST);
    glDepthMask(0);


    glBegin(GL_QUADS);

    glColor3f(palette[0xe3*3]/64.0*redfactor,
              palette[0xe3*3+1]/64.0*greenfactor,
              palette[0xe3*3+2]/64.0*bluefactor);
    glVertex3i(0,240,0);
    glVertex3i(0,240-yy/90,0);
    glVertex3i(360,240-yy/90,0);
    glVertex3i(360,240,0);
    glEnd();

    checkGLStatus();

    /* Switch to labyrinth view transformations. */

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    xmax = neardist * tan(M_PI*0.25);
    xmin = -xmax;

    ymin = xmin * 0.75;
    ymax = -ymin;

    xmax *= aspw; xmin *= aspw;
    ymax *= asph; ymin *= asph;

    glFrustum(xmin, xmax, ymin, ymax, neardist, 98304.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    gluLookAt(posxs, posys, poszs*16.0,
              posxs+sintable[(angs+512)&2047], posys+sintable[angs],
              poszs*16.0,
              0.0,0.0,-1.0);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(1);
    /* Draw solid walls... */

//    printf("Walls found: %d\n",wallsfound);

    for(i=0;i<wallsfound;i++)
    {
        shadecoffs=(walnum[i]>>13)&2;

        if ((walnum[i]&1023) == map-1)
            shadecoffs = 0;

        j=walnum[i]&1023;

        if (wallx[i]<0) continue;

        x1=((K_INT32)wallx[i])<<10;
        y1=((K_INT32)wally[i])<<10;

        switch(wallside[i]) {
            case 0: /* West */
                x2=x1;
                y2=y1+1024;
                break;
            case 1: /* East */
                x1+=1024;
                x2=x1;
                y2=y1;
                y1+=1024;
                break;
            case 2: /* North */
                y2=y1;
                x2=x1;
                x1+=1024;
                break;
            case 3: /* South */
                y1+=1024;
                y2=y1;
                x2=x1+1024;
                break;
            default:
                /* Can't happen. I hope. */
                x2=x1;
                y2=y1;
                break;
        }

        glEnable(GL_TEXTURE_2D);
        glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
        if (lab3dversion)
            k=numsplits;
        else
            for(k=0;k<numsplits;k++)
                if (splitTexNum[k]==j) break;

        if (k<numsplits) {
            glBindTexture(GL_TEXTURE_2D,splitTexName[k][0]);
            glBegin(GL_QUADS);
            if (shadecoffs) {
                glColor3f(redfactor,greenfactor,bluefactor);
            }
            else {
                glColor3f(0.9*redfactor,0.9*greenfactor,0.9*bluefactor);
            }

            glTexCoord2f(0.0,1.0/64.0);
            glVertex3i(x1,y1,0);
            glTexCoord2f(1.0,1.0/64.0);
            glVertex3i(x1,y1,1024);
            glTexCoord2f(1.0,33.0/64.0);
            glVertex3i((x1+x2)>>1,(y1+y2)>>1,1024);
            glTexCoord2f(0.0,33.0/64.0);
            glVertex3i((x1+x2)>>1,(y1+y2)>>1,0);
            glEnd();

            glBindTexture(GL_TEXTURE_2D,splitTexName[k][1]);
            glBegin(GL_QUADS);
            glTexCoord2f(0.0,31.0/64.0);
            glVertex3i((x1+x2)>>1,(y1+y2)>>1,0);
            glTexCoord2f(1.0,31.0/64.0);
            glVertex3i((x1+x2)>>1,(y1+y2)>>1,1024);
            glTexCoord2f(1.0,63.0/64.0);
            glVertex3i(x2,y2,1024);
            glTexCoord2f(0.0,63.0/64.0);
            glVertex3i(x2,y2,0);
            glEnd();

        } else {
            if (j == invisible-1)
                glEnable(GL_BLEND);
            glBindTexture(GL_TEXTURE_2D,texName[j]);
            glBegin(GL_QUADS);
            if (j == invisible-1)
                glColor4f(1.0,1.0,1.0,0.0); /* Must draw invisible walls
                                               to make stuff behind invisible;
                                               see board 15. */
            else {
                if (shadecoffs) {
                    glColor3f(redfactor,greenfactor,bluefactor);
                }
                else {
                    glColor3f(0.9*redfactor,0.9*greenfactor,0.9*bluefactor);
                }
            }
            glTexCoord2f(0.0,walltexcoord[j][0]);
            glVertex3i(x1,y1,0);
            glTexCoord2f(1.0,walltexcoord[j][0]);
            glVertex3i(x1,y1,1024);
            glTexCoord2f(1.0,walltexcoord[j][1]);
            glVertex3i(x2,y2,1024);
            glTexCoord2f(0.0,walltexcoord[j][1]);
            glVertex3i(x2,y2,0);
            glEnd();
            if (j == invisible-1)
                glDisable(GL_BLEND);
        }
        checkGLStatus();
    }


    glDepthMask(0);

    /* Check for visible monsters... */

    if (lab3dversion) {
        for(i=0;i<mnum;i++)
        {
            xc = (mposx[i]>>10);
            yc = (mposy[i]>>10);
            if (tempbuf[(xc<<6)+yc] != 0)
            {
                temp = mboard[xc][yc];
                if ((temp != 68) && (temp != 98))
                    for(k=0;k<bulnum;k++)
                        if ((bulkind[k] == 3) || (bulkind[k] == 4))
                        {
                            x1 = ((long)bulx[k]-(long)mposx[i]);
                            y1 = ((long)buly[k]-(long)mposy[i]);
                            if (labs(x1)+labs(y1) < 32768)
                            {
                                x1 >>= 2;
                                y1 >>= 2;
                                x2 = (((x1*sintable[bulang[k]])-(y1*sintable[(bulang[k]+512)&2047]))>>16);
                                y2 = (((x1*sintable[(bulang[k]+512)&2047])+(y1*sintable[bulang[k]]))>>16);
                                if ((x2|y2) != 0)
                                {
                                    j = ((x2*clockspd)<<11)/(x2*x2+y2*y2);
                                    if (temp == 94)
                                        j >>= 1;
                                    if (temp == 109)
                                        j = -j;
                                    bulang[k] += j;
                                }
                            }
                        }
                if (temp == 66)
                    checkobj(mposx[i],mposy[i],posx,posy,ang,
                             (lab3dversion==2)?kenpos:kenpos2);
                if (temp == 68)
                    checkobj(mposx[i],mposy[i],posx,posy,ang,ballpos);
                if (temp == 38)
                {
                    if (mshock[i] > 0)
                        checkobj(mposx[i],mposy[i],posx,posy,ang,61);
                    else
                    {
                        if ((kenpos&1023) == 66)
                            checkobj(mposx[i],mposy[i],posx,posy,ang,140);
                        else if (kenpos == 65)
                            checkobj(mposx[i],mposy[i],posx,posy,ang,38);
                        else if (kenpos == 67)
                            checkobj(mposx[i],mposy[i],posx,posy,ang,141);
                    }
                }
                if (temp == 54)
                {
                    if (mshock[i] > 0)
                        checkobj(mposx[i],mposy[i],posx,posy,ang,
                                 (lab3dversion==1)?195+((kenpos&1023)==66):56);
                    else
                    {
                        j = 54;
                        if ((posx > mposx[i]) == (posy > mposy[i]))
                        {
                            if ((mgolx[i] < moldx[i]) || (mgoly[i] > moldy[i]))
                                j = 53;
                            if ((mgolx[i] > moldx[i]) || (mgoly[i] < moldy[i]))
                                j = 55;
                            if ((posx < mposx[i]) && (posy < mposy[i]))
                                j = 108 - j;
                        }
                        if ((posx > mposx[i]) != (posy > mposy[i]))
                        {
                            if ((mgolx[i] < moldx[i]) || (mgoly[i] < moldy[i]))
                                j = 53;
                            if ((mgolx[i] > moldx[i]) || (mgoly[i] > moldy[i]))
                                j = 55;
                            if ((posx > mposx[i]) && (posy < mposy[i]))
                                j = 108 - j;
                        }
                        checkobj(mposx[i],mposy[i],posx,posy,ang,j);
                    }
                }
                if (temp == 94)
                {
                    if (mshock[i] > 0)
                        checkobj(mposx[i],mposy[i],posx,posy,ang,95);
                    else
                        checkobj(mposx[i],mposy[i],posx,posy,ang,94);
                }
                if (temp == 98)
                {
                    if (mshock[i] > 0)
                        checkobj(mposx[i],mposy[i],posx,posy,ang,98);
                    else
                        checkobj(mposx[i],mposy[i],posx,posy,ang,kenpos+34);
                }
                if (temp == 109)
                {
                    if (mshock[i] > 0)
                        checkobj(mposx[i],mposy[i],posx,posy,ang,110);
                    else
                        checkobj(mposx[i],mposy[i],posx,posy,ang,109);
                }
                if (temp == 160)
                    checkobj(mposx[i],mposy[i],posx,posy,ang,kenpos+160-66);
                if (temp == 165)
                    checkobj(mposx[i],mposy[i],posx,posy,ang,165);
                if (temp == 187)
                    checkobj(mposx[i],mposy[i],posx,posy,ang,kenpos+187-66);
            }
        }

    } else {
        for(i=0;i<mnum;i++)
        {

            j=((mposx[i]>>10)<<6)+(mposy[i]>>10);

            if (tempbuf[j] != 0)
            {
                temp = mstat[i];
                if ((temp != monbal) && (temp != monhol) && (temp != monke2) && (temp != mondog))
                    for(k=0;k<bulnum;k++)
                        if ((bulkind[k] == 3) || (bulkind[k] == 4))
                        {
                            x1 = ((long)bulx[k]-(long)mposx[i]);
                            y1 = ((long)buly[k]-(long)mposy[i]);
                            if (labs(x1)+labs(y1) < 32768)
                            {
                                x1 >>= 2;
                                y1 >>= 2;
                                x2 = (((x1*sintable[bulang[k]])-(y1*sintable[(bulang[k]+512)&2047]))>>16);
                                y2 = (((x1*sintable[(bulang[k]+512)&2047])+(y1*sintable[bulang[k]]))>>16);
                                if ((x2|y2) != 0)
                                {
                                    j = ((x2*clockspd)<<11)/(x2*x2+y2*y2);
                                    if (temp == monali)
                                        j >>= 1;
                                    if ((temp == monzor) || (temp == monan2))
                                        j = -j;
                                    if (temp == monan3)
                                    {
                                        if (mshock[i] > 0)
                                            j = 0;
                                        else
                                            j = -j;
                                    }
                                    bulang[k] += j;
                                }
                            }
                        }
                switch(temp)
                {
                    case monken:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monken+oscillate5);
                        break;
                    case monbal:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monbal+animate4);
                        break;
                    case mongho:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,mongho+oscillate3);
                        break;
                    case hive:
                        if (((mshock[i]&16384) == 0) && (mstat[i] > 0))
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,hive);
                        else
                        {
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,hivetohoney+((mshock[i]-16384)>>5));
                            mshock[i] += clockspd;
                            if (((mshock[i]-16384)>>5) >= 5)
                            {
                                board[mposx[i]>>10][mposy[i]>>10] = honey+1024;
                                mnum--;
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
                        }
                        break;
                    case monske:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monske+oscillate3);
                        break;
                    case monmum:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monmum+animate8);
                        break;
                    case mongre:
                        if (mshock[i] > 0)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,mongre+5);
                        else
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,mongre+oscillate5);
                        break;
                    case monrob:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monrob+animate10);
                        break;
                    case monro2:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monro2+animate8);
                        break;
                    case mondog:
                        if (boardnum >= 10)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,mondog+animate15+15);
                        else
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,mondog+animate15);
                        break;
                    case monwit:
                        if (mshock[i] > 0)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monwit);
                        else
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monwit+oscillate5);
                        break;
                    case monand:
                        if (mshock[i] > 0)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monand+3+animate2);
                        else
                        {
                            j = monand;
                            if ((posxs > mposx[i]) == (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monand+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monand+2;
                                if ((posxs < mposx[i]) && (posys < mposy[i]) && (j != monand))
                                    j = monand+monand+3-j;
                            }
                            if ((posxs > mposx[i]) != (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monand+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monand+2;
                                if ((posxs > mposx[i]) && (posys < mposy[i]) && (j != monand))
                                    j = monand+monand+3-j;
                            }
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                        }
                        break;
                    case monali:
                        if (mshock[i] > 0)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monali+1);
                        else
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monali);
                        break;
                    case monhol:
                        if (mshock[i] > 0)
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monhol);
                        else
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,monhol+oscillate3+1);
                        break;
                    case monzor:
                        if ((mshock[i]&8192) > 0)
                        {
                            if (mshock[i] < 8192+monzor+11)
                                mshock[i] = 0;
                            else if (mshock[i] <= 8192+monzor+12)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,mshock[i]&1023);
                            else
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monzor+13+animate2);
                        }
                        if (mshock[i] < 8192)
                        {
                            if (mshock[i] > 0)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monzor+10);
                            else
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monzor+animate10);
                        }
                        if ((mshock[i]&16384) > 0)
                        {
                            j = mshock[i]&1023;
                            if (mshock[i] <= 16384+monzor+12)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                            else
                            {
                                mshock[i] = monzor+13+8192;
                                if (clockspd > 0)
                                {
                                    mshock[i] += 512/clockspd;
                                    if (mshock[i] > 16383)
                                        mshock[i] = 16383;
                                }
                            }
                        }
                        break;
                    case monbat:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monbat+oscillate3);
                        break;
                    case monear:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monear+oscillate5);
                        break;
                    case monbee:
                        checkobj(mposx[i]+(rand()&127)-64,mposy[i]+(rand()&127)-64,posxs,posys,angs,monbee+animate6);
                        break;
                    case monspi:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,monspi+animate6);
                        break;
                    case mongr2:
                        checkobj(mposx[i],mposy[i],posxs,posys,angs,mongr2+animate11);
                        break;
                    case monke2:
                        if ((mshock[i]&8192) > 0)
                        {
                            j = (mshock[i]&1023);
                            if (mshock[i] < 8192+monke2+7)
                                mshock[i] = 0;
                            else if (mshock[i] <= 8192+monke2+13)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,mshock[i]&1023);
                        }
                        if (mshock[i] < 8192)
                        {
                            if (mshock[i] > 0)
                            {
                                if (mshot[i] < 32)
                                    checkobj(mposx[i],mposy[i],posxs,posys,angs,monke2+oscillate3+3);
                                else
                                    checkobj(mposx[i],mposy[i],posxs,posys,angs,monke2+6);
                            }
                            else
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monke2+oscillate3);
                        }
                        if ((mshock[i]&16384) > 0)
                        {
                            j = mshock[i]&1023;
                            if (mshock[i] <= 16384+monke2+13)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                            else
                            {
                                mshock[i] = monke2+13+8192;
                                x = (mgolx[i]>>10);
                                y = (mgoly[i]>>10);
                                board[moldx[i]>>10][moldy[i]>>10] &= 0xbfff;
                                board[x][y] &= 0xbfff;
                                for(k=0;k<16;k++)
                                {
                                    j = (rand()&3);
                                    if ((j == 0) && ((board[x-1][y]&0x4c00) == 1024))
                                        x--;
                                    if ((j == 1) && ((board[x+1][y]&0x4c00) == 1024))
                                        x++;
                                    if ((j == 2) && ((board[x][y-1]&0x4c00) == 1024))
                                        y--;
                                    if ((j == 3) && ((board[x][y+1]&0x4c00) == 1024))
                                        y++;
                                }
                                board[x][y] |= 0x4000;
                                mposx[i] = (((unsigned)x)<<10)+512;
                                mposy[i] = (((unsigned)y)<<10)+512;
                                mgolx[i] = mposx[i];
                                mgoly[i] = mposy[i];
                            }
                        }
                        break;
                    case monan2:
                        if (mshock[i] > 0)
                        {
                            if (mshot[i] > 32)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monan2+3);
                            else
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,monan2+4+animate2);
                        }
                        else
                        {
                            j = monan2;
                            if ((posxs > mposx[i]) == (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monan2+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monan2+2;
                                if ((posxs < mposx[i]) && (posys < mposy[i]) && (j != monan2))
                                    j = monan2+monan2+3-j;
                            }
                            if ((posxs > mposx[i]) != (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monan2+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monan2+2;
                                if ((posxs > mposx[i]) && (posys < mposy[i]) && (j != monan2))
                                    j = monan2+monan2+3-j;
                            }
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                        }
                        break;
                    case monan3:
                        if ((mshock[i]&8192) > 0)
                        {
                            if (mshock[i] < 8192+monan3+3)
                                mshock[i] = 0;
                            else if (mshock[i] <= 8192+monan3+8)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,mshock[i]&1023);
                        }
                        if (mshock[i] == 0)
                        {
                            j = monan3;
                            if ((posxs > mposx[i]) == (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monan3+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monan3+2;
                                if ((posxs < mposx[i]) && (posys < mposy[i]) && (j != monan3))
                                    j = monan3+monan3+3-j;
                            }
                            if ((posxs > mposx[i]) != (posys > mposy[i]))
                            {
                                if ((mgolx[i] < moldx[i]) || (mgoly[i] < moldy[i]))
                                    j = monan3+1;
                                if ((mgolx[i] > moldx[i]) || (mgoly[i] > moldy[i]))
                                    j = monan3+2;
                                if ((posxs > mposx[i]) && (posys < mposy[i]) && (j != monan3))
                                    j = monan3+monan3+3-j;
                            }
                            checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                        }
                        if ((mshock[i]&16384) > 0)
                        {
                            j = mshock[i]&1023;
                            if (mshock[i] <= 16384+monan3+8)
                                checkobj(mposx[i],mposy[i],posxs,posys,angs,j);
                            else
                            {
                                mshock[i] = monan3+8+8192;
                                if (clockspd > 0)
                                {
                                    mshock[i] += 240/clockspd;
                                    if (mshock[i] > 16383)
                                        mshock[i] = 16383;
                                }
                            }
                        }
                        break;
                }
            }
        }
    }

    if (lab3dversion) {
        for(xc=0;xc<64;xc++)
            for(yc=0;yc<64;yc++)
                if (tempbuf[(xc<<6)+yc] != 0)
                {
                    tempbuf[(xc<<6)+yc] = 0;
                    i = (board[xc][yc]&1023);
                    if (bmpkind[i] >= 2)
                    {
                        if (i == 49)
                            i = fanpos;
                        if (i == 123)
                            i = warpos;
                        if (i == 163)
                            if (fanpos == 49)
                                i = 77;
                        j = sortcnt;
                        checkobj((xc<<10)+512,(yc<<10)+512,posxs,posys,angs,i);
                        if (bmpkind[i] == 4)
                        {
                            sortcnt = j+1;
                        }
/*          if (bmpkind[i] == 4)
            {
            sortcnt = j+1;
            sortang[sortcnt-1] = (xc<<6)+yc;
            }*/
                    }
                }
    } else {
        /* Check for visible transparent walls... */

        for(k=0;k<4096;k++)
            if (tempbuf[k] != 0)
            {
                tempbuf[k]=0;
                i=board[0][k]&1023;
                if (bmpkind[i] >= 2)
                {
                    if ((i == exitsign) || (i == soda) || (i == tentacles) || (i == tablecandle))
                        i += animate2;
                    if (i == minicolumn)
                        i += animate4;
                    j = sortcnt;
                    checkobj(((k&0xfc0)<<4)+512,((k&63)<<10)+512,posxs,posys,angs,i);
                    if (bmpkind[i] == 4)
                    {
                        sortcnt = j+1;

                        if (i == door3+1)
                        {
                            if (sorti[sortcnt-1] > 512)
                                sortbnum[sortcnt-1] = door3;
                            else if (sorti[sortcnt-1] > 470)
                                sortbnum[sortcnt-1] = door3+1;
                            else if (sorti[sortcnt-1] > 431)
                                sortbnum[sortcnt-1] = door3+2;
                            else if (sorti[sortcnt-1] > 395)
                                sortbnum[sortcnt-1] = door3+3;
                            else if (sorti[sortcnt-1] > 362)
                                sortbnum[sortcnt-1] = door3+4;
                            else if (sorti[sortcnt-1] > 332)
                                sortbnum[sortcnt-1] = door3+5;
                            else if (sorti[sortcnt-1] > 279)
                                sortbnum[sortcnt-1] = door3+6;
                            else if (sorti[sortcnt-1] > 256)
                                sortbnum[sortcnt-1] = door3+7;
                            else
                                sortcnt--;
                        }
                    }
                }
            }
    }

    /* Draw all the partially transparent stuff in order of distance... */

    totalsortcnt = sortcnt;
    for(i=0;i<totalsortcnt;i++)
    {
        temp = 0;
        for(j=0;j<sortcnt;j++)
            if (sorti[j] < sorti[temp])
                temp = j;
        k = sortbnum[temp];
        if (bmpkind[k] == 2)
        {
            if (lab3dversion)
                flatsprite(sortx[temp],sorty[temp],0,angs,k);
            else {
                if (k == warp)
                    flatsprite(sortx[temp],sorty[temp],
                               (K_INT16)((totalclock<<2)&2047),angs,warp);
                else if (k == bul8fly)
                    flatsprite(sortx[temp],sorty[temp],
                               (K_INT16)((totalclock<<3)&2047),angs,
                               bul8fly+animate2);
                else if (k == bul10fly)
                    flatsprite(sortx[temp],sorty[temp],
                               (K_INT16)((totalclock<<3)&2047),angs,
                               bul10fly+animate2);
                else
                    flatsprite(sortx[temp],sorty[temp],0,angs,k);
                if (k == fan)
                    flatsprite(sortx[temp],sorty[temp],
                               (K_INT16)((totalclock<<2)&2047),
                               angs,fan+1);
            }
        }
        if (bmpkind[k] == 4)
        {
            if (!lab3dversion && (k == slotto))
                if (slottime > 0)
                    k++;
            doordraw(sortx[temp],sorty[temp],k,posxs,posys);
        }
        if (bmpkind[k] == 5)
        {
            floorsprite(sortx[temp],sorty[temp],k);
        }
        sortcnt--;
        sortx[temp]=sortx[sortcnt];
        sorty[temp]=sorty[sortcnt];
        sorti[temp] = sorti[sortcnt];
        sortbnum[temp] = sortbnum[sortcnt];
    }
    glDepthMask(1);
    glDisable(GL_LIGHTING);

    if (bossmonster) {
        mixing=1;
        strcpy(textbuf,"BOSS:");
        textprint(139,2,(char)96);
        drawmeter((mshot[bossmonster-1]<<8)/3,0,175,2);
        drawmeter((mshot[bossmonster-1]<<8)/3,4096,175,2);
        mixing=0;
    }

    ShowStatusBar();
}

static char* load_ogl_ext_funcs(void) {
    _DYNAMIC_OGL_FUNCS(_LOAD_FUNC)
    return NULL;
}

void setup_stereo(int s) {
    static int loaded = 0;
    int i;
    if (!loaded) {
        char *failed;
        if ((failed = load_ogl_ext_funcs()) != NULL) {
            loaded = -1;
            fprintf(stderr, "Could not find %s, Stereo not available (OpenGL too old)", failed);
            return;
        }
        loaded = 1;
    }
    if (loaded == -1)
        return;

    ext_glGenFramebuffers(2, stereo_fbufs);
    glGenTextures(2, stereo_tex);
    ext_glGenRenderbuffers(2, stereo_depth);

    stereo = s;

    for (i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0,GL_RGB, stereo == 2 ? screenwidth/2 : screenwidth, screenheight, 0,GL_RGB, GL_UNSIGNED_BYTE, 0);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[i]);

        ext_glBindRenderbuffer(GL_RENDERBUFFER, stereo_depth[i]);
        ext_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        ext_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, stereo_depth[i]);

        ext_glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, stereo_tex[i], 0);
        GLenum drawbuffers[1] = {GL_COLOR_ATTACHMENT0};
        ext_glDrawBuffers(1, drawbuffers);
    }
    ext_glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void picrot(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs) {
    int i;
    if (newkeystatus(SDLK_1)) {
        g_stereo_sep -= 1;
        printf("sep = %d\n", g_stereo_sep);
    } else if (newkeystatus(SDLK_2)) {
        g_stereo_sep += 1;
        printf("sep = %d\n", g_stereo_sep);
    }
    int sep = g_stereo_sep >> 3;
    int asep = stereo == 2 ? 0 : 4;

    if (!stereo) {
        _picrot(posxs, posys, poszs, angs, aspw, asph);
    } else {
        int yo = (sintable[(angs+512)&2047] * sep) >> 17;
        int xo = (sintable[angs&2047] * sep) >> 17;
        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[0]);
        glViewport(0, 0, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        _picrot(posxs - xo, posys - yo, poszs, angs - asep, aspw, stereo == 2 ? asph*2 : asph);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, stereo_fbufs[1]);
        glViewport(0, 0, stereo == 2 ? screenwidth/2 : screenwidth, screenheight);
        _picrot(posxs + xo, posys + yo, poszs, angs + asep, aspw, stereo == 2 ? asph*2 : asph);

        ext_glBindFramebuffer(GL_FRAMEBUFFER, 0);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);

        glMatrixMode( GL_MODELVIEW );
        glLoadIdentity( );

        glViewport(0, 0, screenwidth, screenheight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(0);
        if (stereo == 2) {
            for (i = 0; i < 2; i++) {
                float ofs = i == 0 ? 0.0 : 0.5;
                glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0);
                glVertex3f(0.0+ofs, 0.0, 0.0);

                glTexCoord2f(1.0, 0.0);
                glVertex3f(0.5+ofs, 0.0, 0.0);

                glTexCoord2f(1.0, 1.0);
                glVertex3f(0.5+ofs, 1.0, 0.0);

                glTexCoord2f(0.0, 1.0);
                glVertex3f(0.0+ofs, 1.0, 0.0);
                glEnd();
            }

        } else {
            glDisable(GL_BLEND);
            glEnable(GL_TEXTURE_2D);
            for (i = 0; i < 2; i++) {
                if (i == 0)
                    glColor4f(1.0, 0.0, 0.0, 1.0);
                else {
                    glEnable(GL_BLEND);
                    glColor4f(0.0, 1.0, 1.0, 1.0);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
                }
                glBindTexture(GL_TEXTURE_2D, stereo_tex[i]);
                glBegin(GL_QUADS);
                glTexCoord2f(0.0, 0.0);
                glVertex3f(0.0, 0.0, 0.0);

                glTexCoord2f(1.0, 0.0);
                glVertex3f(1.0, 0.0, 0.0);

                glTexCoord2f(1.0, 1.0);
                glVertex3f(1.0, 1.0, 0.0);

                glTexCoord2f(0.0, 1.0);
                glVertex3f(0.0, 1.0, 0.0);
                glEnd();
            }
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1.0, 1.0, 1.0, 1.0);
        }
    }
}

void floorsprite(K_UINT16 x, K_UINT16 y, K_INT16 walnume) {
    glBindTexture(GL_TEXTURE_2D,texName[walnume-1]);
    glEnable(GL_DEPTH_TEST);

    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);

    glTexCoord2f(0.0,walltexcoord[walnume-1][0]);
    glVertex3i(x-512,y-512,1024);
    glTexCoord2f(1.0,walltexcoord[walnume-1][0]);
    glVertex3i(x-512,y+512,1024);
    glTexCoord2f(1.0,walltexcoord[walnume-1][1]);
    glVertex3i(x+512,y+512,1024);
    glTexCoord2f(0.0,walltexcoord[walnume-1][1]);
    glVertex3i(x+512,y-512,1024);
    glEnd();
    glDisable(GL_BLEND);
}

/* Draw a sprite at (x,y) in the labyrinth, twisted ang round its Z axis
   (warps/fans only!), oriented to be facing the player who is looking in
   direction playerang. Sprite has texture number walnume-1. */

void flatsprite(K_UINT16 x, K_UINT16 y,K_INT16 ang,K_INT16 playerang,
                K_INT16 walnume) {

    K_INT32 x1,y1,x2,y2;
    K_INT32 xoff,yoff;

    yoff=sintable[(playerang+512)&2047]>>7;
    xoff=sintable[(playerang+1024)&2047]>>7;

    x1=x-xoff;
    x2=x+xoff;
    y1=y-yoff;
    y2=y+yoff;

    glEnable(GL_DEPTH_TEST);

    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glPushMatrix();
    glTranslatef((x1+x2)/2.0,(y1+y2)/2.0,512);
    glRotatef(ang/2048.0*360.0,sintable[(playerang+512)&2047],
              sintable[playerang],0.0);

    glTranslatef(-(x1+x2)/2.0,-(y1+y2)/2.0,-512);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,texName[walnume-1]);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);
    glTexCoord2f(0.0,walltexcoord[walnume-1][0]);
    glVertex3i(x1,y1,0);
    glTexCoord2f(1.0,walltexcoord[walnume-1][0]);
    glVertex3i(x1,y1,1024);
    glTexCoord2f(1.0,walltexcoord[walnume-1][1]);
    glVertex3i(x2,y2,1024);
    glTexCoord2f(0.0,walltexcoord[walnume-1][1]);
    glVertex3i(x2,y2,0);
    glEnd();
    checkGLStatus();
    glPopMatrix();

    glDisable(GL_BLEND);

}

/* Draw wall number walnume-1, topleft at (x,y), magnified siz>>8 times
   (i.e. at a size of siz>>2*siz>>2 pixels). Colour 255 is transparent.
   Clip to y=[0, dside[.
   Z-Buffering done using height[] (which contains height of object in each
   column and can be considered a 1D inverse Z-buffer (alternatively add
   a Z parameter?). */

void spridraw(K_INT16 x, K_INT16 y, K_INT16 siz, K_INT16 walnume)
{
    pictur(x+(siz>>3),y+(siz>>3),siz,0,walnume);
}

/* Draw wall number walnume-1, centred at (x,y), magnified siz>>8 times
   (i.e. at a size of siz>>2*siz>>2 pixels), rotated clockwise (2048 is
   full rotation). Colour 255 is transparent. Clip to y=[0, dside[.
   Z-Buffering done using height[] (which contains height of object in each
   column and can be considered a 1D inverse Z-buffer (alternatively add
   a Z parameter?). */

void pictur(K_INT16 x,K_INT16 y,K_INT16 siz,K_INT16 ang,K_INT16 walnume)
{
    y+=spriteyoffset;

    glDisable(GL_DEPTH_TEST);

    glDisable(GL_LIGHTING);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-(virtualscreenwidth-360)/2,
               360+(virtualscreenwidth-360)/2,
               -(virtualscreenheight-240)/2,
               240+(virtualscreenheight-240)/2, -1.0, 1.0);
//    gluOrtho2D(0.0, (GLfloat)360, 0.0, (GLfloat)240);

    glMatrixMode( GL_MODELVIEW );
    glLoadIdentity( );
    glTranslatef(x,240.0-y,0.0);
    glScalef(siz/256.0,siz/256.0,siz/256.0);
    glRotatef(((GLfloat)ang)/2048.0*360.0,0.0,0.0,1.0);
    glTranslatef(-32.0,-32.0,0.0);
    glEnable(GL_TEXTURE_2D);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    if (walnume==gameover)
        glBindTexture(GL_TEXTURE_2D,gameoversprite); /* Horrible kludge. */
    else
        glBindTexture(GL_TEXTURE_2D,texName[walnume-1]);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);
    glTexCoord2f(1.0,walltexcoord[walnume-1][0]);
    glVertex3f(0.0,0.0,0);
    glTexCoord2f(1.0,walltexcoord[walnume-1][1]);
    glVertex3f(64.0,0.0,0);
    glTexCoord2f(0.0,walltexcoord[walnume-1][1]);
    glVertex3f(64.0,64.0,0);
    glTexCoord2f(0.0,walltexcoord[walnume-1][0]);
    glVertex3f(0.0,64.0,0);
    glEnd();
    checkGLStatus();

    glDisable(GL_BLEND);
}

/* Draw wall number walnume-1 at board position (x,y) (multiply by
   1024 to get a value compatible with player co-ordinates), as seen from
   (posxs, posys, poszs) looking in direction angs (0-4095).
   board[x][y]&8192 indicates the direction in which the door points
   (extends over x (0) or over y (1). */

void doordraw(K_UINT16 x,K_UINT16 y,K_INT16 walnume,K_UINT16 posxs,
              K_UINT16 posys)
{
    K_INT32 x1, y1, x2, y2;

    x1=((K_INT32)x);
    y1=((K_INT32)y);

    if ((board[x>>10][y>>10]&8192)) {
        x2=x1;
        y1+=512;
        y2=y1-1024;
    } else {
        y2=y1;
        x1-=512;
        x2=x1;
        x2+=1024;
    }

    if (
        (lab3dversion&&(!(((walnume >= 152) && (walnume <= 157)) ||
                          ((walnume >= 168) && (walnume <= 173)) ||
                          (walnume == 180)))) ||
        ((!lab3dversion)&&((!(((walnume >= door1) && (walnume <= door1+5)) ||
                              ((walnume >= door2) && (walnume <= door2+5)) ||
                              ((walnume >= door5) && (walnume <= door5+7)) ||
                              (walnume == 180)))))) {
        if ((board[x>>10][y>>10]&8192)) {
            if (posxs<x) {
                y1-=1024;
                y2+=1024;
            }
        } else {
            if (posys<y) {
                x1+=1024;
                x2-=1024;
            }
        }
    }
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glTexEnvf(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glBindTexture(GL_TEXTURE_2D,texName[walnume-1]);
    glBegin(GL_QUADS);
    glColor3f(redfactor,greenfactor,bluefactor);
    glTexCoord2f(0.0,walltexcoord[walnume-1][0]);
    glVertex3i(x1,y1,0);
    glTexCoord2f(1.0,walltexcoord[walnume-1][0]);
    glVertex3i(x1,y1,1024);
    glTexCoord2f(1.0,walltexcoord[walnume-1][1]);
    glVertex3i(x2,y2,1024);
    glTexCoord2f(0.0,walltexcoord[walnume-1][1]);
    glVertex3i(x2,y2,0);
    glEnd();
    glDisable(GL_BLEND);
    checkGLStatus();

}

/* Draw an xsiz wide, ysiz high part of texture walnume-1 (from texel
   (picx,picy)) to (x,y) on screen. Shift it a bit to the right to compensate
   for emulating 360x240 rather than 320x200. */

void statusbardraw(K_UINT16 picx, K_UINT16 picy, K_UINT16 xsiz,
                   K_UINT16 ysiz, K_UINT16 x, K_UINT16 y, K_INT16 walnume)
{
    x+=20;

    drawtooverlay(picx,picy,xsiz,ysiz,x,y,walnume-1,0);
}

/* Draw a w wide, h high part of texture walnum (from texel
   (picx,picy)) to (x,y) on screen, adding coloff to colour index. */

void drawtooverlay(K_UINT16 picx, K_UINT16 picy, int w,
                   int h, int x, int y, K_INT16 walnum,
                   unsigned char coloff) {
    int a,b;
    unsigned char *pic,*buf;

    y+=spriteyoffset;
    y+=visiblescreenyoffset;

    ClipToBuffer(&x, &y, &w, &h);

    for(a=0;a<w;a++) {
        pic=walseg[walnum]+(((picx+a)<<6)+picy);
        buf=screenbuffer+(screenbufferwidth*y+(x+a));
        for(b=0;b<h;b++) {
            if ((*pic)!=255) *buf=(*pic)+coloff;
            pic++;
            buf+=screenbufferwidth;
        }
    }

    UploadPartialOverlay(x,y,w,h);
}

/* Wipe a rectangular area of the overlay. */

void wipeoverlay(K_UINT16 x,K_UINT16 y,K_UINT16 w, K_UINT16 h) {
    int a;

    for(a=y;a<y+h;a++)
        memset(screenbuffer+((screenbufferwidth*a)+x),ingame?255:0x50,w);

    UploadPartialOverlay(x,y,w,h);
}
