# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_composer = files(
  'renderControl_dec.cpp',
  'renderControl_server_context.cpp'
)

lib_composer = static_library(
  'composer',
  files_lib_composer,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_apigen_codec,
    inc_common_base,
    inc_gfxstream_server,
    inc_host_iostream,
    inc_host_logging,
    inc_include,
  ],
  link_with: [
    lib_common_base,
    lib_host_iostream,
    lib_host_logging,
  ],
)
