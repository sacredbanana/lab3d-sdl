#!/usr/bin/env bash
#
# Build the AmigaOS 3.x versions of Ken's Labyrinth using the same Docker
# image AmigaGPT uses.  Produces four executables in dist/amiga:
#
#   Kens-Labyrinth.020      68020, no FPU
#   Kens-Labyrinth.020fpu   68020 with a 68881/68882
#   Kens-Labyrinth.040      68040
#   Kens-Labyrinth.060      68060
#
# Set CLEAN=1 to wipe the build directory first, DEBUG=1 for a debug build.
# Pass variant names as arguments to build only those, e.g. ./build-amiga.sh 060

set -e

IMAGE=sacredbanana/amiga-compiler:m68k-amigaos
TARGETS="$*"

if [[ "${CLEAN}" == "1" ]]; then
	docker run --rm \
		-v "${PWD}":/work \
		-e USER=$(id -u) -e GROUP=$(id -g) \
		"${IMAGE}" make -f Makefile.Amiga clean
fi

docker run --rm \
	-v "${PWD}":/work \
	-e USER=$(id -u) -e GROUP=$(id -g) -e DEBUG="${DEBUG}" \
	"${IMAGE}" make -f Makefile.Amiga -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" ${TARGETS}

# ---------------------------------------------------------------------------
# Assemble a drawer that can be copied straight onto an Amiga.
# ---------------------------------------------------------------------------
DIST=dist/amiga/Kens-Labyrinth

if ls dist/amiga/Kens-Labyrinth.* >/dev/null 2>&1; then
	rm -rf "${DIST}"
	mkdir -p "${DIST}"
	cp dist/amiga/Kens-Labyrinth.* "${DIST}/"
	cp -R gamedata "${DIST}/gamedata"
	cp AmigaREADME.txt "${DIST}/README" 2>/dev/null || true
fi

echo
echo "Built:"
ls -l dist/amiga/ 2>/dev/null || true
