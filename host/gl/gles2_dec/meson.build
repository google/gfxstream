# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_gles2_dec = files(
  'gles2_dec.cpp',
  'gles2_server_context.cpp',
  'GLESv2Decoder.cpp',
)

lib_gles2_dec = static_library(
  'gles2_dec',
  files_lib_gles2_dec,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_host_decoder_common,
    inc_common_base,
    inc_gfxstream_server,
    inc_gl_openglesdispatch,
    inc_gl_snapshot,
    inc_host_backend,
    inc_host_iostream,
    inc_host_library,
    inc_host_logging,
    inc_include,
  ],
  link_with: [
    lib_common_base,
    lib_gl_snapshot,
    lib_host_backend,
    lib_host_iostream,
    lib_host_logging,
  ],
)
