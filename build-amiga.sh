#!/usr/bin/env bash
#
# Build the AmigaOS 3.x versions of Ken's Labyrinth using the same Docker
# image AmigaGPT uses.  Produces four executables and an LHA package in dist/amiga:
#
#   Kens-Labyrinth.020      68020, no FPU
#   Kens-Labyrinth.020fpu   68020 with a 68881/68882
#   Kens-Labyrinth.040      68040
#   Kens-Labyrinth.060      68060
#   Kens-Labyrinth.lha      executables, Amiga game data, readme, icons and
#                           the Installer script
#   Kens-Labyrinth.readme   the Aminet readme, uploaded beside the archive
#
# Set CLEAN=1 to wipe the build directory first, DEBUG=1 for a debug build.
# Pass variant names as arguments to build only those, e.g. ./build-amiga.sh 060

set -e

IMAGE=sacredbanana/amiga-compiler:m68k-amigaos
TARGETS="$*"

# The image's entrypoint does not drop root itself, so run as the calling user.
# Otherwise, on a Linux host (e.g. CI), dist/ and build/ end up owned by root
# and the packaging steps below cannot write to them.
if [[ "${CLEAN}" == "1" ]]; then
	docker run --rm \
		-v "${PWD}":/work \
		--user "$(id -u):$(id -g)" \
		"${IMAGE}" make -f Makefile.Amiga clean
fi

docker run --rm \
	-v "${PWD}":/work \
	--user "$(id -u):$(id -g)" -e DEBUG="${DEBUG}" \
	"${IMAGE}" make -f Makefile.Amiga -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)" ${TARGETS}

# ---------------------------------------------------------------------------
# Assemble a drawer that can be copied straight onto an Amiga, then archive it.
# ---------------------------------------------------------------------------
DIST=dist/amiga/Kens-Labyrinth
ARCHIVE=dist/amiga/Kens-Labyrinth.lha

if ls dist/amiga/Kens-Labyrinth.0* >/dev/null 2>&1; then
	rm -rf "${DIST}"
	mkdir -p "${DIST}"
	cp dist/amiga/Kens-Labyrinth.0* "${DIST}/"
	cp -R gamedata "${DIST}/gamedata"
	cp Kens-Labyrinth.readme "${DIST}/"
	cp Kens-Labyrinth.readme dist/amiga/
	# Workbench icons, made by tools/amigaicons/mkicons.py.  The drawer's own
	# icon sits beside the drawer, so it goes into the archive next to it.
	for exe in "${DIST}"/Kens-Labyrinth.0*; do
		cp "icons/amiga/$(basename "${exe}").info" "${DIST}/"
	done
	cp icons/amiga/Kens-Labyrinth.readme.info "${DIST}/"
	# Installer script, run from inside the drawer; see installer/amiga.
	cp installer/amiga/Install "${DIST}/"
	cp icons/amiga/Install.info "${DIST}/"
	cp icons/amiga/Kens-Labyrinth.info dist/amiga/
	# The Amiga port uses the original packed assets and supplied MOD music.
	# Desktop-only hires PNGs and macOS Finder metadata are not part of the
	# release package.
	rm -rf "${DIST}"/gamedata/Ken*/hires
	rm -rf "${DIST}"/gamedata/shared/hires
	find "${DIST}/gamedata" -name .DS_Store -type f -delete
	if ! command -v lha >/dev/null 2>&1; then
		echo "Error: lha is required to create ${ARCHIVE}" >&2
		exit 1
	fi
	rm -f "${ARCHIVE}"
	(
		cd dist/amiga
		lha aq0 Kens-Labyrinth.lha Kens-Labyrinth Kens-Labyrinth.info
	)
fi

echo
echo "Built:"
ls -l dist/amiga/ 2>/dev/null || true
