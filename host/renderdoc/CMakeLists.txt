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
    gfxstream_host_renderdoc
    INTERFACE)

target_include_directories(
    gfxstream_host_renderdoc
    INTERFACE
    include)
target_link_libraries(
    gfxstream_host_renderdoc
    INTERFACE
    renderdoc
    gfxstream_vulkan_headers)

if (ENABLE_VKCEREAL_TESTS)
    # AEMU build seems to have strange rpath setup which results in
    # `libc++.so: cannot open shared object file: No such file or directory`.
    if(NOT CONFIG_AEMU)
        add_executable(
            gfxstream_host_renderdoc_tests
            RenderDoc_unittest.cpp)

        target_link_libraries(
            gfxstream_host_renderdoc_tests
            PRIVATE
            gfxstream_common_logging
            gfxstream_host_library
            gfxstream_host_renderdoc
            renderdoc
            gtest_main
            gmock_main)

        gtest_discover_tests(
            gfxstream_host_renderdoc_tests)
    endif()
endif()
