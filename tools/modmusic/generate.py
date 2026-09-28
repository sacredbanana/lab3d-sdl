#!/usr/bin/env python3
"""Convert Ken's Labyrinth KSM songs into instrument-based 12-channel MODs.

Every note is played from a short sample of the game's own OPL instrument,
rendered with the game's adlibemu.c.  Melodic samples hold the attack and
decay, then loop an exact multiple of the waveform period with a crossfaded
seam; the emulator's exponential envelope (held decay and key-off release)
is measured per instrument and reproduced with per-row volume commands.
Drums are one-shot renders of the OPL rhythm section.

Channel allocation mirrors the KSM sequencer exactly: MOD channels 0-5 are
the six melodic OPL channels (same age-based allocation and note-off
matching as ksmhandler), channels 6-10 are the five rhythm voices, and
channel 11 carries the speed, tempo and loop commands.
"""
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import tempfile

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
PAL = 3546895
REF_PERIOD = 214                     # samples are rendered for this period
RATE = PAL / REF_PERIOD              # 16574.3 Hz
RATE_INT = round(RATE)
ROWS_PER_SECOND = 60                 # F01 speed, F96 tempo
KSM_TICKS_PER_ROW = 240 // ROWS_PER_SECOND
CHANNELS = 12
MELODIC_CHANNELS = 6                 # OPL channels 0-5 while rhythm mode is on
CONTROL_CHANNEL = 11
ROW_SAMPLES = RATE / ROWS_PER_SECOND
HOLD_ROWS = 360            # rows of held note measured (6 s); slope extrapolated after
RELEASE_ROWS = 90                    # 1.5 s of key-off release measurement
LOOP_START_CAP = 24                  # store at most 0.4 s of attack/decay
ONE_SHOT_ROWS = 30                   # notes that die within this are unlooped
MIN_LOOP, MAX_LOOP = 900, 2600       # loop length in samples
NOISE_LOOP = 1600                    # loop length for noise-based drums
ONE_SHOT_DB = -60                    # relative to the note's own peak
SILENCE_DB = -66                     # ignore noise floor below this when fitting
FADE = 48
SAMPLE_SLOTS = 31
MIN_BAR = 12                         # shortest pattern length tried for dedup
# Peak OPL output (16-bit, at the default music volume) that maps to sample
# volume 64.  Measured over the whole soundtrack: modmusic.c divides the mix
# by MODMUSIC_GAIN_DIVISOR = 127*64*64/PEAK_REFERENCE to match Adlib mode.
PEAK_REFERENCE = 5911


