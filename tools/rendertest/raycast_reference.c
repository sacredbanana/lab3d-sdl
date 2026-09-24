/*
 * Frozen copy of the ray caster as it was before the fixed point conversion,
 * in its original double precision form, with every name it writes prefixed
 * so the two versions' output can be compared rather than clobbering each
 * other's.
 *
 * As with reference.c this is deliberately NOT kept in step with the live
 * code in src/graphx.c - it is the thing that code is compared against.
 * Lifted from src/graphx.c at commit 8437d20.
 */

#include "shim.h"

#define EPSILON 0.0000001

static double ref_hitpointx, ref_hitpointy;
K_INT16       ref_wallfound[64][64][4];
K_INT16       ref_wallx[16384], ref_wally[16384];
char          ref_wallside[16384];
K_UINT16      ref_walnum[16384];
K_INT32       ref_wallsfound, ref_rayscast;
static K_INT16 ref_mapfound, ref_gameoverfound;
unsigned char ref_tempbuf[4096];

double ref_distance2(double x1,double y1,double x2,double y2) {
    double dx=x2-x1;
    double dy=y2-y1;

    return dx*dx+dy*dy;
}

double ref_angcan(double angle) {
    while(angle<0) angle+=M_PI*2;
    while(angle>=M_PI*2) angle-=M_PI*2;
    return angle;
}

K_INT16 ref_castray(K_UINT16 posxs,K_UINT16 posys, double angle) {
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

    ref_rayscast++;

    angle=ref_angcan(angle);

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
                    ref_tempbuf[(x1i<<6)+y1i]=1;
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
                    ref_tempbuf[(x2i<<6)+y2i]=1;
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
                    ref_tempbuf[(x1i<<6)+y1i]=1;
                    status=bmpkind[board[x1i][y1i]&1023];
                    if (status!=1) {
                        x1+=xdir;
                        y1+=yinc;
                        y1i=y1;
                    }
                } else {
                    if ((x2i<0)||(x2i>=64)||(y2i<0)||(y2i>=64)) break;
                    ref_tempbuf[(x2i<<6)+y2i]=1;
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
                if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
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
            ref_hitpointx=x1;
            ref_hitpointy=y1;
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
                if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
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
            ref_hitpointx=x2;
            ref_hitpointy=y2;
            x2+=xinc;
            y2+=ydir;
        } else return -1;

        if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)
            if (waterstat>0)
                if ((waln&1023)==fountain-1)
                    waln+=(animate2+1);

        if ((status&255)==1) waln|=16384;

        //	fprintf(stderr,"Ray hit at %lf,%lf\n",ref_hitpointx,ref_hitpointy);

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

        if (ref_wallfound[walx][waly][(int)wals]!=-1) {
            if (!cont)
                return ref_wallfound[walx][waly][(int)wals];
            else
                continue;
        }
        ref_wallfound[walx][waly][(int)wals]=ref_wallsfound;

        ref_wallx[ref_wallsfound]=walx;
        ref_wally[ref_wallsfound]=waly;
        ref_wallside[ref_wallsfound]=wals;
        ref_walnum[ref_wallsfound]=waln;

        if ((waln&1023)==map-1) ref_mapfound=1;
        if ((waln&1023)==gameover-1) ref_gameoverfound=1;

        ref_wallsfound++;
/*	if (cont)
        printf("Continuing...\n");*/
    }
    return ref_wallsfound;
}

void ref_recurseray(K_UINT16 posxs,K_UINT16 posys,double angle,double la,double ra,
                double leftx,double lefty,double rightx,double righty) {

    if (ref_angcan(ra-la)<EPSILON) return;

    if (ref_castray(posxs,posys,angle)<0) {
        fprintf(stderr,"Warning: ray to nothing.\n");
        return;
    }
    if ((ref_angcan(ra-la)>=M_PI/2-EPSILON)||
        (ref_distance2(ref_hitpointx,ref_hitpointy,leftx,lefty)>(1.0-EPSILON)))
        ref_recurseray(posxs,posys,(la+angle)/2.0,la,angle,
                   leftx,lefty,ref_hitpointx,ref_hitpointy);
    if ((ref_angcan(ra-la)>=M_PI/2-EPSILON)||
        (ref_distance2(ref_hitpointx,ref_hitpointy,rightx,righty)>(1.0-EPSILON)))
        ref_recurseray(posxs,posys,(ra+angle)/2.0,angle, ra,
                   ref_hitpointx,ref_hitpointy,rightx,righty);
}
