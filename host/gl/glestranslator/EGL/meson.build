# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

files_lib_egl_translator = files(
  'ClientAPIExts.cpp',
  'EglConfig.cpp',
  'EglContext.cpp',
  'EglDisplay.cpp',
  'EglGlobalInfo.cpp',
  'EglImp.cpp',
  'EglPbufferSurface.cpp',
  'EglSurface.cpp',
  'EglThreadInfo.cpp',
  'EglValidate.cpp',
  'EglWindowSurface.cpp',
  'ShaderCache.cpp',
  'ThreadInfo.cpp',
  'EglOsApi_egl.cpp',
)

files_egl_darwin = files(
  'EglOsApi_darwin.cpp',
  'MacNative.m',
  'MacPixelFormatsAttribs.m',
)

files_egl_win32 = files(
  'CoreProfileConfigs_windows.cpp',
  'EglOsApi_wgl.cpp',
)

files_egl_linux = files(
  'CoreProfileConfigs_linux.cpp',
  'EglOsApi_glx.cpp',
  'X11ErrorHandler.cpp',
)

egl_cpp_args = [
  '-fvisibility=hidden',
  '-Wno-inconsistent-missing-override',
]

if host_machine.system() == 'darwin'
  files_lib_egl_translator += files_egl_darwin
elif host_machine.system() == 'windows'
  files_lib_egl_translator += files_egl_win32
elif host_machine.system() == 'linux'
  files_lib_egl_translator += files_egl_linux
endif

lib_egl_translator = static_library(
  'egl_translator',
  files_lib_egl_translator,
  cpp_args: egl_cpp_args + gfxstream_host_args,
  include_directories: [inc_gfxstream_include, inc_include, inc_gl_common, inc_apigen_codec,
                        inc_gl_host_common, inc_stream_servers, inc_etc, inc_host_backend,
                        inc_host_logging, inc_gl_openglesdispatch, inc_gfxstream_server],
  link_with: [lib_gl_common, lib_apigen_codec, lib_gl_host_common, lib_host_backend, 
              lib_host_logging],
  dependencies: [aemu_base_dep, aemu_common_dep, aemu_snapshot_dep,
                 aemu_logging_dep]
)
