# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0 or MIT

inc_host_logging = include_directories('include')

files_lib_gfxstream_host_logging = files(
  'logging.cpp',
)

lib_host_logging = static_library(
  'lib_host_logging',
  files_lib_gfxstream_host_logging,
  include_directories: [inc_host_logging],
)
