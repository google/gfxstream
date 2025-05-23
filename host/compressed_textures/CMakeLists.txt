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

if (ASTC_CPU_DECODING)
    set(astc-cpu-decompressor-sources AstcCpuDecompressorImpl.cpp)
else()
    set(astc-cpu-decompressor-sources AstcCpuDecompressorNoOp.cpp)
endif()

add_library(
    gfxstream_host_compressed_textures
    ${astc-cpu-decompressor-sources})

target_link_libraries(
    gfxstream_host_compressed_textures
    PRIVATE
    gfxstream_etc)

target_include_directories(
    gfxstream_host_compressed_textures
    PUBLIC
    include)

if(ASTC_CPU_DECODING)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        target_link_libraries(
            gfxstream_host_compressed_textures
            PRIVATE
            astcdec-native-static)
    else()
        target_link_libraries(
            gfxstream_host_compressed_textures
            PRIVATE
            astcdec-avx2-static)
    endif()
endif()

if (ENABLE_VKCEREAL_TESTS)
    add_executable(
        gfxstream_host_compressed_textures_unittests
        AstcCpuDecompressor_unittest.cpp)

    target_link_libraries(
        gfxstream_host_compressed_textures_unittests
        PRIVATE
        gfxstream_host_compressed_textures
        ${GFXSTREAM_BASE_LIB}
        gtest_main
        gmock_main)

    gtest_discover_tests(gfxstream_host_compressed_textures_unittests)
endif()
