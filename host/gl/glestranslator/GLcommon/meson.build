# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_gl_common = files(
  'rgtc.cpp',
  'FramebufferData.cpp',
  'GLBackgroundLoader.cpp',
  'GLDispatch.cpp',
  'GLESbuffer.cpp',
  'GLEScontext.cpp',
  'GLESpointer.cpp',
  'GLESvalidate.cpp',
  'GLutils.cpp',
  'NamedObject.cpp',
  'ObjectData.cpp',
  'ObjectNameSpace.cpp',
  'PaletteTexture.cpp',
  'RangeManip.cpp',
  'SaveableTexture.cpp',
  'ScopedGLState.cpp',
  'ShareGroup.cpp',
  'TextureData.cpp',
  'TextureUtils.cpp',
)

lib_gl_common = static_library(
  'gl_common',
  files_lib_gl_common,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_etc, inc_host_logging, inc_gl_openglesdispatch, inc_gfxstream_include,\
                        inc_include, inc_stream_servers, inc_gl_common, inc_apigen_codec],
  link_with: [lib_compressed_textures, lib_host_logging, lib_etc],
  dependencies: [aemu_base_dep, aemu_logging_dep, aemu_snapshot_dep]
)
