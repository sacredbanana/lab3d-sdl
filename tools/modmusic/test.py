#!/usr/bin/env python3
"""Host tests of the shipped modules: player and real game mixer under the
sanitizers, malformed-file rejection, and a fidelity check of every module
against the Adlib emulator playing the same KSM song."""
from pathlib import Path
import json
import subprocess
import tempfile
import numpy as np
import generate as g
from generate import ROOT

PLAYER = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "modmusic.h"
int main(int argc, char **argv) {
    int rate=atoi(argv[2]); long frames=atol(argv[3]), done=0; int16_t buf[4096];
    if(modmusic_load(argv[1])) return 1;
    while(done<frames) {
        long n=frames-done; if(n>4096) n=4096;
        modmusic_render(buf,(int)n,rate,1,64); fwrite(buf,2,n,stdout); done+=n;
    }
    return 0;
}
'''

def log_spectrogram(x, rate, frame):
    n = len(x) // frame
    x = x[:n*frame].reshape(n, frame) * np.hanning(frame)
    spec = np.abs(np.fft.rfft(x, axis=1))**2
    freqs = np.fft.rfftfreq(frame, 1/rate)
    edges = np.geomspace(80, 8000, 49)
    bands = np.zeros((n, 48))
    for i in range(48):
        m = (freqs >= edges[i]) & (freqs < edges[i+1])
        bands[:, i] = spec[:, m].sum(axis=1) if m.any() else 0
    return 10*np.log10(bands + 1e3)

def similarity(a, b, rate):
    """(log-spectrogram correlation, RMS level difference in dB) of two renders."""
    n = min(len(a), len(b)); a, b = a[:n], b[:n]
    corr = np.corrcoef(log_spectrogram(a, rate, rate//30).ravel(),
                       log_spectrogram(b, rate, rate//30).ravel())[0, 1]
    return corr, 20*np.log10(np.sqrt((b**2).mean()) / np.sqrt((a**2).mean()))

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
/* How much of the buffer DumpSound() has filled; the tests below write the
   buffer directly and say so through this, as DumpSound() would. */
static int soundpending;
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
    for(file=1;file<argc;file+=2) {
        int seconds=atoi(argv[file+1]);
        assert(!modmusic_load(argv[file]));
        for(ratio=1;ratio<=4;ratio*=2) for(stereo=1;stereo<=2;stereo++) {
            int len=4096, n;
            soundratio=ratio; channels=stereo; soundratioshift=ratio==4?2:ratio==2?1:0;
            samplerate=11025*ratio;
            memset(SoundBuffer,0,sizeof(SoundBuffer));
            FeedPoint=65536-1024; soundpending=0; modmusic_start();
            n=mixblock((unsigned char *)alone,len);
            assert(n>0 && n<=len);
            for(i=0;i<65536;i++) SoundBuffer[i]=1000;
            FeedPoint=65536-1024; soundpending=65536; modmusic_start();
            assert(n==mixblock((unsigned char *)together,len));
            for(i=0;i<n/2;i++) {
                int expected=alone[i]+1000;
                if(expected>32767) expected=32767;
                assert(together[i]==expected);
            }
            /* What the mixer consumed it cleared, and nothing else. */
            for(i=0;i<65536;i++) assert(SoundBuffer[i]==(i>=65536-1024 && i<65536-1024+n/(2*ratio) ? 0 : 1000));
            for(i=0;i<65536;i++) SoundBuffer[i]=1000;
            FeedPoint=0; soundpending=65536; mute=2;
            n=mixblock((unsigned char *)together,len);
            for(i=0;i<n/2;i++) assert(together[i]==1000);
            mute=1; FeedPoint=0; soundpending=65536;
            n=mixblock((unsigned char *)together,len);
            for(i=0;i<n/2;i++) assert(together[i]==0);
            /* With nothing pending the buffer is left alone and adds nothing. */
            mute=0; FeedPoint=0; soundpending=0;
            for(i=0;i<65536;i++) SoundBuffer[i]=1000;
            modmusic_start();
            n=mixblock((unsigned char *)together,len);
            modmusic_start();
            assert(n==mixblock((unsigned char *)alone,len));
            for(i=0;i<n/2;i++) assert(together[i]==alone[i]);
            for(i=0;i<65536;i++) assert(SoundBuffer[i]==1000);
            mute=0;
        }
        /* Longer than two loops of the longest song, including sample
           boundaries, sequence jump and fractional timing accumulation. */
        modmusic_start();
        for(loops=0;loops<(seconds*2+2)*22050/1024;loops++) modmusic_render(scratch,1024,22050,1,64);
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
    report=json.loads((ROOT/'tools/modmusic/manifest.json').read_text())
    mods=[ROOT/x['path'] for x in report]
    assert len(mods)==132
    assert all(p.is_file() for p in mods)
    args=[str(exe)]
    for p,x in zip(mods,report):
        b=p.read_bytes()
        assert b[1080:1084]==b'12CH' and 2<=x['samples']<=31 and x['loudest']<=64.5
        assert b[:20].rstrip(b'\0')==x['track'].encode()
        pattern=b[1084:1084+b[950]*3072]
        notes=sum(bool(pattern[i]&15 or pattern[i+2]>>4) for i in range(0,len(pattern),4))
        assert notes>10
        # Row 0 carries speed 1 and tempo 150; the last row loops with B00.
        row0={(pattern[i+2]&15,pattern[i+3]) for i in range(0,48,4)}
        assert (15,1) in row0 and (15,150) in row0
        args.extend((str(p),str(int(x['seconds'])+1)))
    subprocess.run(args,check=True)
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
    print(len(mods),'modules and',len(cases),'malformed-file cases passed under ASan/UBSan.')

    # Fidelity: every shipped module, played by the game's player, must sound
    # like the Adlib emulator playing the KSM song it was made from.
    # Both mixers: the interpolating one the fast CPUs get, and the
    # nearest-sample one the 68020/68030 builds use.
    renderer=g.build_renderer(tmp)
    (tmp/'player.c').write_text(PLAYER)
    rate=22050
    opls={}
    for name,flags in (('interpolating',[]),('nearest-sample',['-DMODMUSIC_INTERPOLATE=0'])):
        subprocess.run(['cc','-O2','-I'+str(ROOT/'include')]+flags+[str(tmp/'player.c'),str(ROOT/'src/modmusic.c'),'-o',str(tmp/'player')],check=True)
        worst=(1.0,0.0,'')
        checked=set()
        for x in report:
            if x['path'] in checked:
                continue
            checked.add(x['path'])
            folder=ROOT/'gamedata'/x['version']
            frames=int(x['seconds']*rate)
            if x['path'] not in opls:
                opls[x['path']]=g.render(renderer,'song',folder,x['track'],rate,int(x['seconds']*240)+240)[:frames]
            opl=opls[x['path']]
            pcm=np.frombuffer(subprocess.check_output([str(tmp/'player'),str(ROOT/x['path']),str(rate),str(frames)]),dtype='<i2').astype(float)
            corr,level=similarity(opl,pcm,rate)
            assert corr>0.9 and abs(level)<2.0, (name,x['path'],corr,level)
            if corr<worst[0]:
                worst=(corr,level,x['path'])
        print(f'{len(checked)} modules match the Adlib emulator with the {name} mixer; '
              f'worst spectrogram correlation {worst[0]:.3f} ({worst[2]}, level {worst[1]:+.2f} dB).')
