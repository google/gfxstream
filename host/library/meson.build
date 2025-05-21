# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_library = include_directories('include')

files_lib_host_library = files(
  'SharedLibrary.cpp',
)

lib_host_library = static_library(
  'host_library',
  files_lib_host_library,
  include_directories: [inc_host_library, inc_common_logging, inc_common_base],
  link_with: [lib_common_base, lib_common_logging]
)
