# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_host_metrics = include_directories('include')

files_lib_host_metrics = files(
  'Metrics.cpp',
)

lib_host_metrics = static_library(
  'host_metrics',
  files_lib_host_metrics,
  include_directories: [inc_host_metrics, inc_common_base, inc_host_logging],
  link_with: [lib_common_base, lib_host_logging]
)
