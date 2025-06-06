# Copyright 2025 Android Open Source Project
# SPDX-License-Identifier: Apache-2.0

if use_vulkan
  inc_vulkan_headers = include_directories('vulkan/include')

  if cc.has_header('renderdoc_app.h')
    inc_renderdoc_external = include_directories()
  else
    inc_renderdoc_external = include_directories('renderdoc/include')
  endif
endif

if cc.has_header('glm/glm.hpp')
  inc_glm = include_directories()
else
  inc_glm = include_directories('glm/include')
endif

inc_x11_headers = include_directories('x11/include')

inc_opengl_headers = include_directories('opengl/include')
inc_common_opengl = include_directories('opengl/include')
inc_drm_headers = include_directories('drm/include')

