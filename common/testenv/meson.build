# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

inc_common_testenv = include_directories('include')

files_lib_gfxstream_common_testenv = files(
  'GraphicsTestEnvironment.cpp',
)

lib_common_testenv = static_library(
  'lib_common_testenv',
  files_lib_gfxstream_common_testenv,
  include_directories: [
    inc_common_base,
    inc_common_logging,
    inc_common_testenv,
  ],
  link_with: [
    lib_common_base,
    lib_common_logging,
  ],
)
