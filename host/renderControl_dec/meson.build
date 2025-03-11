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
  include_directories: [inc_gfxstream_include, inc_include, inc_apigen_codec, inc_host_logging],
  link_with: [lib_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep]
)
