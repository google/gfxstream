# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

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
    inc_common_opengl,
    inc_etc,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_glm,
    inc_host_backend,
    inc_host_logging,
    inc_host_snapshot,
    inc_include,
  ],
  link_with: [
    lib_common_base,
    lib_host_backend,
    lib_host_logging,
    lib_host_snapshot,
  ],
)
