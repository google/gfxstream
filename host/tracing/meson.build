# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_tracing = include_directories('include')

files_lib_host_tracing = files(
  'Tracing.cpp',
)

lib_host_tracing = static_library(
  'host_tracing',
  files_lib_host_tracing,
  include_directories: [inc_host_tracing],
)
