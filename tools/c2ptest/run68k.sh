#!/usr/bin/env bash
#
# The chunky to planar test on an emulated 68020, so the assembler paths in
# src/amiga/amiga_c2p.c run for real rather than the C that stands in for
# them on the build host.  Three builds:
#
#   amiga     PLATFORM_AMIGA, as the game builds it: native word loads, CPU only
#   cpu       the host build's code, compiled for the 68k
#   blitter   AMIGA_BLITTER_C2P against the harness's blitter model - which
#             runs the assembler staging loop the real blitter build uses
#
# Then a timing: whole 320x256 frames, cycles as the 68020 core counts them.
#
# Needs VAMOS (see tools/rendertest/bench68k.sh for how to get it).
set -u

cd "$(dirname "$0")/../.."
VAMOS=${VAMOS:-vamos}
IMAGE=sacredbanana/amiga-compiler:m68k-amigaos
OUT=$(mktemp -d "${TMPDIR:-/tmp}/c2p68k.XXXXXX")
trap 'rm -rf "$OUT"' EXIT
TRIALS=${TRIALS:-3000}
fail=0

mkdir -p "$OUT/src/amiga" "$OUT/include/amiga" "$OUT/tools/c2ptest"
cp src/amiga/amiga_c2p.c "$OUT/src/amiga/"
cp include/amiga/amiga_c2p.h "$OUT/include/amiga/"
cp tools/c2ptest/harness.c "$OUT/tools/c2ptest/"

build() {   # build <name> <flags...>
    local name=$1; shift
    docker run --rm -v "$OUT":/work -w /work/tools/c2ptest "$IMAGE" \
        m68k-amigaos-gcc -noixemul -O2 -fomit-frame-pointer -m68020 -msoft-float \
        -I/work/include -I/work/include/amiga "$@" -o "/work/$name" harness.c -lm || exit 1
}

run() { (cd "$OUT" && "$VAMOS" -C 68020 -m 8192 "$@" 2>&1); }

build amiga -DPLATFORM_AMIGA
build cpu
build blitter -DAMIGA_BLITTER_C2P

for v in amiga cpu blitter; do
    for s in 1 7; do
        echo "--- $v (seed $s)"
        out=$(run "$v" "$TRIALS" "$s" | tail -1)
        echo "$out"
        case "$out" in *" 0 failed"*) ;; *) fail=1 ;; esac
    done
done

c0=$(run -l main:info amiga bench 0 | sed -n 's/.*total cycles: //p')
c4=$(run -l main:info amiga bench 4 | sed -n 's/.*total cycles: //p')
echo
echo "320x256x8 frame: $(( (c4 - c0) / 4 )) cycles on the 68020 core"

echo
if [ "$fail" = 0 ]; then echo "c2p 68k test passed"
else                     echo "C2P 68K TEST FAILED"; fi
exit $fail
