#include "modmusic.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* tools/modmusic/generate.py normalises every sample to full scale and puts
 * the instrument's real level in the sample volume, with volume 64 standing
 * for an OPL peak of PEAK_REFERENCE (5911) at the default music volume.
 * 127*64*64/5911 = 88 makes MOD playback as loud as Adlib mode. */
#define MODMUSIC_GAIN_DIVISOR 88

typedef struct {
    const int8_t *data;
    unsigned length, volume, loopstart, loopend;
} Sample;
typedef struct { Sample *sample; uint32_t position, step; unsigned volume; } Voice;
static unsigned char *module;
static Sample samples[31];
static Voice voices[12];
static unsigned orders, order, row, speed, tempo, remaining, fraction;
static unsigned nextorder, nextrow;
static unsigned voicecount, patternbytes;
static int outputrate;
static unsigned be16(const unsigned char *p) { return p[0]*256u+p[1]; }

void modmusic_free(void) {
    free(module); module=NULL;
    memset(samples,0,sizeof(samples));
    memset(voices,0,sizeof(voices));
    remaining=0;
}
void modmusic_start(void) {
    memset(voices,0,sizeof(voices));
    order=row=remaining=fraction=0; speed=6; tempo=125; outputrate=0;
}
int modmusic_load(const char *path) {
    FILE *f;
    long size;
    unsigned patterns=0, i, total;
    modmusic_free();
    f=fopen(path,"rb");
    if(!f) return -1;
    if(fseek(f,0,SEEK_END) || (size=ftell(f))<1084 || size>4*1024*1024 || fseek(f,0,SEEK_SET)) goto bad;
    module=malloc((size_t)size);
    if(!module || fread(module,1,(size_t)size,f)!=(size_t)size) goto bad;
    if(!memcmp(module+1080,"12CH",4)) { voicecount=12; patternbytes=3072; }
    else if(!memcmp(module+1080,"8CHN",4)) { voicecount=8; patternbytes=2048; }
    else if(!memcmp(module+1080,"M.K.",4)) { voicecount=4; patternbytes=1024; }
    else goto bad;
    orders=module[950];
    if(!orders || orders>128) goto bad;
    for(i=0;i<128;i++) {
        if(module[952+i]>127) goto bad;
        if(module[952+i]>=patterns) patterns=module[952+i]+1;
    }
    total=1084+patterns*patternbytes;
    if(total>(unsigned)size) goto bad;
    for(i=0;i<31;i++) {
        unsigned char *h=module+20+i*30;
        samples[i].length=be16(h+22)*2;
        samples[i].volume=h[25];
        samples[i].loopstart=be16(h+26)*2;
        samples[i].loopend=samples[i].loopstart+be16(h+28)*2;
        if(h[24] || h[25]>64 ||
           (samples[i].length && samples[i].loopend>samples[i].length) ||
           (!samples[i].length && samples[i].loopend>2) ||
           samples[i].loopstart>samples[i].length) goto bad;
        if(samples[i].length>(unsigned)size-total) goto bad;
        samples[i].data=(const int8_t *)(module+total);
        total+=samples[i].length;
    }
    for(i=1084;i<1084+patterns*patternbytes;i+=4) {
        unsigned char *e=module+i;
        unsigned s=(e[0]&240)|(e[2]>>4), p=((e[0]&15)<<8)|e[1], fx=e[2]&15;
        if(s>31 || (p && (p<113 || p>856))) goto bad;
        if(fx && fx!=9 && fx!=11 && fx!=12 && fx!=13 && fx!=15) goto bad;
        if((!fx && e[3]) || (fx==12 && e[3]>64) ||
           (fx==11 && e[3]>=orders) || (fx==15 && !e[3]) ||
           (fx==13 && ((e[3]>>4)*10+(e[3]&15)>63 || (e[3]&15)>9))) goto bad;
    }
    fclose(f); modmusic_start(); return 0;
bad:
    fclose(f); modmusic_free(); return -1;
}
static void beginrow(int rate) {
    unsigned c;
    const unsigned char *p=module+1084+module[952+order]*patternbytes+row*voicecount*4;
    nextrow=row+1; nextorder=order;
    if(nextrow==64) { nextrow=0; nextorder=(order+1)%orders; }
    for(c=0;c<voicecount;c++,p+=4) {
        unsigned s=(p[0]&240)|(p[2]>>4), period=((p[0]&15)<<8)|p[1];
        unsigned fx=p[2]&15, arg=p[3];
        Voice *v=&voices[c];
        if(s) { v->sample=&samples[s-1]; v->volume=v->sample->volume; }
        if(period) {
            /* 9xx starts the note xx*256 bytes into the sample (used to
               resume a slow attack from the level the voice was left at) */
            v->position=fx==9 ? (uint32_t)arg<<23 : 0;
            v->step=(uint32_t)(((uint64_t)3546895*32768)/(period*(unsigned)rate));
        }
        if(fx==12) v->volume=arg;
        if(fx==15) { if(arg<32) speed=arg; else tempo=arg; }
        if(fx==11) { nextorder=arg; nextrow=0; }
        if(fx==13) { nextorder=(order+1)%orders; nextrow=(arg>>4)*10+(arg&15); }
    }
    /* Q16 fractional frames prevents cumulative row timing drift. */
    {
        uint32_t duration=(uint32_t)(((uint64_t)rate*5*speed*65536)/(2*tempo));
        fraction+=(duration&65535);
        remaining=(duration>>16)+(fraction>>16);
        fraction&=65535;
    }
    order=nextorder; row=nextrow;
}
/*
 * Mixing.
 *
 * Twelve voices at 22kHz is a lot of work for a 68020, and the first version
 * of this player - one pass over the voices per output frame, with a loop
 * test, an interpolation multiply and a divide each - cost more CPU than the
 * whole of the 3D renderer.  So the voices are now mixed one at a time into
 * an integer accumulator, each in runs that are known in advance not to reach
 * the end of the sample or its loop, which leaves the inner loop with a load,
 * a multiply, an add and the position step.  The output stage then scales
 * the accumulator by the music volume with one multiply and clamps.
 *
 * Linear interpolation between samples is kept on the CPUs that can afford
 * it.  On a 68020 or 68030 the extra multiply per voice per frame is a third
 * of the mixing cost, so those builds pick the nearest sample instead, as
 * ProTracker on Paula always did.
 */
