# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

files_lib_glesv2_translator = files(
  'angle_shader_parser.cpp',
  'gles_v2_context.cpp',
  'gles_v2_imp.cpp',
  'gles_v2_validate.cpp',
  'program_data.cpp',
  'sampler_data.cpp',
  'shader_parser.cpp',
  'shader_validator.cpp',
  'transform_feedback_data.cpp',
)

lib_glesv2_translator = static_library(
  'glesv2_translator',
  files_lib_glesv2_translator,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_common_base,
    inc_etc,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_host_common,
    inc_host_decoder_common,
    inc_common_logging,
    inc_host_snapshot,
    inc_include,
    inc_opengl_headers,
    inc_stream_servers,
  ],
  link_with: [
    lib_common_base,
    lib_host_common,
    lib_host_decoder_common,
    lib_common_logging,
    lib_host_snapshot,
  ],
)
