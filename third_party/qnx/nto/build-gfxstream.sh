#!/bin/sh
set -e

if [ -z "$QNX_HOST" ]; then
    echo "QNX_HOST not set"
    exit 1
fi

if [ -z "$QNX_TARGET" ]; then
    echo "QNX_TARGET not set"
    exit 1
fi

export PKG_CONFIG_LIBDIR=../../pkgconfig

meson setup ../../.. gfxstream/ \
      --cross-file ./meson.cross.ini \
      -Ddefault_library=shared \
      -Ddecoders=gles,vulkan,composer \
      -Dqnx_target=$QNX_TARGET

ninja -v -C gfxstream
