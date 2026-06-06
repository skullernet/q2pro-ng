#!/bin/sh -e

SRC="$1"
DST=~/.q2pro-ng

if [ ! -z "$SRC" ] ; then
    if [ ! -f "$SRC/baseq2/pak0.pak" ] ; then
        echo "ERROR: $SRC doesn't look like re-release install directory"
        exit 1
    fi

    mkdir -p "$DST/baseq2/music"
    mkdir -p "$DST/baseq2/video/n64"

    umask 0113

    ./pak_to_zip.py "$SRC/baseq2/pak0.pak" "$DST/baseq2/pak0.pkz"
    cp "$SRC"/baseq2/music/*.ogg "$DST/baseq2/music"
    cp "$SRC"/baseq2/video/*.ogv "$DST/baseq2/video"
    cp "$SRC"/baseq2/video/n64/*.ogv "$DST/baseq2/video/n64"
    cp "$SRC/Q2Game.kpf" "$DST"
fi

mkdir -p "$DST/baseq2"

cd baseq2
zip -9 "$DST/baseq2/q2pro.pkz" q2pro.menu default.cfg vm/*.qvm vispatches/* entpatches/*
