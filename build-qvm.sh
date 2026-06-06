#!/bin/sh -e

if [ -z "$QVM_CC" ] ; then
    QVM_CC=clang
fi

if [ -z "$QVM_DIR" ] ; then
    QVM_DIR=./baseq2/vm
fi

mkdir -p "$QVM_DIR"

QVM_OPT="-O2 -flto -target wasm32 -nostdlib -mbulk-memory -mnontrapping-fptoint
    -Wl,--no-entry,--export-dynamic,--import-undefined,--export=__stack_pointer
    -Wl,--no-growable-memory,--stack-first,-z,stack-size=0x100000
    -Wall -Wformat-security -Wpointer-arith -Wstrict-prototypes -Wno-missing-braces
    -Iinc -DQ2_VM -DUSE_DEBUG=1 $QVM_OPT"

GAME_SRC="
    src/game/xatrix/*.c
    src/game/rogue/*.c
    src/game/ctf/*.c
    src/game/q1q2/*.c
    src/game/*.c"

GAME_PTR=`mktemp /tmp/XXXXXXXXXX.c`

./src/game/genptr.py $GAME_SRC $GAME_PTR

$QVM_CC -o "$QVM_DIR/game.qvm" $QVM_OPT -Isrc/game \
    src/shared/shared.c \
    src/bgame/*.c \
    $GAME_SRC $GAME_PTR

rm $GAME_PTR

$QVM_CC -o "$QVM_DIR/cgame.qvm" $QVM_OPT \
    src/shared/shared.c \
    src/bgame/*.c \
    src/cgame/*.c
