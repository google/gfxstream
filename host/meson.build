# Copyright 2023 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

gfxstream_headers = files(
  'include/gfxstream/virtio-gpu-gfxstream-renderer.h',
  'include/gfxstream/virtio-gpu-gfxstream-renderer-unstable.h')

inc_gfxstream_server = include_directories('include')
inc_stream_servers = include_directories('.')
inc_host_include = include_directories('include')

inc_gl_common = include_directories('gl/glestranslator/common/include')

subdir('features')
subdir('iostream')
subdir('library')
subdir('tracing')
subdir('renderdoc')
subdir('snapshot')
subdir('decoder_common')
subdir('address_space')
subdir('backend')

inc_gfxstream_backend = [
  inc_host_decoder_common,
  inc_common_base,
  inc_common_utils,
  inc_drm_headers,
  inc_glm,
  inc_host_address_space,
  inc_host_backend,
  inc_host_features,
  inc_host_include,
  inc_host_iostream,
  inc_host_library,
  inc_common_logging,
  inc_host_renderdoc,
  inc_host_snapshot,
  inc_host_tracing,
  inc_include,
  inc_opengl_headers,
  inc_root,
]

link_gfxstream_backend = [
  lib_host_decoder_common,
  lib_common_base,
  lib_common_utils,
  lib_host_address_space,
  lib_host_backend,
  lib_host_features,
  lib_host_iostream,
  lib_host_library,
  lib_common_logging,
  lib_host_snapshot,
  lib_host_tracing,
]

files_lib_gfxstream_backend = files(
  'buffer.cpp',
  'channel_stream.cpp',
  'color_buffer.cpp',
  'frame_buffer.cpp',
  'hwc2.cpp',
  'post_worker.cpp',
  'read_buffer.cpp',
  'render_api.cpp',
  'render_channel_impl.cpp',
  'renderer_impl.cpp',
  'render_lib_impl.cpp',
  'render_thread.cpp',
  'render_thread_info.cpp',
  'render_window.cpp',
  'ring_stream.cpp',
  'sync_thread.cpp',
  'virtio_gpu_context.cpp',
  'virtio_gpu_frontend.cpp',
  'virtio_gpu_gfxstream_renderer.cpp',
  'virtio_gpu_pipe.cpp',
  'virtio_gpu_resource.cpp',
  'virtio_gpu_ring_blob.cpp',
  'virtio_gpu_timelines.cpp',
  'vsync_thread.cpp',
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

  files_lib_gfxstream_backend += files('post_worker_gl.cpp')
  files_lib_gfxstream_backend += files('render_control.cpp')

  inc_gfxstream_backend += [
    inc_gl_common,
    inc_gl_openglesdispatch,
    inc_gl_server,
    inc_gl_snapshot,
    inc_opengl_headers,
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
  files_lib_gfxstream_backend += files('native_sub_window_stub.cpp')
elif host_machine.system() == 'darwin'
  files_lib_gfxstream_backend += files('native_sub_window_cocoa.mm')
elif host_machine.system() == 'windows'
  files_lib_gfxstream_backend += files('native_sub_window_win32.cpp')
elif host_machine.system() == 'linux' and use_gles
  files_lib_gfxstream_backend += files('native_sub_window_x11.cpp')
elif host_machine.system() == 'qnx'
  files_lib_gfxstream_backend += files(
    'native_sub_window_qnx.cpp',
  )
endif

gfxstream_backend_cpp_args = [
  '-Wno-unused-parameter',
  '-Wno-unused-variable',
  '-Wno-unused-function',
  '-DVK_GFXSTREAM_STRUCTURE_TYPE_EXT',
]

if host_machine.system() == 'windows'
  gfxstream_backend_cpp_args += '-DVK_USE_PLATFORM_WIN32_KHR'
elif host_machine.system() == 'darwin'
  gfxstream_backend_cpp_args += '-DVK_USE_PLATFORM_METAL_EXT'
  gfxstream_backend_cpp_args += '-DVK_USE_PLATFORM_MACOS_MVK'
elif host_machine.system() == 'linux'
  gfxstream_backend_cpp_args += '-DVK_USE_PLATFORM_XCB_KHR'
endif

deps_gfxstream_backend = []

link_args_gfxstream_backend = ''

if host_machine.system() == 'linux'
  deps_gfxstream_backend += [
    thread_dep,
  ]
  link_args_gfxstream_backend = '-Wl,-lpthread,-lrt,-ldl'
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
