#!/usr/bin/env bash
#
# Build and run the renderer differential tests.
#
#   tools/rendertest/run.sh            all three tests, three seeds each
#   tools/rendertest/run.sh softtri    just one
#
# Exits non-zero if any test exceeds its threshold.
set -u

cd "$(dirname "$0")/../.."
HERE=tools/rendertest
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

mkdir -p "$HERE/generated"
python3 "$HERE/extract.py" . "$HERE/generated" || exit 1

${CC:-cc} -O2 -Wall -Wextra -Wno-unused-parameter -I"$HERE" \
    -o "$OUT/harness" "$HERE/harness.c" -lm || exit 1

TESTS=${*:-softtri floor wall span castray raycast}
SEEDS=${SEEDS:-1 7 31337}
fail=0

for t in $TESTS; do
    for s in $SEEDS; do
        echo "--- $t (seed $s)"
        "$OUT/harness" "$t" 0 "$s" || fail=1
    done
done

echo
if [ "$fail" = 0 ]; then echo "all renderer differential tests passed"
else                     echo "RENDERER DIFFERENTIAL TESTS FAILED"; fi
exit $fail
