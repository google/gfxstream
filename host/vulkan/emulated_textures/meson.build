# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

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
  include_directories: [
    inc_cereal_common,
    inc_common_base,
    inc_gfxstream_include,
    inc_host_compressed_textures,
    inc_host_decoder_common,
    inc_host_health,
    inc_host_include,
    inc_common_logging,
    inc_host_metrics,
    inc_include,
    inc_root,
    inc_stream_servers,
    inc_vulkan_headers,
    inc_vulkan_server,
  ],
  link_with: [
    lib_common_base,
    lib_host_compressed_textures,
    lib_host_health,
    lib_common_logging,
    lib_host_metrics,
  ],
)
