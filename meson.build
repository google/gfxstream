# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

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

build = get_option('gfxstream-build')

with_guest = build.contains('guest') or build.contains('both')
with_host = build.contains('host') or build.contains('both')
with_guest_test = build.contains('guest-test')

#========================#
# Logging + error report #
#========================#
log_level = get_option('log-level')

#===============#
# Decoders      #
#===============#
decoders = get_option('decoders')

use_auto = decoders.contains('auto') and with_host
use_gles = decoders.contains('gles') and with_host
use_vulkan = decoders.contains('vulkan') and with_host
use_composer = decoders.contains('composer') and with_host

if with_guest or with_guest_test
  genvk = files('codegen/vulkan/vulkan-docs-next/scripts/genvk.py')
  vk_xml_gfxstream = files('codegen/vulkan/vulkan-docs-next/xml/vk.xml')
  vk_gfxstream_xml = files('codegen/vulkan/vulkan-docs-next/xml/vk_gfxstream.xml')
  subdir('guest')
endif
if with_host and not with_guest_test
  subdir('common')
  subdir('host')
endif

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
