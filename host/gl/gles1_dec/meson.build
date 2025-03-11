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
  include_directories: [inc_gfxstream_include, inc_include, inc_apigen_codec, inc_host_logging],
  link_with: [lib_host_logging],
  dependencies: [aemu_base_dep, aemu_logging_dep]
)
