# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_health = include_directories('include')

files_lib_host_health = files(
  'HealthMonitor.cpp',
)

lib_host_health = static_library(
  'host_health',
  files_lib_host_health,
  include_directories: [inc_host_health, inc_common_base, inc_common_logging, inc_host_metrics],
  link_with: [lib_common_base, lib_common_logging, lib_host_metrics]
)
