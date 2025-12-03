# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

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
  'buffer_gl.cpp',
  'color_buffer_gl.cpp',
  'compositor_gl.cpp',
  'debug_gl.cpp',
  'display_gl.cpp',
  'display_surface_gl.cpp',
  'emulated_egl_config.cpp',
  'emulated_egl_context.cpp',
  'emulated_egl_fence_sync.cpp',
  'emulated_egl_image.cpp',
  'emulated_egl_window_surface.cpp',
  'emulation_gl.cpp',
  'gles_version_detector.cpp',
  'pixel_read_formats.cpp',
  'readback_worker_gl.cpp',
  'render_thread_info_gl.cpp',
  'texture_draw.cpp',
  'texture_resize.cpp',
  'yuv_converter.cpp',
)

lib_gl_server = static_library(
  'gl_server',
  files_lib_gl_server,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_common_base,
    inc_common_logging,
    inc_common_utils,
    inc_gfxstream_server,
    inc_glm,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_gl_snapshot,
    inc_gl_server,
    inc_host_common,
    inc_host_decoder_common,
    inc_host_features,
    inc_host_iostream,
    inc_host_library,
    inc_host_renderdoc,
    inc_host_snapshot,
    inc_include,
    inc_opengl_headers,
    inc_renderdoc_external,
    inc_stream_servers,
    inc_vulkan_headers,
  ],
  link_with: [
    lib_common_base,
    lib_common_logging,
    lib_common_utils,
    lib_gl_openglesdispatch,
    lib_gles1_dec,
    lib_gles2_dec,
    lib_host_common,
    lib_host_decoder_common,
    lib_host_features,
    lib_host_iostream,
    lib_host_library,
    lib_host_snapshot,
  ],
)
