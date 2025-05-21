# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

files_lib_gl_snapshot = files(
  'GLSnapshot.cpp',
)

lib_gl_snapshot = static_library(
  'gl_snapshot',
  files_lib_gl_snapshot,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_gl_openglesdispatch,
    inc_gfxstream_include,
    inc_include,
    inc_opengl_headers
  ],
  link_with: [lib_gl_openglesdispatch],
)
