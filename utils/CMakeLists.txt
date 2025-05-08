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

add_library(
    gfxstream_utils.headers
    INTERFACE)

target_include_directories(
    gfxstream_utils.headers
    INTERFACE
    include)
target_link_libraries(
    gfxstream_utils.headers
    INTERFACE
    renderdoc
    gfxstream_vulkan_headers)

add_library(
    gfxstream_utils
    OBJECT
    GfxApiLogger.cpp)
target_link_libraries(
    gfxstream_utils
    PUBLIC
    gfxstream_utils.headers)

if (ENABLE_VKCEREAL_TESTS)
    add_executable(
        gfxstream_utils_unittests
        RenderDoc_unittest.cpp
        GfxApiLogger_unittest.cpp)

    target_link_libraries(
        gfxstream_utils_unittests
        PRIVATE
        gfxstream_host_library
        gfxstream_host_logging
        gfxstream_utils
        ${GFXSTREAM_BASE_LIB}
        renderdoc
        gtest_main
        gmock_main)

    gtest_discover_tests(gfxstream_utils_unittests)
endif()
