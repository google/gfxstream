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

set(egl-translator-common-sources
    client_api_exts.cpp
    egl_config.cpp
    egl_context.cpp
    egl_display.cpp
    egl_global_info.cpp
    egl_imp.cpp
    egl_pbuffer_surface.cpp
    egl_surface.cpp
    egl_thread_info.cpp
    egl_validate.cpp
    egl_window_surface.cpp
    shader_cache.cpp
    thread_info.cpp)
set(egl-translator-windows-sources
    core_profile_configs_windows.cpp
    egl_os_api_egl.cpp
    egl_os_api_wgl.cpp)
set(egl-translator-darwin-sources
    egl_os_api_darwin.cpp
    egl_os_api_egl.cpp
    mac_native.m
    mac_pixel_formats_attribs.m)
set(egl-translator-linux-sources
    core_profile_configs_linux.cpp
    egl_os_api_egl.cpp
    egl_os_api_glx.cpp
    x11_error_handler.cpp)
set(egl-translator-qnx-sources
    egl_os_api_egl.cpp)
if (WIN32)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-windows-sources})
elseif (APPLE)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-darwin-sources})
elseif (QNX)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-qnx-sources})
else()
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-linux-sources})
endif()

target_link_libraries(
    EGL_translator_static
    PUBLIC
    GLcommon
    gfxstream_host_decoder_common
    PRIVATE
    gfxstream_common_utils
    gfxstream_host_common
    )
target_link_libraries(EGL_translator_static PUBLIC GLES_CM_translator_static GLES_V2_translator_static)
if (NOT MSVC)
    target_compile_options(EGL_translator_static PRIVATE -fvisibility=hidden)
endif()
target_compile_options(
    EGL_translator_static
    PRIVATE -Wno-inconsistent-missing-override -Wno-macro-redefined)

if (APPLE)
    target_compile_options(EGL_translator_static PRIVATE -Wno-deprecated-declarations)
    target_compile_options(EGL_translator_static PRIVATE "-Wno-deprecated-declarations")
endif()

if (WIN32)
elseif (APPLE)
    target_link_libraries(EGL_translator_static PUBLIC "-framework AppKit -framework IOSurface")
elseif (QNX)
    target_link_libraries(EGL_translator_static PUBLIC "-lscreen -lregex -lEGL -lGLESv2")
else()
    target_link_libraries(EGL_translator_static PUBLIC "-ldl -lpthread")
endif()
