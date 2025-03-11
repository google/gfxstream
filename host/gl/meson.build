# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

inc_gl_server = include_directories('.')
inc_gl_snapshot = include_directories('glsnapshot')

# snapshot
subdir('glsnapshot')

# GLES decoder
subdir('gles1_dec')
subdir('gles2_dec')

# GLES translator
subdir('glestranslator')

# GLES dispatch based on Translator
subdir('OpenGLESDispatch')

files_lib_gl_server = files(
  'BufferGl.cpp',
  'ColorBufferGl.cpp',
  'CompositorGl.cpp',
  'DebugGl.cpp',
  'DisplayGl.cpp',
  'DisplaySurfaceGl.cpp',
  'EmulatedEglConfig.cpp',
  'EmulatedEglContext.cpp',
  'EmulatedEglFenceSync.cpp',
  'EmulatedEglImage.cpp',
  'EmulatedEglWindowSurface.cpp',
  'EmulationGl.cpp',
  'GLESVersionDetector.cpp',
  'ReadbackWorkerGl.cpp',
  'TextureDraw.cpp',
  'TextureResize.cpp',
  'YUVConverter.cpp',
)

lib_gl_server = static_library(
  'gl_server',
  files_lib_gl_server,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_stream_servers, inc_apigen_codec,
                        inc_gl_snapshot, inc_gl_host_common, inc_host_features, inc_host_logging, inc_gl_common],
  link_with: [lib_gles1_dec, lib_gles2_dec, lib_apigen_codec, lib_gl_dispatch,
              lib_apigen_codec, lib_host_features, lib_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep, aemu_snapshot_dep]
)
