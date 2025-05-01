# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_host_backend = include_directories('include')

files_lib_host_backend = files(
  'address_space_operations.cpp',
  'display_operations.cpp',
  'display_surface.cpp',
  'display_surface_user.cpp',
  'dma_device.cpp',
  'file_stream.cpp',
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
  include_directories: [inc_host_backend, inc_host_logging, inc_gfxstream_server],
  link_with: [lib_host_logging],
)