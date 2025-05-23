# Copyright 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# GLES decoder
add_subdirectory(gles1_dec)
add_subdirectory(gles2_dec)

# GLES dispatch based on Translator
add_subdirectory(OpenGLESDispatch)

# GLES translator
add_subdirectory(glestranslator)
add_subdirectory(glsnapshot)

add_library(gfxstream-gl-server
            BufferGl.cpp
            ColorBufferGl.cpp
            CompositorGl.cpp
            DebugGl.cpp
            DisplayGl.cpp
            DisplaySurfaceGl.cpp
            EmulatedEglConfig.cpp
            EmulatedEglContext.cpp
            EmulatedEglFenceSync.cpp
            EmulatedEglImage.cpp
            EmulatedEglWindowSurface.cpp
            EmulationGl.cpp
            GLESVersionDetector.cpp
            ReadbackWorkerGl.cpp
            RenderThreadInfoGl.cpp
            TextureDraw.cpp
            TextureResize.cpp
            YUVConverter.cpp)

target_link_libraries(
        gfxstream-gl-server
        PUBLIC
        gfxstream_backend_headers
        gfxstream_backend.headers
        gfxstream_common_logging
        gfxstream_features
        gfxstream_host_backend
        gfxstream_host_health
        gfxstream_opengl_headers
        gfxstream_openglesdispatch
        gfxstream_vulkan_headers
        gles1_dec
        gles2_dec
        GLcommon
        GLSnapshot)

target_include_directories(gfxstream-gl-server
                           PRIVATE
                           ${GFXSTREAM_REPO_ROOT}
                           ${GFXSTREAM_REPO_ROOT}/gldispatch/include
                           ${GFXSTREAM_REPO_ROOT}/host
                           ${GFXSTREAM_REPO_ROOT}/host/gl
                           ${GFXSTREAM_REPO_ROOT}/third_party/glm/include)
