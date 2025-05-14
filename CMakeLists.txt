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

cmake_policy(SET CMP0048 NEW)
# Set this policy to allow gfxstream to modify the passed in targets like
# ${GFXSTREAM_HOST_COMMON_LIB}.
cmake_policy(SET CMP0079 NEW)
get_directory_property(hasParent PARENT_DIRECTORY)
if(NOT hasParent)
    project(gfx-streaming-kit)
endif()
cmake_minimum_required(VERSION 3.18)

option(ENABLE_VKCEREAL_TESTS "Enable building vulkan-cereal unittests" OFF)
option(BUILD_ASAN_WIN32 "Build with ASAN on Windows platform" OFF)

set(VALID_DEPENDENCY_RESOLUTION AOSP SYSTEM)
set(DEPENDENCY_RESOLUTION "AOSP"  CACHE STRING "\
How to resolve the dependencies. Currently there are 3 options: AOSP, and SYSTEM. AOSP \
will assume the repo is checked out in an Android tree, and find the dependencies through \
relative paths. SYSTEM will use the cmake find_package to identify the package.")
set_property(CACHE DEPENDENCY_RESOLUTION PROPERTY STRINGS ${VALID_DEPENDENCY_RESOLUTION})
if(NOT DEPENDENCY_RESOLUTION IN_LIST VALID_DEPENDENCY_RESOLUTION)
    message(FATAL_ERROR "DEPENDENCY_RESOLUTION must be one of ${VALID_DEPENDENCY_RESOLUTION}.")
endif()

option(BUILD_STANDALONE "Build with standalone implementations of base/host-common libraries" ON)
if (BUILD_STANDALONE)
    # These libraries may be changed in non-standalone builds to inject another implementation of
    # these APIs.
    set(GFXSTREAM_HOST_COMMON_LIB "")
endif()

option(BUILD_GRAPHICS_DETECTOR "Build the graphics detector utility" OFF)

if (WIN32)
    add_definitions("-DUNICODE -D_UNICODE -DNOMINMAX -DEMUGL_BUILD -DVK_USE_PLATFORM_WIN32_KHR -DBUILDING_EMUGL_COMMON_SHARED")
endif()

option(VIRGL_RENDERER_UNSTABLE_APIS "Use unstable virglrenderer APIs" ON)
if(VIRGL_RENDERER_UNSTABLE_APIS)
    add_definitions(-DVIRGL_RENDERER_UNSTABLE_APIS)
endif()

add_definitions(-DGFXSTREAM_ENABLE_HOST_GLES=1)

option(ASTC_CPU_DECODING "Enable decoding ASTC textures on the CPU" OFF)

option(GFXSTREAM_ENABLE_HOST_TRACING "Enable Perfetto trace events on the host" OFF)
if(GFXSTREAM_ENABLE_HOST_TRACING)
    add_compile_definitions(GFXSTREAM_BUILD_WITH_TRACING)
    add_compile_definitions(GFXSTREAM_BUILD_WITH_PERFETTO_SDK)
endif()

# For now the caller of the cmake script is responsible to create the angle_shader_translator
# target.
option(USE_ANGLE_SHADER_PARSER "Build with ANGLE shader parser." OFF)

option(WITH_BENCHMARK "Builds benchmarking code" OFF)

if(UNIX AND NOT APPLE AND NOT QNX)
    set(LINUX TRUE)
endif()

find_package(Threads)
include(ExternalProject)
include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

include(GoogleTest)
enable_testing()
# Disable test discovery after build.
# By default, `gtest_discover_tests()` adds a post-build step to run the test executables in order to discover the test
# targets. This is problematic in some build environments. (for example: if cross-compiling)
set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE "PRE_TEST")

# set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_INSTALL_PREFIX ${CMAKE_BINARY_DIR}/distribution)
if (WIN32)
    SET(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /Zi")
else()
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g3 -O3")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

if (APPLE)
    add_compile_definitions(VK_USE_PLATFORM_MACOS_MVK)
    add_compile_definitions(VK_USE_PLATFORM_METAL_EXT)
elseif(QNX)
    # TODO(jsimonot): fix build error
    # add_compile_definitions(VK_USE_PLATFORM_SCREEN_QNX)
elseif(UNIX)
    # TODO(kaiyili, b/179477624): Add Linux specific Vulkan platform macro definitions
    # Use X11 version of EGL platform specific definitions.
    add_compile_definitions(USE_X11)
elseif(WIN32)
    add_compile_definitions(VK_USE_PLATFORM_WIN32_KHR)
endif()

add_compile_definitions(GLM_FORCE_RADIANS)
add_compile_definitions(GLM_FORCE_DEFAULT_ALIGNED_GENTYPES)
add_compile_definitions(VK_GFXSTREAM_STRUCTURE_TYPE_EXT)

if (MSVC)
    # ask msvc not to warn not secure C ISO functions
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    # ask msvc not to warn non C ISO POSIX functions
    add_compile_definitions(_CRT_NONSTDC_NO_DEPRECATE)
endif()

if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # generate PDB files
    add_link_options("/DEBUG")
endif()

# Uncomment for ASAN support
# set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer -fsanitize=address")
# set (CMAKE_LINKER_FLAGS "${CMAKE_LINKER_FLAGS} -fno-omit-frame-pointer -fsanitize=address")

if (WIN32)
    if (BUILD_ASAN_WIN32)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
        # ASAN does not work with flag /MDd, replace it with /MD
        string(REPLACE "/MDd" "/MD" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        set(CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG})

        # ASAN linker
        # User needs to use -D ASAN_LIB_DIR:STRING=/path/to/asan_libs to add library directory
        if (NOT DEFINED ASAN_LIB_DIR)
            message(FATAL_ERROR "Please input ASAN library path with -D ASAN_LIB_DIR:STRING=/path/to/asan_lib_dir")
        endif()
        link_libraries(clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib)
        message("Linking ASAN libraries from: ${ASAN_LIB_DIR}")
        link_directories(${ASAN_LIB_DIR})
    endif()
endif()

set(GFXSTREAM_REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

if(USE_ANGLE_SHADER_PARSER)
    add_compile_definitions(USE_ANGLE_SHADER_PARSER)
endif()

if(CONFIG_AEMU)
    include(android.cmake)
endif()

set(EXTRA_SUBDIR_TEST_INCLUDE_FILES)

add_subdirectory(third_party)
add_subdirectory(common)
add_subdirectory(codegen)
add_subdirectory(host)

if (ENABLE_VKCEREAL_TESTS)
    list(APPEND EXTRA_SUBDIR_TEST_INCLUDE_FILES ${CMAKE_SOURCE_DIR}/toolchain/cmake/test_properties.cmake)
endif()

include(toolchain/cmake/SetSubdirectorProperties.cmake)
