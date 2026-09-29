#include "lab3d.h"
#include <math.h>

static K_INT16 wallfound[64][64][4];
static char wallside[16384];
static K_INT16 wallx[16384],wally[16384];
static K_UINT16 walnum[16384];

static K_INT32 wallsfound;

static K_INT32 rayscast;


/* Where the last ray hit, in 16.16 cell coordinates. */
static K_INT32 hitpointx,hitpointy;

static K_INT16 mapfound;
static K_INT16 gameoverfound;

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * ------------------------------------------------------------------------
 * Fixed point angles and slopes for the ray caster.
 *
 * The ray caster is the other half of the floating point work in the renderer
 * and the more expensive half on a machine with no FPU: it used to call tan()
 * twice per ray - hundreds of soft float calls a frame before a single grid
 * cell had been stepped - and then walk the grid in doubles.
 *
 * Angles are integers, 2^24 to the turn.  That is finer than the binary
 * subdivision below can ever need, it makes the quadrant tests into masks,
 * and it makes subdividing exact, so the recursion cannot fail to terminate
 * the way a floating point midpoint eventually can.
 *
 * Cell coordinates and slopes are 16.16.
 * ------------------------------------------------------------------------
 */

/* rendertest:begin-fixedpoint  (tools/rendertest lifts this block verbatim) */
/*
 * 2^26 to the turn.  The size is set by where the recursion bottoms out, not
 * by the accuracy of any one ray: recurseray() subdivides until it runs out
 * of angle, and every silhouette edge in the scene drives it all the way
 * down, so the floor decides how many rays a frame costs.  At 2^26 the floor
 * lands within a factor of two of the 1e-7 radian epsilon the double version
 * used, which keeps the same walls in the frame.  At 2^24 it stopped three
 * levels early and dropped the odd sliver.
 */
#define ANG_BITS    26
#define ANG_FULL    (1L << ANG_BITS)            /* a full turn   */
#define ANG_PI      (ANG_FULL >> 1)
#define ANG_HALFPI  (ANG_FULL >> 2)
#define ANG_QTRPI   (ANG_FULL >> 3)
#define ANG_MASK    (ANG_FULL - 1)

#define FX_ONE      65536L                      /* 1.0 in 16.16  */

/*
 * A ray a hair off the axis has a slope of order 1e7 cells per cell, which no
 * 16.16 accumulator can hold.  Clamping is invisible rather than approximate:
 * the board is 64 cells across, so any slope past 1024 leaves it in a single
 * step from anywhere inside it, exactly as the true slope would, and which of
 * the two walks is ahead of the other is decided the same way either way.
 */
#define SLOPE_MAX   (1024L << 16)

/* How far the subdivision test below leans toward subdividing: its threshold
   is one cell less a 1/HIT_SLOP part.  See hits_apart(). */
#define HIT_SLOP    64

/* tan over [0, pi/4].  1024 entries put the linear interpolation error below
   one part in 65536 across the whole span - tan is very nearly straight at
   this scale - so the table costs 4K and still beats the 16.16 output. */
#define TANTAB_N    1024
#define TANTAB_SH   (ANG_BITS - 3 - 10)         /* ANG_QTRPI / TANTAB_N */

static K_INT32 tantab[TANTAB_N + 1];
static int     tantab_ready;
/* rendertest:end-fixedpoint */

static void build_tantab(void) {
    int i;
    for (i = 0; i <= TANTAB_N; i++)
        tantab[i] = (K_INT32)(tan((M_PI * 0.25) * (double)i / TANTAB_N)
                              * 65536.0 + 0.5);
    tantab_ready = 1;
}

/* a * b, rounded.  The 68020 has a 32x32 into 64 multiply and gcc emits it,
   so this is a handful of cycles and not a soft float call. */
static K_INT32 fxmul(K_INT32 a, K_INT32 b) {
    return (K_INT32)(((long long)a * (long long)b + (FX_ONE / 2)) >> 16);
}

/* The double code truncated toward zero with a C cast where a shift floors.
   The two differ for a coordinate just below zero, which is what a ray
   escaping through a gap in the board edge produces, so keep the old
   behaviour rather than change which cell such a ray reports. */
static int fxtrunc(K_INT32 v) {
    return (v >= 0) ? (int)(v >> 16) : -(int)((-v) >> 16);
}

