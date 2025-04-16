# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_host_health = include_directories('include')

files_lib_host_health = files(
  'HealthMonitor.cpp',
)

lib_host_health = static_library(
  'host_health',
  files_lib_host_health,
  include_directories: [inc_host_health, inc_common_base, inc_host_logging, inc_host_metrics],
  link_with: [lib_common_base, lib_host_logging, lib_host_metrics]
)
