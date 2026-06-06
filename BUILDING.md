Prerequisities
--------------

Q2PRO-ng requires a C11 compiler with GNU extensions support. Using recent
version of GCC or Clang is strongly recommended.

For graphics output there are native Windows, X11 and Wayland backends, as well
as generic SDL2 backend.

SDL2 is completely optional, and should be only used on systems lacking native
graphics backend such as macOS.

For audio output Q2PRO-ng can use either built-in Miniaudio sound backend or
OpenAL. OpenAL is currently required for reverb effect to work.

Both client and dedicated server require zlib support for full compatibility at
network protocol level. The rest of dependencies are optional.

For JPEG support libjpeg-turbo is required, plain libjpeg will not work. Most
Linux distributions already provide libjpeg-turbo in place of libjpeg.

For playing back music and cinematics FFmpeg libraries are required.

OpenAL sound backend requires OpenAL Soft development headers for compilation.
At runtime, OpenAL library from any vendor can be used (but OpenAL Soft is
strongly recommended).

To install the *full* set of dependencies for building Q2PRO-ng on Debian or
Ubuntu use the following command:

    apt-get install meson gcc clang libc6-dev libsdl2-dev libopenal-dev \
                    libpng-dev libjpeg-dev zlib1g-dev mesa-common-dev \
                    libcurl4-gnutls-dev libx11-dev libxi-dev \
                    libwayland-dev wayland-protocols libdecor-0-dev \
                    libavcodec-dev libavformat-dev libavutil-dev \
                    libswresample-dev libswscale-dev

If you intend to build just dedicated server, smaller set of dependencies can
be installed:

    apt-get install meson gcc clang libc6-dev zlib1g-dev

Users of other distributions should look for equivalent development packages
and install them.


Building
--------

Q2PRO-ng uses Meson build system for its build process.

Setup build directory (arbitrary name can be used instead of `builddir`):

    meson setup builddir

Review and configure options:

    meson configure builddir

Q2PRO-ng specific options are listed in `Project options` section. They are
defined in `meson_options.txt` file.

E.g. to install to different prefix:

    meson configure -Dprefix=/usr builddir

Finally, invoke build command:

    meson compile -C builddir

To enable verbose output during the build, use `meson compile -C builddir -v`.

Building QVMs
-------------

For building WASM-based Quake Virtual Machine (QVM) modules Clang 19 or higher
is required. Run `build-qvm.sh` shell script from the root of source tree to build.

You may need to set `QVM_CC` environment variable to the name of your Clang
compiler binary before invoking `build-qvm.sh`.

For debugging, development, or embedded systems it is possible to use native
game modules as well.

Installation
------------

Q2PRO-ng requires 2023 re-release (aka "remaster") game assets.

After building the client using Meson, run the following commands, assuming
`re-release-dir` is where re-release assets are located:

    ./build-qvm.sh
    ./copy-assets.sh <re-release-dir>

This will build QVM files and copy re-release assets to `~/.q2pro-ng` directory.
Then run the client with `./builddir/q2pro-ng`.

When invoking `copy-assets.sh` without arguments it will refresh `q2pro.pkz` file.
This is useful for updating QVM files.

MinGW-w64
---------

Windows binaries are built using MinGW-w64 cross-compiler on Linux.

Library dependencies that Q2PRO-ng uses have been prepared as Meson subprojects
and will be automatically downloaded and built by Meson (except of FFmpeg).

To install MinGW-w64 on Debian or Ubuntu, use the following command:

    apt-get install mingw-w64

It is recommended to also install nasm, which is needed to build libjpeg-turbo
with SIMD support:

    apt-get install nasm

Meson needs correct cross build definition file for compilation. Example
cross-files can be found in `.ci` subdirectory (available in git
repository, but not source tarball). Note that these cross-files are specific
to CI scripts and shouldn't be used directly (you'll need, at least, to
customize default `pkg-config` search path). Refer to Meson documentation for
more info.

Setup build directory:

    meson setup --cross-file x86_64-w64-mingw32.txt -Dwrap_mode=forcefallback builddir

Build:

    meson compile -C builddir