/* tan of an angle in [0, pi/4], 16.16, so at most 1.0. */
static K_INT32 small_tan(K_INT32 x) {
    K_INT32 i = x >> TANTAB_SH;
    K_INT32 f = x & ((1L << TANTAB_SH) - 1);

    if (i >= TANTAB_N) return tantab[TANTAB_N];
    /* Rounded, not truncated: truncating biases every slope low, and the
       grid walk then accumulates that bias over as many cells as it crosses. */
    return tantab[i] + (((tantab[i + 1] - tantab[i]) * f
                         + (1L << (TANTAB_SH - 1))) >> TANTAB_SH);
}

/* 65536^2 / v for a tangent v in [0, 65536], clamped to SLOPE_MAX.  The clamp
   covers v == 0, so there is no division by zero to guard separately. */
static K_INT32 slope_recip(K_INT32 v) {
    if (v <= (K_INT32)(FX_ONE / 1024)) return SLOPE_MAX;
    return (K_INT32)((0xFFFFFFFFu / (K_UINT32)v) + 1u);
}

/* Signed tangent and cotangent of a fixed point angle, both 16.16.  Exactly
   one of the two has magnitude at most 1 and comes from the table; the other
   is its reciprocal, and is the one the clamp above can bite on. */
static void ray_slopes(K_INT32 a, K_INT32 *tanp, K_INT32 *cotp) {
    K_INT32 t, lo, hi, big, small;
    int neg, inv;

    if (!tantab_ready) build_tantab();

    t = a & (ANG_PI - 1);            /* tan and cot both have period pi */

    if      (t <  ANG_QTRPI)              { small = t;              neg = 0; inv = 0; }
    else if (t <  ANG_HALFPI)             { small = ANG_HALFPI - t; neg = 0; inv = 1; }
    else if (t <  ANG_HALFPI + ANG_QTRPI) { small = t - ANG_HALFPI; neg = 1; inv = 1; }
    else                                  { small = ANG_PI - t;     neg = 1; inv = 0; }

    lo  = small_tan(small);
    big = slope_recip(lo);

    t = inv ? big : lo;              /* |tan| */
    hi = inv ? lo : big;             /* |cot| */

    *tanp = neg ? -t  : t;
    *cotp = neg ? -hi : hi;
}

/*
 * Are two hit points more than one cell apart?  This is the ray caster's
 * subdivision test, and the two directions of error are not equivalent: an
 * answer that is too small stops the recursion early and a wall goes missing
 * from the frame, while one that is too large only casts a ray that finds
 * nothing new.  So it is deliberately biased toward subdividing.
 *
 * HIT_SLOP is that bias.  A fixed point hit point lands a little way from
 * where the double version put it - the slope carries a rounding error that
 * the walk accumulates over up to 64 cells - so a pair whose true separation
 * is a hair over one cell can measure a hair under it.  Shrinking the
 * threshold by comfortably more than that error covers it; the cost is a few
 * extra rays on pairs that are within a 64th of a cell of the boundary.
 *
 * The 68020 has a 32x32 into 64 multiply and gcc emits it, so forming the
 * squares at full width costs two of those per test, a few hundred times a
 * frame.  Forming them narrower and truncating is what a first cut did, and
 * it lost walls.
 */
static int hits_apart(K_INT32 ax, K_INT32 ay, K_INT32 bx, K_INT32 by) {
    K_INT32 dx = ax - bx, dy = ay - by;

    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (dx >= FX_ONE || dy >= FX_ONE) return 1;   /* either alone settles it */

    return ((long long)dx * dx + (long long)dy * dy)
           > (((long long)(HIT_SLOP - 1) * (HIT_SLOP - 1) << 32)
              / ((long long)HIT_SLOP * HIT_SLOP));
}

/* Cast a ray from (posxs,posys) in direction angle, and see what it hits
   and passes through. */

