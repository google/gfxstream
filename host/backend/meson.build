# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

inc_host_backend = include_directories('include')

files_lib_host_backend = files(
  'address_space_operations.cpp',
  'display_operations.cpp',
  'display_surface.cpp',
  'display_surface_user.cpp',
  'dma_device.cpp',
  'external_object_manager.cpp',
  'file_stream.cpp',
  'graphics_driver_lock.cpp',
  'guest_operations.cpp',
  'mem_stream.cpp',
  'renderer_operations.cpp',
  'stream_utils.cpp',
  'sync_device.cpp',
  'vm_operations.cpp',
  'window_operations.cpp',
)

lib_host_backend = static_library(
  'host_backend',
  files_lib_host_backend,
  include_directories: [
    inc_common_base,
    inc_gfxstream_server,
    inc_host_address_space,
    inc_host_backend,
    inc_common_logging,
  ],
  link_with: [
    lib_common_base,
    lib_host_address_space,
    lib_common_logging,
  ],
)