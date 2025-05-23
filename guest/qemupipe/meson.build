# Copyright 2022 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

inc_qemu_pipe = include_directories('include')
inc_qemu_pipe_types = include_directories('include-types')

files_qemu_pipe = files(
  'qemu_pipe_common.cpp',
  'qemu_pipe_guest.cpp',
)

lib_qemu_pipe = static_library(
   'qemu_pipe',
   files_qemu_pipe,
   cpp_args: gfxstream_guest_args,
   include_directories: [inc_android_compat, inc_qemu_pipe, inc_qemu_pipe_types]
)
