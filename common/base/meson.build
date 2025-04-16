# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_common_base = include_directories('include')

files_lib_common_base = files(
    'AlignedBuf.cpp',
    'CpuTime.cpp',
    'FileUtils.cpp',
    'FunctorThread.cpp',
    'GraphicsObjectCounter.cpp',
    'GLObjectCounter.cpp',
    'LayoutResolver.cpp',
    'MemStream.cpp',
    'StdioStream.cpp',
    'MemoryTracker.cpp',
    'MessageChannel.cpp',
    'PathUtils.cpp',
    'ring_buffer.cpp',
    'SharedMemory_posix.cpp',
    'StringFormat.cpp',
    'Stream.cpp',
    'StreamSerializing.cpp',
    'SubAllocator.cpp',
    'System.cpp',
    'Tracing.cpp',
    'Thread_pthread.cpp',
)

lib_common_base = static_library(
  'common_base',
  files_lib_common_base,
  include_directories: [inc_common_base]
)
