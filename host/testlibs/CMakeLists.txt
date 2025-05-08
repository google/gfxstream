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

if (APPLE)
    set(oswindow-platform-sources
        osx/OSXWindow.mm)
elseif (WIN32)
    set(oswindow-platform-sources
        windows/WindowsTimer.cpp
        windows/Windows_system_utils.cpp
        windows/win32/Win32Window.cpp)
elseif(QNX)
    set(oswindow-platform-sources
        qnx/QNXWindow.cpp)
else()
    set(oswindow-platform-sources
        x11/X11Window.cpp)
endif()

add_library(
    OSWindow
    OSWindow.cpp
    ${oswindow-platform-sources})
target_link_libraries(
    OSWindow
    PRIVATE
    gfxstream_common_base
    gfxstream_egl_headers)
target_include_directories(
    OSWindow
    PUBLIC
    .
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}/third_party/glm/include)

if (LINUX)
    target_link_libraries(
        OSWindow
        PRIVATE
        X11
        )
elseif (APPLE)
    target_link_libraries(
        OSWindow
        PRIVATE
        "-framework AppKit -framework QuartzCore")
endif()
