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

${CC:-cc} -O2 -Wall -Wextra -o "$OUT/harness" "$HERE/harness.c" || exit 1

SEEDS=${SEEDS:-1 7 31337}
fail=0

for s in $SEEDS; do
    echo "--- c2p (seed $s)"
    "$OUT/harness" 0 "$s" || fail=1
done

echo
if [ "$fail" = 0 ]; then echo "c2p test passed"
else                     echo "C2P TEST FAILED"; fi
exit $fail
