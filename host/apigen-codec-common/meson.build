# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_apigen_codec = include_directories('.')

files_lib_apigen_codec = files(
  'ChecksumCalculator.cpp',
  'ChecksumCalculatorThreadInfo.cpp',
  'glUtils.cpp',
)

if host_machine.system() == 'linux'
  files_lib_apigen_codec += files('X11Support.cpp')
endif

lib_apigen_codec = static_library(
  'apigen_codec',
  files_lib_apigen_codec,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_host_logging],
  link_with: [lib_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep]
)
