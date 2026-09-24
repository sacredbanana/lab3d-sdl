# Renderer differential tests

The Amiga software renderer used to rasterise in double precision. On a 68020
with no FPU that costs tens of soft float library calls per pixel — `softtri`
alone ran 26 of them, two being `__divdf3`. The inner loops in
`src/amiga/render_soft.c` were converted to fixed point; these tests exist to
prove the conversion still draws the same picture.

    tools/rendertest/run.sh

Needs only `cc` and `python3`. Nothing here is built by the Amiga makefile.

## How it works

`extract.py` lifts the functions under test straight out of
`src/amiga/render_soft.c` by brace matching, so the tests always exercise the
live code and a rename fails the build rather than silently passing.
`reference.c` holds frozen copies of the same functions as they were *before*
the conversion, in their original double precision form. `harness.c` runs both
over the same randomised geometry and compares every pixel.

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

| test | pixels differing | note |
|---|---|---|
| `softtri` | ~0.0017% | zero away from a boundary |
| `floor` | ~0.07% | |
| `wall` | ~0.007% | depth error max 0.35% relative, 8 absolute |

These tests have already earned their keep twice: they caught the two
deliberate changes below, and they caught a precision cliff in `softtri` where
the fixed point scale was being sized from the texture coordinates at the
*corners of the bounding box*. On a sliver triangle those corners lie far
outside the triangle, so the values there blow up, the scale collapses and
precision is lost in the middle of the triangle where it is the only thing
that matters. It showed up as two wrong pixels in 137 million. Sizing the
scale from the step instead fixed it and made the common case 3x better too.

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

The raycaster in `src/graphx.c` is the other half of the floating point work
and the next thing to convert. It is testable the same way, and more easily:
`castray()` has no gameplay side effects — it writes only `hitpointx/y`,
`tempbuf` (the automap) and the wall list — so a fixed point version can be
compared against the double version wall-for-wall without any risk to
recorded demo playback.

Add the function to `WANTED` in `extract.py`, freeze the old version in
`reference.c`, and add a `test_*` to `harness.c`.