#ifndef MODMUSIC_INTERPOLATE
#if defined(__mc68020__) || defined(__mc68030__)
#define MODMUSIC_INTERPOLATE 0
#else
#define MODMUSIC_INTERPOLATE 1
#endif
#endif

#define MIXRUN 256          /* frames per accumulator fill */

/* sample * gain for every sample value and every gain 0..64: a table lookup
   is a good deal cheaper than the multiply on a 68020, and the same 33K
   serves every voice. */
static int16_t voltab[65][256];
static int     voltab_ready;

static void build_voltab(void) {
    int g, s;
    for(g=0;g<=64;g++)
        for(s=-128;s<128;s++)
            voltab[g][s+128]=(int16_t)(s*g);
    voltab_ready=1;
}

/* Whether a sample loops, in the sense the player has always used. */
#define LOOPS(s) ((s)->loopend>(s)->loopstart+2)

/* Frames a voice can advance from pos before reaching limit (17.15, pos <
   limit), at most n. */
static unsigned run_until(uint32_t pos, uint32_t step, uint32_t limit, unsigned n) {
    uint32_t k=(limit-1-pos)/step+1;
    return k<n ? (unsigned)k : n;
}

/* Bring a voice's position back inside its loop, or retire it if the sample
   is over.  Returns 0 if the voice has nothing more to play. */
