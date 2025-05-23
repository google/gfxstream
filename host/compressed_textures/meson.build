# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_compressed_textures = include_directories('include')

files_lib_host_compressed_textures = files(
  'AstcCpuDecompressorNoOp.cpp',
)

lib_host_compressed_textures = static_library(
  'host_compressed_textures',
  files_lib_host_compressed_textures,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_etc,
    inc_host_compressed_textures,
  ],
  link_with: [
    lib_etc,
  ],
)
