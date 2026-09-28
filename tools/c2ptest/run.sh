#!/usr/bin/env bash
#
# Build and run the chunky to planar correctness test.
#
#   tools/c2ptest/run.sh
#
# Exits non-zero if any conversion differs from the reference.
set -u

cd "$(dirname "$0")/../.."
HERE=tools/c2ptest
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

# Twice: the CPU-only conversion every build uses, and the plain 68020
# build's blitter assisted one against a model of the blitter.
${CC:-cc} -O2 -Wall -Wextra -Iinclude \
    -o "$OUT/cpu" "$HERE/harness.c" || exit 1
${CC:-cc} -O2 -Wall -Wextra -Iinclude -DAMIGA_BLITTER_C2P \
    -o "$OUT/blitter" "$HERE/harness.c" || exit 1

SEEDS=${SEEDS:-1 7 31337}
fail=0

for v in cpu blitter; do
    for s in $SEEDS; do
        echo "--- $v (seed $s)"
        "$OUT/$v" 0 "$s" || fail=1
    done
done

echo
if [ "$fail" = 0 ]; then echo "c2p test passed"
else                     echo "C2P TEST FAILED"; fi
exit $fail
