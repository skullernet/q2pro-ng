#!/bin/sh -e

if [ -z "$WASM_CC" ] ; then
    WASM_CC=clang
fi

if [ -z "$WASM_DIR" ] ; then
    WASM_DIR=./baseq2/vm
fi

WASM_OPT="-O2 -flto -target wasm32 -nostdlib -mbulk-memory -mnontrapping-fptoint
    -Wl,--no-entry,--export-dynamic,--import-undefined,--export=__stack_pointer
    -Wl,--no-growable-memory,--stack-first,-z,stack-size=0x100000
    -Iinc -Wall -DQ2_VM -DUSE_DEBUG=1 $WASM_OPT"

GAME_SRC="
    src/game/xatrix/*.c
    src/game/rogue/*.c
    src/game/ctf/*.c
    src/game/*.c"

GAME_PTR=`mktemp /tmp/XXXXXXXXXX.c`

./src/game/genptr.py $GAME_SRC $GAME_PTR

$WASM_CC -o $WASM_DIR/game.qvm $WASM_OPT -Isrc/game \
    src/shared/shared.c \
    src/bgame/*.c \
    $GAME_SRC $GAME_PTR

rm $GAME_PTR

$WASM_CC -o $WASM_DIR/cgame.qvm $WASM_OPT \
    src/shared/shared.c \
    src/bgame/*.c \
    src/cgame/*.c
