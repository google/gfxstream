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
  include_directories: [inc_gfxstream_include, inc_include, inc_apigen_codec,
                        inc_gl_snapshot, inc_host_logging],
  link_with: [lib_gl_snapshot, lib_host_logging],
  dependencies: [aemu_base_dep, aemu_logging_dep]
)
