#!/bin/sh
set -e

AEMU_COMMIT_SHA=HEAD

if [ ! -d aemu ]
then
    git clone https://android.googlesource.com/platform/hardware/google/aemu aemu
fi

cd aemu
git checkout $AEMU_COMMIT_SHA

cmake -D CMAKE_TOOLCHAIN_FILE=../../qnx.nto.toolchain.cmake \
      -D AEMU_COMMON_GEN_PKGCONFIG=ON \
      -D AEMU_COMMON_BUILD_CONFIG=gfxstream \
      -D ENABLE_VKCEREAL_TESTS=OFF -B build

cmake --build build -j
cmake --install build --prefix install
