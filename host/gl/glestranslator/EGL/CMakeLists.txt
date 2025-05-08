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

set(egl-translator-common-sources
    ClientAPIExts.cpp
    EglConfig.cpp
    EglContext.cpp
    EglDisplay.cpp
    EglGlobalInfo.cpp
    EglImp.cpp
    EglPbufferSurface.cpp
    EglSurface.cpp
    EglThreadInfo.cpp
    EglValidate.cpp
    EglWindowSurface.cpp
    ShaderCache.cpp
    ThreadInfo.cpp)
set(egl-translator-windows-sources
    CoreProfileConfigs_windows.cpp EglOsApi_egl.cpp EglOsApi_wgl.cpp)
set(egl-translator-darwin-sources
    EglOsApi_darwin.cpp EglOsApi_egl.cpp MacNative.m
    MacPixelFormatsAttribs.m)
set(egl-translator-linux-sources
    CoreProfileConfigs_linux.cpp EglOsApi_egl.cpp EglOsApi_glx.cpp X11ErrorHandler.cpp)
set(egl-translator-qnx-sources
    EglOsApi_egl.cpp)
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
    gfxstream_host_backend
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
