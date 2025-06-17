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

# Use EGL platform specific definitions.
if (APPLE)
    add_compile_definitions(__APPLE__)
elseif(UNIX)
    add_compile_definitions(USE_X11)
endif()

add_library(
    gfxstream_openglesdispatch.headers
    INTERFACE)
target_link_libraries(
    gfxstream_openglesdispatch.headers
    INTERFACE
    gfxstream_host_backend.headers
    gfxstream_opengl_headers)
target_include_directories(
    gfxstream_openglesdispatch.headers
    INTERFACE
    include)

add_library(
    gfxstream_openglesdispatch
    EGLDispatch.cpp
    gles1_stubbed_in_translator_namespace.cpp
    gles2_stubbed_in_translator_namespace.cpp
    GLESv1Dispatch.cpp
    GLESv2Dispatch.cpp
    OpenGLDispatchLoader.cpp
    StaticDispatch.cpp)

if (NOT MSVC)
    target_compile_options(gfxstream_openglesdispatch
                           PRIVATE
                           -fvisibility=default)
endif()

target_link_libraries(
    gfxstream_openglesdispatch
    PUBLIC
    gfxstream_openglesdispatch.headers
    gles1_dec
    gles2_dec
    gfxstream_host_backend
    gfxstream_host_decoder_common
    PRIVATE
    EGL_translator_static)

