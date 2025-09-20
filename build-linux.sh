#!/usr/bin/env bash

# This script is used to build all of the Linux versions of the game using the Docker container
# All builds are output to the dist folder

set -e

mkdir -p build

rm -drf dist/* || true

if ! docker buildx inspect kenbuilder >/dev/null 2>&1; then
    docker buildx create --driver=docker-container --name=kenbuilder
fi
docker buildx use kenbuilder
docker buildx build --platform=linux/amd64,linux/i386,linux/arm64,linux/arm/v7,linux/ppc64le --output=dist --target=ken -t kenbuilder .
