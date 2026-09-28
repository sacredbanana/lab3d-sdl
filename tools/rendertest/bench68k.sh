#!/usr/bin/env bash
#
# Time the renderer's live code on an emulated 68020.
#
#   tools/rendertest/bench68k.sh [bench] [trials] [git-rev]
#
# Builds the rendertest harness with the Amiga cross compiler (the same
# Docker image build-amiga.sh uses) for the plain 68020 build, and runs it
# under vamos from amitools, whose 68020 core counts cycles.  With a git
# revision the renderer as it was there is built and timed too.
#
# Two numbers come out per version, both per trial and with the harness's own
# setup subtracted:
#
#   cycles    68020 cycles of everything but floating point.  The count
#             ignores caches and chip RAM wait states, so compare versions
#             with it rather than reading it as a frame time.
#   IEEE      soft float calls, by operation.  libnix sends every double
#             operation on an FPU-less build to mathieeedoubbas.library,
#             which on a real 020 is 68k code in ROM but under vamos is
#             emulated on the host, so those calls are counted, not timed.
#             "est" weighs them at rough costs for the ROM code on a 68020 -
#             an estimate, not a measurement.
#
# Needs VAMOS pointing at a vamos binary; amitools 0.8.1 works with
#   pip install amitools machine68k==0.3.0
# (newer machine68k releases break it).
set -u

cd "$(dirname "$0")/../.."
HERE=tools/rendertest
BENCH=${1:-benchwall}
TRIALS=${2:-400}
REV=${3:-}
VAMOS=${VAMOS:-vamos}
CFLAGS68K=${CFLAGS68K:--m68020 -msoft-float}
IMAGE=sacredbanana/amiga-compiler:m68k-amigaos
OUT=$(mktemp -d "${TMPDIR:-/tmp}/bench68k.XXXXXX")
trap 'rm -rf "$OUT"' EXIT

build() {   # build <root> <name>: extract from <root>, compile to $OUT/<name>
    mkdir -p "$OUT/$2/generated"
    cp "$HERE"/harness.c "$HERE"/shim.h "$HERE"/*reference.c "$OUT/$2/"
    python3 "$1/$HERE/extract.py" "$1" "$OUT/$2/generated" >/dev/null || exit 1
    docker run --rm -v "$OUT/$2":/work -w /work "$IMAGE" \
        m68k-amigaos-gcc -noixemul -O2 -fomit-frame-pointer $CFLAGS68K \
        -I. -o harness harness.c -lm || exit 1
}

# run <name> <mode>: prints "cycles add sub mul div cmp conv"
run() {
    (cd "$OUT/$1" && "$VAMOS" -C 68020 -m 8192 --profile --profile-dump \
        --profile-libs mathieeedoubbas.library \
        -l main:info,prof:info harness "$BENCH-$2" "$TRIALS" 2>&1) | awk '
        /total cycles:/ { c = $NF }
        / calls / {
            n = $(NF-7); f = $(NF-8)
            if (f ~ /IEEEDPAdd/)            a += n
            else if (f ~ /IEEEDPSub/)       s += n
            else if (f ~ /IEEEDPMul/)       m += n
            else if (f ~ /IEEEDPDiv/)       d += n
            else if (f ~ /IEEEDP(Cmp|Tst)/) k += n
            else if (f ~ /IEEEDP(Fix|Flt|Floor|Ceil|Neg|Abs)/) v += n
        }
        END { printf "%d %d %d %d %d %d %d\n", c, a, s, m, d, k, v }'
}

report() {  # report <name> <mode> <label>
    set -- "$1" "$2" "$3" $(run "$1" none) $(run "$1" "$2")
    awk -v t="$TRIALS" -v label="$3" -v c0="$4" -v a0="$5" -v s0="$6" \
        -v m0="$7" -v d0="$8" -v k0="$9" -v v0="${10}" \
        -v c="${11}" -v a="${12}" -v s="${13}" -v m="${14}" -v d="${15}" \
        -v k="${16}" -v v="${17}" 'BEGIN {
        a = (a - a0) / t; s = (s - s0) / t; m = (m - m0) / t
        d = (d - d0) / t; k = (k - k0) / t; v = (v - v0) / t
        cyc = (c - c0) / t
        # Rough 68020 costs of the ROM routines, in cycles.
        est = cyc + (a + s) * 300 + m * 700 + d * 1600 + k * 80 + v * 200
        printf "%-10s %9.0f cycles  IEEE add %5.1f sub %5.1f mul %5.1f div %4.1f cmp %5.1f conv %5.1f  est %9.0f\n",
               label, cyc, a, s, m, d, k, v, est
    }'
}

build . cur
report cur live  "current"
report cur depth "  no pixels"
report cur ref   "original"

if [ -n "$REV" ]; then
    mkdir -p "$OUT/rev"
    git archive "$REV" src include tools/rendertest | tar -x -C "$OUT/rev"
    # The bench modes live in the current harness, which build() copies in.
    build "$OUT/rev" old
    report old live "$REV"
fi
