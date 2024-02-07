#!/usr/bin/env bash

# This script is used to build all of the Linux versions of the game using the Docker container
# All builds are output to the dist folder

set -e

mkdir -p build

rm -drf dist/* || true

docker buildx rm kenbuilder || true
docker buildx create --driver=docker-container --name=kenbuilder
docker buildx use kenbuilder
docker buildx build --platform=linux/amd64,linux/i386,linux/arm64,linux/arm/v7,linux/ppc64le --output=dist --target=ken -t kenbuilder .
docker buildx rm kenbuilder || true
