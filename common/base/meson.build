# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_common_base = include_directories('include')

files_lib_common_base = files(
    'AlignedBuf.cpp',
    'CpuTime.cpp',
    'FileUtils.cpp',
    'MessageChannel.cpp',
    'PathUtils.cpp',
    'SharedMemory_posix.cpp',
    'StringFormat.cpp',
    'System.cpp',
    'Tracing.cpp',
    'Thread_pthread.cpp',
)


if cc.has_header('linux/udmabuf.h')
  files_lib_common_base += files('UdmabufCreator_linux.cpp')
else
  files_lib_common_base += files('UdmabufCreator_stub.cpp')
endif

lib_common_base = static_library(
  'common_base',
  files_lib_common_base,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_common_base,
    inc_common_logging,
  ],
  link_with: [
    lib_common_logging,
  ],
)
