# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_host_decoder_common = [
  include_directories('include'),
  include_directories('.'),
]

files_lib_host_decoder_common = files(
  'ChecksumCalculator.cpp',
  'ChecksumCalculatorThreadInfo.cpp',
  'glUtils.cpp',
)

if host_machine.system() == 'linux'
  files_lib_host_decoder_common += files('X11Support.cpp')
endif

lib_host_decoder_common = static_library(
  'host_decoder_common',
  files_lib_host_decoder_common,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_common_base,
    inc_gfxstream_include,
    inc_host_decoder_common,
    inc_host_include,
    inc_host_library,
    inc_host_logging,
    inc_include,
  ],
  link_with: [
    lib_common_base,
    lib_host_library,
    lib_host_logging,
  ],
)
