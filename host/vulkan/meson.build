# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_cereal = include_directories('cereal')
inc_cereal_common = include_directories('cereal/common')
inc_vulkan_server = include_directories('.')

subdir('cereal')
subdir('emulated_textures')

files_lib_vulkan_server = files(
  'BorrowedImageVk.cpp',
  'BufferVk.cpp',
  'ColorBufferVk.cpp',
  'CompositorVk.cpp',
  'DependencyGraph.cpp',
  'DeviceLostHelper.cpp',
  'DeviceOpTracker.cpp',
  'DisplayVk.cpp',
  'DisplaySurfaceVk.cpp',
  'PostWorkerVk.cpp',
  'DebugUtilsHelper.cpp',
  'SwapChainStateVk.cpp',
  'RenderThreadInfoVk.cpp',
  'VkAndroidNativeBuffer.cpp',
  'VkCommonOperations.cpp',
  'VkDecoder.cpp',
  'VkEmulatedPhysicalDeviceMemory.cpp',
  'VkEmulatedPhysicalDeviceQueue.cpp',
  'VkDecoderGlobalState.cpp',
  'VkDecoderSnapshot.cpp',
  'VkDecoderSnapshotUtils.cpp',
  'VkFormatUtils.cpp',
  'VkReconstruction.cpp',
  'VulkanBoxedHandles.cpp',
  'VulkanDispatch.cpp',
  'VulkanHandleMapping.cpp',
  'VulkanStream.cpp',
  'vk_util.cpp'
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

# Shouldn't these be defined by standard Vulkan headers?
if host_machine.system() == 'darwin'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_METAL_EXT'
elif host_machine.system() == 'windows'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_WIN32_KHR'
elif host_machine.system() == 'qnx'
  vulkan_server_cpp_args += '-DVK_USE_PLATFORM_SCREEN_QNX'
endif

lib_vulkan_server = static_library(
  'lib_vulkan_server',
  files_lib_vulkan_server,
  cpp_args: vulkan_server_cpp_args + gfxstream_host_args,
  include_directories: [inc_root, inc_gfxstream_include, inc_include, inc_utils, inc_apigen_codec,
                        inc_cereal, inc_cereal_common, inc_stream_servers,
                        inc_vulkan_server, inc_renderdoc_external,
                        inc_gl_host_common, inc_vulkan_headers, inc_glm, inc_host_backend,
                        inc_host_features, inc_host_logging, inc_host_iostream, inc_host_tracing,
                        inc_common_utils, inc_host_include],
  link_with: [lib_vulkan_cereal, lib_emulated_textures, lib_host_backend, lib_host_logging, lib_host_features,
              lib_host_tracing, lib_host_logging, lib_host_iostream],
  dependencies: [aemu_base_dep, aemu_common_dep, aemu_snapshot_dep]
)
