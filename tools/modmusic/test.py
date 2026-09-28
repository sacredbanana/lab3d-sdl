#!/usr/bin/env python3
"""Host sanitizer tests of the shipped modules, player, and real game mixer."""
from pathlib import Path
import subprocess
import tempfile
from generate import ROOT, function

HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "modmusic.h"
typedef int16_t K_INT16;
#define PLATFORM_AMIGA 1
#define MUSIC_SOURCE_ADLIB 2
#define MUSIC_SOURCE_ADLIB_RANDOM 3
#define MUSIC_SOURCE_MOD 4
static int soundratio, channels, FeedPoint, musicsource=4, mute, musicstatus=1;
static int samplerate=22050, musicvolume=64, soundratioshift;
static int16_t SoundBuffer[65536];
static void preparesound(void *p, long n) { memset(p,0,n); }
'''
# mixblock is static, unlike the functions used by the offline renderer.
mixer=(ROOT/'src/subs.c').read_text()
start=mixer.index('static int mixblock(')
end=mixer.index('\nvoid AudioCallback',start)
HARNESS+=mixer[start:end]
HARNESS+=r'''
static int16_t alone[8192], together[8192], scratch[2048];
int main(int argc,char **argv) {
    int file, ratio, stereo, i, loops;
    for(file=1;file<argc;file++) {
        assert(!modmusic_load(argv[file]));
        for(ratio=1;ratio<=4;ratio*=2) for(stereo=1;stereo<=2;stereo++) {
            int len=4096, n;
            soundratio=ratio; channels=stereo; soundratioshift=ratio==4?2:ratio==2?1:0;
            samplerate=11025*ratio;
            memset(SoundBuffer,0,sizeof(SoundBuffer));
            FeedPoint=65536-1024; modmusic_start();
            n=mixblock((unsigned char *)alone,len);
            assert(n>0 && n<=len);
            for(i=0;i<65536;i++) SoundBuffer[i]=1000;
            FeedPoint=65536-1024; modmusic_start();
            assert(n==mixblock((unsigned char *)together,len));
            for(i=0;i<n/2;i++) {
                int expected=alone[i]+1000;
                if(expected>32767) expected=32767;
                assert(together[i]==expected);
            }
            for(i=0;i<65536;i++) SoundBuffer[i]=1000;
            FeedPoint=0; mute=2;
            n=mixblock((unsigned char *)together,len);
            for(i=0;i<n/2;i++) assert(together[i]==1000);
            mute=1; FeedPoint=0;
            n=mixblock((unsigned char *)together,len);
            for(i=0;i<n/2;i++) assert(together[i]==0);
            mute=0;
        }
        /* Longer than two loops of the longest song, including sample
           boundaries, sequence jump and fractional timing accumulation. */
        modmusic_start();
        for(loops=0;loops<3000;loops++) modmusic_render(scratch,1024,22050,1,64);
        modmusic_start();
        modmusic_render(alone,2048,22050,1,64);
        modmusic_start();
        modmusic_render(together,2048,22050,1,64);
        assert(!memcmp(alone,together,4096));
        modmusic_start();
        modmusic_render(together,2048,22050,1,0);
        for(i=0;i<2048;i++) assert(together[i]==0);
        modmusic_free(); modmusic_free();
        modmusic_render(together,2048,22050,1,64);
        for(i=0;i<2048;i++) assert(together[i]==0);
    }
    assert(modmusic_load("/missing/music.mod")==-1);
    puts("Module playback, restart, looping, mute, volume, cleanup and SFX mixing passed.");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp)
    src=tmp/'test.c'; src.write_text(HARNESS)
    exe=tmp/'test'
    subprocess.run(['cc','-O1','-g','-fsanitize=address,undefined','-I'+str(ROOT/'include'),str(src),str(ROOT/'src/modmusic.c'),'-o',str(exe)],check=True)
    mods=sorted((ROOT/'gamedata').glob('*/mods/*.mod'))
    assert len(mods)==121
    subprocess.run([str(exe),*map(str,mods)],check=True)
    # Reject truncated headers, samples, invalid orders, and unsupported effects.
    valid=mods[0].read_bytes()
    cases=[valid[:100],valid[:-100],valid[:1084]]
    for offset,value in [(950,0),(950,129),(1080,0),(1086,0x17),(44,1),(45,65),(953,255)]:
        bad=bytearray(valid);bad[offset]=value;cases.append(bad)
    reject=tmp/'reject.c'
    reject.write_text('#include "modmusic.h"\nint main(int c,char **v) { return modmusic_load(v[1]) == -1 ? 0 : 1; }\n')
    subprocess.run(['cc','-fsanitize=address,undefined','-I'+str(ROOT/'include'),str(reject),str(ROOT/'src/modmusic.c'),'-o',str(tmp/'reject')],check=True)
    for i,data in enumerate(cases):
        path=tmp/f'bad{i}.mod';path.write_bytes(data)
        subprocess.run([str(tmp/'reject'),str(path)],check=True)
    print('121 modules and',len(cases),'malformed-file cases passed under ASan/UBSan.')
