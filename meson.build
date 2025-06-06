# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

project('gfxstream', 'cpp', 'c',
         version : '0.1.2',
         license : 'Apache-2.0',
         default_options : ['cpp_std=gnu++17',
                            'b_ndebug=if-release'])

cc = meson.get_compiler('cpp')
prog_python = import('python').find_installation('python3')

#========================#
# Logging + error report #
#========================#
log_level = get_option('log-level')

#===============#
# Decoders      #
#===============#
decoders = get_option('decoders')

use_auto = decoders.contains('auto')
use_gles = decoders.contains('gles')
use_vulkan = decoders.contains('vulkan')
use_composer = decoders.contains('composer')

#===============#
# Configuration #
#===============#
gfxstream_host_args = [
  '-D_FILE_OFFSET_BITS=64',
  '-Wno-unused-parameter',
  '-Wno-unused-function',
  '-Wno-unused-variable',
  '-Wno-ignored-qualifiers',
  '-Wno-mismatched-tags',
  '-Wno-missing-field-initializers',
  '-Wno-implicit-fallthrough',
]

if host_machine.system() == 'qnx'
  gfxstream_host_args += '-D_QNX_SOURCE'
  qnx_target = get_option('qnx_target')
  if qnx_target == ''
    error('option qnx_target is not set')
  endif
endif

pkg_cflags = []
pkg_cflags += '-DGFXSTREAM_UNSTABLE=1'

#===============#
# Dependencies  #
#===============#
if host_machine.system() == 'qnx'
  ## have not yet got pkgconfig to work with cross-compile,
  ## finding libraries manually in the meantime.

  ## ERROR: Dependency "screen" not found, tried pkgconfig
  # qnx_screen_dep = dependency('screen')

  rel_path_prefix = meson.get_external_property('qnx_path_prefix')
  abs_path_prefix = meson.current_source_dir() + '/' + rel_path_prefix

  inc_qnx_headers = include_directories(join_paths(qnx_target, 'usr/include'))
  qnx_screen_lib = cc.find_library('screen', required : true)
  qnx_screen_dep = declare_dependency(include_directories: inc_qnx_headers, dependencies: [qnx_screen_lib])

  qnx_egl_lib = cc.find_library('EGL', required : true)
  qnx_egl_dep = declare_dependency(include_directories: inc_qnx_headers, dependencies: [qnx_egl_lib])

  qnx_gles2_lib = cc.find_library('GLESv2', required : true)
  qnx_gles2_dep = declare_dependency(include_directories: inc_qnx_headers, dependencies: [qnx_gles2_lib])

elif host_machine.system() == 'linux'
  thread_dep = dependency('threads')
  gfxstream_host_args += '-DGFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC=1'
  gfxstream_host_args += '-DGFXSTREAM_UNSTABLE_VULKAN_DMABUF_WINSYS=1'
else
  thread_dep = dependency('threads')
endif

if log_level == 'error'
  gfxstream_host_args += '-DSTREAM_RENDERER_LOG_LEVEL=1'
elif log_level == 'warn'
  gfxstream_host_args += '-DSTREAM_RENDERER_LOG_LEVEL=2'
elif log_level == 'info'
  gfxstream_host_args += '-DSTREAM_RENDERER_LOG_LEVEL=3'
endif

if use_auto and (use_gles or use_vulkan)
  error('Can not specify auto and custom options are same time')
endif

if use_auto
  use_gles = true
  use_vulkan = true
  use_composer = true
endif

gfxstream_host_args += '-DGLM_ENABLE_EXPERIMENTAL=1'
gfxstream_host_args += '-DGFXSTREAM_MESON_BUILD=1'
gfxstream_host_args += '-DGFXSTREAM_ENABLE_HOST_GLES=@0@'.format(use_gles ? '1' : '0')

#==========================#
# Includes + Subdirs       #
#==========================#

inc_root = include_directories('.')
# Included by all host component builds. Leave empty for future build updates.
inc_include = include_directories()

subdir('third_party')
subdir('common')
subdir('host')

#================#
# Summary        #
#================#

summary({'c_args': (' ').join(get_option('c_args')),
         'cpp_args': (' ').join(get_option('cpp_args')),
         'buildtype': get_option('buildtype'),
         'log-level': log_level,
         'gles': use_gles,
         'vulkan': use_vulkan,
         'composer': use_composer,
        }, section: 'Configuration')
