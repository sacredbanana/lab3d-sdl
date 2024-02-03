@echo off
setlocal

REM -- Set your build directory path here --
set BUILD_DIR=build

REM -- List of directories to delete --
set DIRS_TO_DELETE=".cmake" "CMakeFiles" "Testing"

REM -- List of files to delete --
set FILES_TO_DELETE=".ninja_deps" ".ninja_log" "build.ninja" "cmake_install.cmake" "CMakeCache.txt" "VSInheritEnvironments.txt"

REM -- Deleting specified directories recursively --
for %%d in (%DIRS_TO_DELETE%) do (
    for /d /r "%BUILD_DIR%" %%i in (%%d) do (
        if exist "%%i" (
            echo Deleting directory: %%i
            rmdir /s /q "%%i"
        )
    )
)

REM -- Deleting specified files recursively --
for %%f in (%FILES_TO_DELETE%) do (
    for /r "%BUILD_DIR%" %%i in (%%f) do (
        if exist "%%i" (
            echo Deleting file: %%i
            del /q "%%i"
        )
    )
)

endlocal