static int voice_place(Voice *v) {
    const Sample *s=v->sample;
    unsigned pos=v->position>>15;
    if(LOOPS(s) && pos>=s->loopend) {
        unsigned span=s->loopend-s->loopstart;
        pos=s->loopstart+(pos-s->loopstart)%span;
        v->position=(pos<<15)|(v->position&32767);
    }
    if(pos>=s->length) { v->sample=NULL; return 0; }
    return 1;
}

/* Mix n frames of one voice into acc, at gain g (1..64) per sample unit. */
static void voice_mix(Voice *v, int32_t *acc, unsigned n, unsigned g) {
    /* Indexed straight by the signed sample byte. */
    const int16_t *tab=voltab[g]+128;

    while(n>0) {
        const Sample *s;
        const int8_t *data;
        uint32_t pos, step, limit;
        unsigned k;

        if(!voice_place(v)) return;
        s=v->sample; data=s->data; pos=v->position; step=v->step;
        limit=(uint32_t)(LOOPS(s) ? s->loopend : s->length)<<15;

#if MODMUSIC_INTERPOLATE
        /* Up to the last sample before the limit the neighbour is plain
           data[i+1]; that last one wants the loop start (or itself) instead
           and is done on its own below.  The 16 bit multiply is enough: the
           difference is nine bits and the fraction fifteen. */
        if(pos<limit-(1u<<15)) {
            k=run_until(pos,step,limit-(1u<<15),n);
            n-=k;
            while(k--) {
                unsigned i=pos>>15;
                int a=data[i], b=data[i+1];
                a+=(int32_t)((int16_t)(b-a)*(int16_t)(pos&32767))>>15;
                *acc+++=tab[a];
                pos+=step;
            }
        } else {
            unsigned i=pos>>15, next=i+1;
            int a=data[i], b;
            if(next==s->loopend && LOOPS(s)) next=s->loopstart;
            b=next<s->length ? data[next] : a;
            a+=(int32_t)((int16_t)(b-a)*(int16_t)(pos&32767))>>15;
            *acc+++=tab[a];
            pos+=step;
            n--;
        }
#else
        k=run_until(pos,step,limit,n);
        n-=k;
        while(k--) {
            *acc+++=tab[data[pos>>15]];
            pos+=step;
        }
#endif
        v->position=pos;
    }
}

void modmusic_render(int16_t *out, int frames, int rate, int channels, int volume) {
    static int32_t acc[MIXRUN];
    int vmul;
    if(!module || rate<=0 || (channels!=1 && channels!=2)) {
        memset(out,0,(size_t)frames*channels*2); return;
    }
    if(volume<0) volume=0;
    if(volume>256) volume=256;
    if(outputrate && outputrate!=rate) modmusic_start();
    outputrate=rate;
    if(!voltab_ready) build_voltab();
    /* value * volume / 88 as (value * vmul) >> 8: value is at most twelve
       voices of 128 * 64, so the product stays well inside 32 bits. */
    vmul=(volume*256+MODMUSIC_GAIN_DIVISOR/2)/MODMUSIC_GAIN_DIVISOR;
    while(frames>0) {
        unsigned n, c, i;
        if(!remaining) beginrow(rate);
        if(!remaining) remaining=1;     /* only at absurd rates; never stall */
        n=(unsigned)frames;
        if(n>remaining) n=remaining;
        if(n>MIXRUN) n=MIXRUN;
        memset(acc,0,n*sizeof(acc[0]));
        for(c=0;c<voicecount;c++) {
            Voice *v=&voices[c];
            if(v->sample && v->volume)
                voice_mix(v,acc,n,(int)v->volume);
        }
        if(channels==2) {
            for(i=0;i<n;i++) {
                int value=(acc[i]*vmul)>>8;
                if(value>32767) value=32767;
                if(value< -32768) value= -32768;
                out[0]=out[1]=(int16_t)value;
                out+=2;
            }
        } else {
            for(i=0;i<n;i++) {
                int value=(acc[i]*vmul)>>8;
                if(value>32767) value=32767;
                if(value< -32768) value= -32768;
                *out++=(int16_t)value;
            }
        }
        remaining-=n;
        frames-=(int)n;
    }
}
