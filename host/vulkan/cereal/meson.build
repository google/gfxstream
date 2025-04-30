# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_cereal = files(
  'common/goldfish_vk_extension_structs.cpp',
  'common/goldfish_vk_marshaling.cpp',
  'common/goldfish_vk_reserved_marshaling.cpp',
  'common/goldfish_vk_deepcopy.cpp',
  'common/goldfish_vk_dispatch.cpp',
  'common/goldfish_vk_transform.cpp'
)

cereal_cpp_args = [
  '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT',
  '-DVK_GOOGLE_address_space',
]

if host_machine.system() == 'qnx'
  cereal_cpp_args += '-DVK_USE_PLATFORM_SCREEN_QNX'
endif

lib_vulkan_cereal = static_library(
  'cereal',
  files_lib_cereal,
  cpp_args: cereal_cpp_args + gfxstream_host_args,
  include_directories: [inc_cereal, inc_cereal_common, inc_stream_servers,
                        inc_vulkan_server, inc_utils,
                        inc_gfxstream_include, inc_include, inc_vulkan_headers,
                        inc_host_features, inc_host_logging, inc_common_base,
                        inc_host_health, inc_host_metrics],
  dependencies: [aemu_common_dep],
  link_with: [lib_host_features, lib_host_health, lib_host_logging, lib_common_base,
              lib_host_metrics]
)
