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

if(APPLE)
    set(gfxstream_host_testing_oswindow_support_platform_sources
        osx/OSXWindow.mm)
elseif(WIN32)
    set(gfxstream_host_testing_oswindow_support_platform_sources
        windows/WindowsTimer.cpp
        windows/Windows_system_utils.cpp
        windows/win32/Win32Window.cpp)
elseif(QNX)
    set(gfxstream_host_testing_oswindow_support_platform_sources
        qnx/QNXWindow.cpp)
else()
    set(gfxstream_host_testing_oswindow_support_platform_sources
        x11/X11Window.cpp)
endif()

add_library(
    gfxstream_host_testing_oswindow_support
    OSWindow.cpp
    ${gfxstream_host_testing_oswindow_support_platform_sources}
    )

target_include_directories(
    gfxstream_host_testing_oswindow_support
    PUBLIC
    include
    PRIVATE
    .
    )
target_link_libraries(
    gfxstream_host_testing_oswindow_support
    PUBLIC
    gfxstream_common_base
    gfxstream_opengl_headers
    )

if(LINUX)
    target_link_libraries(
        gfxstream_host_testing_oswindow_support
        PRIVATE
        X11
        )
elseif(APPLE)
    target_link_libraries(
        gfxstream_host_testing_oswindow_support
        PRIVATE
        "-framework AppKit"
        "-framework QuartzCore")
endif()
