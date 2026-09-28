#!/usr/bin/env python3
"""Render the actual game sequencer/OPL emulator into four-channel ProTracker MODs.
Requires Python 3 and a host C compiler; no downloaded instruments or music.
"""
import array
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
RATE = 22050
PERIOD = 214
PAL = 3546895
ROWS_PER_SAMPLE = 192


def function(source, name):
    match = re.search(r'^(?:void|K_INT16) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    start = match.start()
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos]


def build_renderer(directory):
    src = (ROOT / 'src/subs.c').read_text()
    header = (ROOT / 'include/lab3d.h').read_text()
    freq = re.search(r'EXTERN K_UINT16 adlibfreq\[63\] =\s*(\{.*?\});', header, re.S)[1]
    shim = r'''
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "adlibemu.h"
typedef int16_t K_INT16;
typedef int32_t K_INT32;
typedef uint32_t K_UINT32;
#define O_BINARY 0
#define MUSIC_SOURCE_NONE 0
#define MUSIC_SOURCE_MIDI 1
#define MUSIC_SOURCE_ADLIB 2
#define MUSIC_SOURCE_ADLIB_RANDOM 3
int musicsource=2, firstime=1, musicpan=0, mute=0, lastTick;
unsigned int musicstatus, count, countstop;
unsigned short numnotes, numchans, nownote, drumstat;
uint32_t note[65536], chanage[18];
unsigned char inst[256][11], databuf[512], chanfreq[18], chantrack[18];
unsigned char trinst[16], trquant[16], trchan[16], trprio[16], trvol[16];
int gminst[256];
char gameroot[1024], filepath[2048], filepathUpper[2048], lastPlayedMusicFile[32];
int readlong(unsigned char *p) { return p[0]|p[1]<<8|p[2]<<16|p[3]<<24; }
void readLE16(int f, void *p, int n) { unsigned char b[2]; uint16_t *v=p; while(n>0) { if(read(f,b,2)!=2) exit(2); *v++=b[0]|b[1]<<8; n-=2; } }
void readLE32(int f, void *p, int n) { unsigned char b[4]; uint32_t *v=p; while(n>0) { if(read(f,b,4)!=4) exit(2); *v++=readlong(b); n-=4; } }
void PL_LockSound(void) {}
void PL_UnlockSound(void) {}
void PL_StartClock(void) {}
int PL_GetTicks(void) {return 0;}
void setmidiinsts(void) {}
void randominsts(void) {}
'''
    names = ['outdata', 'setinst', 'loadmusic', 'musicon', 'ksmhandler']
    funcs = [function(src, n) for n in names]
    # Count completed loops without changing the sequencer's scheduling.
    funcs[-1] = funcs[-1].replace('nownote = 0;', '{ nownote = 0; loops++; }')
    shim += '\nunsigned short adlibfreq[63] = ' + freq + ';\nint loops;\n'
    shim += '\n'.join(f[:f.index('{')].strip() + ';' for f in funcs) + '\n'
    shim += '\n'.join(funcs)
    shim += r'''
int main(int argc, char **argv) {
    int tick=0, previous=0; int16_t pcm[128];
    if(argc!=3) return 2;
    snprintf(gameroot,sizeof(gameroot),"%s/",argv[1]);
    adlibinit(22050,1,2); adlibsetvolume(64*48);
    if(loadmusic(argv[2])) return 3;
    musicon();
    /* Warm up one loop so sustained envelopes survive the loop seam. */
    while(loops<2 && tick<240*180) {
        int end=(int)((int64_t)(tick+1)*22050/240), size=end-previous;
        int output=loops>=1;
        ksmhandler();
        adlibgetsample(pcm,size*2);
        if(output) fwrite(pcm,2,size,stdout);
        previous=end; tick++;
    }
    return loops==2 ? 0 : 4;
}
'''
    path = directory / 'render.c'
    path.write_text(shim)
    exe = directory / 'render'
    subprocess.run(['cc', '-O2', '-I'+str(ROOT/'include'), str(path), str(ROOT/'src/adlibemu.c'), '-lm', '-o', str(exe)], check=True)
    return exe


def event(sample=0, period=0, effect=0, arg=0):
    return bytes([(sample & 0xf0) | (period >> 8), period & 255,
                  ((sample & 15) << 4) | effect, arg])


def module(name, raw):
    pcm = array.array('h', raw)
    # F01 + F96: 60 rows/s, within one half-row of the original loop length.
    rows = max(1, round(len(pcm) / RATE * 60))
    segments = math.ceil(rows / ROWS_PER_SAMPLE)
    assert segments <= 31
    samples = []
    for s in range(segments):
        first = s * ROWS_PER_SAMPLE
        last = min(rows, first + ROWS_PER_SAMPLE)
        n = round((last-first)*PAL/PERIOD/60)
        data = bytearray()
        for j in range(n):
            x = (first/60 + j*PERIOD/PAL) * len(pcm)/(rows/60)
            ix = min(int(x), len(pcm)-1)
            frac = x-ix
            value = pcm[ix]*(1-frac)+pcm[min(ix+1,len(pcm)-1)]*frac
            data.append(max(-128,min(127,round(value/256))) & 255)
        data[:2] = b'\0\0'  # ProTracker uses this word as the silent loop.
        if len(data)%2: data.append(data[-1])
        assert len(data)<=65534
        samples.append(data)
    out = bytearray(name.encode().ljust(20,b'\0'))
    for i in range(31):
        data = samples[i] if i<len(samples) else b''
        label = ('OPL mix %02d'%i).encode() if data else b''
        out += label.ljust(22,b'\0') + struct.pack('>HBBHH',len(data)//2,0,64,0,1)
    patterns = math.ceil(rows/64)
    out += bytes([patterns,0])+bytes(range(patterns)).ljust(128,b'\0')+b'M.K.'
    pat = bytearray(patterns*1024)
    for row in range(rows):
        if row%ROWS_PER_SAMPLE==0:
            pat[row*16:row*16+4] = event(row//ROWS_PER_SAMPLE+1,PERIOD)
    pat[4:8]=event(effect=15,arg=1)
    pat[8:12]=event(effect=15,arg=150)
    pat[(rows-1)*16+12:(rows-1)*16+16]=event(effect=11,arg=0)
    out += pat
    for data in samples: out += data
    return out, rows/60


def main():
    report=[]
    with tempfile.TemporaryDirectory() as tmp:
        renderer=build_renderer(Path(tmp))
        for folder in sorted((ROOT/'gamedata').iterdir()):
            archive=next((p for p in folder.iterdir() if p.name.lower()=='songs.kzp'),None)
            if not archive: continue
            b=archive.read_bytes()
            dest=folder/'mods'; dest.mkdir(exist_ok=True)
            for i in range(struct.unpack_from('<H',b)[0]):
                name=b[2+i*12:10+i*12].split(b'\0')[0].decode('ascii')
                raw=subprocess.check_output([str(renderer),str(folder),name])
                mod,duration=module(name,raw)
                (dest/(name+'.mod')).write_bytes(mod)
                report.append(dict(version=folder.name,track=name,seconds=duration,bytes=len(mod),sha256=hashlib.sha256(mod).hexdigest()))
            print(folder.name, 'converted', flush=True)
    (ROOT/'tools/modmusic/manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(len(report),'MODs;',sum(r['bytes'] for r in report),'bytes')

if __name__=='__main__': main()
