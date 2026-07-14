# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_iostream = include_directories('include')

files_lib_host_iostream = files(
)
if host_machine.system() == 'darwin'
  # macOS ar requires at least one member per static archive.
  files_lib_host_iostream += files('iostream_stub.cpp')
endif

lib_host_iostream = static_library(
  'host_iostream',
  files_lib_host_iostream,
  include_directories: [inc_host_iostream],
)