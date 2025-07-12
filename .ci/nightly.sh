#!/bin/sh -ex

MESON_OPTS="
    --auto-features=enabled
    --fatal-meson-warnings
    -Dwerror=true
    -Dwrap_mode=forcefallback
    -Dsdl2=disabled -Dwayland=disabled -Dx11=disabled"

SRC_DIR=`pwd`
CI=$SRC_DIR/.ci

TMP_DIR=$SRC_DIR/q2pro-build
mkdir $TMP_DIR

export MESON_PACKAGE_CACHE_DIR=$SRC_DIR/subprojects/packagecache

### Source ###

REV=$(git rev-list --count HEAD)
SHA=$(git rev-parse --short HEAD)
VER="r$REV~$SHA"
SRC="q2pro-ng-r$REV"

cd $TMP_DIR
GIT_DIR=$SRC_DIR/.git git archive --format=tar --prefix=$SRC/ HEAD | tar x
echo "$VER" > $SRC/VERSION
rm -rf $SRC/.gitignore $SRC/.ci $SRC/.github
fakeroot tar czf q2pro-ng-source.tar.gz $SRC

sed -e "s/##VER##/$VER/" -e "s/##DATE##/`date -R`/" $CI/readme-template.txt > README

### FFmpeg ###

cd $TMP_DIR
git clone --depth=1 https://github.com/FFmpeg/FFmpeg.git ffmpeg
cd ffmpeg

mkdir build
cd build
$CI/configure-ffmpeg.sh --win64 $TMP_DIR/ffmpeg-prefix-64
make -j4 install
cd ..

### Win64 ###

export PKG_CONFIG_SYSROOT_DIR="$TMP_DIR/ffmpeg-prefix-64"
export PKG_CONFIG_LIBDIR="$PKG_CONFIG_SYSROOT_DIR/lib/pkgconfig"

cd $TMP_DIR
meson setup --cross-file $CI/x86_64-w64-mingw32.txt $MESON_OPTS build $SRC
ninja -C build q2pro-ng.exe
x86_64-w64-mingw32-strip build/q2pro-ng.exe

mkdir -p build/baseq2/vm
cd $SRC
export WASM_CC=clang-19
export WASM_DIR=../build/baseq2/vm
export WASM_OPT=-Werror
./wasm.sh

cd ..
unix2dos -k -n $SRC/LICENSE build/LICENSE.txt $SRC/doc/client.asciidoc build/MANUAL.txt README build/README.txt

cp -a $SRC/etc/q2pro.menu build/baseq2/
cp -a $SRC/etc/default.cfg build/baseq2/

cd build/baseq2
zip -9 q2pro.pkz q2pro.menu default.cfg vm/*.qvm

cd ..
zip -9 ../q2pro-ng-client_win64_x64.zip \
    q2pro-ng.exe \
    LICENSE.txt \
    MANUAL.txt \
    README.txt \
    baseq2/q2pro.pkz \

### Version ###

cd $TMP_DIR
echo $VER > version.txt
