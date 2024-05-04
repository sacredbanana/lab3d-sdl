#!/usr/bin/env bash

# This script is used to build the Nintendo Switch version of the game using the Devkitpro Docker container
# All builds are output to the dist folder

docker run --rm \
    -v ${PWD}:/work \
    -e USER=$( id -u ) -e GROUP=$( id -g ) \
    -w /work \
    -it devkitpro/devkita64 /bin/bash -c "make clean -f Makefile.Switch && make -f Makefile.Switch"

mkdir -p dist/ken
rm -dr dist/switch || true
cp dist/Kens-Labyrinth.nro dist/ken/
cp -r gamedata dist/ken/
cd dist
zip -r ken/ken.zip ken/
mv ken switch
rm -f Kens-Labyrinth.nro
rm -f Kens-Labyrinth.nacp
rm -f Kens-Labyrinth.nso
rm -f Kens-Labyrinth.pfs0
rm -f Kens-Labyrinth.elf