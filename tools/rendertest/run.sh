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
# The FPU builds (020fpu, 040, 060) take a few different paths - fxround()
# in doubles rather than on the bits - so that variant is built and checked
# too, on one seed.
${CC:-cc} -O2 -Wall -Wextra -Wno-unused-parameter -I"$HERE" -D__HAVE_68881__ \
    -o "$OUT/harness_fpu" "$HERE/harness.c" -lm || exit 1

TESTS=${*:-softtri floor wall span castray raycast}
SEEDS=${SEEDS:-1 7 31337}
fail=0

for t in $TESTS; do
    for s in $SEEDS; do
        echo "--- $t (seed $s)"
        "$OUT/harness" "$t" 0 "$s" || fail=1
    done
done

for t in $TESTS; do
    echo "--- $t (FPU build paths, seed 1)"
    "$OUT/harness_fpu" "$t" 0 1 || fail=1
done

echo
if [ "$fail" = 0 ]; then echo "all renderer differential tests passed"
else                     echo "RENDERER DIFFERENTIAL TESTS FAILED"; fi
exit $fail
