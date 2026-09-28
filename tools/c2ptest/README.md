# Chunky to planar correctness test

    tools/c2ptest/run.sh

Needs only `cc`. Nothing here is built by the Amiga makefile.

`harness.c` includes `src/amiga/amiga_c2p.c` as it stands, in its host build
mode, and checks it against the plain definition of the planar layout — bit
`7 - (x & 7)` of byte `x >> 3` of plane `p` holds bit `p` of pixel `x`,
set one bit at a time. There is no rounding in a conversion, so the test is
exact: every byte of every plane has to match, including the bytes around the
target rectangle, which catches a stray write as surely as a wrong one.

Each trial picks a random depth (1 to 8), pen map or none, interleaved or
plain bitmap, plane alignment, row width and rectangle. That puts the
longword aligning head, the 32 pixel merge blocks and the tail through every
combination of offsets. 20000 trials per seed, three seeds.

The test was checked against planted bugs: swapping two plane outputs of the
merge path fails about 15% of trials, and clearing one bit of one merge mask
fails about 27%.