def function(source, name):
    match = re.search(r'^(?:void|K_INT16) ' + name + r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise ValueError(name)
    pos = match.end()
    depth = 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[match.start():pos]


def adlibfreq_table():
    header = (ROOT / 'include/lab3d.h').read_text()
    body = re.search(r'EXTERN K_UINT16 adlibfreq\[63\] =\s*\{(.*?)\};', header, re.S)[1]
    return [int(v) for v in re.findall(r'\d+', body)]


ADLIBFREQ = adlibfreq_table()


def opl_rate(note):
    """Relative OPL frequency of a KSM note (fnum << block)."""
    value = ADLIBFREQ[note]
    return (value & 1023) << ((value >> 10) & 7)


def period_for(note, reference):
    return round(REF_PERIOD * opl_rate(reference) / opl_rate(note))


def build_renderer(directory):
    src = (ROOT / 'src/subs.c').read_text()
    freq = re.search(r'EXTERN K_UINT16 adlibfreq\[63\] =\s*(\{.*?\});',
                     (ROOT / 'include/lab3d.h').read_text(), re.S)[1]
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
    shim += '\nunsigned short adlibfreq[63] = ' + freq + ';\n'
    shim += '\n'.join(f[:f.index('{')].strip() + ';' for f in funcs) + '\n'
    shim += '\n'.join(funcs)
    shim += r'''
static int speakers = 1;
static void emit(long n) {
    int16_t pcm[2048];
    long i;
    while (n > 0) {
        long k = n > 1024 ? 1024 : n;
        adlibgetsample(pcm, k*2*speakers);
        if (speakers == 2)            /* per-channel volumes only work in stereo */
            for (i = 0; i < k; i++) pcm[i] = pcm[2*i];
        if (fwrite(pcm, 2, k, stdout) != (size_t)k) exit(5);
        n -= k;
    }
}
static void reset(int rate) {
    adlibinit(rate, speakers, 2);
    adlibsetvolume(64*48);            /* default music volume */
    outdata(0, 0x1, 32); outdata(0, 0x4, 0); outdata(0, 0x8, 0);
}
int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "song") && (argc == 6 || argc == 7)) {
        /* song <folder> <name> <rate> <ticks> [OPL channel mask]:
           the real sequencer driving the real OPL emulator */
        int rate = atoi(argv[4]), i;
        long ticks = atol(argv[5]), tick, previous = 0;
        snprintf(gameroot, sizeof(gameroot), "%s/", argv[2]);
        if (argc == 7) {
            speakers = 2;
            for (i = 0; i < 9; i++) lvol[i] = rvol[i] = (atoi(argv[6]) >> i) & 1;
        }
        reset(rate);
        if (loadmusic(argv[3])) return 3;
        musicon();
        for (tick = 0; tick < ticks; tick++) {
            long end = (tick+1)*(long)rate/240;
            ksmhandler();
            emit(end-previous);
            previous = end;
        }
        return 0;
    }
    if (!strcmp(argv[1], "inst") && argc == 7) {
        /* inst <rate> <11 hex bytes> <note> <hold samples> <total samples> */
        int rate = atoi(argv[2]), n = atoi(argv[4]), i;
        long hold = atol(argv[5]), total = atol(argv[6]);
        unsigned char v[11];
        for (i = 0; i < 11; i++) {
            unsigned x;
            if (sscanf(argv[3]+2*i, "%2x", &x) != 1) return 2;
            v[i] = (unsigned char)x;
        }
        reset(rate);
        outdata(0, 0xbd, 32);         /* rhythm mode on, as in every song */
        setinst(0, 0, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
        outdata(0, 0xa0, adlibfreq[n]&255);
        outdata(0, 0xb0, (adlibfreq[n]>>8)|32);
        emit(hold);
        outdata(0, 0xa0, adlibfreq[n]&255);
        outdata(0, 0xb0, (adlibfreq[n]>>8)&223);
        emit(total-hold);
        return 0;
    }
    if (!strcmp(argv[1], "drum") && (argc == 8 || argc == 10)) {
        /* drum <folder> <name> <track> <note> <rate> <total samples>
                [<context 0xB0/0xB1 register> <context instrument hex>]
           The emulator's snare and cymbal take their pitch, KSL and rate
           registers from melodic channel 0 and 1 (cellon(16,...) reads
           0xB0/0xC0, cellon(17,...) reads 0xB1/0xC1), so those are set to
           the state they most often have when the drum is hit. */
        int track = atoi(argv[4]), freq = adlibfreq[atoi(argv[5])];
        int rate = atoi(argv[6]), bit, chan, i;
        snprintf(gameroot, sizeof(gameroot), "%s/", argv[2]);
        switch (track) {
            case 11: bit = 16; chan = 6; freq -= 2048; break;
            case 12: bit = 8; chan = 7; freq -= 2048; break;
            case 13: bit = 4; chan = 8; break;
            case 14: bit = 2; chan = 8; break;
            case 15: bit = 1; chan = 7; freq -= 2048; break;
            default: return 4;
        }
        speakers = 2;
        for (i = 0; i < 9; i++) lvol[i] = rvol[i] = (i == chan);
        reset(rate);
        if (loadmusic(argv[3])) return 3;
        if (argc == 10) {
            unsigned char v[11];
            int ctx = track == 12 ? 0 : 1;
            for (i = 0; i < 11; i++) {
                unsigned x;
                if (sscanf(argv[9]+2*i, "%2x", &x) != 1) return 2;
                v[i] = (unsigned char)x;
            }
            setinst(0, ctx, v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10]);
            outdata(0, 0xb0+ctx, atoi(argv[8]));
        }
        outdata(0, 0xa0+chan, freq&255);
        outdata(0, 0xb0+chan, (freq>>8)&223);
        outdata(0, 0xbd, drumstat&~bit);
        drumstat |= bit;
        outdata(0, 0xbd, drumstat);
        emit(atol(argv[7]));
        return 0;
    }
    return 2;
}
'''
    source = directory / 'render.c'
    source.write_text(shim)
    exe = directory / 'render'
    subprocess.run(['cc', '-O2', '-w', '-I' + str(ROOT / 'include'), str(source),
                    str(ROOT / 'src/adlibemu.c'), '-lm', '-o', str(exe)], check=True)
    return exe


def render(renderer, *args):
    raw = subprocess.check_output([str(renderer), *map(str, args)])
    return np.frombuffer(raw, dtype='<i2').astype(np.float64)


def archive_songs(archive):
    data = archive.read_bytes()
    songs = []
    for i in range(struct.unpack_from('<H', data)[0]):
        name = data[2+i*12:10+i*12].split(b'\0')[0].decode('ascii')
        off = struct.unpack_from('<I', data, 10+i*12)[0]
        meta = data[off:off+80]
        count = struct.unpack_from('<H', data, off+80)[0]
        notes = struct.unpack_from('<%dI' % count, data, off+82)
        songs.append((name, meta, notes))
    return songs


def load_instruments(folder):
    path = next(p for p in folder.iterdir() if p.name.lower() == 'insts.dat')
    data = path.read_bytes()
    insts = []
    for i in range(256):
        record = data[i*33:i*33+33]
        insts.append((record[:20].split(b'\0')[0].decode('ascii', 'replace').strip(),
                      bytes(record[20:31])))
    return insts


def sequence(meta, notes):
    """Mirror ksmhandler: KSM ticks, quantisation, channel allocation and
    note-off matching.  Returns (rows, events); each event is
    (row, kind, channel, track, pitch, context) with kind in
    {'on', 'off', 'drum'}.  context is only meaningful for snare and cymbal
    hits: the emulator derives their pitch and envelope rates from the
    0xB0/0xB1 register of melodic channel 0/1, so it records that value."""
    trquant, trchan = meta[16:32], meta[32:48]
    numchans = 9 - trchan[11]*3
    chantrack = []
    for track in range(16):
        if trchan[track] and len(chantrack) < numchans:
            chantrack.extend([track] * min(trchan[track], numchans - len(chantrack)))
    chantrack.extend([0] * (numchans - len(chantrack)))
    chanfreq = [0] * numchans
    chanage = [0] * numchans
    regb = [0] * numchans     # last value written to 0xB0+channel
    t0 = notes[0] >> 12
    count = t0                # tick at which the current note fires
    countstop = t0 - 1        # musicon() starts with the unquantised first tick
    events = []
    for index, raw in enumerate(notes):
        track, kind, pitch = (raw >> 8) & 15, raw & 255, raw & 63
        row = count - t0      # in ticks for now; converted to rows below
        if 1 <= kind <= 61:
            for i in range(numchans):
                if chanfreq[i] == pitch and chantrack[i] == track:
                    events.append((row, 'off', i, track, pitch, 0))
                    chanfreq[i] = 0
                    chanage[i] = 0
                    regb[i] = (ADLIBFREQ[pitch] >> 8) & 223
                    break
        elif 65 <= kind <= 125:
            if track < 11:
                best, temp = numchans, 0
                for j in range(numchans):
                    # ksmhandler does this in uint32: a channel whose age is
                    # ahead of a rounded-down countstop wraps and always wins.
                    age = (countstop - chanage[j]) & 0xFFFFFFFF
                    if age >= temp and chantrack[j] == track:
                        temp = age
                        best = j
                if best < numchans:
                    events.append((row, 'on', best, track, pitch, 0))
                    chanfreq[best] = pitch
                    chanage[best] = countstop
                    regb[best] = (ADLIBFREQ[pitch] >> 8) | 32
            else:
                context = regb[0] if track == 12 else regb[1] if track == 14 else 0
                events.append((row, 'drum', MELODIC_CHANNELS + track - 11, track, pitch, context))
        nxt = notes[(index + 1) % len(notes)]
        quanter = 240 // max(1, trquant[(nxt >> 8) & 15])
        countstop = ((nxt >> 12) + quanter // 2) // quanter * quanter
        if index + 1 < len(notes) and count < countstop:
            count = countstop
    # Quantised KSM ticks mostly sit on one phase of the 4-tick row grid;
    # anchor the grid there so those events land exactly on rows.
    phases = collections.Counter(e[0] % KSM_TICKS_PER_ROW for e in events)
    phase = phases.most_common(1)[0][0]
    # The sequencer restarts one tick after the last event fires.
    rows = max(1, (count - t0 + 1 + KSM_TICKS_PER_ROW // 2) // KSM_TICKS_PER_ROW)
    converted = []
    for tick, kind, channel, track, pitch, context in events:
        row = (tick - phase + KSM_TICKS_PER_ROW // 2) // KSM_TICKS_PER_ROW
        converted.append((min(max(row, 0), rows - 1), kind, channel, track, pitch, context))
    return rows, converted


def instrument_key(insts, meta, track):
    v = bytearray(insts[meta[track]][1])
    v[1] = (v[1] & 192) | (63 - meta[64 + track])      # musicon() carrier level
    return bytes(v)


def drum_key(insts, meta, track, pitch, context):
    section = tuple(insts[meta[t]][1] for t in range(11, 16))
    return ('drum', section, bytes(meta[75:80]), track, pitch, context)


def drum_context(meta, events, insts, track):
    """(0xB0/0xB1 register, instrument bytes) of the melodic channel the
    emulator borrows for the snare (channel 0) or cymbal (channel 1), in the
    state it is most often in when that drum is hit; None for other drums."""
    if track not in (12, 14):
        return None
    hits = collections.Counter(e[5] for e in events if e[1] == 'drum' and e[3] == track)
    channel = 0 if track == 12 else 1
    trchan = meta[32:48]
    owner = 0
    for t in range(16):
        if trchan[t]:
            if channel < trchan[t]:
                owner = t
                break
            channel -= trchan[t]
    return hits.most_common(1)[0][0], instrument_key(insts, meta, owner)


def plan_samples(meta, events, insts):
    """Assign every (track, pitch) to a sample slot.  Melodic notes share one
    sample per instrument per pitch range; drums get one sample per hit type."""
    melodic = collections.defaultdict(set)
    drums = collections.defaultdict(set)
    for row, kind, channel, track, pitch, context in events:
        if kind == 'on':
            melodic[instrument_key(insts, meta, track)].add(pitch)
        elif kind == 'drum':
            drums[track].add(pitch)

    def group(pitches, span):
        remaining = sorted(pitches)
        groups = []
        while remaining:
            low = remaining[0]
            chosen = [p for p in remaining if p <= low + span]
            high = chosen[-1]
            reference = (low + high + 1) // 2
            if high - reference > 9:                  # keep periods >= 113
                reference = high - 9
            groups.append((reference, chosen))
            remaining = remaining[len(chosen):]
        return groups

    for melodic_span, drum_span in ((12, 4), (16, 4), (19, 4), (19, 7), (24, 12)):
        specs, assignment = [], {}
        for key, pitches in sorted(melodic.items()):
            for reference, chosen in group(pitches, melodic_span):
                specs.append(('inst', key, reference))
                for pitch in chosen:
                    assignment[('inst', key, pitch)] = (len(specs), reference)
        for track, pitches in sorted(drums.items()):
            context = drum_context(meta, events, insts, track)
            for reference, chosen in group(pitches, drum_span):
                specs.append(drum_key(insts, meta, track, reference, context))
                for pitch in chosen:
                    assignment[('drum', track, pitch)] = (len(specs), reference)
        if len(specs) <= SAMPLE_SLOTS:
            return specs, assignment
    raise ValueError('song needs more than 31 sample slots')


def row_edges(rows):
    return np.rint(np.arange(rows + 1) * ROW_SAMPLES).astype(int)


def row_levels(x, rows):
    """Per-row RMS level in dB and the row boundaries in samples."""
    edges = row_edges(rows)
    levels = np.empty(rows)
    for r in range(rows):
        seg = x[edges[r]:edges[r+1]]
        levels[r] = 20 * np.log10(max(np.sqrt(np.mean(seg*seg)), 1e-3))
    return levels, edges


def attack_end(levels):
    """First row within 0.5 dB of the loudest one (the attack has finished)."""
    return int(np.nonzero(levels >= np.max(levels) - 0.5)[0][0])


def tail_slope(levels, floor):
    """dB/row slope of the audible end of a decaying level curve (<= 0)."""
    audible = np.nonzero(levels > floor)[0]
    if len(audible) < 4:
        return -np.inf
    end = audible[-1] + 1
    start = max(0, end - 30)
    if end - start < 4:
        return -np.inf
    slope = np.polyfit(np.arange(end - start), levels[start:end], 1)[0]
    return min(0.0, slope)


def spectral_centroid(x):
    spectrum = np.abs(np.fft.rfft(x * np.hanning(len(x))))
    return float((spectrum * np.arange(len(spectrum))).sum() / max(spectrum.sum(), 1e-9))


def pick_loop_start(x, levels, edges, peak_row):
    """First row after the attack where the level follows a straight dB line
    and the timbre has settled, capped so samples stay short."""
    window = 4                        # rows per centroid measurement
    last = min(max(LOOP_START_CAP, peak_row + 3), HOLD_ROWS - 31)
    for r in range(peak_row + 3, last):
        seg = levels[r:r+30]
        fit = np.polyval(np.polyfit(np.arange(30), seg, 1), np.arange(30))
        if np.max(np.abs(seg - fit)) > 0.5:
            continue
        early = spectral_centroid(x[edges[r]:edges[r+window]])
        late = spectral_centroid(x[edges[r+30-window]:edges[r+30]])
        if abs(early - late) > 0.05 * max(late, 1.0):
            continue
        return r
    return last


def pick_loop_length(period):
    best = None
    n = max(1, math.ceil(MIN_LOOP / period))
    while n * period <= MAX_LOOP:
        length = n * period
        error = abs(length - round(length))
        if best is None or error < best[0] - 1e-9:
            best = (error, length)
        if error < 0.05:
            break
        n += 1
    if best is None:
        best = (0, n * period)
    length = int(round(best[1]))
    return length + (length & 1)


class Sample:
    """Rendered 8-bit sample plus the envelope model the pattern reproduces."""

    def __init__(self, name, pcm, loop, baked, held, held_slope, release, release_slope):
        self.name = name
        peak = float(np.max(np.abs(pcm))) if len(pcm) else 1.0
        self.peak = max(peak, 1.0)
        self.data = np.clip(np.rint(pcm * 127.0 / self.peak), -127, 127).astype(np.int8)
        if len(self.data) & 1:
            self.data = np.append(self.data, np.int8(0))
        self.loop = loop                   # (start, length) or None
        if loop is None:
            self.data[:2] = 0              # ProTracker's silent "no loop" word
        self.baked = baked                 # dB per row stored in the sample
        self.held = held                   # dB per row while the key is held
        self.held_slope = held_slope
        self.release = release             # dB relative to key-off level
        self.release_slope = release_slope
        self.volume = 64.0 * self.peak / PEAK_REFERENCE

    def header_volume(self):
        return int(np.clip(round(self.volume), 1, 64))

    def baked_db(self, k):
        if k < len(self.baked):
            return self.baked[k]
        return None if self.loop is None else self.baked[-1]

    def held_db(self, k):
        if k < len(self.held):
            return self.held[k]
        return self.held[-1] + self.held_slope * (k - len(self.held) + 1)

    def release_db(self, j):
        if j < len(self.release):
            return self.release[j]
        return self.release[-1] + self.release_slope * (j - len(self.release) + 1)

    def header(self):
        if self.loop:
            start, length = self.loop
            assert start % 2 == 0 and length % 2 == 0 and start + length <= len(self.data)
            start, length = start // 2, length // 2
        else:
            start, length = 0, 1
        return struct.pack('>HBBHH', len(self.data) // 2, 0, 0, start, length)


def fade_out(pcm):
    n = min(FADE, len(pcm))
    if n:
        pcm[-n:] *= np.linspace(1.0, 0.0, n)
    return pcm


def analyse(name, x, period, release_render):
    """Turn a held-note render into a Sample: either a one-shot (the note
    dies quickly) or attack/decay plus a crossfaded loop, with the measured
    held envelope.  release_render(keyoff_row) renders the key-off response;
    None means the voice is never released (drums)."""
    total_rows = HOLD_ROWS + 6
    levels, edges = row_levels(x, total_rows)
    peak_db = float(np.max(levels[:HOLD_ROWS]))
    peak_row = attack_end(levels[:HOLD_ROWS])
    floor = peak_db + SILENCE_DB
    # First row from which the voice stays quiet (some rhythm voices are
    # amplitude modulated and dip to silence between louder bursts).
    tail_max = np.maximum.accumulate(levels[HOLD_ROWS-1::-1])[::-1]
    quiet = next((r for r in range(peak_row, HOLD_ROWS) if tail_max[r] < peak_db + ONE_SHOT_DB), None)

    if quiet is not None and quiet <= ONE_SHOT_ROWS:
        pcm = fade_out(x[:edges[quiet]].copy())
        loop = None
        baked = list(levels[:quiet])
        held, held_slope = list(levels[:quiet]), -np.inf
        keyoff_row = max(1, min(peak_row + 3, quiet - 1))
    else:
        start_row = pick_loop_start(x, levels, edges, peak_row)
        start = int(edges[start_row]) & ~1
        length = pick_loop_length(period) if period else NOISE_LOOP
        pcm = x[:start + length].copy()
        cross = min(length // 2, start)
        if cross:
            w = np.linspace(0.0, 1.0, cross)
            pcm[start+length-cross:start+length] = (pcm[start+length-cross:start+length] * (1-w) +
                                                    x[start-cross:start] * w)
        loop = (start, length)
        loop_db = 20 * np.log10(max(np.sqrt(np.mean(pcm[start:start+length]**2)), 1e-3))
        baked = list(levels[:start_row]) + [loop_db]
        held = list(levels[:HOLD_ROWS])
        held_slope = tail_slope(levels[:HOLD_ROWS], floor)
        keyoff_row = peak_row + 3

    if release_render is None:
        release, release_slope = [-np.inf], -np.inf
    else:
        y = release_render(keyoff_row)
        ylevels, _ = row_levels(y, keyoff_row + RELEASE_ROWS)
        release = list(ylevels[keyoff_row:] - ylevels[keyoff_row - 1])
        release_slope = tail_slope(ylevels[keyoff_row:], floor)
    return Sample(name, pcm, loop, baked, held, held_slope, release, release_slope)


def analyse_melodic(renderer, name, key, reference):
    edges = row_edges(HOLD_ROWS + 6)
    x = render(renderer, 'inst', RATE_INT, key.hex(), reference, edges[-1], edges[-1])
    mul_zero = (key[0] & 15) == 0 or (key[5] & 15) == 0
    period = RATE / (opl_rate(reference) * 49716 / 2**20) * (2 if mul_zero else 1)

    def release_render(keyoff_row):
        yedges = row_edges(keyoff_row + RELEASE_ROWS)
        return render(renderer, 'inst', RATE_INT, key.hex(), reference, yedges[keyoff_row], yedges[-1])
    return analyse(name, x, period, release_render)


def analyse_drum(renderer, name, folder, song, track, pitch, context):
    edges = row_edges(HOLD_ROWS + 6)
    extra = (context[0], context[1].hex()) if context else ()
    x = render(renderer, 'drum', folder, song, track, pitch, RATE_INT, edges[-1], *extra)
    # Rhythm voices are noise based or die quickly; a crossfaded fixed-length
    # loop is fine for the ones that ring on.
    return analyse(name, x, None, None)


def make_event(sample=0, period=0, effect=0, arg=0):
    return bytes([(sample & 240) | (period >> 8), period & 255, ((sample & 15) << 4) | effect, arg])


def build_cells(rows, events, assignment, samples, insts, meta):
    """Return cells[row][channel] = (sample, period, effect, arg) or None."""
    cells = [[None] * CHANNELS for _ in range(rows)]
    per_channel = collections.defaultdict(lambda: collections.defaultdict(list))
    order = {}                                   # (row, channel) -> position of the drum hit
    for n, (row, kind, channel, track, pitch, context) in enumerate(events):
        per_channel[channel][row].append((kind, track, pitch))
        if kind == 'drum':
            order[(row, channel)] = n

    def put(row, channel, sample=0, period=0, effect=0, arg=0):
        cells[row % rows][channel] = (sample, period, effect, arg)

    # The OPL shares channel 7 between snare and hi-hat and channel 8 between
    # tom and cymbal.  The KSM writes the channel frequency for every hit,
    # which makes the emulator re-run cellfreq() on the other voice's cell:
    # that drops the x2 volume cellon() gave the snare and cymbal, and the
    # cymbal also loses its x16 pitch (four octaves down: gone for practical
    # purposes).  Hi-hat and tom rings are only retuned, which is ignored.
    interrupts = {
        MELODIC_CHANNELS + 1: (MELODIC_CHANNELS + 4, -6.02),    # snare cut by hi-hat
        MELODIC_CHANNELS + 3: (MELODIC_CHANNELS + 2, -np.inf),  # cymbal cut by tom
    }

    for channel in range(MELODIC_CHANNELS + 5):
        timeline = per_channel[channel]
        other, interrupt_db = interrupts.get(channel, (None, 0.0))
        active = None
        # Rows past the end wrap around: a release tail that is still sounding
        # at the loop point keeps fading over the start of the next iteration,
        # exactly as the OPL channel would, until the next note takes over.
        for row in range(2 * rows):
            wrapped = row >= rows
            if wrapped:
                existing = cells[row % rows][channel]
                if active is None or (existing and existing[0]):
                    break
            else:
                todo = timeline.get(row, ())
                for n, (kind, track, pitch) in enumerate(todo):
                    if kind == 'on' and ('off', track, pitch) in todo[n+1:]:
                        # Key-on and key-off in the same tick: the carrier never
                        # attacks, so the OPL just retunes and keeps releasing
                        # from the level it was at.  Silent unless something
                        # was still sounding here.
                        level = current_db(active, row) if active else -np.inf
                        if level == -np.inf:
                            active = None
                            continue
                        index, reference = assignment[('inst', instrument_key(insts, meta, track), pitch)]
                        sample = samples[index - 1]
                        put(row, channel, index, period_for(pitch, reference))
                        active = dict(sample=sample, onset=row, keyoff=row, base=level,
                                      last=sample.header_volume(), offset=0.0)
                        continue
                    if kind == 'drum':
                        index, reference = assignment[('drum', track, pitch)]
                        sample = samples[index - 1]
                        put(row, channel, index, period_for(pitch, reference))
                        # Rhythm voices never get a key-off: they follow their
                        # held decay until the next hit or until they fall silent.
                        active = dict(sample=sample, onset=row, keyoff=None, last=sample.header_volume(), offset=0.0)
                    elif kind == 'on':
                        index, reference = assignment[('inst', instrument_key(insts, meta, track), pitch)]
                        sample = samples[index - 1]
                        # The OPL carrier keeps its envelope amplitude across a
                        # key-on, so a note retriggered while the previous one
                        # is still sounding attacks from that level: start the
                        # sample (9xx) where its baked attack has reached it.
                        skip, offset = attack_offset(sample, current_db(active, row) if active else -np.inf)
                        put(row, channel, index, period_for(pitch, reference), 9 if offset else 0, offset)
                        active = dict(sample=sample, onset=row - skip, keyoff=None, last=sample.header_volume(), offset=0.0)
                    elif kind == 'off' and active and active['keyoff'] is None:
                        active['keyoff'] = row
                if active and other is not None and (row, other) in order and \
                        order[(row, other)] > order.get((row, channel), -1):
                    active['offset'] = interrupt_db      # cellfreq() is idempotent
            if active is None:
                continue
            sample = active['sample']
            baked = sample.baked_db(row - active['onset'])
            if baked is None:
                active = None                    # one-shot sample has finished
                continue
            target = current_db(active, row)
            if target == -np.inf:
                volume = 0
            else:
                volume = int(np.clip(round(sample.volume * 10 ** ((target - baked) / 20)), 0, 64))
            if volume != active['last']:
                existing = cells[row % rows][channel]
                if existing and existing[0]:
                    if existing[2]:              # 9xx already there: volume follows next row
                        continue
                    put(row, channel, existing[0], existing[1], 12, volume)
                else:
                    put(row, channel, 0, 0, 12, volume)
                active['last'] = volume
            if volume == 0:
                active = None
    return cells


def current_db(active, row):
    """Absolute level (dB, render units) the OPL voice modelled by `active`
    has at `row`, or -inf once it is silent."""
    sample, k = active['sample'], row - active['onset']
    if sample.baked_db(k) is None:
        return -np.inf
    if active['keyoff'] is None:
        target = sample.held_db(k)
    else:
        base = active.get('base')
        if base is None:
            base = sample.held_db(active['keyoff'] - active['onset'])
        target = base + sample.release_db(row - active['keyoff'])
    return target + active['offset']


def attack_offset(sample, level):
    """(rows skipped, 9xx argument) to start `sample` where its attack has
    reached `level` dB; (0, 0) when the attack is already that quiet or the
    sample would start at its beginning anyway."""
    if level == -np.inf:
        return 0, 0
    peak = attack_end(np.array(sample.baked))
    row = next((r for r in range(peak + 1) if sample.baked[r] >= level), peak)
    if row == 0:
        return 0, 0
    offset = min(int(row_edges(row)[-1]) // 256, 255)
    if offset * 256 >= len(sample.data):
        return 0, 0
    return int(round(offset * 256 / ROW_SAMPLES)), offset


def pack_patterns(cells, rows):
    """Split the song into equal bars ending in a D00 pattern break and pick
    the bar length whose repeated bars dedupe into the fewest patterns."""
    row_bytes = [b''.join(make_event(*c) if c else bytes(4) for c in cells[r]) for r in range(rows)]
    stride = CONTROL_CHANNEL * 4
    broken = [rb[:stride] + make_event(effect=13) + rb[stride+4:] for rb in row_bytes]
    best = None
    for bar in range(64, MIN_BAR - 1, -1):
        count = math.ceil(rows / bar)
        if count > 128:
            break
        patterns, orders, seen = [], [], {}
        for c in range(count):
            start = c * bar
            end = min(start + bar, rows)
            body = b''.join(row_bytes[start:end-1])
            body += row_bytes[end-1] if end == rows else broken[end-1]
            data = body.ljust(64 * CHANNELS * 4, b'\0')
            if data not in seen:
                seen[data] = len(patterns)
                patterns.append(data)
            orders.append(seen[data])
        if best is None or len(patterns) < len(best[0]):
            best = (patterns, orders, bar)
    if best is None:
        raise ValueError('song exceeds 128 patterns')
    return best


def make_module(name, meta, notes, renderer, folder, insts, cache):
    rows, events = sequence(meta, notes)
    if rows > 128 * 64:
        raise ValueError('song exceeds 128 patterns')
    specs, assignment = plan_samples(meta, events, insts)
    samples = []
    for spec in specs:
        if spec not in cache:
            if spec[0] == 'inst':
                _, key, reference = spec
                label = next(n for n, v in insts if v[0] == key[0] and v[2:] == key[2:]) or 'FM'
                cache[spec] = analyse_melodic(renderer, f'{label[:17]} {reference:02d}', key, reference)
            else:
                _, section, vols, track, pitch, context = spec
                label = ('BD', 'SD', 'TT', 'CY', 'HH')[track - 11]
                cache[spec] = analyse_drum(renderer, f'{label} {insts[meta[track]][0][:16]} {pitch:02d}',
                                           folder, name, track, pitch, context)
        samples.append(cache[spec])
    cells = build_cells(rows, events, assignment, samples, insts, meta)
    if cells[0][CONTROL_CHANNEL] or cells[rows-1][CONTROL_CHANNEL]:
        raise ValueError('control channel in use')
    cells[0][CONTROL_CHANNEL] = (0, 0, 15, 1)                       # F01 speed
    # F96 tempo goes on any other row 0 cell that carries no effect.
    for channel in range(CHANNELS - 2, -1, -1):
        cell = cells[0][channel]
        if not cell or not cell[2]:
            cells[0][channel] = (cell[0], cell[1], 15, 150) if cell else (0, 0, 15, 150)
            break
    else:
        raise ValueError('no free cell for the tempo command')
    cells[rows-1][CONTROL_CHANNEL] = (0, 0, 11, 0)                  # B00 loop

    patterns, orders, bar = pack_patterns(cells, rows)

    out = bytearray(name.encode().ljust(20, b'\0'))
    for i in range(SAMPLE_SLOTS):
        if i < len(samples):
            s = samples[i]
            header = bytearray(s.header())
            header[3] = s.header_volume()
            out += s.name.encode('ascii', 'replace')[:22].ljust(22, b'\0') + header
        else:
            out += bytes(22) + struct.pack('>HBBHH', 0, 0, 0, 0, 1)
    out += bytes([len(orders), 127]) + bytes(orders).ljust(128, b'\0') + b'12CH'
    for p in patterns:
        out += p
    for s in samples:
        out += s.data.tobytes()
    stats = dict(seconds=rows / ROWS_PER_SECOND, samples=len(samples), patterns=len(patterns),
                 orders=len(orders), bar=bar, sample_bytes=sum(len(s.data) for s in samples),
                 loudest=max(s.volume for s in samples))
    return bytes(out), stats


def organize_assets(report):
    """Keep one MOD for identical tracks across the four game versions."""
    versions = ('Ken1.0', 'Ken1.1', 'Ken2.0', 'Ken2.1')
    by_name = collections.defaultdict(dict)
    for item in report:
        by_name[item['track']][item['version']] = item
    desired = {}
    for name, version_map in by_name.items():
        # game_data_path() looks in the version folder, then shared/<family>,
        # then shared: the most common rendition lives in shared and any
        # other rendition overrides it for its family or single version.
        groups = collections.defaultdict(list)
        for version, item in version_map.items():
            groups[item['sha256']].append(version)
        ranked = sorted(groups.values(), key=lambda g: (-len(g), g))
        for n, group in enumerate(ranked):
            spans = {v[:4] for v in group}
            if n == 0 and len(spans) == 2:
                target = ROOT / 'gamedata/shared/mods' / (name + '.mod')
            elif len(group) == 2 and len(spans) == 1:
                target = ROOT / 'gamedata/shared' / group[0][:4] / 'mods' / (name + '.mod')
            else:
                target = None
            for version in group:
                desired[(version, name)] = target or ROOT / 'gamedata' / version / 'mods' / (name + '.mod')
    payload = {}
    for item in report:
        key = (item['version'], item['track'])
        payload[key] = (ROOT / 'gamedata' / key[0] / 'mods' / (key[1] + '.mod')).read_bytes()
    for item in report:
        key = (item['version'], item['track'])
        target = desired[key]
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(payload[key])
        item['path'] = str(target.relative_to(ROOT))
    keep = set(desired.values())
    for folder in list((ROOT / 'gamedata').glob('Ken*/mods')) + list((ROOT / 'gamedata/shared').glob('**/mods')):
        if folder.is_dir():
            for path in folder.glob('*.mod'):
                if path not in keep:
                    path.unlink()
    return len(keep), sum(p.stat().st_size for p in keep)


def main():
    report = []
    modules = {}
    cache = {}
    with tempfile.TemporaryDirectory() as tmp:
        renderer = build_renderer(Path(tmp))
        for folder in sorted((ROOT / 'gamedata').iterdir()):
            if not folder.is_dir():
                continue
            archive = next((p for p in folder.iterdir() if p.name.lower() == 'songs.kzp'), None)
            if not archive:
                continue
            insts = load_instruments(folder)
            dest = folder / 'mods'
            dest.mkdir(exist_ok=True)
            for name, meta, notes in archive_songs(archive):
                signature = hashlib.sha256(b''.join(v for _, v in insts) + meta +
                                           struct.pack('<%dI' % len(notes), *notes)).digest()
                if signature in modules:
                    old, stats = modules[signature]
                    mod = name.encode().ljust(20, b'\0') + old[20:]
                else:
                    mod, stats = make_module(name, meta, notes, renderer, folder, insts, cache)
                    modules[signature] = (mod, stats)
                (dest / (name + '.mod')).write_bytes(mod)
                report.append(dict(version=folder.name, track=name, bytes=len(mod),
                                   sha256=hashlib.sha256(mod).hexdigest(), **stats))
            print(folder.name, 'converted', flush=True)
    physical, bytes_used = organize_assets(report)
    (ROOT / 'tools/modmusic/manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    loudest = max(x['loudest'] for x in report)
    print(f'{len(report)} tracks; {physical} MOD files; {bytes_used} bytes; '
          f'largest {max(x["bytes"] for x in report)} bytes; loudest sample volume {loudest:.1f}')
    if loudest > 64.5:
        print('WARNING: raise PEAK_REFERENCE; samples were clipped to volume 64')


if __name__ == '__main__':
    main()
