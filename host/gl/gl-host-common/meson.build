# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_gl_host_common = files(
  'opengl/EmuglBackendList.cpp',
  'opengl/misc.cpp',
)

# HACK: For the misc.h file already in AEMU host common
inc_gl_host_common = include_directories('include')

lib_gl_host_common = static_library(
  'gl_host_common',
  files_lib_gl_host_common,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_gl_host_common, inc_host_features,
                        inc_host_logging, inc_gfxstream_server],
  dependencies: [aemu_base_dep, aemu_snapshot_dep],
  link_with: [lib_host_features, lib_host_logging]
)
