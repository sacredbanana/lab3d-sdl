#!/usr/bin/env bash

# This script is used to build the Nintendo Switch version of the game using the Devkitpro Docker container
# All builds are output to the dist folder

docker run --rm \
    -v ${PWD}:/work \
    -e USER=$( id -u ) -e GROUP=$( id -g ) \
    -w /work \
    -it devkitpro/devkita64 /bin/bash -c "make clean package -f Makefile.Switch"