K_INT16 castray(K_UINT16 posxs,K_UINT16 posys, K_INT32 angle) {
    K_INT16 walx,waly,waln;
    char wals;

    K_INT32 tan1,tan2;          /* |cot| and |tan| of the ray, 16.16     */

    K_INT32 y1,x2;              /* the fractional coordinate of each walk */
    K_INT16 x1,y2,x1i,y2i;
    int y1i,x2i;

    K_INT16 xdir,ydir;
    K_INT32 xinc,yinc;          /* signed, 16.16                          */

    K_INT32 a;                  /* the ray angle, canonical               */
    int west;                   /* pointing into the x-negative half      */

    int status;

    char xdet,detr;

    char cont;

    K_INT16 j,k;

    K_INT32 cx1,cy1;

    rayscast++;

    a = angle & ANG_MASK;
    west = (a >= ANG_HALFPI) && (a < 3 * ANG_HALFPI);

    ray_slopes(a, &tan2, &tan1);

    x1=posxs>>10;
    y1=(K_INT32)(posxs&1023)<<6;        /* (posxs&1023)/1024 in 16.16 */
    yinc=tan2;
    xdir=1;
    if (west) {
        xdir = -1;
        yinc = -yinc;
        x1 += 1; /* Note: if xdir==-1, use x1-1 for wall checks. */
        y1 = FX_ONE - y1;
    }
    if (tan2<0) {
        tan2=-tan2;
    }

    y1=fxmul(y1,tan2);

    if (!(a>=ANG_PI))
        y1=-y1;
    y1+=(K_INT32)posys<<6;

    y2=posys>>10;
    x2=(K_INT32)(posys&1023)<<6;
    xinc=tan1;
    ydir=1;
    if (a>=ANG_PI) {
        ydir = -1;
        xinc = -xinc;
        y2 += 1; /* Note: if ydir==-1, use y2-1 for wall checks. */
        x2 = FX_ONE - x2;
    }
    if (tan1<0) {
        tan1=-tan1;
    }

    x2=fxmul(x2,tan1);

    if (!west)
        x2=-x2;
    x2+=(K_INT32)posxs<<6;

    x1+=xdir;
    y1+=yinc;
    x2+=xinc;
    y2+=ydir;

    x1i=x1-(xdir<0);
    x2i=fxtrunc(x2);
    y1i=fxtrunc(y1);
    y2i=y2-(ydir<0);

    cont=1;
    while(cont) {
        status=0;
        cont=0;

        if (tan2==0) {
            y1i=fxtrunc(y1);
            x1i=x1-(xdir<0);
            if ((y1i>=0)&&(y1i<64))
                while((status!=1)&&(x1i>=0)&&(x1i<64)) {
                    tempbuf[(x1i<<6)+y1i]=1;
                    status=bmpkind[board[x1i][y1i]&1023];
                    if (status!=1) {
                        x1i+=xdir;
                    }
                }
            x1=(K_INT16)(x1i+(xdir<0));
            if (status!=1) return -1;
        } else if (tan1==0) {
            x2i=fxtrunc(x2);
            y2i=y2-(ydir<0);
            if ((x2i>=0)&&(x2i<64))
                while((status!=256)&&(y2i>=0)&&(y2i<64)) {
                    tempbuf[(x2i<<6)+y2i]=1;
                    status=bmpkind[board[x2i][y2i]&1023]<<8;
                    if (status!=256) {
                        y2i+=ydir;
                    }
                }
            y2=(K_INT16)(y2i+(ydir<0));
            if (status!=256) return -1;
        } else {
            xdet=(tan2<tan1);
            y1i=fxtrunc(y1); x2i=fxtrunc(x2);

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
                /* x1 and y2 are whole cells, y1 and x2 are 16.16, so the two
                   comparisons have to be made in the same units. */
                if (xdet)
                    detr=((((K_INT32)x1<<16)-x2)<0)^(xdir<0);
                else
                    detr=((y1-((K_INT32)y2<<16))<0)^(ydir<0);
                if (detr) {
                    if ((x1i<0)||(x1i>=64)||(y1i<0)||(y1i>=64)) break;
                    tempbuf[(x1i<<6)+y1i]=1;
                    status=bmpkind[board[x1i][y1i]&1023];
                    if (status!=1) {
                        x1+=xdir;
                        y1+=yinc;
                        y1i=fxtrunc(y1);
                    }
                } else {
                    if ((x2i<0)||(x2i>=64)||(y2i<0)||(y2i>=64)) break;
                    tempbuf[(x2i<<6)+y2i]=1;
                    status=bmpkind[board[x2i][y2i]&1023]<<8;
                    if (status!=256) {
                        y2+=ydir;
                        x2+=xinc;
                        x2i=fxtrunc(x2);
                    }
                }
            }
            if ((status!=1)&&(status!=256)) return -1;
        }

        if ((status&0xff)==1) {
            j = ((int)(board[x1-(xdir<0)][y1i]-1)&1023);
            if (west)
                k=board[x1i+1][y1i];
            else
                k=board[x1i-1][y1i];
            if ((k&8192)==0) {
                k &= 1023;
                if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
                    if (((k >= 152) && (k <= 157)) || ((k >= 168) && (k <= 173)))
                        j = 188;
                } else if (lab3dversion != WALKEN) {
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
            hitpointx=(K_INT32)x1<<16;
            hitpointy=y1;
            x1+=xdir;
            y1+=yinc;
        } else if ((status&0xff00)==256) {
            j = ((int)(board[x2i][y2-(ydir<0)]-1)&1023);
            if (a<ANG_PI)
                k=board[x2i][y2i-1];
            else
                k=board[x2i][y2i+1];
            if ((k&8192) > 0)
            {
                k &= 1023;
                if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
                    if (((k >= 152) && (k <= 157)) || ((k >= 168) && (k <= 173)))
                        j = 188;
                } else if (lab3dversion != WALKEN) {
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
            hitpointy=(K_INT32)y2<<16;
            x2+=xinc;
            y2+=ydir;
        } else return -1;

        if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)
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

/*
 * la < angle < ra throughout, so the span is just ra - la and no angle here
 * ever needs canonicalising - only castray() folds its argument.  Midpoints
 * are taken as la + (angle-la)/2 rather than (la+angle)/2, which both avoids
 * overflow and floors correctly when the frustum straddles zero.
 */
void recurseray(K_UINT16 posxs,K_UINT16 posys,K_INT32 angle,K_INT32 la,K_INT32 ra,
                K_INT32 leftx,K_INT32 lefty,K_INT32 rightx,K_INT32 righty) {

    K_INT32 span = ra - la;

    /* Below two units there is no midpoint left to pick, so the recursion is
       out of angular resolution.  In the double version this was a 1e-7
       radian epsilon guarding against a midpoint that had stopped moving; in
       integers it is exact and cannot fail to terminate. */
    if (span < 2) return;

    if (castray(posxs,posys,angle)<0) {
        fprintf(stderr,"Warning: ray to nothing.\n");
        return;
    }
    /* A span this wide is too wide to trust the hit point test, so subdivide
       regardless; the slack absorbs rounding in the frustum half angle. */
    if ((span>=ANG_HALFPI-16)||
        hits_apart(hitpointx,hitpointy,leftx,lefty))
        recurseray(posxs,posys,la+(angle-la)/2,la,angle,
                   leftx,lefty,hitpointx,hitpointy);
    if ((span>=ANG_HALFPI-16)||
        hits_apart(hitpointx,hitpointy,rightx,righty))
        recurseray(posxs,posys,angle+(ra-angle)/2,angle, ra,
                   hitpointx,hitpointy,rightx,righty);
}

float fogcol[4]={0.5,0.5,0.5,1.0};

void update_bulrot(K_UINT16 posxs, K_UINT16 posys) {
    /* Update bullet rotations... Somewhat misplaced IMHO. */

    K_INT16 i, j, k;
    K_INT32 x1, x2, y1, y2;

    static int spareframes=0;

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
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

/* Render one eye's view of the labyrinth.  Called once per frame, or twice
   by the stereoscopic wrapper in the renderer back end. */

void picrot_view(K_UINT16 posxs, K_UINT16 posys, K_INT16 poszs, K_INT16 angs, double aspw, double asph)
{
    unsigned char shadecoffs;
    K_INT16 i, j, k, x, y;
    K_INT16 yy, temp;
    K_INT32 x1, y1, x2, y2;
    K_INT16 xc, yc;

    K_INT32 hpx1,hpy1;

    GLdouble xmin,xmax,ymin,ymax;

    K_INT32 angl, angr, angc;

    for(i=0;i<explonum;i++)
    {
        checkobj(explox[i],exploy[i],posxs,posys,angs,explostat[i]);
    }

    if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)
    {
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
    }
    

    /* Clear last frame's wall marks rather than the whole 32 KB table. */
    {
        K_INT32 wf = wallsfound, wi;
        if (wf > 0 && wf <= 16384) {
            for (wi = 0; wi < wf; wi++)
                wallfound[wallx[wi]][wally[wi]][(int)wallside[wi]] = -1;
        } else {
            memset(wallfound,255,32768);
        }
    }
    wallsfound=0;
    mapfound=0;
    gameoverfound=0;

    memset(tempbuf, 0, 4096);

    // jspenguin hack:
    // It always annoyed me that whenever you enter
    // a cube, its contents disappear. This makes sure
    // that whatever is in the current cube is drawn.
    tempbuf[((posxs>>10)<<6)+(posys>>10)]=1;

    rayscast=0;

    /* Half the horizontal field of view, in fixed point angle units.  It
       depends only on the aspect ratio, so it is worth not recomputing the
       atan every frame. */
    {
        static double lastaspw = -1.0;
        static K_INT32 vangw;

        if (aspw != lastaspw) {
            lastaspw = aspw;
            vangw = (K_INT32)(atan(tan(M_PI*0.25)*aspw) / (M_PI*2.0)
                              * (double)ANG_FULL + 0.5);
        }

        /* angs counts 2048 to the turn, ANG_FULL counts 2^24. */
        angc = (K_INT32)angs << (ANG_BITS - 11);
        angl = angc - vangw;
        angr = angc + vangw;
    }

    if (castray(posxs,posys,angr)<0)
        fprintf(stderr,"Warning: ray to nothing.\n");
    hpx1=hitpointx;
    hpy1=hitpointy;

    if (castray(posxs,posys,angl)<0)
        fprintf(stderr,"Warning: ray to nothing.\n");

    if ((angr-angl>=ANG_HALFPI-16)||
        hits_apart(hitpointx,hitpointy,hpx1,hpy1))
        recurseray(posxs,posys,angc,angl,
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

    if (lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1) {
        if (mapfound)
            updatemap();
        if (gameoverfound)
            updategameover();
    }

    /* I suppose it could be faster on some systems to do tricks with the
       viewport rather than just draw everything that goes under the status
       bar. Perhaps... later. */

    R_BeginScene(posxs, posys, poszs, angs, aspw, asph, yy);

    /* Draw solid walls... */

//    printf("Walls found: %d\n",wallsfound);

    for(i=0;i<wallsfound;i++)
    {
        shadecoffs=(walnum[i]>>13)&2;

        if ((walnum[i]&1023) == map-1)
            shadecoffs = 0;

        /* Walken does not shade walls by orientation yet: every wall is
           drawn as the brighter of the two.  Its palette is a colour cube,
           not the ramps the Amiga renderer's darker shade steps down. */
        if (lab3dversion == WALKEN)
            shadecoffs = 2;

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

        if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)
            k=numsplits;
        else
            for(k=0;k<numsplits;k++)
                if (splitTexNum[k]==j) break;

        if (k<numsplits && R_HaveTransitionTextures()) {
            R_DrawSplitWall(x1,y1,x2,y2,k,shadecoffs!=0);
        } else {
            R_DrawWall(x1,y1,x2,y2,j,walltexcoord[j][0],walltexcoord[j][1],
                       shadecoffs!=0, j == invisible-1);
        }
    }

    R_EndWalls();

    /* Check for visible monsters... */

    if (lab3dversion == WALKEN) {
        walkenmonsters(posxs, posys, angs);
    } else if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1) {
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
                             (lab3dversion == KENS_LABYRINTH_1_0)?kenpos:kenpos2);
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
                                 (lab3dversion==KENS_LABYRINTH_1_1)?195+((kenpos&1023)==66):56);
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

    if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 || lab3dversion == WALKEN) {
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
            if (lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1 || lab3dversion == WALKEN)
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
            if ((lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1) && (k == slotto))
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
    R_EndScene();

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

void floorsprite(K_UINT16 x, K_UINT16 y, K_INT16 walnume) {
    R_DrawFloorSprite(x, y, walnume-1);
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

    R_DrawBillboard(x1,y1,x2,y2,walnume-1,
                    walltexcoord[walnume-1][0], walltexcoord[walnume-1][1],
                    ang, playerang);
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

    R_DrawSprite2D(x, y, siz, ang, walnume-1, walnume==gameover);
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
        ((lab3dversion == KENS_LABYRINTH_1_0 || lab3dversion == KENS_LABYRINTH_1_1)&&(!(((walnume >= 152) && (walnume <= 157)) ||
                          ((walnume >= 168) && (walnume <= 173)) ||
                          (walnume == 180)))) ||
        ((lab3dversion == KENS_LABYRINTH_2_0 || lab3dversion == KENS_LABYRINTH_2_1)&&((!(((walnume >= door1) && (walnume <= door1+5)) ||
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
    R_DrawBillboard(x1,y1,x2,y2,walnume-1,
                    walltexcoord[walnume-1][0], walltexcoord[walnume-1][1],
                    0, 0);
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

    for(a=y;a<y+h;a++) {
        memset(screenbuffer+((screenbufferwidth*a)+x),ingame?255:0x50,w);
    }

    UploadPartialOverlay(x,y,w,h);
}
