#!/bin/bash

# Set your build directory path here
BUILD_DIR="build"

# List of directories to delete
DIRS_TO_DELETE=(".cmake" "external" "CMakeScripts" "CMakeFiles" "Testing" "lab3d-sdl.xcodeproj")

# List of files to delete
FILES_TO_DELETE=(".ninja_deps" "Makefile" "myports.txt" "requested.txt" "restore_ports.tcl" ".ninja_log" "build.ninja" "cmake_install.cmake" "CMakeCache.txt" "VSInheritEnvironments.txt")

# Deleting specified directories recursively
for dir in "${DIRS_TO_DELETE[@]}"; do
    find "$BUILD_DIR" -type d -name "$dir" -exec echo Deleting directory: {} \; -exec rm -rf {} +
done

# Deleting specified files recursively
for file in "${FILES_TO_DELETE[@]}"; do
    find "$BUILD_DIR" -type f -name "$file" -exec echo Deleting file: {} \; -exec rm -f {} +
done
