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
void modmusic_render(int16_t *out, int frames, int rate, int channels, int volume) {
    int i;
    if(!module || rate<=0 || (channels!=1 && channels!=2)) {
        memset(out,0,(size_t)frames*channels*2); return;
    }
    if(volume<0) volume=0;
    if(volume>256) volume=256;
    if(outputrate && outputrate!=rate) modmusic_start();
    outputrate=rate;
    for(i=0;i<frames;i++) {
        int value=0; unsigned c;
        if(!remaining) beginrow(rate);
        for(c=0;c<voicecount;c++) {
            Voice *v=&voices[c];
            if(v->sample && v->volume) {
                unsigned pos=v->position>>15;
                if(v->sample->loopend>v->sample->loopstart+2 &&
                   pos>=v->sample->loopend) {
                    unsigned span=v->sample->loopend-v->sample->loopstart;
                    pos=v->sample->loopstart+(pos-v->sample->loopstart)%span;
                    v->position=(pos<<15)|(v->position&32767);
                }
                if(pos>=v->sample->length) { v->sample=NULL; continue; }
                int a=v->sample->data[pos];
                unsigned next=pos+1;
                if(next==v->sample->loopend &&
                   v->sample->loopend>v->sample->loopstart+2)
                    next=v->sample->loopstart;
                int b=next<v->sample->length ? v->sample->data[next] : a;
                int interpolated=a*32768+(b-a)*(int)(v->position&32767);
                value+=(interpolated*(int)v->volume)/32768;
                v->position+=v->step;
            }
        }
        value=value*volume/MODMUSIC_GAIN_DIVISOR;
        if(value>32767) value=32767;
        if(value< -32768) value= -32768;
        *out++=(int16_t)value;
        if(channels==2) *out++=(int16_t)value;
        remaining--;
    }
}
