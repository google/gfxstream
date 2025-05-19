# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT OR Apache-2.0

project('gfxstream', 'cpp', 'c',
         version : '0.1.2',
         license : 'MIT OR Apache-2.0',
         default_options : ['cpp_std=gnu++17',
                            'b_ndebug=if-release'])

cc = meson.get_compiler('cpp')
prog_python = import('python').find_installation('python3')

#================#
# Subdirectories #
#================#

inc_opengl_headers = include_directories('third_party/opengl/include')

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
