# Sampled MOD soundtrack

Run `python3 tools/modmusic/generate.py` from a host with Python 3 and a C
compiler. It generates all 121 files in `gamedata/Ken*/mods` and a SHA-256
manifest. No network access, downloaded instruments or Python packages are
needed. Regenerate after changing the original music sequencer or emulator.

The renderer extracts `loadmusic`, `musicon`, `ksmhandler`, `setinst` and
`outdata` from the game and links the game's `adlibemu.c`. It renders one warmup
loop and records the next loop at 22050 Hz, at the default music volume.
KSM voice allocation, quantisation, FM patches and rhythm mode therefore use
the original code. Source archives and instrument banks are left untouched.

The converter resamples that complete mix to PAL period 214 (16574.3 Hz),
quantises to signed 8-bit PCM, and stores consecutive sections of at most 192
rows as unlooped samples in an M.K. four-channel MOD. F01/F96 produce 60 rows
per second; B00 loops the sequence. The loop duration is rounded to the nearest
row and the PCM is stretched to fit (at most 8.34 ms per loop). This is a
sampled rendition, **not an instrument-by-instrument tracker arrangement**.
It trades disk space for fidelity to the original arrangement and very cheap
playback. It does not preserve random Adlib instruments or stereo panning.
The two-byte silent sample prefix follows the ProTracker convention.

The in-game player uses integer interpolation and ordinary RAM. It mixes the
MOD into the existing audio callback before effects are added and clipped,
so Paula/AHI ownership does not change. Unsupported effects, finetuning and
sample loops are rejected explicitly; this is a player for these assets, not
a general-purpose tracker replay library. Music is centered in stereo output.

Run `python3 tools/modmusic/test.py` for ASan/UBSan playback and real-game mixer
tests: every track, repeated loops, restarts, volume, mute, cleanup, malformed
files, and effects mixed at 11025/22050/44100 Hz in mono/stereo. These tests do
not establish audible quality or frame rate on real Amiga hardware.

Format reference: [ProTracker 2.3A format notes](https://www.eblong.com/zarf/blorb/mod-spec.txt).
