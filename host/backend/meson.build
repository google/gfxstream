# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_host_backend = include_directories('include')

files_lib_host_backend = files(
  'display_surface.cpp',
  'display_surface_user.cpp',
)

lib_host_backend = static_library(
  'host_backend',
  files_lib_host_backend,
  include_directories: [inc_host_backend, inc_host_logging],
  link_with: [lib_host_logging],
)