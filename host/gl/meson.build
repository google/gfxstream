# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

inc_gl_server = include_directories('.')
inc_gl_snapshot = include_directories('glsnapshot')

# Needs forward declaration.
inc_gl_openglesdispatch = include_directories('OpenGLESDispatch/include')

# GLES translator
subdir('glestranslator')

# GLES dispatch based on Translator
subdir('OpenGLESDispatch')

# snapshot
subdir('glsnapshot')

# GLES decoder
subdir('gles1_dec')
subdir('gles2_dec')

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
  'RenderThreadInfoGl.cpp',
  'TextureDraw.cpp',
  'TextureResize.cpp',
  'YUVConverter.cpp',
)

lib_gl_server = static_library(
  'gl_server',
  files_lib_gl_server,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_host_decoder_common,
    inc_common_base,
    inc_common_opengl,
    inc_common_utils,
    inc_gfxstream_include,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_gl_snapshot,
    inc_host_backend,
    inc_host_features,
    inc_host_health,
    inc_host_iostream,
    inc_host_library,
    inc_host_logging,
    inc_host_metrics,
    inc_host_snapshot,
    inc_include,
    inc_renderdoc_external,
    inc_stream_servers,
    inc_utils,
  ],
  link_with: [
    lib_host_decoder_common,
    lib_common_base,
    lib_common_utils,
    lib_gl_openglesdispatch,
    lib_gles1_dec,
    lib_gles2_dec,
    lib_host_backend,
    lib_host_features,
    lib_host_health,
    lib_host_iostream,
    lib_host_library,
    lib_host_logging,
    lib_host_metrics,
    lib_host_snapshot,
  ],
)
