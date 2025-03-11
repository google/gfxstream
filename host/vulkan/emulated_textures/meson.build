# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_emulated_textures = files(
  'AstcTexture.cpp',
  'CompressedImageInfo.cpp',
  'GpuDecompressionPipeline.cpp',
)

emulated_texture_args = [
  '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT'
]

lib_emulated_textures = static_library(
  'emulated_textures',
  files_emulated_textures,
  cpp_args: emulated_texture_args + gfxstream_host_args,
  include_directories: [inc_utils, inc_root, inc_vulkan_headers, inc_vulkan_server,
                        inc_gfxstream_include, inc_include, inc_cereal_common, inc_stream_servers,
                        inc_host_logging, inc_host_include],
  link_with: [lib_compressed_textures, lib_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep]
)
