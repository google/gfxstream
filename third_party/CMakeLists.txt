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

# Configure and add `astc-encoder`, if needed
if (ASTC_CPU_DECODING)
    set(DECOMPRESSOR ON)   # Disable compression code
    set(CLI OFF)           # Disable the command line interface

    # Compile with the AVX2 instruction set. This is the fastest option available on x86_64.
    # At run time, if the CPU doesn't support AVX2, the library will simply return an error status
    # during initialization and we will fall back on the compute shader to decompress ASTC textures.
    #
    # In the future, we should define `ASTCENC_DYNAMIC_LIBRARY` and build multiple versions of the
    # library for each SIMD architecture, and dynamically load the fastest one at run time.
    # See also: https://github.com/ARM-software/astc-encoder/issues/79
    set(ISA_AVX2 ON)

    add_subdirectory(astc-encoder)
endif ()

# Enable perfetto in CMake if needed
# add_subdirectory(perfetto-tracing-only)

add_subdirectory(drm)
add_subdirectory(glm)
add_subdirectory(opengl)
add_subdirectory(renderdoc)
add_subdirectory(stb)
add_subdirectory(vulkan)
add_subdirectory(x11)

if(NOT TARGET gtest_main AND ENABLE_VKCEREAL_TESTS)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    if(DEPENDENCY_RESOLUTION STREQUAL "AOSP")
        set(GOOGLETEST_PATH ${PROJECT_SOURCE_DIR}/../../../external/googletest)
        if(EXISTS ${GOOGLETEST_PATH})
            add_subdirectory(${GOOGLETEST_PATH} googletest)
        endif()
    elseif(DEPENDENCY_RESOLUTION STREQUAL "SYSTEM")
        find_package(PkgConfig REQUIRED)
        pkg_search_module(gtest REQUIRED IMPORTED_TARGET GLOBAL gtest)
        pkg_search_module(gtest_main REQUIRED IMPORTED_TARGET GLOBAL gtest_main)
        pkg_search_module(gmock REQUIRED IMPORTED_TARGET GLOBAL gmock)
        pkg_search_module(gmock_main REQUIRED IMPORTED_TARGET GLOBAL gmock_main)
        add_library(gtest ALIAS PkgConfig::gtest)
        add_library(gtest_main ALIAS PkgConfig::gtest_main)
        add_library(gmock ALIAS PkgConfig::gmock)
        add_library(gmock_main ALIAS PkgConfig::gmock_main)
    endif()
endif()

if(NOT TARGET flatbuffers)
    set(FLATBUFFERS_BUILD_TESTS OFF)
    set(FLATBUFFERS_BUILD_CPP17 ON)

    if(DEPENDENCY_RESOLUTION STREQUAL "AOSP")
        set(FLATBUFFERS_PATH ${PROJECT_SOURCE_DIR}/../../../external/flatbuffers)
        if(EXISTS ${FLATBUFFERS_PATH})
            add_subdirectory(${FLATBUFFERS_PATH} flatbuffers)
        endif()
    elseif(DEPENDENCY_RESOLUTION STREQUAL "SYSTEM")
        find_package(PkgConfig REQUIRED)
        pkg_search_module(flatbuffers REQUIRED IMPORTED_TARGET GLOBAL flatbuffers>=2.0.6)
        add_library(flatbuffers ALIAS PkgConfig::flatbuffers)
        find_program(FLATC flatc NO_CACHE REQUIRED)
        add_executable(flatc IMPORTED GLOBAL)
        set_property(TARGET flatc PROPERTY IMPORTED_LOCATION ${FLATC})
    endif()
endif()

if (QNX) # QNX SDP provides EGL headers
    add_library(gfxstream_egl_headers INTERFACE)
    target_include_directories(gfxstream_egl_headers INTERFACE ${QNX_TARGET}/usr/include)
endif()

if(NOT TARGET gfxstream_egl_headers)
    if(DEPENDENCY_RESOLUTION STREQUAL "AOSP")
        # ANGLE provides the EGL headers for us.
        set(ANGLE_PATH ${PROJECT_SOURCE_DIR}/../../../external/angle)
        if(NOT EXISTS ${ANGLE_PATH})
            message(FATAL_ERROR "ANGLE is not found.")
        endif()
        add_library(gfxstream_egl_headers INTERFACE)
        target_include_directories(gfxstream_egl_headers INTERFACE ${ANGLE_PATH}/include)
    elseif(DEPENDENCY_RESOLUTION STREQUAL "SYSTEM")
        find_package(PkgConfig REQUIRED)
        pkg_search_module(egl REQUIRED IMPORTED_TARGET GLOBAL egl>=1.5)
        add_library(gfxstream_egl_headers ALIAS PkgConfig::egl)
    endif()
endif()

if(WITH_BENCHMARK)
    # Add Google's benchmarking framework
    set(BENCHMARK_ENABLE_EXCEPTIONS OFF)  # We disable exceptions in gfxstream
    set(BENCHMARK_ENABLE_TESTING OFF)     # Don't build the unit tests for the library, to save time
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF)

    if(DEPENDENCY_RESOLUTION STREQUAL "AOSP")
        set(GOOGLE_BENCHMARK_PATH ${PROJECT_SOURCE_DIR}/../../../external/google-benchmark)
        if(EXISTS ${GOOGLE_BENCHMARK_PATH})
            add_subdirectory(${GOOGLE_BENCHMARK_PATH} google-benchmark)
        endif()
    elseif(DEPENDENCY_RESOLUTION STREQUAL "SYSTEM")
        message(FATAL_ERROR "Not implemented")
    endif()
endif()

if(GFXSTREAM_ENABLE_HOST_TRACING)
    if(DEPENDENCY_RESOLUTION STREQUAL "AOSP")
        set(GFXSTREAM_PERFETTO_PATH ${PROJECT_SOURCE_DIR}/../../../external/perfetto)
        if(NOT EXISTS ${GFXSTREAM_PERFETTO_PATH})
            message(FATAL_ERROR "Perfetto is not found.")
        endif()
        add_library(perfetto
                    STATIC
                    ${GFXSTREAM_PERFETTO_PATH}/sdk/perfetto.cc)
        target_include_directories(perfetto
                                   INTERFACE
                                   ${GFXSTREAM_PERFETTO_PATH}/sdk)
    endif()
endif()

if(NOT TARGET flatbuffers)
    message(FATAL_ERROR "The dependency flatbuffers not found.")
endif()
