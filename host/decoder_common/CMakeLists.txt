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
    set(gfxstream_host_decoder_common-platform-sources)
elseif (WIN32)
    set(gfxstream_host_decoder_common-platform-sources)
elseif (QNX)
    set(gfxstream_host_decoder_common-platform-sources)
else()
    set(gfxstream_host_decoder_common-platform-sources X11Support.cpp)
endif()

add_library(
    gfxstream_host_decoder_common
    ChecksumCalculator.cpp
    ChecksumCalculatorThreadInfo.cpp
    glUtils.cpp
    ${gfxstream_host_decoder_common-platform-sources})
if (NOT MSVC)
    target_compile_options(
        gfxstream_host_decoder_common PRIVATE -fvisibility=hidden)
endif()
target_link_libraries(
    gfxstream_host_decoder_common
    PUBLIC
    gfxstream_backend_headers
    gfxstream_common_base
    gfxstream_host_library
    gfxstream_common_logging
    gfxstream_opengl_headers
    gfxstream_x11_headers
    )
target_include_directories(
    gfxstream_host_decoder_common
    PUBLIC
    .
    include
    )
