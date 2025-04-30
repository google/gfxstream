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
  include_directories: [inc_gfxstream_include, inc_include, inc_gl_common, inc_glm,
                        inc_etc, inc_host_logging,inc_common_base, inc_gl_openglesdispatch,
                        inc_host_snapshot],
  link_with: [lib_host_logging, lib_common_base, lib_host_snapshot],
)
