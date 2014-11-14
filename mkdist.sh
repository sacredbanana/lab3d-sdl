#!/bin/sh

VERS=3.0
DATE=$1
XPATH=dist/$DATE

if [ -z "$DATE" ]; then
    echo "foo!"
    exit 1
fi

makezip() {
    fmt=$1
    zipname=LAB3D-SDL-$VERS-$2
    zpath=$XPATH/$zipname
    cpath=$zpath/LAB3D-SDL-$VERS
    mkdir -p $cpath

    shift; shift
    for j; do
        "copy_$j" $cpath;
    done
    if [ $fmt = tar ]; then
        tar zcfvC $XPATH/$zipname.tar.gz $zpath LAB3D-SDL-$VERS
    else
        (cd $zpath && rm -f ../$zipname.zip && zip ../$zipname.zip -r .)
    fi
    rm -rf $zpath
}

copy_source() {
    cp -a adlibemu.c demo.c graphx.c init.c lab3d.c oldlab3d.c setup.c subs.c adlibemu.h demo.h lab3d.h Makefile ken $1
}

copy_tex() {
    cp -a wallparams.ini hires $1
}

copy_winbin() {
    cp ken.exe setup.bat $1
    cp dist/winlibs-sdl2/* $1
}

copy_linbin32() {
    cp ken $1
    cp ken.bin.i686 $1/ken.bin
    cp -a dist/linlibs32 $1/libs
}

copy_linbin64() {
   cp ken $1
   cp ken.bin $1/ken.bin
    cp -a dist/linlibs64 $1/libs
}

copy_data() {
    cpath=$1
    mkdir -p $cpath
    cp ken.bmp ken.ico $cpath
    cp dist/data-files/* $cpath
}

makezip tar src source data tex
makezip zip win32 winbin data tex
#makezip zip win32-all winbin data tex
makezip tar lin32 linbin32 data tex
#makezip tar lin32-all linbin32 data tex
makezip tar lin64 linbin64 data tex
#makezip tar lin64-all linbin64 data tex
