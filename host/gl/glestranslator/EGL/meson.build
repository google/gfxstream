# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

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
  include_directories: [
    inc_host_decoder_common,
    inc_common_base,
    inc_common_opengl,
    inc_common_utils,
    inc_etc,
    inc_gfxstream_include,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_host_backend,
    inc_host_library,
    inc_common_logging,
    inc_host_snapshot,
    inc_include,
    inc_stream_servers,
    inc_x11_headers,
  ],
  link_with: [
    lib_host_decoder_common,
    lib_common_base,
    lib_common_utils,
    lib_gl_common,
    lib_host_backend,
    lib_host_library,
    lib_common_logging,
    lib_host_snapshot,
  ],
)
