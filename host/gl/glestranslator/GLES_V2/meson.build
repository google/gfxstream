# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_glesv2_translator = files(
  'ANGLEShaderParser.cpp',
  'GLESv2Context.cpp',
  'GLESv2Imp.cpp',
  'GLESv2Validate.cpp',
  'ProgramData.cpp',
  'SamplerData.cpp',
  'ShaderParser.cpp',
  'ShaderValidator.cpp',
  'TransformFeedbackData.cpp',
)

lib_glesv2_translator = static_library(
  'glesv2_translator',
  files_lib_glesv2_translator,
  cpp_args: gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_gl_common, inc_stream_servers,
                        inc_etc, inc_host_logging, inc_gl_openglesdispatch],
  link_with: [lib_host_logging],
  dependencies: [aemu_base_dep, aemu_logging_dep, aemu_snapshot_dep]
)
