# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_gl_host_common = files(
  'opengles.cpp',
  'opengl/EmuglBackendList.cpp',
  'opengl/emugl_config.cpp',
  'opengl/GLProcessPipe.cpp',
  'opengl/gpuinfo.cpp',
  'opengl/logger.cpp',
  'opengl/misc.cpp',
  'opengl/OpenglEsPipe.cpp',
)

files_gl_host_common_darwin = files(
  'opengl/macTouchOpenGL.m',
  'opengl/NativeGpuInfo_darwin.cpp',
)

files_gl_host_common_win32 = files(
  'opengl/NativeGpuInfo_windows.cpp',
)

files_gl_host_common_linux = files(
  'opengl/NativeGpuInfo_linux.cpp',
)

files_gl_host_common_qnx = files(
  'opengl/NativeGpuInfo_qnx.cpp',
)

# HACK: For the misc.h file already in AEMU host common
inc_gl_host_common = include_directories('include')

if host_machine.system() == 'darwin'
  files_lib_gl_host_common += files_gl_host_common_darwin
elif host_machine.system() == 'windows'
  files_lib_gl_host_common += files_gl_host_common_win32
elif host_machine.system() == 'linux'
  files_lib_gl_host_common += files_gl_host_common_linux
elif host_machine.system() == 'qnx'
  files_lib_gl_host_common += files_gl_host_common_qnx
endif

lib_gl_host_common = static_library(
  'gl_host_common',
  files_lib_gl_host_common,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_gl_host_common, inc_host_features,
                        inc_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep, aemu_snapshot_dep],
  link_with: [lib_host_features, lib_host_logging]
)
