# Instrument-based MOD soundtrack

Run `python3 tools/modmusic/generate.py` with Python 3, NumPy and a host C
compiler. It reads the original KSM song archives and `insts.dat`, renders
every instrument with the game's own `adlibemu.c` and instrument setup code,
and writes 121 playable MOD tracks plus a manifest. No external sound library
or instrument bank is used. Identical tracks across versions are deduplicated
into `gamedata/shared`; keep the entire `gamedata` drawer together.

## How a song is converted

The converter mirrors `ksmhandler()` tick for tick: the same 240 Hz clock,
per-track quantisation, channel allocation (including its unsigned wrap-around
quirk) and note-off matching, so every note lands on the OPL channel the game
would have used. MOD channels 0-5 are those six melodic channels, 6-10 are the
bass drum, snare, tom, cymbal and hi-hat, and channel 11 only carries speed,
tempo, pattern-break and loop commands. Rows are 4 KSM ticks (60 rows per
second, speed 1 at tempo 150), which is the grid the quantised songs sit on.

Every distinct instrument (with its track volume) is rendered once per
pitch range of at most an octave: the attack and early decay are stored as-is,
then the sustained waveform is looped over a whole number of periods with a
crossfade, so a sample is typically 3-8 KB. Voices that die within half a
second become one-shot samples. The held decay and key-off release of each
instrument are measured in dB per row and reproduced with `Cxx` volume
commands, so a fast-decaying piano, a sustaining organ and a long release
tail all behave as on the OPL. Rhythm voices are rendered from the song's own
drum setup, with the emulator's quirks reproduced: the snare and cymbal take
their pitch and rates from melodic channels 0 and 1, a hi-hat hit halves a
ringing snare and a tom hit kills a ringing cymbal. A note retriggered while
its predecessor is still sounding resumes the attack from that level via a
`9xx` sample offset, matching the OPL carrier's un-reset envelope.

Loudness is calibrated so that the MOD player at the default music volume
matches the Adlib emulator to within about 1 dB. Files are 90-285 KB, 8.6 MB in
total for all 63 physical MODs; a track needs at most about 290 KB of RAM.

## Checking the result

`python3 tools/modmusic/test.py` builds the player and the real game mixer
under ASan/UBSan and exercises parsing, looping, restart, volume, mute and SFX
mixing in mono and stereo at 11025, 22050 and 44100 Hz, rejects malformed
files, and finally renders every shipped module with the game's player next to
the Adlib emulator playing the same KSM song, requiring a log-spectrogram
correlation above 0.9 and a level within 2 dB (currently the worst module
scores 0.949).

Format references: [MOD sample and pattern layout](https://www.eblong.com/zarf/blorb/mod-spec.txt)
and [OpenMPT's multichannel MOD notes](https://wiki.openmpt.org/Manual%3A_Module_formats).
