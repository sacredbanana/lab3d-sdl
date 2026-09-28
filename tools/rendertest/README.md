# Renderer and ray caster differential tests

The Amiga port used to do its rasterising and its visibility work in double
precision. On a 68020 with no FPU that costs tens of soft float library calls
per pixel — `softtri` alone ran 26 of them, two being `__divdf3` — and two
`tan()` calls per ray on top. Both were converted to fixed point; these tests
exist to prove the conversions still draw the same picture.

    tools/rendertest/run.sh

Needs only `cc` and `python3`. Nothing here is built by the Amiga makefile.

## How it works

`extract.py` lifts the functions under test straight out of
`src/amiga/render_soft.c` and `src/graphx.c` by brace matching, so the tests
always exercise the live code and a rename fails the build rather than
silently passing. It also lifts the constants they need — a block marked
`rendertest:begin-fixedpoint` in `graphx.c`, and the tile numbers the ray
caster branches on, straight out of `include/lab3d.h` — so nothing here can
drift from the real definitions. `reference.c` and `raycast_reference.c` hold
frozen copies of the same functions as they were *before* the conversion, in
their original double precision form. `harness.c` runs both over the same
randomised input and compares the output.

`shim.h` is the handful of globals and constants the inner loops touch — no
AmigaOS headers are involved.

## What the tests actually assert

A fixed point rewrite is never bit exact, so the tests do not ask for that.
They assert that every disagreement sits on a *decision boundary* — a triangle
edge, a texel boundary, a depth tie — where the two arithmetics were always
free to round either way, and that the overall rate stays under a fraction of
a percent. A real bug moves pixels nowhere near a boundary, which is what the
`differing pixels away from any decision boundary: 0` line catches.

Current behaviour, for reference:

| test | what it compares | result |
|---|---|---|
| `softtri` | every pixel | ~0.0017% differ, zero away from a boundary |
| `floor` | every pixel | ~0.07% differ |
| `wall` | every pixel, plus the depth buffer | ~0.007% differ; depth within 0.35% relative, 8 absolute |
| `castray` | one ray at a time, same angle both sides | same wall, face and texture; hit point within 0.007 cells |
| `raycast` | the whole visibility pass | walls lost are at most ~0.1 px wide on a 360 px view |

These tests have already earned their keep twice: they caught the two
deliberate changes below, and they caught a precision cliff in `softtri` where
the fixed point scale was being sized from the texture coordinates at the
*corners of the bounding box*. On a sliver triangle those corners lie far
outside the triangle, so the values there blow up, the scale collapses and
precision is lost in the middle of the triangle where it is the only thing
that matters. It showed up as two wrong pixels in 137 million. Sizing the
scale from the step instead fixed it and made the common case 3x better too.

## What the ray caster tests assert, and what they only report

`castray` compares single rays at the same angle. It asserts that both
versions agree on which wall, which face and which texture, and that the hit
point lands within a fiftieth of a cell.

It does *not* assert that every ray takes the same path, because about one ray
in ten thousand does not. Those are rays passing within rounding distance of a
cell corner, where the two grid walks are all but tied and the last bit of the
slope decides which steps first — the double version has fifty-odd mantissa
bits to break the tie with and 16.16 has sixteen. The ray then slips past the
corner and runs on for several cells. This cannot be asserted away at this
precision and is not worth chasing: an extra wall in the list is drawn and
then correctly hidden by the nearer walls in front of it, and a wall *lost*
this way shows up in the `raycast` test.

`raycast` is where the consequence is measured. It runs the whole visibility
pass and compares the set of walls found, and for each wall the rewrite lost
it measures how wide that wall would actually have been on screen — by
sweeping the frustum with the reference caster and counting the angles that
reach it. The naive measure, the angle the wall subtends with nothing in front
of it, is badly wrong here and was worth getting right: a full cell wall seen
from a thousandth of a cell off its own plane subtends a huge unoccluded angle
while being a fiftieth of a pixel wide. Every wall lost so far has been one of
those grazing slivers, 0.02 to 0.10 px on a 360 px view.

## Two deliberate behaviour changes

Both were found *because* these tests flagged them, and both are accounted for
rather than hidden:

1. **`softtri` bounding box.** The rewrite rounds the x bounds outward with
   `ceil()`, matching what the y bounds always did. The original's `(int)maxx`
   dropped the last partial column, skipping pixels genuinely inside the
   triangle. The harness classifies differences in those columns separately.

2. **Floor decal texel lookup.** The rewrite floors the world coordinate where
   the original truncated toward zero, which had made the texel straddling the
   middle of a decal one world unit wider than the rest. `reference.c` is
   adjusted on that one line — the only such adjustment in the file, and
   commented as one — so the test measures fixed point error rather than the
   intended change. Left unadjusted it accounted for 97.6% of the difference.

## Adding a test

Add the function to `WANTED` in `extract.py`, freeze the old version in a
`*_reference.c`, and add a `test_*` to `harness.c`. If the function needs
constants or file-local state, wrap them in the source with
`rendertest:begin-<tag>` / `rendertest:end-<tag>` and add the tag to `BLOCKS`
rather than copying them here.

## On a 68k

`run.sh` also builds the harness with `-D__HAVE_68881__`, which picks the
paths the FPU builds take, and runs every test on it once.

Two scripts run the same code on an emulated 68020 under vamos (from
amitools: `pip install amitools machine68k==0.3.0`; newer machine68k
releases break vamos; point `VAMOS` at the binary):

- `bench68k.sh [bench] [trials] [git-rev]` times the live renderer, and
  optionally the one at a git revision, in 68020 cycles. `benchwall` draws
  the wall test's geometry; it reports cycles, the same without the pixels,
  and soft float calls by operation. libnix hands every double operation of
  the FPU-less build to mathieeedoubbas.library, which vamos runs on the
  host, so those calls are counted rather than timed, and "est" weighs them
  at rough costs for the ROM code. The cycle count ignores caches and chip
  RAM wait states: compare versions with it, do not read it as a frame time.
- The harness itself runs under vamos too, which is how the assembler span
  loops, the `divu.l` and the bit level helpers are checked on the real
  instructions: build it with `m68k-amigaos-gcc -m68020 -msoft-float` and
  run `vamos -C 68020 -m 8192 harness wall` (or `span`, `softtri`, ...).

`span` compares `draw_span()`'s 68k assembler loops with the C loops they
replaced, pixel for pixel; off the 68k it compares the C with itself.

The wall test uses whole world units for the camera and the walls, as the
game does: the camera transform is exact integer arithmetic on that basis.

