# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_cereal = include_directories('cereal')
inc_cereal_common = include_directories('cereal/common')
inc_vulkan_server = include_directories('.')

subdir('cereal')
subdir('emulated_textures')

files_lib_vulkan_server = files(
  'borrowed_image_vk.cpp',
  'buffer_vk.cpp',
  'color_buffer_vk.cpp',
  'compositor_vk.cpp',
  'dependency_graph.cpp',
  'device_lost_helper.cpp',
  'device_op_tracker.cpp',
  'display_vk.cpp',
  'display_surface_vk.cpp',
  'external_memory.cpp',
  'post_worker_vk.cpp',
  'debug_utils_helper.cpp',
  'swap_chain_state_vk.cpp',
  'render_thread_info_vk.cpp',
  'vk_android_native_buffer_operations.cpp',
  'vk_common_operations.cpp',
  'vk_decoder.cpp',
  'vk_emulated_physical_device_memory.cpp',
  'vk_emulated_physical_device_queue.cpp',
  'vk_decoder_global_state.cpp',
  'vk_decoder_snapshot.cpp',
  'vk_decoder_snapshot_utils.cpp',
  'vk_format_support.cpp',
  'vk_format_utils.cpp',
  'vk_reconstruction.cpp',
  'vk_utils.cpp',
  'vulkan_boxed_handles.cpp',
  'vulkan_dispatch.cpp',
  'vulkan_handle_mapping.cpp',
  'vulkan_stream.cpp',
)

vulkan_server_cpp_args = [
  '-fvisibility=hidden',
  '-Wno-inconsistent-missing-override',
  '-Wno-unused-value',
  '-Wno-return-type',
  '-Wno-return-type-c-linkage',
  '-Wno-uninitialized',
  '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT',
]
if host_machine.system() == 'windows'
  vulkan_server_cpp_args += '-Wa,-mbig-obj'
endif


if host_machine.system() == 'darwin'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_METAL_EXT'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_MACOS_MVK'
elif host_machine.system() == 'windows'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_WIN32_KHR'
elif host_machine.system() == 'qnx'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_SCREEN_QNX'
elif host_machine.system() == 'linux'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_XCB_KHR'
endif

lib_vulkan_server = static_library(
  'lib_vulkan_server',
  files_lib_vulkan_server,
  cpp_args: vulkan_server_cpp_args + gfxstream_host_args,
  include_directories: [
    inc_cereal_common,
    inc_cereal,
    inc_common_base,
    inc_common_utils,
    inc_gl_openglesdispatch,
    inc_gl_server,
    inc_glm,
    inc_host_common,
    inc_host_compressed_textures,
    inc_host_decoder_common,
    inc_host_features,
    inc_host_include,
    inc_host_iostream,
    inc_host_library,
    inc_common_logging,
    inc_host_renderdoc,
    inc_host_snapshot,
    inc_host_tracing,
    inc_include,
    inc_opengl_headers,
    inc_renderdoc_external,
    inc_root,
    inc_stream_servers,
    inc_vulkan_headers,
    inc_vulkan_server,
  ],
  link_with: [
    lib_common_base,
    lib_emulated_textures,
    lib_host_common,
    lib_host_compressed_textures,
    lib_host_features,
    lib_host_iostream,
    lib_host_library,
    lib_common_logging,
    lib_common_logging,
    lib_host_snapshot,
    lib_host_tracing,
    lib_vulkan_cereal,
  ],
)
