# Copyright 2024 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_features = include_directories('include')

files_lib_host_features = files(
  'Features.cpp',
)

lib_host_features = static_library(
  'host_features',
  files_lib_host_features,
  include_directories: [inc_host_features],
)
