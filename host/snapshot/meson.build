# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_host_snapshot = include_directories('include')

files_lib_host_snapshot = files()
if host_machine.system() == 'darwin'
  # macOS ar requires at least one member per static archive.
  files_lib_host_snapshot += files('snapshot_stub.cpp')
endif

lib_host_snapshot = static_library(
  'host_snapshot',
  files_lib_host_snapshot,
  include_directories: [inc_host_snapshot],
)
