#!/bin/sh

VERS=3.0
DATE=$1
XPATH=dist/$DATE

if [ -z "$DATE" ]; then
    echo "foo!"
    exit 1
fi

makezip() {
    zipname=$1
    zpath=$XPATH/$zipname
    (cd $zpath && rm -f ../$zipname.zip && zip ../$zipname.zip -r .)
}

copy_tex() {
    cpath=$1
    mkdir -p $cpath
    cp -a wallparams.ini hires $cpath
}

copy_winbin() {
    cpath=$1
    mkdir -p $cpath
    cp ken.bmp ken.ico ken.exe $cpath
    cp dist/data-files/* $cpath
    cp dist/winlibs-sdl2/* $cpath
}

make_tex() {
    zipname=LAB3D-SDL-$VERS-tex
    zpath=$XPATH/$zipname

    copy_tex $zpath/LAB3D-SDL-$VERS
    cp -a wallparams.ini hires-tex-install.txt hires-tex-readme.txt $zpath/LAB3D-SDL-$VERS

    makezip $zipname
    rm -rf $zpath
}

make_winbin() {
    zipname=LAB3D-SDL-$VERS-win32
    zpath=$XPATH/$zipname

    copy_winbin $zpath/LAB3D-SDL-$VERS

    makezip $zipname
    rm -rf $zpath
}

make_winall() {
    zipname=LAB3D-SDL-$VERS-win32-all
    zpath=$XPATH/$zipname

    copy_tex $zpath/LAB3D-SDL-$VERS
    copy_winbin $zpath/LAB3D-SDL-$VERS

    makezip $zipname
    rm -rf $zpath
}

if [ -z "$2" ]; then
    make_tex
    make_winbin
    make_winall
fi
