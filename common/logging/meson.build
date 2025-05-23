# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_common_logging = include_directories('include')

files_lib_gfxstream_common_logging = files(
  'logging.cpp',
)

lib_common_logging = static_library(
  'lib_common_logging',
  files_lib_gfxstream_common_logging,
  include_directories: [inc_common_logging],
)
