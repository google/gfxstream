# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_gles1_dec = files(
  'gles1_dec.cpp',
  'gles1_server_context.cpp',
  'GLESv1Decoder.cpp',
)

lib_gles1_dec = static_library(
  'gles1_dec',
  files_lib_gles1_dec,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_apigen_codec,
    inc_common_base,
    inc_gl_openglesdispatch,
    inc_gfxstream_server,
    inc_host_iostream,
    inc_host_library,
    inc_host_logging,
    inc_include,
  ],
  link_with: [
    lib_common_base,
    lib_gl_openglesdispatch,
    lib_host_iostream,
    lib_host_library,
    lib_host_logging,
  ],
)
