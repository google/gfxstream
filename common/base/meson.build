# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_common_base = include_directories('include')

files_lib_common_base = files(
    'AlignedBuf.cpp',
    'CpuTime.cpp',
    'FileUtils.cpp',
    'MessageChannel.cpp',
    'PathUtils.cpp',
    'StringFormat.cpp',
    'System.cpp',
    'Tracing.cpp',
)

common_base_deps = []

if host_machine.system() == 'windows'
  files_lib_common_base += 'SharedMemory_win32.cpp'
  files_lib_common_base += 'Thread_win32.cpp'
  files_lib_common_base += 'Win32UnicodeString.cpp'
  cpp = meson.get_compiler('cpp')
  common_base_deps += cpp.find_library('shlwapi', required: true)
  common_base_deps += cpp.find_library('kernel32', required: true)
else
  files_lib_common_base += 'SharedMemory_posix.cpp'
  files_lib_common_base += 'Thread_pthread.cpp'
endif

lib_common_base = static_library(
  'common_base',
  files_lib_common_base,
  dependencies: common_base_deps, 
  cpp_args: gfxstream_host_args,
  include_directories: [inc_common_base]
)
