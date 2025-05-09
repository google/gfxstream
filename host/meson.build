# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: MIT

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

else
  dl_dep = dependency('dl')
  thread_dep = dependency('threads')
  gfxstream_host_args += '-DGFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC=1'
  gfxstream_host_args += '-DGFXSTREAM_UNSTABLE_VULKAN_DMABUF_WINSYS=1'
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

gfxstream_host_args += '-DGFXSTREAM_MESON_BUILD=1'
gfxstream_host_args += '-DGFXSTREAM_ENABLE_HOST_GLES=@0@'.format(use_gles ? '1' : '0')

#===============#
# Includes      #
#===============#

gfxstream_headers = files(
  'include/gfxstream/virtio-gpu-gfxstream-renderer.h',
  'include/gfxstream/virtio-gpu-gfxstream-renderer-unstable.h')

inc_gfxstream_server = include_directories('include')

inc_common_opengl = include_directories('../third_party/opengl/include')
inc_drm_headers = include_directories('../third_party/drm/include')

inc_root = include_directories('../')
inc_gfxstream_include = []
# Included by all host component builds. Leave empty for future build updates.
inc_include = include_directories()
inc_utils = include_directories('../utils/include')

if host_machine.system() == 'qnx'
  inc_gfxstream_include = [inc_qnx_headers, inc_gfxstream_include]
endif

if use_vulkan
  inc_vulkan_headers = include_directories('../third_party/vulkan/include')

  if cc.has_header('renderdoc_app.h')
    inc_renderdoc_external = include_directories()
  else
    inc_renderdoc_external = include_directories('../third_party/renderdoc/include')
  endif
endif

if cc.has_header('glm/glm.hpp')
  inc_glm = include_directories()
else
  inc_glm = include_directories('../third_party/glm/include')
endif

inc_x11_headers = include_directories('../third_party/x11/include')

inc_stream_servers = include_directories('.')
inc_host_include = include_directories('include')

inc_gl_common = include_directories('gl/glestranslator/GLcommon/include')

subdir('logging')
subdir('metrics')
subdir('features')
subdir('iostream')
subdir('health')
subdir('library')
subdir('tracing')
subdir('snapshot')
subdir('decoder_common')
subdir('address_space')
subdir('backend')

inc_gfxstream_backend = [
  inc_host_decoder_common,
  inc_common_base,
  inc_common_opengl,
  inc_common_utils,
  inc_drm_headers,
  inc_gfxstream_include,
  inc_glm,
  inc_host_address_space,
  inc_host_backend,
  inc_host_features,
  inc_host_health,
  inc_host_include,
  inc_host_iostream,
  inc_host_library,
  inc_host_logging,
  inc_host_metrics,
  inc_host_snapshot,
  inc_host_tracing,
  inc_include,
  inc_root,
  inc_utils,
]

link_gfxstream_backend = [
  lib_host_decoder_common,
  lib_common_base,
  lib_common_utils,
  lib_host_address_space,
  lib_host_backend,
  lib_host_features,
  lib_host_health,
  lib_host_iostream,
  lib_host_library,
  lib_host_logging,
  lib_host_metrics,
  lib_host_snapshot,
  lib_host_tracing,
]

files_lib_gfxstream_backend = files(
  'Buffer.cpp',
  'ChannelStream.cpp',
  'ColorBuffer.cpp',
  'FrameBuffer.cpp',
  'Hwc2.cpp',
  'PostWorker.cpp',
  'ReadBuffer.cpp',
  'render_api.cpp',
  'RenderChannelImpl.cpp',
  'RendererImpl.cpp',
  'RenderLibImpl.cpp',
  'RenderThread.cpp',
  'RenderThreadInfo.cpp',
  'RenderWindow.cpp',
  'RingStream.cpp',
  'SyncThread.cpp',
  'virtio-gpu-gfxstream-renderer.cpp',
  'VirtioGpuContext.cpp',
  'VirtioGpuFrontend.cpp',
  'VirtioGpuPipe.cpp',
  'VirtioGpuResource.cpp',
  'VirtioGpuRingBlob.cpp',
  'VirtioGpuTimelines.cpp',
  'VsyncThread.cpp',
)

if use_gles or use_vulkan
  subdir('compressed_textures')

  inc_gfxstream_backend += [
    inc_host_compressed_textures,
  ]
  link_gfxstream_backend + [
    lib_host_compressed_textures,
  ]
endif

if use_gles
  subdir('gl')

  files_lib_gfxstream_backend += files('PostWorkerGl.cpp')
  files_lib_gfxstream_backend += files('RenderControl.cpp')

  inc_gfxstream_backend += [
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_gl_server,
    inc_gl_snapshot,
  ]
  link_gfxstream_backend += [
    lib_gl_server,
  ]
endif

if use_vulkan
  subdir('vulkan')
  inc_gfxstream_backend += [inc_cereal, inc_cereal_common, inc_vulkan_headers,
                            inc_vulkan_server, inc_renderdoc_external]
  link_gfxstream_backend += lib_vulkan_server
endif

if use_composer
  subdir('renderControl_dec')
  link_gfxstream_backend += lib_composer
endif

if not use_gles
  files_lib_gfxstream_backend += files('NativeSubWindow_stub.cpp')
elif host_machine.system() == 'darwin'
  files_lib_gfxstream_backend += files('NativeSubWindow_cocoa.mm')
elif host_machine.system() == 'windows'
  files_lib_gfxstream_backend += files('NativeSubWindow_win32.cpp')
elif host_machine.system() == 'linux' and use_gles
  files_lib_gfxstream_backend += files('NativeSubWindow_x11.cpp')
elif host_machine.system() == 'qnx'
  files_lib_gfxstream_backend += files(
    'NativeSubWindow_qnx.cpp',
  )
endif

gfxstream_backend_cpp_args = [
  '-Wno-unused-parameter',
  '-Wno-unused-variable',
  '-Wno-unused-function',
  '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT',
]

deps_gfxstream_backend = []

link_args_gfxstream_backend = ''

if host_machine.system() == 'linux'
  deps_gfxstream_backend += [
    dl_dep,
    thread_dep,
  ]
  link_args_gfxstream_backend = '-Wl,-lpthread,-lrt'
endif

if host_machine.system() == 'qnx'
  deps_gfxstream_backend += [
    qnx_egl_dep,
    qnx_gles2_dep,
    qnx_screen_dep,
  ]
endif

gfxstream_backend = library(
  'gfxstream_backend',
  files_lib_gfxstream_backend,
  cpp_args: gfxstream_host_args + gfxstream_backend_cpp_args,
  include_directories: [inc_gfxstream_backend],
  gnu_symbol_visibility: 'default',
  dependencies: deps_gfxstream_backend,
  link_with: link_gfxstream_backend,
  link_args : link_args_gfxstream_backend,
  version: '0.1.2',
  install: true,
)

install_headers(gfxstream_headers,
                subdir: 'gfxstream')

pkg = import('pkgconfig')
pkg.generate(gfxstream_backend,
             description: 'gfxstream backend',
             extra_cflags: pkg_cflags,
             subdirs: 'gfxstream'
)
