# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

files_lib_glescm_translator = files(
  'CoreProfileEngine.cpp',
  'GLEScmContext.cpp',
  'GLEScmImp.cpp',
  'GLEScmUtils.cpp',
  'GLEScmValidate.cpp',
)

lib_glescm_translator = static_library(
  'glescm_translator',
  files_lib_glescm_translator,
  cpp_args: gfxstream_host_args,
  include_directories: [
    inc_common_base,
    inc_etc,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_glm,
    inc_host_backend,
    inc_common_logging,
    inc_host_snapshot,
    inc_include,
    inc_opengl_headers,
  ],
  link_with: [
    lib_common_base,
    lib_host_backend,
    lib_common_logging,
    lib_host_snapshot,
  ],
)
