# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0
files_lib_egl_translator = files(
  'client_api_exts.cpp',
  'egl_config.cpp',
  'egl_context.cpp',
  'egl_display.cpp',
  'egl_global_info.cpp',
  'egl_imp.cpp',
  'egl_pbuffer_surface.cpp',
  'egl_surface.cpp',
  'egl_thread_info.cpp',
  'egl_validate.cpp',
  'egl_window_surface.cpp',
  'shader_cache.cpp',
  'thread_info.cpp',
  'egl_os_api_egl.cpp',
)

files_egl_darwin = files(
  'egl_os_api_darwin.cpp',
  'mac_native.m',
  'mac_pixel_formats_attribs.m',
)

files_egl_win32 = files(
  'core_profile_configs_windows.cpp',
  'egl_os_api_wgl.cpp',
)

files_egl_linux = files(
  'core_profile_configs_linux.cpp',
  'egl_os_api_glx.cpp',
  'x11_error_handler.cpp',
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
    inc_common_utils,
    inc_etc,
    inc_gfxstream_server,
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_host_backend,
    inc_host_library,
    inc_common_logging,
    inc_host_snapshot,
    inc_include,
    inc_opengl_headers,
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
