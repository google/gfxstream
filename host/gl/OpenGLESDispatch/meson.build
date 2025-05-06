# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

# Forward declared in host/gl/meson.build:
# inc_gl_openglesdispatch = include_directories('include')

files_lib_gl_openglesdispatch = files(
  'EGLDispatch.cpp',
  'gles1_stubbed_in_translator_namespace.cpp',
  'gles2_stubbed_in_translator_namespace.cpp',
  'GLESv1Dispatch.cpp',
  'GLESv2Dispatch.cpp',
  'OpenGLDispatchLoader.cpp',
  'StaticDispatch.cpp',
)

lib_gl_openglesdispatch = static_library(
  'gl_openglesdispatch',
  files_lib_gl_openglesdispatch,
  include_directories: [
    inc_common_base,
    inc_common_opengl,
    inc_gfxstream_include,
    inc_gl_openglesdispatch,
    inc_include,
  ],
  cpp_args: gfxstream_host_args,
  link_with: [
    lib_common_base,
    lib_egl_translator,
    lib_glescm_translator,
    lib_glesv2_translator,
  ],
)
