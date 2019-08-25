
#include "lab3d.h"

static K_UINT16 pageoffset, lastpageoffset; // Obsolete flipping code.

K_INT16 oldintroduction(void);
K_INT16 oldcheckhitwall(K_UINT16 oposx,K_UINT16 oposy,K_UINT16 posix,
                        K_UINT16 posiy,K_INT16 angl);
void oldwingame(K_UINT16 mxpos, K_UINT16 mypos);
void olddrawlife(void);
K_INT16 oldloadgame(K_INT16 gamenum);
K_INT16 oldsavegame(K_INT16 gamenum);
void oldstatusbaralldraw(void);
void oldshowcompass(K_INT16 compang);
K_INT16 oldloadstory();
void olddrawscore(K_INT32 thescore);
void olddrawtime(K_INT32 thetime);
void oldstatusbardraw(K_UINT16 picx, K_UINT16 picy, K_UINT16 xsiz,
                      K_UINT16 ysiz, K_UINT16 x, K_UINT16 y, K_INT16 walnume);
void oldtextprint(K_INT16 x,K_INT16 y,char coloffs);

void oldmain(void)
{
    char ksmfile[15];
    K_INT16 i, j=0, k, m, n, x, y, incenter=0;
    K_UINT16 l, newx, newy, oposx, oposy, plcx, plcy;
    K_INT32 tanz;
    K_INT16 inhibitrepeat=0;
    K_INT16 thvel, tsvel;
    char cheatkeysdown;

    // K_INT32 fwdvel=0, sidevel=0, standvel=0;
    // K_INT32 dfwdvel=0, dsidevel=0, dturnvel=0, dstandvel=0;

    clockspd=0;

    settransferpalette();
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
    musicoff();
    hiscorenamstat = 0;
    SetVisibleScreenOffset(0);
    fade(63);
    oldintroduction();
    visiblescreenyoffset=0;

    while (getkeydefstat(ACTION_MENU) == 0)
    {
        PollInputs();
        /* Check cheat keys... */

        if ((keystatus[42] > 0) && (keystatus[54] > 0) && (cheatenable == 1))
            cheatkeysdown = 1;
        else if ((keystatus[42] > 0) && (keystatus[29] > 0) && (cheatenable == 2))
            cheatkeysdown=1;
        else
            cheatkeysdown = 0;

        if (death < 4095)
        {
            fade(death>>6);
            posz+=2;
            if (posz > 64)
                posz = 64;
            if (angvel < 0)
                angvel -= 20;
            if (angvel > 0)
                angvel += 20;
            ang = (ang+angvel)&2047;
            death -= 128;
            if (death <= 0)
            {
                death = 0;
                fade(63);
                if (lifevests > 0)
                {
                    death = 4095;
                    life = 4095;
                    if (lifevests == 1) oldstatusbardraw(22,33,16,16,31,3,85);
                    else if (lifevests == 2) oldstatusbardraw(22,33,16,16,43,13,85);
                    else if (lifevests == 3) oldstatusbardraw(22,33,16,16,55,3,85);
                    else if (lifevests == 4) oldstatusbardraw(22,33,16,16,67,13,85);
                    purpletime = totalclock-1;
                    greentime = totalclock-1;
                    capetime[0] = totalclock-1;
                    capetime[1] = totalclock-1;
                    oldstatusbardraw(16,17,16,15,114,17,93);
                    oldstatusbardraw(16,2,16,15,114,2,93);
                    oldstatusbardraw(16,2,22,30,130,2,93);
                    oldstatusbardraw(15,2,21,30,153,2,93);
                    olddrawlife();
                    lifevests--;
                    musicon();
                    posz = 32;
                    posx = startx;
                    posy = starty;
                    ang = startang;
                    angvel = 0;
                    vel = 0;
                    mxvel = 0;
                    myvel = 0;
                    svel = 0;
                    hvel = 0;
                }
                else
                    oldintroduction();
            }
        }
        for(i=0;i<bulnum;i++)
        {
            if (bulkind[i] == 1)
                checkobj(bulx[i],buly[i],posx,posy,ang,39);
            if (bulkind[i] == 2)
                checkobj(bulx[i],buly[i],posx,posy,ang,27);
            if (bulkind[i] == 3)
            {
                k = heatpos;
                j = (1024+bulang[i]-ang)&2047;
                if (j < 960)
                    k += 2;
                if (j > 1088)
                    k += 4;
                checkobj(bulx[i],buly[i],posx,posy,ang,k);
            }
            if (bulkind[i] == 4)
                checkobj(bulx[i],buly[i],posx,posy,ang,heatpos-47);
            if ((bulkind[i] == 5) || (bulkind[i] == 6))
                checkobj(bulx[i],buly[i],posx,posy,ang,57);
            if ((bulkind[i] == 7) || (bulkind[i] == 8))
                checkobj(bulx[i],buly[i],posx,posy,ang,ballpos+43);
            if (bulkind[i] < 5)
                if (bulstat[i] < totalclock)
                {
                    bulnum--;
                    bulx[i] = bulx[bulnum];
                    buly[i] = buly[bulnum];
                    bulang[i] = bulang[bulnum];
                    bulstat[i] = bulstat[bulnum];
                    bulkind[i] = bulkind[bulnum];
                }
            if (bulkind[i] >= 5)
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
        SetVisibleScreenOffset(0);
        update_bulrot(posx, posy);
        picrot(posx,posy,posz,ang);
        sortcnt = 0;
        SDL_LockMutex(soundmutex);
        SDL_LockMutex(timermutex);

        /* Speed cap at 2 ticks/frame (about 120 fps). */
        if ((musicstatus == 1) && (clockspeed >= 0) && (clockspeed < 2)) {
            SDL_UnlockMutex(soundmutex);
            while(clockspeed<2) {
                SDL_Delay(0); /* Give other threads a chance. */
                PollInputs();
                updateclock();
            }
            SDL_LockMutex(soundmutex);
        }

        if (musicstatus!=1)
            SDL_Delay(10); /* Just to prevent insane speeds... */

        clockspd=clockspeed;
        if (clockspd>48) clockspd=48; /* Prevent total insanity if game
                                         is suspended. */
        clockspeed=0;
        SDL_UnlockMutex(timermutex);
        SDL_UnlockMutex(soundmutex);
        lastpageoffset = pageoffset;
        if (vidmode == 0)
        {
            /* TODO: Set screen start to lastpageoffset+5. */
            if (lastpageoffset < 20880) pageoffset = 20880;
            if (lastpageoffset == 20880) pageoffset = 38880;
            if (lastpageoffset > 20880) pageoffset = 2880;
        }
        else
        {
            /* TODO: Set screen start to lastpageoffset. */
            if (statusbar == 415)
            {
                if (lastpageoffset < 21600) pageoffset = 21600;
                if (lastpageoffset == 21600) pageoffset = 40320;
                if (lastpageoffset > 21600) pageoffset = 2880;
            }
            else
            {
                if (lastpageoffset < 21600) pageoffset = 21600;
                if (lastpageoffset == 21600) pageoffset = 43200;
                if (lastpageoffset > 21600) pageoffset = 0;
            }
        }
        if (newkeystatus(SDLK_r) > 0)
            rogermode = rogermode ^ 1; /* Don't ask me, I'm just porting this. */
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

        if (death == 4095)
        {
            if ((getkeydefstat(ACTION_FIREBALLS)||getkeydefstat(ACTION_BOUNCY)||getkeydefstat(ACTION_HEAT)) > 0)
            {
                if (getkeydefstat(ACTION_FIREBALLS) > 0) bulchoose = 0;
                if (getkeydefstat(ACTION_BOUNCY) > 0) bulchoose = 1;
                if (getkeydefstat(ACTION_HEAT) > 0) bulchoose = 2;
                for(i=1;i<=6;i++)
                {
                    if (firepowers[bulchoose] >= i)
                    {
                        if (bulchoose == 0)
                            oldstatusbardraw(16,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
                        if (bulchoose == 1)
                            oldstatusbardraw(32,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
                        if (bulchoose == 2)
                            oldstatusbardraw(48,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
                    }
                    else
                        oldstatusbardraw(16+(i&1),4,14,14,326-((i<<5)-(i<<3)-i),4,93);
                }
            }
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
        for(i=0;i<bulnum;i++)
        {
            x = (int)((clockspd*sintable[(bulang[i]+512)&2047])>>13);
            y = (int)((clockspd*sintable[bulang[i]])>>13);
            for(m=0;m<4;m++)
                if (bulstat[i] > 0)
                {
                    bulx[i] += x;
                    buly[i] += y;
                    if (((bulkind[i] == 7) || (bulkind[i] == 5)) && (death == 4095) && (m < 4))
                    {
                        l = labs((long)bulx[i]-(long)posx)+labs((long)buly[i]-(long)posy);
                        if (l < 768)
                        {
                            if (greentime < totalclock)
                            {
                                if ((capetime[0] < totalclock) && (capetime[1] < totalclock))
                                {
                                    n = 128;
                                    if (bulkind[i] == 7)
                                        n = 320;
                                    if (purpletime >= totalclock)
                                        n >>= 1;
                                    life -= n;
                                    if (life <= 0)
                                    {
                                        life = 0;
                                        olddrawlife();
                                        death = 4094;
                                        angvel = (rand()&32)-16;
                                        ksay(5);
                                        musicoff();
                                    }
                                    else
                                    {
                                        olddrawlife();
                                        angvel = (rand()&32)-16;
                                        ksay(14);
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
                                bulang[i] = ((bulang[i]+1024)&2047);
                                m = 4;
                                if (bulkind[i] == 5)
                                    bulkind[i] = 6;
                                if (bulkind[i] == 7)
                                    bulkind[i] = 8;
                                bulstat[i] = totalclock+240;
                            }
                        }
                    }
                    j = mboard[bulx[i]>>10][buly[i]>>10];
                    if ((j > 0) && ((bulkind[i] != 5) && (bulkind[i] != 7)) && (m < 4))
                    {
                        l = 0;
                        for(k=0;k<mnum;k++)
                            if (labs((long)bulx[i]-(long)mposx[k])+labs((long)buly[i]-(long)mposy[k]) < 768)
                                if (mstat[k] != 98)
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
                                        if (mstat[k] == 109)
                                            mshock[k] += 15;
                                        if ((mstat[k] == 54) || (mstat[k] == 94))
                                            mshock[k] += 30;
                                        if ((mstat[k] == 38) || (mstat[k] == 68))
                                            mshock[k] += 60;
                                        if ((mstat[k] != 68) && (mstat[k] != 165))
                                            l |= 2;
                                    }
                                    if (mshot[k] == 0)
                                    {
                                        if ((mstat[k] == 68) || (mstat[k] == 165))
                                        {
                                            mshot[k] = 1;
                                            l |= 4;
                                        }
                                        else
                                        {
                                            l |= 1;
                                            checkobj(mposx[k],mposy[k],posx,posy,ang,44);
                                            if (mstat[k] == 109)
                                            {
                                                oldwingame(mposx[k],mposy[k]);
                                                quit();
                                                l = 0;
                                            }
                                            if (mstat[k] == 66) scorecount += 100;
                                            if (mstat[k] == 38) scorecount += 250;
                                            if (mstat[k] == 54) scorecount += 500;
                                            if (mstat[k] == 68) scorecount += 250;
                                            if (mstat[k] == 94) scorecount += 1000;
                                            if (mstat[k] == 98) scorecount += 1500;
                                            if (mstat[k] == 160) scorecount += 50, l |= 16;
                                            if (mstat[k] == 165) scorecount += 2500;
                                            if (mstat[k] == 187) scorecount += 100, l |= 8;
                                            olddrawscore(scorecount);
                                            mnum--;
                                            mboard[moldx[k]>>10][moldy[k]>>10] = 0;
                                            mboard[mgolx[k]>>10][mgoly[k]>>10] = 0;
                                            moldx[k] = moldx[mnum];
                                            moldy[k] = moldy[mnum];
                                            mposx[k] = mposx[mnum];
                                            mposy[k] = mposy[mnum];
                                            mgolx[k] = mgolx[mnum];
                                            mgoly[k] = mgoly[mnum];
                                            mstat[k] = mstat[mnum];
                                            mshot[k] = mshot[mnum];
                                            mshock[k] = mshock[mnum];
                                        }
                                    }
                                }
                        if (l > 0)
                        {
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
                            if (death == 4095)
                            {
                                if ((l&1) > 0)
                                {
                                    if ((l&8) == 0)
                                    {
                                        if ((l&16) == 0)
                                            ksay(1);
                                        else
                                            ksay(0);
                                    }
                                    else
                                        ksay(10);
                                }
                                if ((l&2) > 0)
                                    ksay(8);
                            }
                        }
                    }
                    j = board[bulx[i]>>10][buly[i]>>10];
                    if ((bmpkind[j&1023] != 0) && (((bulx[i]|1023) != (posx|1023)) || ((buly[i]|1023) != (posy|1023))) && (death == 4095))
                    {
                        if ((bulkind[i] < 5) && ((j == 5) || (j == 6) || (j == 139)) && (m < 4))
                        {
                            if (j == 5)
                                board[bulx[i]>>10][buly[i]>>10] = 139;
                            life-=64;
                            if (life <= 0)
                            {
                                life = 0;
                                olddrawlife();
                                death = 4094;
                                angvel = (rand()&32)-16;
                                ksay(5);
                                musicoff();
                            }
                            else
                            {
                                olddrawlife();
                                angvel = (rand()&32)-16;
                                ksay(14);
                            }
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if ((bulkind[i] < 5) && (j == 15) && (m < 4))
                        {
                            if (mnum < 512)
                            {
                                mposx[mnum] = ((bulx[i]>>10)<<10)+512;
                                mposy[mnum] = ((buly[i]>>10)<<10)+512;
                                mgolx[mnum] = mposx[mnum];
                                mgoly[mnum] = mposy[mnum];
                                moldx[mnum] = mposx[mnum];
                                moldy[mnum] = mposy[mnum];
                                mstat[mnum] = 54;
                                mshock[mnum] = 0;
                                mshot[mnum] = 10;
                                mnum++;
                                mboard[bulx[i]>>10][buly[i]>>10] = 15;
                            }
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if ((bulkind[i] < 5) && (j == 52))
                        {
                            ksay(3);
                            bulnum--;
                            bulx[i] = bulx[bulnum];
                            buly[i] = buly[bulnum];
                            bulang[i] = bulang[bulnum];
                            bulstat[i] = bulstat[bulnum];
                            bulkind[i] = bulkind[bulnum];
                            m = 4;
                        }
                        if (((wallheader[j&1023]&8) == 0) && ((j&1023) != 40) && (m < 4))
                            if ((bmpkind[j&1023] == 1) || (bmpkind[j&1023] == 4) || ((((bulx[i]&1023)<<2) > lborder[j&1023]) && (((bulx[i]&1023)<<2) < (4096-lborder[j])) && (((buly[i]&1023)<<2) > lborder[j]) && (((buly[i]&1023)<<2) < (4096-lborder[j&1023]))))
                            {
                                if (((j&2048) > 0) && (bulkind[i] < 5))
                                {
                                    ksay(10);
                                    checkobj(bulx[i],buly[i],posx,posy,ang,44);
                                    if ((j&1024) == 0)
                                        board[bulx[i]>>10][buly[i]>>10] = 1024;
                                    else
                                        board[bulx[i]>>10][buly[i]>>10] ^= 2048;
                                    scorecount += 750;
                                    olddrawscore(scorecount);
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
                                    if (bulkind[i] == 2)
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
                                            plcy = oposy+(int)((((long)oposx-(long)plcx)*tantable[bulang[i]&1023])>>16);
                                            if ((wallheader[board[(plcx>>10)-1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                            if ((wallheader[board[(plcx>>10)-1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                        }
                                        if ((newx&0xfc00) > (oposx&0xfc00))
                                        {
                                            plcx = (oposx&0xfc00)+1023;
                                            plcy = oposy+(int)((((long)oposx-(long)plcx)*tantable[bulang[i]&1023])>>16);
                                            if ((wallheader[board[(plcx>>10)+1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                            if ((wallheader[board[(plcx>>10)+1][plcy>>10]&1023]&8) == 0)
                                                k |= 1;
                                        }
                                        if ((newy&0xfc00) < (oposy&0xfc00))
                                        {
                                            plcy = (oposy&0xfc00);
                                            plcx = oposx+(int)((((long)oposy-(long)plcy)*tantable[(2560-bulang[i])&1023])>>16);
                                            if ((wallheader[board[plcx>>10][(plcy>>10)-1]&1023]&8) == 0)
                                                k |= 2;
                                            if ((wallheader[board[plcx>>10][(plcy>>10)-1]&1023]&8) == 0)
                                                k |= 2;
                                        }
                                        if ((newy&0xfc00) > (oposy&0xfc00))
                                        {
                                            plcy = (oposy&0xfc00)+1023;
                                            plcx = oposx+(int)((((long)oposy-(long)plcy)*tantable[(2560-bulang[i])&1023])>>16);
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
                        if (((j&1023) == 40) && (m < 4))
                        {
                            while ((board[bulx[i]>>10][buly[i]>>10]&1023) == 40)
                            {
                                bulx[i] -= x;
                                buly[i] -= y;
                            }
                            bulang[i] = ((bulang[i]+1024)&2047);
                            m = 4;
                            if (bulkind[i] < 5)
                            {
                                if (death == 4095)
                                    ksay(2);
                                bulstat[i] = totalclock+240;
                            }
                        }
                    }
                }
        }
        for(i=0;i<mnum;i++)
            if (labs((long)mposx[i]-(long)posx)+labs((long)mposy[i]-(long)posy) < 16384)
            {
                j=0;
                if (mstat[i] == 66)                 //Ken speed
                    j = (clockspd<<2)+(clockspd<<1);
                if (mstat[i] == 38)                 //Monster speed
                    j = (clockspd<<3)+(clockspd<<1);
                if (mstat[i] == 54)                 //Andy speed
                    j = (clockspd<<3)+(clockspd<<2);
                if (mstat[i] == 68)                 //Ball speed
                    j = (clockspd<<3);
                if (mstat[i] == 94)                 //Alien speed
                    j = (clockspd<<4);
                if (mstat[i] == 98)                 //Hole speed
                    j = (clockspd<<2);
                if (mstat[i] == 109)                //Grim speed
                    j = (clockspd<<4);
                if (mstat[i] == 160)                //Bat speed
                    j = (clockspd<<3)+(clockspd<<2);
                if (mstat[i] == 165)                //Ghost
                    j = clockspd;
                if (mstat[i] == 187)                //Skeleton speed
                    j = (clockspd<<2)+(clockspd<<1);

                if (mshock[i] > 0)
                {
                    mshock[i] -= clockspd;
                    if (mshock[i] < 0)
                        mshock[i] = 0;
                }
                else
                {
                    if (mgolx[i] > mposx[i])
                    {
                        mposx[i] += j;
                        if (mposx[i] > mgolx[i])
                            mposx[i] = mgolx[i];
                    }
                    if (mgolx[i] < mposx[i])
                    {
                        mposx[i] -= j;
                        if (mposx[i] < mgolx[i])
                            mposx[i] = mgolx[i];
                    }
                    if (mgoly[i] > mposy[i])
                    {
                        mposy[i] += j;
                        if (mposy[i] > mgoly[i])
                            mposy[i] = mgoly[i];
                    }
                    if (mgoly[i] < mposy[i])
                    {
                        mposy[i] -= j;
                        if (mposy[i] < mgoly[i])
                            mposy[i] = mgoly[i];
                    }
                }
                if ((labs((long)posx-(long)mposx[i])+labs((long)posy-(long)mposy[i]) < 768) && (death == 4095) && ((capetime[0] < totalclock) || (capetime[1] >= totalclock)))
                {
                    if (capetime[1] < totalclock)
                    {
                        if ((mstat[i] == 98) && (mshock[i] > 0))
                        {
                            life = 0;
                            olddrawlife();
                            death = 4094;
                            angvel = 0;
                            ksay(6);
                            musicoff();
                        }
                        else if (mshock[i] == 0)
                        {
                            life -= 48+(clockspd<<2);
                            if (life <= 0)
                            {
                                life = 0;
                                olddrawlife();
                                death = 4094;
                                angvel = (rand()&32)-16;
                                ksay(5);
                                musicoff();
                            }
                            else
                            {
                                olddrawlife();
                                angvel = (rand()&32)-16;
                                ksay(14);
                            }
                        }
                    }
                    else
                    {
                        if (mstat[i] != 109)
                        {
                            checkobj(mposx[i],mposy[i],posx,posy,ang,44);
                            mnum--;
                            mboard[moldx[i]>>10][moldy[i]>>10] = 0;
                            mboard[mgolx[i]>>10][mgoly[i]>>10] = 0;
                            moldx[i] = moldx[mnum];
                            moldy[i] = moldy[mnum];
                            mposx[i] = mposx[mnum];
                            mposy[i] = mposy[mnum];
                            mgolx[i] = mgolx[mnum];
                            mgoly[i] = mgoly[mnum];
                            mstat[i] = mstat[mnum];
                            mshot[i] = mshot[mnum];
                            mshock[i] = mshock[mnum];
                            ksay(1);
                        }
                    }
                }
                if ((mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]) && (mshock[i] == 0))
                {
                    if ((board[mposx[i]>>10][mposy[i]>>10]&1023) == 73)
                    {
                        if (mstat[i] == 68)
                            checkobj(mposx[i],mposy[i],posx,posy,ang,72);
                        mnum--;
                        mboard[moldx[i]>>10][moldy[i]>>10] = 0;
                        mboard[mgolx[i]>>10][mgoly[i]>>10] = 0;
                        moldx[i] = moldx[mnum];
                        moldy[i] = moldy[mnum];
                        mposx[i] = mposx[mnum];
                        mposy[i] = mposy[mnum];
                        mgolx[i] = mgolx[mnum];
                        mgoly[i] = mgoly[mnum];
                        mstat[i] = mstat[mnum];
                        mshot[i] = mshot[mnum];
                        mshock[i] = mshock[mnum];
                        ksay(6);
                    }
                    else
                    {
                        x = (mposx[i]>>10);
                        y = (mposy[i]>>10);
                        if (mstat[i] == 98)
                        {
                            k = 4;
                            do
                            {
                                j = (rand()&3);
                                if ((j == 0) && ((board[x-1][y]&3072) == 1024) && (board[x-1][y] != 40+1024) && (mboard[x-1][y] == 0))
                                    mgolx[i] = mposx[i]-1024;
                                if ((j == 1) && ((board[x+1][y]&3072) == 1024) && (board[x+1][y] != 40+1024) && (mboard[x+1][y] == 0))
                                    mgolx[i] = mposx[i]+1024;
                                if ((j == 2) && ((board[x][y-1]&3072) == 1024) && (board[x][y-1] != 40+1024) && (mboard[x][y-1] == 0))
                                    mgoly[i] = mposy[i]-1024;
                                if ((j == 3) && ((board[x][y+1]&3072) == 1024) && (board[x][y+1] != 40+1024) && (mboard[x][y+1] == 0))
                                    mgoly[i] = mposy[i]+1024;
                                k--;
                            }
                            while ((k > 0) && (mposx[i] == mgolx[i]) && (mposy[i] == mgoly[i]));
                        }
                        else
                        {
                            if ((rand()&1) == 0)
                            {
                                if ((posx < mposx[i]) && ((board[x-1][y]&3072) == 1024) && (board[x-1][y] != 40+1024) && (mboard[x-1][y] == 0))
                                    mgolx[i] = mposx[i]-1024;
                                if ((posx > mposx[i]) && ((board[x+1][y]&3072) == 1024) && (board[x+1][y] != 40+1024) && (mboard[x+1][y] == 0))
                                    mgolx[i] = mposx[i]+1024;
                            }
                            else
                            {
                                if ((posy < mposy[i]) && ((board[x][y-1]&3072) == 1024) && (board[x][y-1] != 40+1024) && (mboard[x][y-1] == 0))
                                    mgoly[i] = mposy[i]-1024;
                                if ((posy > mposy[i]) && ((board[x][y+1]&3072) == 1024) && (board[x][y+1] != 40+1024) && (mboard[x][y+1] == 0))
                                    mgoly[i] = mposy[i]+1024;
                            }
                        }
                        if (mstat[i] == 109)
                            if (labs((long)mposx[i]-(long)posx)+labs((long)mposy[i]-(long)posy) < 5000)
                            {
                                if ((posx > mposx[i]) && ((board[x-1][y]&3072) == 1024) && (board[x-1][y] != 40+1024) && (mboard[x-1][y] == 0))
                                    mgolx[i] = mposx[i]-1024;
                                if ((posx < mposx[i]) && ((board[x+1][y]&3072) == 1024) && (board[x+1][y] != 40+1024) && (mboard[x+1][y] == 0))
                                    mgolx[i] = mposx[i]+1024;
                                if ((posy > mposy[i]) && ((board[x][y-1]&3072) == 1024) && (board[x][y-1] != 40+1024) && (mboard[x][y-1] == 0))
                                    mgoly[i] = mposy[i]-1024;
                                if ((posy < mposy[i]) && ((board[x][y+1]&3072) == 1024) && (board[x][y+1] != 40+1024) && (mboard[x][y+1] == 0))
                                    mgoly[i] = mposy[i]+1024;
                            }
                        mboard[moldx[i]>>10][moldy[i]>>10] = 0;
                        mboard[mposx[i]>>10][mposy[i]>>10] = mstat[i];
                        mboard[mgolx[i]>>10][mgoly[i]>>10] = mstat[i];
                        moldx[i] = mposx[i];
                        moldy[i] = mposy[i];
                    }
                    if (mstat[i] == 98)
                        mshock[i] = 480;
                }
                j = rand()&2047;
                if (mstat[i] == 38)
                    j &= 1023;
                if (mstat[i] == 54)
                    j &= 511;
                if ((mstat[i] == 94) || (mstat[i] == 109) || (mstat[i] == 165))
                    j &= 127;
                if ((mstat[i] != 68) && (mstat[i] != 98) && (mstat[i] != 160) && (j < clockspd) && (bulnum < 64) && (mshock[i] == 0))
                {
                    bulx[bulnum] = mposx[i];
                    buly[bulnum] = mposy[i];
                    k = 512;
                    if (mposx[i] != posx)
                    {
                        tanz = (((((long)mposy[i]-(long)posy)<<12)/((long)mposx[i]-(long)posx))<<4);
                        if (tanz < 0)
                            k = 768;
                        else
                            k = 256;
                        for (m=128;m>0;m>>=1)
                        {
                            if (tantable[k] < tanz)
                                k += m;
                            else
                                k -= m;
                        }
                    }
                    if (mposy[i] > posy)
                        k += 1024;
                    bulang[bulnum] = ((k+2016+(rand()&63))&2047);
                    bulstat[bulnum] = totalclock+240;
                    if (mstat[i] == 54)
                        bulstat[bulnum] += 240;
                    if (mstat[i] == 94)
                        bulstat[bulnum] += 480;
                    bulkind[bulnum] = 5;
                    if (mstat[i] == 109)
                        bulkind[bulnum] = 7;
                    bulnum++;
                }
            }
        if (death == 4095)
        {
            mousx=mousy=0;
            bstatus=0;
            if (joystat == 0)
            {
                /*bstatus|=readjoystick(&mousx,&mousy);*/

                if (mousx < joyx2)
                    mousx = (K_INT16)((((K_INT32)(mousx-joyx2))<<6)/((K_INT32)(joyx2-joyx1)));
                else
                    mousx = (K_INT16)((((K_INT32)(mousx-joyx2))<<6)/((K_INT32)(joyx3-joyx2)));
                if (mousy < joyy2)
                    mousy = (K_INT16)((((K_INT32)(mousy-joyy2))<<5)/((K_INT32)(joyy2-joyy1)));
                else
                    mousy = (K_INT16)((((K_INT32)(mousy-joyy2))<<5)/((K_INT32)(joyy3-joyy2)));
            }
            if (moustat == 0)
                bstatus|=readmouse(&mousx,&mousy);

                if (!getkeydefstat(ACTION_STRAFE) && getkeydefstat(ACTION_LEFT) && (angvel > -40))
                    angvel -= 14;
                if (!getkeydefstat(ACTION_STRAFE) && getkeydefstat(ACTION_RIGHT) && (angvel < 40))
                    angvel += 14;
                if ((getkeydefstat(ACTION_STRAFE) && getkeydefstat(ACTION_LEFT)) || getkeydefstat(ACTION_MOVELEFT))
                {
                    svel += 40;
                    if (svel > maxvel)
                        svel = maxvel;
                }
                if ((getkeydefstat(ACTION_STRAFE) && getkeydefstat(ACTION_RIGHT)) || getkeydefstat(ACTION_MOVERIGHT))
                {
                    svel -= 40;
                    if (svel < -maxvel)
                        svel = -maxvel;
                }
        }
        if (angvel < 0)
        {
            angvel += 8;
            if (angvel > 0)
                angvel = 0;
        }
        if (angvel > 0)
        {
            angvel -= 8;
            if (angvel < 0)
                angvel = 0;
        }
        if ((moustat == 0)||(joystat==0))
        {
            if (mousx != 0)
            {
                mousx += (mousx>>1);
                if (mousx < -24)
                    mousx = -24;
                if (mousx > 24)
                    mousx = 24;
                mxvel += mousx;
                if (mxvel+angvel < -40)
                    mxvel = -40-angvel;
                if (mxvel+angvel > 40)
                    mxvel = 40-angvel;
            }
            if (mxvel < 0)
            {
                mxvel += 8;
                if (mxvel > 0)
                    mxvel = 0;
            }
            if (mxvel > 0)
            {
                mxvel -= 8;
                if (mxvel < 0)
                    mxvel = 0;
            }
        }
        if ((rogermode > 0) && (death == 4095) && (getkeydefstat(ACTION_LEFT) == 0) && (getkeydefstat(ACTION_RIGHT) == 0))
            angvel = 0;
        ang = (ang+2048+(((angvel+mxvel)*clockspd)>>3))&2047;
        if (compass > 0)
            oldshowcompass(ang);
        if ((getkeydefstat(ACTION_STANDHIGH) > 0) && (hvel > -5) && (death == 4095))
            hvel-=2;
        if ((getkeydefstat(ACTION_STANDLOW) > 0) && (hvel < 5) && (death == 4095))
            hvel+=2;
        if (hvel < 0)
            hvel++;
        if (hvel > 0)
            hvel--;

        thvel=hvel;
        if (joystat==0) {
            //thvel += (getaxispos(3,1024))>>13;
        }
        i=((thvel*clockspd)/8);
        if ((thvel<0)&&(i==0)) i=-1;
        if ((thvel>0)&&(i==0)) i=1;

        posz += ((thvel*clockspd)>>3);
        if (posz < 8)
        {
            posz = 8;
            hvel = 0;
        }
        if (posz > 56)
        {
            posz = 56;
            hvel = 0;
        }
        if (((getkeydefstat(ACTION_USE) > 0) || ((bstatus&2) > 0)) && (death == 4095))
        {
            x = (posx>>10);
            y = (posy>>10);
            if (!inhibitrepeat) {
                if ((lab3dversion==KENS_LABYRINTH_1_1)&&((board[x][y]&1023) == 56)) {
                    board[x][y] = 1024;
                    coins++;
                    oldstatusbardraw(16,62,6,3,16,10,84);
                    inhibitrepeat=1;
                    if (death == 4095)
                        ksay(7);
                }
            }
            if ((!inhibitrepeat)&(coins>0)) {
                i=0;
                if (((board[x-1][y]&1023) == 179))
                    i = 1;
                if (((board[x+1][y]&1023) == 179))
                    i = 1;
                if (((board[x][y-1]&1023) == 179))
                    i = 1;
                if (((board[x][y+1]&1023) == 179))
                    i = 1;
                if (i == 1) {
                    coins--;
                    if (coins<=0)
                        oldstatusbardraw(16,16,6,3,16,10,93);
                    if (life<1024) {
                        life += 1280;

                        olddrawlife();
                    } else if (purpletime<totalclock+4800) {
                        if (purpletime < totalclock)
                            purpletime = totalclock + 9600;
                        else
                            purpletime += 9600;
                        oldstatusbardraw(0,16,16,15,114,17,85);
                    } else if (greentime<totalclock+4800) {
                        if (greentime < totalclock)
                            greentime = totalclock + 9600;
                        else
                            greentime += 9600;
                        oldstatusbardraw(0,0,16,15,114,2,85);
                    } else if (life<4095) {
                        life += 1280;

                        if (life > 4095)
                            life = 4095;
                        olddrawlife();
                    } else {
                        if (purpletime < totalclock)
                            purpletime = totalclock + 9600;
                        else
                            purpletime += 9600;
                        oldstatusbardraw(0,16,16,15,114,17,85);
                    }
                    ksay(24);
                    inhibitrepeat=1;
                }
            }
            i=0;
            if ((!inhibitrepeat)) {
                if ((keys[0] > 0) || (keys[1] > 0))
                {
                    if (((board[x-1][y] == 21) && (keys[0] > 0)) || ((board[x-1][y] == 31) && (keys[1] > 0)))
                    {
                        board[x-1][y] = 1024;
                        i = 1;
                    }
                    if (((board[x+1][y] == 21) && (keys[0] > 0)) || ((board[x+1][y] == 31) && (keys[1] > 0)))
                    {
                        board[x+1][y] = 1024;
                        i = 1;
                    }
                    if (((board[x][y-1] == 21) && (keys[0] > 0)) || ((board[x][y-1] == 31) && (keys[1] > 0)))
                    {
                        board[x][y-1] = 1024;
                        i = 1;
                    }
                    if (((board[x][y+1] == 21) && (keys[0] > 0)) || ((board[x][y+1] == 31) && (keys[1] > 0)))
                    {
                        board[x][y+1] = 1024;
                        i = 1;
                    }
                    if (i == 1) {
                        ksay(16);
                        inhibitrepeat=1;
                    }
                }
                if (i == 0)
                {
                    if (doorstat == 0)
                    {
                        i = 0;
                        if (((board[x-1][y]&1023) == 152) || ((board[x-1][y]&1023) == 157))
                            if (mboard[x-1][y] == 0)
                            {
                                doorx = x-1;
                                doory = y;
                                doorstat = 152;
                                if ((posx&1023) < 128)
                                    posx = (posx&0xfc00)+128;
                                i = 1;
                            }
                        if (((board[x+1][y]&1023) == 152) || ((board[x+1][y]&1023) == 157))
                            if (mboard[x+1][y] == 0)
                            {
                                doorx = x+1;
                                doory = y;
                                doorstat = 152;
                                if ((posx&1023) > 895)
                                    posx = (posx&0xfc00)+895;
                                i = 1;
                            }
                        if (((board[x][y-1]&1023) == 152) || ((board[x][y-1]&1023) == 157))
                            if (mboard[x][y-1] == 0)
                            {
                                doorx = x;
                                doory = y-1;
                                doorstat = 152;
                                if ((posy&1023) < 128)
                                    posy = (posy&0xfc00)+128;
                                i = 1;
                            }
                        if (((board[x][y+1]&1023) == 152) || ((board[x][y+1]&1023) == 157))
                            if (mboard[x][y+1] == 0)
                            {
                                doorx = x;
                                doory = y+1;
                                doorstat = 152;
                                if ((posy&1023) > 895)
                                    posy = (posy&0xfc00)+895;
                                i = 1;
                            }
                        if (((board[x-1][y]&1023) == 168) || ((board[x-1][y]&1023) == 173))
                            if (mboard[x-1][y] == 0)
                            {
                                doorx = x-1;
                                doory = y;
                                doorstat = 168;
                                if ((posx&1023) < 128)
                                    posx = (posx&0xfc00)+128;
                                i = 1;
                            }
                        if (((board[x+1][y]&1023) == 168) || ((board[x+1][y]&1023) == 173))
                            if (mboard[x+1][y] == 0)
                            {
                                doorx = x+1;
                                doory = y;
                                doorstat = 168;
                                if ((posx&1023) > 895)
                                    posx = (posx&0xfc00)+895;
                                i = 1;
                            }
                        if (((board[x][y-1]&1023) == 168) || ((board[x][y-1]&1023) == 173))
                            if (mboard[x][y-1] == 0)
                            {
                                doorx = x;
                                doory = y-1;
                                doorstat = 168;
                                if ((posy&1023) < 128)
                                    posy = (posy&0xfc00)+128;
                                i = 1;
                            }
                        if (((board[x][y+1]&1023) == 168) || ((board[x][y+1]&1023) == 173))
                            if (mboard[x][y+1] == 0)
                            {
                                doorx = x;
                                doory = y+1;
                                doorstat = 168;
                                if ((posy&1023) > 895)
                                    posy = (posy&0xfc00)+895;
                                i = 1;
                            }
                        if (i == 1)
                        {
                            if ((doorstat == 152) && ((board[doorx][doory]&1023) == 157))
                                doorstat = 157+1024;
                            if ((doorstat == 168) && ((board[doorx][doory]&1023) == 173))
                                doorstat = 173+1024;
                            if (doorstat < 1024)
                                ksay(13);
                            else
                                ksay(4);
                            inhibitrepeat=1;
                        }
                        if ((lastunlock == 0) && (i == 0))
                            ksay(12);
                    }
                }
                lastunlock = 1;
            }
        }
        else {
            lastunlock = 0;
            inhibitrepeat=0;
        }
        if (doorstat > 0)
        {
            if (doorstat < 1024)
            {
                doorstat++;
                if (doorstat == 158)
                    doorstat = 0;
                if (doorstat == 174)
                    doorstat = 0;
            }
            else
            {
                doorstat--;
                if (doorstat == 1024+151)
                    doorstat = 0;
                if (doorstat == 1024+167)
                    doorstat = 0;
            }
            if (doorstat != 0)
            {
                board[doorx][doory] &= 8192;
                board[doorx][doory] |= (doorstat&1023);
                if ((wallheader[doorstat&1023]&8) > 0)
                    board[doorx][doory] |= 1024;
            }
        }
        maxvel = 150;
        if (getkeydefstat(ACTION_RUN) > 0)
            maxvel = 300;
        if (death == 4095)
        {
            if (getkeydefstat(ACTION_FORWARD))
            {
                vel += 40;
                if (vel > maxvel)
                    vel = maxvel;
            }
            if (getkeydefstat(ACTION_BACKWARD))
            {
                vel -= 40;
                if (vel < -maxvel)
                    vel = -maxvel;
            }
        }
        if (vel < 0)
        {
            vel += 25;
            if (vel > 0)
                vel = 0;
        }
        if (vel > 0)
        {
            vel -= 25;
            if (vel < 0)
                vel = 0;
        }
        if (myvel < 0)
        {
            myvel += 25;
            if (myvel > 0)
                myvel = 0;
        }
        if (myvel > 0)
        {
            myvel -= 25;
            if (myvel < 0)
                myvel = 0;
        }
        if (svel < 0)
        {
            svel += 25;
            if (svel > 0)
                svel = 0;
        }
        if (svel > 0)
        {
            svel -= 25;
            if (svel < 0)
                svel = 0;
        }
        if (mousy != 0)
        {
            myvel = -(mousy<<3);
            if (myvel < -300)
                myvel = -300;
            if (myvel > 300)
                myvel = 300;
            if (myvel+vel < -300)
                myvel = -300-vel;
            if (myvel+vel > 300)
                myvel = 300-vel;
        }
        tsvel=svel;
        if (joystat==0) {
            //tsvel -= (getaxispos(2,1024)>>7);
            if (tsvel>256) tsvel=256;
            if (tsvel<-256) tsvel=-256;

        }
        if (((vel+myvel) != 0) || (tsvel != 0))
        {
            oldposx = posx;
            oldposy = posy;
            if ((vel+myvel) != 0)
            {
                posx += (int)(((long)(vel+myvel)*clockspd*sintable[(ang+512)&2047])>>19);
                posy += (int)(((long)(vel+myvel)*clockspd*sintable[ang])>>19);
                i = oldcheckhitwall(oldposx,oldposy,posx,posy,ang);
            }
            if (tsvel != 0)
            {
                posx += (int)(((long)tsvel*clockspd*sintable[ang])>>19);
                posy += (int)(((long)tsvel*clockspd*sintable[(ang+1536)&2047])>>19);
                i = oldcheckhitwall(oldposx,oldposy,posx,posy,(ang+1536)&2047);
            }
            if ((i&1) > 0)
                posx = (oldposx&0xfc00)+128;
            if ((i&2) > 0)
                posx = (oldposx&0xfc00)+895;
            if ((i&4) > 0)
                posy = (oldposy&0xfc00)+128;
            if ((i&8) > 0)
                posy = (oldposy&0xfc00)+895;
            if ((i&16) > 0)
                posx = ((oldposx-128)&0xfc00)+128;
            if ((i&32) > 0)
                posx = ((oldposx+128)&0xfc00)+895;
            if ((i&64) > 0)
                posy = ((oldposy-128)&0xfc00)+128;
            if ((i&128) > 0)
                posy = ((oldposy+128)&0xfc00)+895;
        }
        x = (posx>>10);
        y = (posy>>10);
        i = (board[x][y]&1023);
        if (i != 0)
        {
            incenter = 1;
            j = abs(512-(posx&1023));
            k = abs(512-(posy&1023));
            if ((j >= 360) || (k >= 360))
                incenter = 0;
            if ((j+k) >= 1229)
                incenter = 0;
        }
        if ((i == 32) || (cheatkeysdown && (keystatus[18] > 0)))
        {
            lifevests++;
            if (lifevests > 4)
                lifevests = 4;
            if (i == 32)
            {
                board[x][y] = 1024;
                scorecount += 7500;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            if (lifevests == 1) oldstatusbardraw(19,16,16,16,31,3,85);
            else if (lifevests == 2) oldstatusbardraw(19,16,16,16,43,13,85);
            else if (lifevests == 3) oldstatusbardraw(19,16,16,16,55,3,85);
            else if (lifevests == 4) oldstatusbardraw(19,16,16,16,67,13,85);
        }
        if ((i == 36) || (cheatkeysdown && (keystatus[38] > 0)))
        {
            lightnings++;
            if (lightnings > 6)
                lightnings = 6;
            if (i == 36)
            {
                board[x][y] = 1024;
                scorecount += 5000;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(50,41,13,19,313-((lightnings<<5)-(lightnings<<3)-lightnings),1,85);
        }
        if ((i == 130) || (cheatkeysdown && (keystatus[32] > 0)))
        {
            compass = 1;
            if (i == 130)
            {
                board[x][y] = 1024;
                scorecount += 15000;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
        }
        if (((i == 28) || (i == 34) || (i == 75)) || (cheatkeysdown && ((keystatus[33] > 0) || (keystatus[34] > 0) || (keystatus[35] > 0))))
        {
            if ((i == 75) || (cheatkeysdown && (keystatus[33] > 0)))
            {
                firepowers[0]++;
                if (firepowers[0] > 6)
                    firepowers[0] = 6;
                if (bulchoose == 0)
                    oldstatusbardraw(16,0,14,14,326-((firepowers[0]<<5)-(firepowers[0]<<3)-firepowers[0]),4,85);
            }
            if ((i == 28) || (cheatkeysdown && (keystatus[34] > 0)))
            {
                firepowers[1]++;
                if (firepowers[1] > 6)
                    firepowers[1] = 6;
                if (bulchoose == 1)
                    oldstatusbardraw(32,0,14,14,326-((firepowers[1]<<5)-(firepowers[1]<<3)-firepowers[1]),4,85);
            }
            if ((i == 34) || (cheatkeysdown && (keystatus[35] > 0)))
            {
                firepowers[2]++;
                if (firepowers[2] > 6)
                    firepowers[2] = 6;
                if (bulchoose == 2)
                    oldstatusbardraw(48,0,14,14,326-((firepowers[2]<<5)-(firepowers[2]<<3)-firepowers[2]),4,85);
            }
            if ((i == 28) || (i == 34) || (i == 75))
            {
                board[x][y] = 29+1024;
                scorecount += 2500;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
        }
        if ((i == 37) || (cheatkeysdown && (keystatus[37] > 0)))
        {
            keys[0]++;
            if (i == 37)
            {
                board[x][y] = 84+1024;
                scorecount += 500;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(40,16,14,6,10,4,85);
        }
        if ((i == 107) || (cheatkeysdown && (keystatus[37] > 0)))
        {
            keys[1]++;
            if (i == 107)
            {
                board[x][y] = 84+1024;
                scorecount += 500;
                olddrawscore(scorecount);
            }
            else
                cheated++;
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(40,28,14,6,10,13,85);
        }
        if (i == 123)
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
                    posx = (((unsigned)xwarp[k])<<10)+512;
                    posy = (((unsigned)ywarp[k])<<10)+512;
                    if (death == 4095)
                        ksay(17);
                }
                justwarped = 1;
            }
        }
        else
            justwarped = 0;
        if (i == 40)
        {
            svel = -svel;
            vel = -vel;
            myvel = -myvel;
            if ((vel > -50) && (vel <= 0))
                vel = -50;
            if ((vel < 50) && (vel > 0))
                vel = 50;
            if ((myvel > -50) && (myvel <= 0))
                myvel = -50;
            if ((myvel < 50) && (myvel > 0))
                myvel = 50;
            if ((svel > -50) && (svel <= 0))
                svel = -50;
            if ((svel < 50) && (svel > 0))
                svel = 50;
            vel <<= 1;
            myvel <<= 1;
            svel <<= 1;

            if (vel<-500) vel=-500;
            if (vel>500) vel=500;
            if (svel<-500) svel=-500;
            if (svel>500) svel=500;
            if (myvel<-500) myvel=-500;
            if (myvel>500) myvel=500;

            if (death == 4095)
                ksay(2);
        }
        if (purpletime >= ototclock)
        {
            if (purpletime >= totalclock)
            {
                if (purpletime < totalclock+3840)
                {
                    k = ((3840-(int)(purpletime-totalclock))>>8);
                    if ((k >= 0) && (k <= 15))
                        oldstatusbardraw(30,49+k,16,1,114,17+k,85);
                }
            }
            else
                oldstatusbardraw(16,17,16,15,114,17,93);
        }
        if (greentime >= ototclock)
        {
            if (greentime >= totalclock)
            {
                if (greentime < totalclock+3840)
                {
                    k = ((3840-(int)(greentime-totalclock))>>8);
                    if ((k >= 0) && (k <= 15))
                        oldstatusbardraw(30,49+k,16,1,114,2+k,85);
                }
            }
            else
                oldstatusbardraw(16,2,16,15,114,2,93);
        }
        if (capetime[0] >= ototclock)
        {
            if (capetime[0] >= totalclock)
            {
                if (capetime[0] < totalclock+3072)
                {
                    k = (int)((capetime[0]-totalclock)>>9);
                    if ((k >= 0) && (k <= 5))
                    {
                        if (k == 5) oldstatusbardraw(0,2,21,30,131,2,90);
                        if (k == 4) oldstatusbardraw(21,2,21,30,131,2,90);
                        if (k == 3) oldstatusbardraw(42,2,21,30,131,2,90);
                        if (k == 2) oldstatusbardraw(0,34,21,30,131,2,90);
                        if (k == 1) oldstatusbardraw(21,34,21,30,131,2,90);
                        if (k == 0) oldstatusbardraw(42,34,21,30,131,2,90);
                    }
                }
            }
            else
                oldstatusbardraw(16,2,22,30,130,2,93);
        }
        if (capetime[1] >= ototclock)
        {
            if (capetime[1] >= totalclock)
            {
                if (capetime[1] < totalclock+1536)
                {
                    k = (int)((capetime[1]-totalclock)>>8);
                    if ((k >= 0) && (k <= 5))
                    {
                        if (k == 5) oldstatusbardraw(0,2,21,30,153,2,182);
                        if (k == 4) oldstatusbardraw(21,2,21,30,153,2,182);
                        if (k == 3) oldstatusbardraw(42,2,21,30,153,2,182);
                        if (k == 2) oldstatusbardraw(0,34,21,30,153,2,182);
                        if (k == 1) oldstatusbardraw(21,34,21,30,153,2,182);
                        if (k == 0) oldstatusbardraw(42,34,21,30,153,2,182);
                    }
                }
            }
            else
                oldstatusbardraw(15,2,21,30,153,2,93);
        }
        if ((i == 86) || (cheatkeysdown && (keystatus[39] > 0)))
        {
            if (purpletime < totalclock)
                purpletime = totalclock + 4800;
            else
                purpletime += 4800;
            if (i == 86)
            {
                board[x][y] = 87+1024;
                scorecount += 1000;
                olddrawscore(scorecount);
            }
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(0,16,16,15,114,17,85);
        }
        if ((i == 91) || (cheatkeysdown && (keystatus[40] > 0)))
        {
            if (greentime < totalclock)
                greentime = totalclock + 4800;
            else
                greentime += 4800;
            if (i == 91)
            {
                board[x][y] = 92+1024;
                scorecount += 1500;
                olddrawscore(scorecount);
            }
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(0,0,16,15,114,2,85);
        }
        if ((i == 88) || (cheatkeysdown && (keystatus[36] > 0)))
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 4800;
            else
                capetime[0] += 4800;
            if (i == 88)
            {
                board[x][y] = 89+1024;
                scorecount += 2500;
                olddrawscore(scorecount);
            }
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(0,34,21,30,131,2,85);
        }
        if ((i == 129) || (cheatkeysdown && (keystatus[30] > 0)))
        {
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 2400;
            else
                capetime[1] += 2400;
            if (i == 129)
            {
                board[x][y] = 89+1024;
                scorecount += 5000;
                olddrawscore(scorecount);
            }
            if (death == 4095)
                ksay(7);
            oldstatusbardraw(0,2,21,30,153,2,174);
        }
        if (i == 181)
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 4800;
            else
                capetime[0] += 4800;
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 2400;
            else
                capetime[1] += 2400;
            board[x][y] = 89+1024;
            if (death == 4095)
                ksay(7);
            scorecount += 7500;
            olddrawscore(scorecount);
            oldstatusbardraw(0,2,21,30,153,2,174);
            oldstatusbardraw(0,34,21,30,131,2,85);
        }
        if (i == 191)
        {
            if (capetime[0] < totalclock)
                capetime[0] = totalclock + 4800;
            else
                capetime[0] += 4800;
            if (capetime[1] < totalclock)
                capetime[1] = totalclock + 2400;
            else
                capetime[1] += 2400;
            if (purpletime < totalclock)
                purpletime = totalclock + 4800;
            else
                purpletime += 4800;
            if (greentime < totalclock)
                greentime = totalclock + 4800;
            else
                greentime += 4800;
            board[x][y] = 192+1024;
            if (death == 4095)
                ksay(7);
            scorecount += 10000;
            olddrawscore(scorecount);
            oldstatusbardraw(0,2,21,30,153,2,174);
            oldstatusbardraw(0,34,21,30,131,2,85);
            oldstatusbardraw(0,16,16,15,114,17,85);
            oldstatusbardraw(0,0,16,15,114,2,85);
        }
        if (((i == 46) || (i == 59) || (i == 60)) || (cheatkeysdown && (keystatus[31] > 0)))
        {
            if (cheatkeysdown && (keystatus[31] > 0))
            {
                life += 320;
                cheated++;
            }
            if (i == 59)
            {
                life += 320;
                board[x][y] = 83+1024;
            }
            if (i == 46)
            {
                life += 640;
                board[x][y] = 81+1024;
            }
            if (i == 60)
            {
                life += 1280;
                board[x][y] = 82+1024;
            }
            if (life > 4095)
                life = 4095;
            olddrawlife();
            if (death == 4095)
                ksay(7);
        }
        if ((i == 63) || (cheatkeysdown && (keystatus[48] > 0)))
        {
            keystatus[48]=0;
            musicoff();
            if (boardnum < numboards-1)
            {
                if (i == 63)
                    ksay(11);
                if ((boardnum&1) == 0)
                    loadmusic("CONGRAT1");
                else
                    loadmusic("CONGRAT0");
                musicon();
                if (i == 63)
                {
                    hiscorecheck();
                    boardnum++;
                    //reviewboard();
                }
                else
                {
                    boardnum++;
                    cheated++;
                }
                musicoff();
                loadboard();
                ksmfile[0] = 'L', ksmfile[1] = 'A', ksmfile[2] = 'B';
                ksmfile[3] = 'S', ksmfile[4] = 'N', ksmfile[5] = 'G';
                ksmfile[6] = (boardnum/10)+48, ksmfile[7] = (boardnum%10)+48;
                ksmfile[8] = 0;
                oldstatusbardraw(16,4,25,25,4,4,93);
                j = pageoffset;
                pageoffset = 0;
                textbuf[0] = ((boardnum+1)/10)+48;
                textbuf[1] = ((boardnum+1)%10)+48;
                textbuf[2] = 0;
                if (textbuf[0] == 48)
                    textbuf[0] = 32;
                if (vidmode == 0)
                    oldtextprint(6,22+statusbaryoffset,176);
                else
                {
                    if (statusbar == 415)
                        oldtextprint(26,22+statusbaryoffset,176);
                }
                pageoffset = j;
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
                olddrawscore(scorecount);
                olddrawtime(scoreclock);
                clockspd = 0;
            }
            else
            {
                /* End of shareware version. */
                loadmusic("WINGAME");
                musicon();
                for(n=-2;n>=-4;n--)
                {
                    kgif(n);
                    SetVisibleScreenOffset(0);
                    SDL_GL_SwapWindow(mainwindow);
                    setnewkeystatus(SDLK_ESCAPE, 0);
                    setnewkeystatus(SDLK_SPACE, 0);
                    setnewkeystatus(SDLK_RETURN, 0);
                    bstatus = 0;
                    while ((newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
                    (getkeydefstatlock(ACTION_MENU) == 0) && (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
                    (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
                    (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) && (bstatus == 0)) {
                        PollInputs();
                        if (moustat == 0)
                            bstatus=readmouse(NULL, NULL);
                        SDL_Delay(10);
                    }
                }
                quit();
                /*setnewkeystatus(action_key[ACTION_MENU], 1);
                death = 4094;
                ototclock = 1;*/
            }
        }
        if (death == 4095)
        {
            if ((i == 49) && (capetime[0] < totalclock) && (capetime[1] < totalclock))
            {
                life -= (clockspd<<3);
                if (life <= 0)
                {
                    life = 0;
                    olddrawlife();
                    death = 4094;
                    angvel = (rand()&32)-16;
                    ksay(5);
                    musicoff();
                }
                else
                {
                    olddrawlife();
                    angvel = (rand()&32)-16;
                    ksay(9);
                }
            }
            if (((i&1023) == 73) && (incenter == 1))
            {
                life = 0;
                olddrawlife();
                death = 4094;
                angvel = 0;
                ksay(6);
                musicoff();
            }
        }
        if (!cheatkeysdown)
        {
            K_UINT32 switchKeyPressed = -1;

            #ifdef __SWITCH__
            hidScanInput();
            switchKeyPressed = hidKeysDown(CONTROLLER_P1_AUTO);
            #endif

            if (((switchKeyPressed == KEY_PLUS || (getkeydefstat(ACTION_OLD_SAVE) > 0))) && (death == 4095))
            {
                j = pageoffset;
                pageoffset = lastpageoffset;
                picrot(posx,posy,posz,ang);
                spridraw((int)180-64,(int)halfheight-64,(int)128<<2,(int)79);
                SDL_GL_SwapWindow(mainwindow);
                m = 0;
                n = 0;
                ototclock = totalclock;
                while ((m == 0) && (newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
                (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT3) == 0))
                {
                    PollInputs();
                    #ifdef __SWITCH__
                    switchKeyPressed = hidKeysDown(CONTROLLER_P1_AUTO);
                    #endif

                    if (newkeystatus(SDLK_1) || switchKeyPressed == KEY_L) {
                        i=0;
                        m = 1;
                        setnewkeystatus(SDLK_1, 0);
                    } else if (newkeystatus(SDLK_2) || switchKeyPressed == KEY_ZL) {
                        i=1;
                        m = 1;
                        setnewkeystatus(SDLK_2, 0);
                    } else if (newkeystatus(SDLK_3) || switchKeyPressed == KEY_R) {
                        i=2;
                        m = 1;
                        setnewkeystatus(SDLK_3, 0);
                    } else if (newkeystatus(SDLK_4) || switchKeyPressed == KEY_ZR) {
                        i=3;
                        m = 1;
                        setnewkeystatus(SDLK_4, 0);
                    } else if (newkeystatus(SDLK_5)) {
                        i=4;
                        m = 1;
                        setnewkeystatus(SDLK_5, 0);
                    } else if (newkeystatus(SDLK_6)) {
                        i=5;
                        m = 1;
                        setnewkeystatus(SDLK_6, 0);
                    } else if (newkeystatus(SDLK_7)) {
                        i=6;
                        m = 1;
                        setnewkeystatus(SDLK_7, 0);
                    } else if (newkeystatus(SDLK_8)) {
                        i=7;
                        m = 1;
                        setnewkeystatus(SDLK_8, 0);
                    }

                    if (m) {
                        if (hiscorenamstat == 0)
                        {
                            spridraw((int)180 - 64, (int)halfheight - 64, (int)128 << 2, (int)79);
                            drawinputbox();
                            getname();
                        }
                        oldsavegame(i);
                    }
                    SDL_Delay(10);
                }
                pageoffset = j;
                totalclock = ototclock;
                clockspd = 0;
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                picrot(posx,posy,posz,ang);
            }
            if (switchKeyPressed == KEY_MINUS || getkeydefstat(ACTION_OLD_LOAD) > 0)
            {
                j = pageoffset;
                pageoffset = lastpageoffset;
                picrot(posx,posy,posz,ang);
                spridraw((int)180-64,(int)halfheight-64,(int)128<<2,(int)78);
                SDL_GL_SwapWindow(mainwindow);
                pageoffset = j;
                m = 0;
                ototclock = totalclock;

                while ((m == 0) && (newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
                (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT3) == 0))
                {
                    PollInputs();
                    #ifdef __SWITCH__
                    switchKeyPressed = hidKeysDown(CONTROLLER_P1_AUTO);
                    #endif

                    if (newkeystatus(SDLK_1) || switchKeyPressed == KEY_L) {
                        oldloadgame(0);
                        m = 1;
                        setnewkeystatus(SDLK_1, 0);
                    } else if (newkeystatus(SDLK_2) || switchKeyPressed == KEY_ZL) {
                        oldloadgame(1);
                        m = 1;
                        setnewkeystatus(SDLK_2, 0);
                    } else if (newkeystatus(SDLK_3) || switchKeyPressed == KEY_R) {
                        oldloadgame(2);
                        m = 1;
                        setnewkeystatus(SDLK_3, 0);
                    } else if (newkeystatus(SDLK_4) || switchKeyPressed == KEY_ZR) {
                        oldloadgame(3);
                        m = 1;
                        setnewkeystatus(SDLK_4, 0);
                    } else if (newkeystatus(SDLK_5)) {
                        oldloadgame(4);
                        m = 1;
                        setnewkeystatus(SDLK_5, 0);
                    } else if (newkeystatus(SDLK_6)) {
                        oldloadgame(5);
                        m = 1;
                        setnewkeystatus(SDLK_6, 0);
                    } else if (newkeystatus(SDLK_7)) {
                        oldloadgame(6);
                        m = 1;
                        setnewkeystatus(SDLK_7, 0);
                    } else if (newkeystatus(SDLK_8)) {
                        oldloadgame(7);
                        m = 1;
                        setnewkeystatus(SDLK_8, 0);
                    }


                    SDL_Delay(10);
                }
                totalclock = ototclock;
                clockspd = 0;
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                picrot(posx,posy,posz,ang);
            }
            if (getkeydefstat(ACTION_PAUSE) > 0)
            {
                wipeoverlay(0,0,361,statusbaryoffset);
                picrot(posx,posy,posz,ang);
                j = pageoffset;
                pageoffset = lastpageoffset;
                if (vidmode == 0)
                    n = 18;
                else
                    n = 38;
                mixing=1;
                sprintf(textbuf,"GAME PAUSED");
                oldtextprint(200-(strlen(textbuf)<<2),n,0);
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
                    n = 28;
                else
                    n = 48;
                oldtextprint(200-(strlen(textbuf)<<2),n,0);
                pageoffset = j;
                ototclock = totalclock;
                mixing=0;
                SDL_GL_SwapWindow(mainwindow);
                while ((newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
                (getkeydefstatlock(ACTION_MENU) == 0) && (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) && (bstatus == 0)) {
                    PollInputs();
                    if (moustat == 0)
                        bstatus=readmouse(NULL, NULL);
                    SDL_Delay(10);
                }
                totalclock = ototclock;
                clockspd = 0;
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
                picrot(posx,posy,posz,ang);
            }
        }
        if (keystatus[88] > 0)
            screencapture();
        if (ototclock == 0)
        {
            for(i=0;i<16;i++)
            {
                tempbuf[i*3] = (i*63)>>4;
                tempbuf[i*3+1] = (i*63)>>4;
                tempbuf[i*3+2] = (i*63)>>4;
            }
            SetVisibleScreenOffset(0);
            fade(27);
            ingame=1;
            mixing=1;
            settransferpalette();
            wipeoverlay(0,0,361,statusbaryoffset);
            updateoverlaypalette(240,16,tempbuf);
            loadmusic("DRUMSONG");
            musicon();
            oldloadstory();
            mixing=0;
            setnewkeystatus(SDLK_ESCAPE, 0);
            setnewkeystatus(SDLK_SPACE, 0);
            setnewkeystatus(SDLK_RETURN, 0);
            bstatus=0;
            while ((newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
            (getkeydefstatlock(ACTION_MENU) == 0) && (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) && (bstatus == 0))
            {
                PollInputs();
                if (moustat == 0)
                    bstatus=readmouse(NULL, NULL);
                SDL_LockMutex(timermutex);
                totalclock += clockspeed;
                clockspeed = 0;
                SDL_UnlockMutex(timermutex);
                j = 63-(((int)labs((totalclock%120)-60))>>3);
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
            lastunlock = 1;
            lastshoot = 1;
            lastbarchange = 1;
            SDL_LockMutex(timermutex);
            clockspeed = 0;
            SDL_UnlockMutex(timermutex);
            scoreclock = 0;
            scorecount = 0;
            olddrawscore(scorecount);
            olddrawtime(scoreclock);
            musicoff();
            ksmfile[0] = 'L', ksmfile[1] = 'A', ksmfile[2] = 'B';
            ksmfile[3] = 'S', ksmfile[4] = 'N', ksmfile[5] = 'G';
            ksmfile[6] = (boardnum/10)+48, ksmfile[7] = (boardnum%10)+48;
            ksmfile[8] = 0;
            loadmusic(ksmfile);
            musicon();
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
            clockspd = 0;
            totalclock = ototclock;
            ototclock = 1;
            picrot(posx,posy,posz,ang);
        }
        #ifdef __SWITCH__ // No real need to waste a button just for quitting on Switch since the home button does that already
        clearkeydefstat(ACTION_MENU);
        #endif
        if (getkeydefstat(ACTION_MENU) == 1)
        {
            clearkeydefstat(ACTION_MENU_CANCEL);
            if (ototclock > 1)
            {
                j = pageoffset;
                pageoffset = lastpageoffset;
                ingame=1;
                settransferpalette();
                for(i=0;i<16;i++)
                {
                    tempbuf[i*3] = (i*63)>>4;
                    tempbuf[i*3+1] = (i*63)>>4;
                    tempbuf[i*3+2] = (i*63)>>4;
                }
                updateoverlaypalette(240,16,tempbuf);
                wipeoverlay(0,0,361,statusbaryoffset);
                picrot(posx,posy,posz,ang);
                sprintf(textbuf,"Are you sure you want to quit? (Y or N)");
                mixing=1;
                oldtextprint(200-(strlen(textbuf)<<2),28,0);
                mixing=0;
                SDL_GL_SwapWindow(mainwindow);
                pageoffset = j;
                j = 0;
                ototclock = totalclock;
                while (j == 0)
                {
                    PollInputs();

                    if (newkeystatus(SDLK_n) != 0)  j |= 1;
                    if (newkeystatus(SDLK_SPACE) != 0)  j |= 1;
                    if (newkeystatus(SDLK_RETURN) != 0)  j |= 1;
                    if (newkeystatus(SDLK_y) != 0)  j |= 2;
                    if (getkeydefstat(ACTION_MENU_SELECT1) != 0) j |= 2;
                    if (getkeydefstat(ACTION_MENU_SELECT2) != 0) j |= 1;
                    if (getkeydefstat(ACTION_MENU_SELECT3) != 0) j |= 1;
                    if (getkeydefstat(ACTION_MENU_CANCEL) != 0) j |= 1;
                }
                if (j == 1)
                {
                    clearkeydefstat(ACTION_MENU);
                    lastunlock = 1;
                    lastshoot = 1;
                    lastbarchange = 1;
                }
                else
                    quit();
                totalclock = ototclock;
                clockspd = 0;
                picrot(posx,posy,posz,ang);
            }
            else
            {
                clearkeydefstat(ACTION_MENU);
                lastunlock = 1;
                lastshoot = 1;
                lastbarchange = 1;
            }
        }
        if (ototclock <= 0)
            ototclock++;
        else
            ototclock = totalclock;
        totalclock += clockspd;
        scoreclock += clockspd;
        if ((scoreclock%240) < clockspd)
            olddrawtime(scoreclock);
        SDL_GL_SwapWindow(mainwindow);
    }
    musicoff();
}

void olddrawlife(void)
{
    int y=statusbaryoffset;
    int x=0;
    unsigned char col=0;
    int n,blocks=0;
    unsigned char *c;
    if (statusbar == 479)
        return;
    if (life < 0)
        life = 0;
    if (life > 4095)
        life = 4095;
    /* TODO: Write only to horizontal X offsets that are 1 or 2 (mod 4). */
    if ((life&0xfc0) < (oldlife&0xfc0)) {
        col=0;
        x=((life>>6)+13)<<2;
        blocks=(oldlife>>6)-(life>>6);
    }
    if ((life&0xfc0) > (oldlife&0xfc0)) {
        col=93;
        x=((oldlife>>6)+13)<<2;
        blocks=(life>>6)-(oldlife>>6);
    }

    if (blocks!=0) {
        c=(screenbuffer+y*screenbufferwidth+x+1);
        for(n=0;n<blocks;n++) {
            *(c+1)=col;
            *(c+2)=col;
            *(c+1+screenbufferwidth)=col;
            *(c+2+screenbufferwidth)=col;
            c+=4;
        }
        UploadPartialOverlay(x,y,blocks<<2,2);
    }

    oldlife = life;
}

K_INT16 oldloadgame(K_INT16 gamenum)
{
    char filename[20];
    K_INT16 i;
    int fil;
    unsigned char t;

    filename[0] = 'S', filename[1] = 'A', filename[2] = 'V';
    filename[3] = 'G', filename[4] = 'A', filename[5] = 'M';
    filename[6] = 'E', filename[7] = gamenum+48;
    filename[8] = '.', filename[9] = 'D', filename[10] = 'A';
    filename[11] = 'T', filename[12] = 0;
    sprintf(filepathUpper, "%s%s", gameroot, filename);
    if((fil=open(filepathUpper,O_BINARY|O_RDONLY,
                 S_IWRITE|S_IREAD|S_IRGRP|S_IROTH))==-1) {
        filename[0] = 's', filename[1] = 'a', filename[2] = 'v';
        filename[3] = 'g', filename[4] = 'a', filename[5] = 'm';
        filename[6] = 'e', filename[7] = gamenum+48;
        filename[8] = '.', filename[9] = 'd', filename[10] = 'a';
        filename[11] = 't', filename[12] = 0;
        sprintf(filepath, "%s%s", gameroot, filename);
        if((fil=open(filepath,O_BINARY|O_RDONLY,
                     S_IWRITE|S_IREAD|S_IRGRP|S_IROTH))==-1)
            return(-1);
    }
    readLE16(fil,&board[0][0],8192);
    read(fil,&mboard[0][0],4096);
    readLE16(fil,&boardnum,2);
    readLE16(fil,&life,2);
    readLE16(fil,&death,2);
    readLE16(fil,&lifevests,2);
    readLE16(fil,&lightnings,2);
    readLE16(fil,&firepowers[0],6);
    readLE16(fil,&bulchoose,2);
    readLE16(fil,&keys[0],4);
    if (lab3dversion==KENS_LABYRINTH_1_1)
        readLE16(fil,&coins,2);
    readLE16(fil,&compass,2);
    readLE16(fil,&cheated,2);
    readLE16(fil,&heatpos,2);
    readLE16(fil,&fanpos,2);
    readLE16(fil,&warpos,2);
    if (lab3dversion==KENS_LABYRINTH_1_1)
        readLE16(fil,&kenpos2,2);
    readLE16(fil,&kenpos,2);
    readLE16(fil,&ballpos,2);
    readLE16(fil,&rogermode,2);
    readLE16(fil,&statusbar,2);
    readLE16(fil,&statusbargoal,2);
    readLE16(fil,&posx,2);
    readLE16(fil,&posy,2);
    readLE16(fil,&posz,2);
    readLE16(fil,&ang,2);
    readLE16(fil,&startx,2);
    readLE16(fil,&starty,2);
    readLE16(fil,&startang,2);
    readLE16(fil,&angvel,2);
    readLE16(fil,&vel,2);
    readLE16(fil,&mxvel,2);
    readLE16(fil,&myvel,2);
    readLE16(fil,&svel,2);
    readLE16(fil,&hvel,2);
    readLE16(fil,&oldposx,2);
    readLE16(fil,&oldposy,2);
    readLE16(fil,&bulnum,2);
    readLE16(fil,&bulang[0],bulnum<<1);
    readLE16(fil,&bulkind[0],bulnum<<1);
    readLE16(fil,&bulx[0],bulnum<<1);
    readLE16(fil,&buly[0],bulnum<<1);
    readLE32(fil,&bulstat[0],bulnum<<2);
    readLE32(fil,&lastbulshoot,4);
    readLE16(fil,&mnum,2);
    readLE16(fil,&mposx[0],mnum<<1);
    readLE16(fil,&mposy[0],mnum<<1);
    readLE16(fil,&mgolx[0],mnum<<1);
    readLE16(fil,&mgoly[0],mnum<<1);
    readLE16(fil,&moldx[0],mnum<<1);
    readLE16(fil,&moldy[0],mnum<<1);
    for(i=0;i<mnum;i++) {
        read(fil,&t,1);
        mstat[i]=t;
    }
    readLE16(fil,&mshock[0],mnum<<1);
    read(fil,&mshot[0],mnum);
    readLE16(fil,&doorx,2);
    readLE16(fil,&doory,2);
    readLE16(fil,&doorstat,2);
    read(fil,&numwarps,1);
    read(fil,&justwarped,1);
    read(fil,&xwarp[0],numwarps);
    read(fil,&ywarp[0],numwarps);
    readLE32(fil,&totalclock,4);
    readLE32(fil,&scoreclock,4);
    readLE32(fil,&scorecount,4);
    ototclock = totalclock;
    readLE32(fil,&purpletime,4);
    readLE32(fil,&greentime,4);
    readLE32(fil,&capetime[0],8);
    read(fil,&hiscorenam[0],16);
    read(fil,&hiscorenamstat,1);
    musicoff();
    readLE32(fil,&musicstatus,4);
    if (musicstatus != 0)
    {
        filename[0] = 'L', filename[1] = 'A', filename[2] = 'B';
        filename[3] = 'S', filename[4] = 'N', filename[5] = 'G';
        filename[6] = (boardnum/10)+48, filename[7] = (boardnum%10)+48;
        filename[8] = 0;
        loadmusic(filename);
        musicon();
    }
    readLE16(fil,(K_INT16*)(&clockspeed),2);
    readLE32(fil,&count,4);
    readLE32(fil,&countstop,4);
    readLE16(fil,&nownote,2);
    readLE32(fil,&chanage,18<<2);
    read(fil,&chanfreq,18);
    close(fil);
    for(i=0;i<4096;i++)
        walseg[79][i]=*(((K_INT16 *)board)+i)&255;
    if ((vidmode == 0) && (statusbargoal > 400))
    {
        statusbar -= 80;
        statusbargoal -= 80;
    }
    if ((vidmode == 1) && (statusbargoal < 400))
    {
        statusbar += 80;
        statusbargoal += 80;
    }
    if (vidmode == 0)
    {
        scrsize = 18000-2880;
        if (statusbar == 399)
            scrsize += 2880;
    }
    else
    {
        scrsize = 21600-2880;
        if (statusbar == 479)
            scrsize += 2880;
    }
    linecompare(statusbar);
    oldstatusbaralldraw();
    fade(63);
    return 0;
}

K_INT16 oldsavegame(K_INT16 gamenum)
{
    char filename[20];
    int fil;
    unsigned char t;
    K_INT16 i;

    filename[0] = 's', filename[1] = 'a', filename[2] = 'v';
    filename[3] = 'g', filename[4] = 'a', filename[5] = 'm';
    filename[6] = 'e', filename[7] = gamenum+48;
    filename[8] = '.', filename[9] = 'd', filename[10] = 'a';
    filename[11] = 't', filename[12] = 0;
    sprintf(filepath, "%s%s", gameroot, filename);
    unlink(filepath);

    filename[0] = 'S', filename[1] = 'A', filename[2] = 'V';
    filename[3] = 'G', filename[4] = 'A', filename[5] = 'M';
    filename[6] = 'E', filename[7] = gamenum+48;
    filename[8] = '.', filename[9] = 'D', filename[10] = 'A';
    filename[11] = 'T', filename[12] = 0;
    sprintf(filepathUpper, "%s%s", gameroot, filename);

    if((fil=open(filepathUpper,O_BINARY|O_CREAT|O_WRONLY,
                 S_IWRITE|S_IREAD|S_IRGRP|S_IROTH))==-1)
        return(-1);
    writeLE16(fil,&board[0][0],8192);
    write(fil,&mboard[0][0],4096);
    writeLE16(fil,&boardnum,2);
    writeLE16(fil,&life,2);
    writeLE16(fil,&death,2);
    writeLE16(fil,&lifevests,2);
    writeLE16(fil,&lightnings,2);
    writeLE16(fil,&firepowers[0],6);
    writeLE16(fil,&bulchoose,2);
    writeLE16(fil,&keys[0],4);
    if (lab3dversion==KENS_LABYRINTH_1_1)
        writeLE16(fil,&coins,2);
    writeLE16(fil,&compass,2);
    writeLE16(fil,&cheated,2);
    writeLE16(fil,&heatpos,2);
    writeLE16(fil,&fanpos,2);
    writeLE16(fil,&warpos,2);
    if (lab3dversion==KENS_LABYRINTH_1_1)
        writeLE16(fil,&kenpos2,2);
    writeLE16(fil,&kenpos,2);
    writeLE16(fil,&ballpos,2);
    writeLE16(fil,&rogermode,2);
    writeLE16(fil,&statusbar,2);
    writeLE16(fil,&statusbargoal,2);
    writeLE16(fil,&posx,2);
    writeLE16(fil,&posy,2);
    writeLE16(fil,&posz,2);
    writeLE16(fil,&ang,2);
    writeLE16(fil,&startx,2);
    writeLE16(fil,&starty,2);
    writeLE16(fil,&startang,2);
    writeLE16(fil,&angvel,2);
    writeLE16(fil,&vel,2);
    writeLE16(fil,&mxvel,2);
    writeLE16(fil,&myvel,2);
    writeLE16(fil,&svel,2);
    writeLE16(fil,&hvel,2);
    writeLE16(fil,&oldposx,2);
    writeLE16(fil,&oldposy,2);
    writeLE16(fil,&bulnum,2);
    writeLE16(fil,&bulang[0],bulnum<<1);
    writeLE16(fil,&bulkind[0],bulnum<<1);
    writeLE16(fil,&bulx[0],bulnum<<1);
    writeLE16(fil,&buly[0],bulnum<<1);
    writeLE32(fil,&bulstat[0],bulnum<<2);
    writeLE32(fil,&lastbulshoot,4);
    writeLE16(fil,&mnum,2);
    writeLE16(fil,&mposx[0],mnum<<1);
    writeLE16(fil,&mposy[0],mnum<<1);
    writeLE16(fil,&mgolx[0],mnum<<1);
    writeLE16(fil,&mgoly[0],mnum<<1);
    writeLE16(fil,&moldx[0],mnum<<1);
    writeLE16(fil,&moldy[0],mnum<<1);
    for(i=0;i<mnum;i++) {
        t=mstat[i];
        write(fil,&t,1);
    }
    writeLE16(fil,&mshock[0],mnum<<1);
    write(fil,&mshot[0],mnum);
    writeLE16(fil,&doorx,2);
    writeLE16(fil,&doory,2);
    writeLE16(fil,&doorstat,2);
    write(fil,&numwarps,1);
    write(fil,&justwarped,1);
    write(fil,&xwarp[0],numwarps);
    write(fil,&ywarp[0],numwarps);
    writeLE32(fil,&totalclock,4);
    writeLE32(fil,&scoreclock,4);
    writeLE32(fil,&scorecount,4);
    writeLE32(fil,&purpletime,4);
    writeLE32(fil,&greentime,4);
    writeLE32(fil,&capetime[0],8);
    write(fil,&hiscorenam[0],16);
    write(fil,&hiscorenamstat,1);
    writeLE32(fil,(void *)&musicstatus,4);
    writeLE16(fil,(K_INT16 *)(&clockspeed),2);
    writeLE32(fil,&count,4);
    writeLE32(fil,&countstop,4);
    writeLE16(fil,&nownote,2);
    writeLE32(fil,&chanage,18<<2);
    write(fil,&chanfreq,18);
    close(fil);
    ksay(16);
    return 0;
}

K_INT16 oldintroduction(void)
{
    K_UINT16 plc;
    K_INT16 lasti, i, j, m, fil;

    wipeoverlay(0,0,361,statusbaryoffset);
    fade(63);
    for(i=0;i<16;i++)
    {
        tempbuf[i*3] = (i*63)>>4;
        tempbuf[i*3+1] = (i*63)>>4;
        tempbuf[i*3+2] = (i*63)>>4;
    }
    updateoverlaypalette(240,16,tempbuf);
    /*for(m=0;m<=320;m+=64)
    {
        pageoffset = 22320;
        lastpageoffset = 22320;
        for(j=0;j<7;j++)
        {
            if (m==320)
                drawtooverlay(0, 0, 40, 64, m, pageoffset/90, 1, 0);
            else
                drawtooverlay(0, 0, 64, 64, m, pageoffset/90, 1, 0);
            pageoffset += 5760;
            lastpageoffset += 5760;
        }
    }
    */
    pageoffset = 22320;
    lastpageoffset = 22320;
    boardnum = -1;
    visiblescreenyoffset=pageoffset/90;
    oldloadstory();
    dside = 240;
    halfheight = 120;
    scrsize = 21600;
    lasti = 0;
    i = 0;
    loadmusic("INTRO");
    clockspeed = 0;
    ototclock = -1;
    totalclock = 1;
    plc = 0;
    if (vidmode == 0)
        plc = 1805;
    // plcinc = 0;
    musicon();
    if (saidwelcome == 0)
    {
        ksay(18);
        saidwelcome = 1;
    }

    while ((newkeystatus(SDLK_ESCAPE) == 0) &&
            (newkeystatus(SDLK_SPACE) == 0) &&
            (newkeystatus(SDLK_RETURN) == 0) &&
            (getkeydefstatlock(ACTION_MENU) == 0) &&
            (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
            (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) &&
            (bstatus == 0))
    {
        glClearColor(0,0,0,0);
        glClear(GL_COLOR_BUFFER_BIT);
        PollInputs();
        if (moustat == 0)
            bstatus=readmouse(NULL, NULL);
        j = (((int)labs((totalclock%120)-60))>>3);
        SDL_LockMutex(timermutex);
        while(clockspeed<4) {
            SDL_UnlockMutex(timermutex);
            SDL_Delay(10);
            SDL_LockMutex(timermutex);
        }
        totalclock += clockspeed;
        clockspeed = 0;
        SDL_UnlockMutex(timermutex);
        if (totalclock > 11520)
            totalclock -= 11520;
        lasti = i;
        i = (int)(totalclock>>2);
        if ((i >= 0) && (i < 240))
        {
            if (lasti >= 240)
            {
                plc = 0;
                if (vidmode == 0)
                    plc += 1805;
                SetVisibleScreenOffset(plc);
                ShowPartialOverlay(0,visiblescreenyoffset,360,240,0);
                linecompare(511);
                pageoffset = 0;
            }
            spridraw(180-(i>>1),120-(i>>1),i<<2,64);
        }
        if ((i >= 240) && (i < 360))
        {
            spridraw(180-120,120-120,240<<2,64);
            if (i >= 300)
                spridraw(180-32,120-32,64<<2,108);
        }
        if (i >= 360)
        {
            plc=(i-360)/5*90;
            if (vidmode == 0)
                if (plc > 42845)
                    plc = 42845;
            if (vidmode == 1)
                if (plc > 41040)
                    plc = 41040;
            visiblescreenyoffset=(plc/90);
            ShowPartialOverlay(0,visiblescreenyoffset,360,240,0);
            spridraw(180-120,120-120-visiblescreenyoffset,240<<2,64);
            spridraw(180-32,120-32-visiblescreenyoffset,64<<2,108);
        }
        if (getkeydefstat(ACTION_MUTE) > 0)
        {
            pageoffset = plc;
            spridraw((int)180-64,(int)halfheight-64,(int)128<<2,(int)78);
            SDL_GL_SwapWindow(mainwindow);
            m = 0;
            while ((m == 0) && (newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
                (getkeydefstatlock(ACTION_MENU) == 0) &&
                (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
                (getkeydefstatlock(ACTION_MENU_SELECT3) == 0))
            {
                PollInputs();
                i=-1;
                if (newkeystatus(SDLK_1)) {
                    i=0;
                    setnewkeystatus(SDLK_1, 0);
                } else if (newkeystatus(SDLK_2)) {
                    i=1;
                    setnewkeystatus(SDLK_2, 0);
                } else if (newkeystatus(SDLK_3)) {
                    i=2;
                    setnewkeystatus(SDLK_3, 0);
                } else if (newkeystatus(SDLK_4)) {
                    i=3;
                    setnewkeystatus(SDLK_4, 0);
                } else if (newkeystatus(SDLK_5)) {
                    i=4;
                    setnewkeystatus(SDLK_5, 0);
                } else if (newkeystatus(SDLK_6)) {
                    i=5;
                    setnewkeystatus(SDLK_6, 0);
                } else if (newkeystatus(SDLK_7)) {
                    i=6;
                    setnewkeystatus(SDLK_7, 0);
                } else if (newkeystatus(SDLK_8)) {
                    i=7;
                    setnewkeystatus(SDLK_8, 0);
                }
                if (i >= 0)
                {
                    if (oldloadgame(i) != -1)
                    {
                        if (numboards == 0)
                        {
                            sprintf(filepath, "%sboards.dat", gameroot);
                            if ((fil = open(filepath,O_BINARY|O_RDONLY,
                                            S_IREAD)) != -1)
                            {
                                numboards = 27;
                                close(fil);
                            }
                            else
                            {
                                sprintf(filepathUpper, "%sBOARDS.DAT", gameroot);
                                if ((fil = open(filepathUpper,O_BINARY|O_RDONLY,
                                            S_IREAD)) != -1)
                                {
                                    numboards = 27;
                                    close(fil);
                                }
                            }
                        }
                                      
                        setnewkeystatus(SDLK_SPACE, 0);
                        setnewkeystatus(SDLK_RETURN, 0);
                        setnewkeystatus(SDLK_ESCAPE, 0);
                        if (vidmode == 0)
                        {
                            dside = 200;
                            halfheight = 100;
                        }
                        else
                        {
                            dside = 240;
                            halfheight = 120;
                        }
                        if (statusbar == 479)
                        {
                            lastpageoffset = 0;
                            pageoffset = 0;
                        }
                        else
                        {
                            lastpageoffset = 2880;
                            pageoffset = 2880;
                        }
                        sortcnt = 0;
                        lastunlock = 1;
                        lastshoot = 1;
                        lastbarchange = 1;
                        SetVisibleScreenOffset(0);
                        oldstatusbaralldraw();
                        return(0);
                    }
                    setnewkeystatus(SDLK_SPACE, 1);
                    m = 1;
                }
            }
            clockspeed = 0;
            lastunlock = 1;
            lastshoot = 1;
            lastbarchange = 1;
        }
        SDL_GL_SwapWindow(mainwindow);
    }
    musicoff();

    fade(0);

    setnewkeystatus(SDLK_SPACE, 0);
    setnewkeystatus(SDLK_RETURN, 0);
    setnewkeystatus(SDLK_ESCAPE, 0);
    if (vidmode == 0)
    {
        dside = 200;
        halfheight = 100;
        scrsize = 18000-2880;
    }
    else
    {
        dside = 240;
        halfheight = 120;
        scrsize = 21600-2880;
    }
    boardnum = 0;
    pageoffset = 2880;
    loadboard();
    pageoffset = 2880;
    sortcnt = 0;
    oldlife = 0;
    life = 4095;
    death = 4095;
    lifevests = 0;
    lightnings = 0;
    firepowers[0] = 0;
    firepowers[1] = 0;
    firepowers[2] = 0;
    bulchoose = 0;
    heatpos = 143;
    fanpos = 49;
    warpos = 123;
    kenpos = 66;
    kenpos2 = 193;
    ballpos = 68;
    rogermode = 0;
    ototclock = -1;
    totalclock = 1;
    purpletime = 0;
    greentime = 0;
    capetime[0] = 0;
    capetime[1] = 0;
    compass = 0;
    coins = 0;
    cheated = 0;
    doorstat = 0;
    statusbar = 335;
    if (vidmode == 1)
        statusbar += 80;
    statusbargoal = statusbar;
    linecompare(statusbar);
    lastpageoffset = 2880;
    pageoffset = 2880;
    namrememberstat = hiscorenamstat;
    hiscorenamstat = 0;
    clockspeed = 0;
    scoreclock = 0;
    scorecount = 0;
    SetVisibleScreenOffset(0);
    oldstatusbaralldraw();
    return 0;
}

K_INT16 oldcheckhitwall(K_UINT16 oposx,K_UINT16 oposy,K_UINT16 posix,
                        K_UINT16 posiy,K_INT16 angl)
{
    K_UINT16 plcx, plcy, newx, newy;
    K_INT16 i, j;

    newx = posix;
    newy = posiy;
    i = 0;
    if (((newx-128)&0xfc00) < ((oposx-128)&0xfc00))
    {
        plcx = ((oposx-128)&0xfc00)+128;
        plcy = oposy+(int)((((long)oposx-(long)plcx)*tantable[angl&1023])>>16);
        j = board[(plcx>>10)-1][(plcy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posix = ((oposx-128)&0xfc00) + 128, i |= 16;
        j = board[(plcx>>10)-1][(plcy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posix = ((oposx-128)&0xfc00) + 128, i |= 16;
        j = board[(posix-128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 4;
        j = board[(posix+128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 4;
        j = board[(posix-128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 8;
        j = board[(posix+128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 8;
    }
    if (((newx+128)&0xfc00) > ((oposx+128)&0xfc00))
    {
        plcx = ((oposx+128)&0xfc00)+895;
        plcy = oposy+(int)((((long)oposx-(long)plcx)*tantable[angl&1023])>>16);
        j = board[(plcx>>10)+1][(plcy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posix = ((oposx+128)&0xfc00) + 895, i |= 32;
        j = board[(plcx>>10)+1][(plcy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posix = ((oposx+128)&0xfc00) + 895, i |= 32;
        j = board[(posix-128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 4;
        j = board[(posix+128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 4;
        j = board[(posix-128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 8;
        j = board[(posix+128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 8;
    }
    if (((newy-128)&0xfc00) < ((oposy-128)&0xfc00))
    {
        plcy = ((oposy-128)&0xfc00)+128;
        plcx = oposx+(int)((((long)oposy-(long)plcy)*tantable[(2560-angl)&1023])>>16);
        j = board[(plcx-128)>>10][(plcy>>10)-1];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posiy = ((oposy-128)&0xfc00) + 128, i |= 64;
        j = board[(plcx+128)>>10][(plcy>>10)-1];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posiy = ((oposy-128)&0xfc00) + 128, i |= 64;
        j = board[(posix-128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 1;
        j = board[(posix-128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 1;
        j = board[(posix+128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 2;
        j = board[(posix+128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 2;
    }
    if (((newy+128)&0xfc00) > ((oposy+128)&0xfc00))
    {
        plcy = ((oposy+128)&0xfc00)+895;
        plcx = oposx+(int)((((long)oposy-(long)plcy)*tantable[(2560-angl)&1023])>>16);
        j = board[(plcx-128)>>10][(plcy>>10)+1];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posiy = ((oposy-128)&0xfc00) + 895, i |= 128;
        j = board[(plcx+128)>>10][(plcy>>10)+1];
        if (((j&3072) != 1024) && ((j&1023) != 0))
            posiy = ((oposy-128)&0xfc00) + 895, i |= 128;
        j = board[(posix-128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 1;
        j = board[(posix-128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 1;
        j = board[(posix+128)>>10][(posiy-128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 2;
        j = board[(posix+128)>>10][(posiy+128)>>10];
        if (((j&3072) != 1024) && ((j&1023) != 0)) i |= 2;
    }
    return(i);
}

/* Put in (*x,*y) the position for the explosion in end of game that is
   s pixels big from the monster at (mx,my) as seen by the player in (px,py)
   at angle a. Return 1 on success, 0 on failure (out of range). */
int getendexplosionpos(K_UINT16 *x, K_UINT16 *y, K_UINT16 mx,
                       K_UINT16 my, K_UINT16 px, K_UINT16 py,
                       K_INT32 s, K_INT16 a) {
    double z, tx, ty;
    K_INT32 n=
        ((((K_INT32)((K_INT32)mx-(K_INT32)px)>>2)*sintable[(a+512)&2047]+
          ((K_INT32)((K_INT32)my-(K_INT32)py)>>2)*sintable[a]));
    if (n>0) {
        z=((double)2684354560.0)/((double)n)/((double)s);
        tx=px+z*(mx-px);
        ty=py+z*(my-py);
        if ((tx>0.1)&&(ty>0.1)&&(tx<65534.9)&&(ty<65534.9)) {
            *x=tx;
            *y=ty;
            return 1;
        } else return 0;
    } else return 0;
}

void oldwingame(K_UINT16 mxpos, K_UINT16 mypos)
{
    K_INT16 j, brightness;
    K_INT32 revtotalclock, revototclock;

    K_UINT16 x, y;

    revototclock = -1;
    revtotalclock = 0;
    brightness = 63;
    musicoff();
    loadmusic("WINGAME");
    musicon();
    ksay(1);
    while ((newkeystatus(SDLK_ESCAPE) == 0) && (newkeystatus(SDLK_SPACE) == 0) && (newkeystatus(SDLK_RETURN) == 0) &&
        (getkeydefstatlock(ACTION_MENU) == 0) && (getkeydefstatlock(ACTION_MENU_CANCEL) == 0) &&
        (getkeydefstatlock(ACTION_MENU_SELECT1) == 0) && (getkeydefstatlock(ACTION_MENU_SELECT2) == 0) &&
        (getkeydefstatlock(ACTION_MENU_SELECT3) == 0) && (bstatus == 0) && (brightness > 2))
    {
        PollInputs();
        if (moustat == 0)
            bstatus=readmouse(NULL, NULL);

        sortcnt = 0;
        if ((revtotalclock < 480) && (revtotalclock>0))
        {
            if (((posx|1023) != (mxpos|1023)) || ((posy|1023) != (mypos|1023)))
                if (getendexplosionpos(&x, &y, mxpos, mypos, posx, posy,
                                       revtotalclock>>1, ang))
                    checkobj(x,y,posx,posy,ang,44);
        }
        picrot(posx,posy,posz,ang);
        if (revtotalclock >= 480)
        {
            j = rand()&127;
            if (vidmode == 0)
                spridraw((rand()%(318-j))+21,(rand()%(198-j)+21),j<<2,106);
            if (vidmode == 1)
                spridraw((rand()%(358-j))+1,(rand()%(238-j)+1),j<<2,106);
        }
        if (revtotalclock < 1440)
            if ((revototclock|15) != (revtotalclock|15))
                if ((rand()&7) == 0)
                    ksay(1);
        if ((revototclock|127) != (revtotalclock|127))
        {
            brightness--;
            fade(brightness);
        }
        revototclock = revtotalclock;
        SDL_LockMutex(timermutex);
        revtotalclock += clockspeed;
        clockspeed = 0;
        SDL_UnlockMutex(timermutex);
        SDL_GL_SwapWindow(mainwindow);
    }
    musicoff();
    ksay(23);
    posz = 32;
    lastunlock = 1;
    lastshoot = 1;
    lastbarchange = 1;
    SDL_Delay(2000);
}

void oldshowcompass(K_INT16 compang)
{
    K_INT16 i;

    i = (((compang+64)&2047)>>7);
    oldstatusbardraw((i&2)<<4,((i&1)<<5)+2,29,29,84,2,(i>>2)+117);
}

K_INT16 oldloadstory()
{
    unsigned char xordat;
    K_UINT16 storyoffs[128];
    int fil, i, k, textbufcnt, textypos;

    ototclock = totalclock;

    sprintf(filepath, "%sstory.kzp", gameroot);
    sprintf(filepathUpper, "%sSTORY.KZP", gameroot);
    if ((fil = open(filepath,O_BINARY|O_RDONLY,S_IREAD)) == -1)
        if ((fil = open(filepathUpper,O_BINARY|O_RDONLY,S_IREAD)) == -1)
            return(-1);
    read(fil,&storyoffs[0],256);
    lseek(fil,(long)(storyoffs[boardnum+1]),SEEK_SET);
    read(fil,&tempbuf[0],4096);
    k = pageoffset;
    pageoffset = lastpageoffset;
    i = 0;
    xordat = 0;
    if (boardnum<0)
        textypos = 4;
    else
        textypos = 24;
    textbufcnt = 0;
    while ((tempbuf[i] != 0) && (textbufcnt < 40))
    {
        xordat ^= tempbuf[i];
        if (tempbuf[i] >= 32)
            textbuf[textbufcnt++] = tempbuf[i];
        if (tempbuf[i] == 13)
        {
            textbuf[textbufcnt] = 0;
            textprint(180-(textbufcnt<<2),textypos,0);
            textypos += 12;
            textbufcnt = 0;
        }
        i++;
        tempbuf[i] ^= xordat;
    }
    textbuf[textbufcnt] = 0;
    textprint(180-(textbufcnt<<2),textypos,0);

    close(fil);
    pageoffset = k;

    return 0;
}

void oldstatusbaralldraw(void)
{
    K_INT16 i;

    for(i=0;i<361;i++)
        screenbuffer[i+(statusbaryoffset-1)*screenbufferwidth]=0xff;
    UploadPartialOverlay(0,(statusbaryoffset-1),361,1);
    for(i=0;i<361;i++)
        screenbuffer[i+(statusbaryoffset+32)*screenbufferwidth]=0x50;
    UploadPartialOverlay(0,(statusbaryoffset+32),361,1);
    if (vidmode == 1)
    {
        /* TODO: Wipe bottom 32 lines of display to 0. */
    }
    oldstatusbardraw(0,0,32,32,0,0,93);
    oldstatusbardraw(32,0,32,32,288,0,93);
    for(i=32;i<288;i+=32)
        oldstatusbardraw(16,0,32,32,i,0,93);
    oldlife = 0;
    olddrawlife();
    oldlife = 4095;
    olddrawlife();
    i = pageoffset;
    pageoffset = 0;
    olddrawscore(scorecount);
    olddrawtime(scoreclock);
    textbuf[0] = ((boardnum+1)/10)+48;
    textbuf[1] = ((boardnum+1)%10)+48;
    textbuf[2] = 0;
    if (textbuf[0] == 48)
        textbuf[0] = 32;
    if (vidmode == 0)
        oldtextprint(6,22+statusbaryoffset,176);
    else
    {
        oldtextprint(26,22+statusbaryoffset,176);
        oldstatusbardraw(0,32,20,32,-20,0,93);
        oldstatusbardraw(44,32,20,32,320,0,93);
    }
    pageoffset = i;
    if (lifevests >= 1) oldstatusbardraw(19,16,16,16,31,3,85);
    if (lifevests >= 2) oldstatusbardraw(19,16,16,16,43,13,85);
    if (lifevests >= 3) oldstatusbardraw(19,16,16,16,55,3,85);
    if (lifevests >= 4) oldstatusbardraw(19,16,16,16,67,13,85);
    for(i=1;i<=lightnings;i++)
        oldstatusbardraw(50,41,13,19,313-((i<<5)-(i<<3)-i),1,85);
    if (bulchoose == 0)
        for(i=1;i<=firepowers[0];i++)
            oldstatusbardraw(16,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
    if (bulchoose == 1)
        for(i=1;i<=firepowers[1];i++)
            oldstatusbardraw(32,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
    if (bulchoose == 2)
        for(i=1;i<=firepowers[2];i++)
            oldstatusbardraw(48,0,14,14,326-((i<<5)-(i<<3)-i),4,85);
    if (purpletime >= totalclock)
    {
        oldstatusbardraw(0,16,16,15,114,17,85);
        if (purpletime < totalclock+3840)
        {
            i = ((3840-(int)(purpletime-totalclock))>>8);
            if ((i >= 0) && (i <= 15))
                oldstatusbardraw(30,49,16,i,114,17,85);
        }
    }
    if (greentime >= totalclock)
    {
        oldstatusbardraw(0,0,16,15,114,2,85);
        if (greentime < totalclock+3840)
        {
            i = ((3840-(int)(greentime-totalclock))>>8);
            if ((i >= 0) && (i <= 15))
                oldstatusbardraw(30,49,16,i,114,2,85);
        }
    }
    if (capetime[0] >= totalclock)
    {
        oldstatusbardraw(0,34,21,30,131,2,85);
        if (capetime[0] < totalclock+3072)
        {
            i = (int)((capetime[0]-totalclock)>>9);
            if ((i >= 0) && (i <= 5))
            {
                if (i == 5) oldstatusbardraw(0,2,21,30,131,2,90);
                if (i == 4) oldstatusbardraw(21,2,21,30,131,2,90);
                if (i == 3) oldstatusbardraw(42,2,21,30,131,2,90);
                if (i == 2) oldstatusbardraw(0,34,21,30,131,2,90);
                if (i == 1) oldstatusbardraw(21,34,21,30,131,2,90);
                if (i == 0) oldstatusbardraw(42,34,21,30,131,2,90);
            }
        }
    }
    if (capetime[1] >= ototclock)
    {
        oldstatusbardraw(0,2,21,30,153,2,174);
        if (capetime[1] < totalclock+1536)
        {
            i = (int)((capetime[1]-totalclock)>>8);
            if ((i >= 0) && (i <= 5))
            {
                if (i == 5) oldstatusbardraw(0,2,21,30,153,2,182);
                if (i == 4) oldstatusbardraw(21,2,21,30,153,2,182);
                if (i == 3) oldstatusbardraw(42,2,21,30,153,2,182);
                if (i == 2) oldstatusbardraw(0,34,21,30,153,2,182);
                if (i == 1) oldstatusbardraw(21,34,21,30,153,2,182);
                if (i == 0) oldstatusbardraw(42,34,21,30,153,2,182);
            }
        }
    }
    if (keys[0] > 0)
        oldstatusbardraw(40,16,14,6,10,4,85);
    if (keys[1] > 0)
        oldstatusbardraw(40,28,14,6,10,13,85);
    if (coins > 0)
        oldstatusbardraw(16,62,6,3,16,10,84);
}

void olddrawscore(K_INT32 thescore)
{
    K_INT16 i, j, n;

    if ((vidmode == 0) || (statusbar == 415))
    {
        j = pageoffset;
        pageoffset = 0;
        if (vidmode == 0)
            n = 251;
        else
            n = 271;
        for(i=0;i<8;i++)
            textbuf[i] = 9;
        textbuf[8] = 0;
        oldtextprint(n,20+statusbaryoffset,0);
        textbuf[0] = (char)((thescore/10000000L)%10L)+48;
        textbuf[1] = (char)((thescore/1000000L)%10L)+48;
        textbuf[2] = (char)((thescore/100000L)%10L)+48;
        textbuf[3] = (char)((thescore/10000L)%10L)+48;
        textbuf[4] = (char)((thescore/1000L)%10L)+48;
        textbuf[5] = (char)((thescore/100L)%10L)+48;
        textbuf[6] = (char)((thescore/10L)%10L)+48;
        textbuf[7] = (char)(thescore%10L)+48;
        textbuf[8] = 0;
        i = 0;
        while ((textbuf[i] == 48) && (i < 7))
            textbuf[i++] = 32;
        oldtextprint(n,20+statusbaryoffset,192);
        pageoffset = j;
    }
}

void olddrawtime(K_INT32 thetime)
{
    K_INT16 i, j, n;

    if ((vidmode == 0) || (statusbar == 415))
    {
        j = pageoffset;
        pageoffset = 0;
        if (vidmode == 0)
            n = 179;
        else
            n = 199;
        for(i=0;i<8;i++)
            textbuf[i] = 9;
        textbuf[8] = 0;
        oldtextprint(n,20+statusbaryoffset,0);
        thetime = thetime/240;
        textbuf[0] = (char)((thetime/10000000L)%10L)+48;
        textbuf[1] = (char)((thetime/1000000L)%10L)+48;
        textbuf[2] = (char)((thetime/100000L)%10L)+48;
        textbuf[3] = (char)((thetime/10000L)%10L)+48;
        textbuf[4] = (char)((thetime/1000L)%10L)+48;
        textbuf[5] = (char)((thetime/100L)%10L)+48;
        textbuf[6] = (char)((thetime/10L)%10L)+48;
        textbuf[7] = (char)(thetime%10L)+48;
        textbuf[8] = 0;
        i = 0;
        while ((textbuf[i] == 48) && (i < 7))
            textbuf[i++] = 32;
        oldtextprint(n,20+statusbaryoffset,192);
        pageoffset = j;
    }
}

void oldstatusbardraw(K_UINT16 picx, K_UINT16 picy, K_UINT16 xsiz,
                      K_UINT16 ysiz, K_UINT16 x, K_UINT16 y, K_INT16 walnume) {
    statusbardraw(picx, picy, xsiz, ysiz, x, y+statusbaryoffset, walnume);
}

void oldtextprint(K_INT16 x,K_INT16 y,char coloffs) {
    textprint(x-20, y, coloffs);
}
