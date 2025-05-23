# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_common_base = include_directories('include')

files_lib_common_base = files(
    'AlignedBuf.cpp',
    'CpuTime.cpp',
    'FileUtils.cpp',
    'FunctorThread.cpp',
    'MessageChannel.cpp',
    'PathUtils.cpp',
    'SharedMemory_posix.cpp',
    'StringFormat.cpp',
    'System.cpp',
    'Tracing.cpp',
    'Thread_pthread.cpp',
)

lib_common_base = static_library(
  'common_base',
  files_lib_common_base,
  include_directories: [inc_common_base]
)
