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

if(WIN32)
    add_subdirectory(windows)
endif()

add_library(gfxstream_common_base.headers
            INTERFACE)

target_include_directories(gfxstream_common_base.headers
                           INTERFACE
                           include)

if(WIN32)
    target_compile_definitions(gfxstream_common_base.headers
                               INTERFACE
                               WIN32_LEAN_AND_MEAN)
endif()
if(MSVC)
    target_link_libraries(gfxstream_common_base.headers
                          INTERFACE
                          gfxstream_common_base_windows_compat)
endif()

if(ENABLE_CLANG_THREAD_SAFETY_CHECKS)
    target_compile_definitions(gfxstream_common_base.headers
                               INTERFACE
                               -D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS)
    target_compile_options(gfxstream_common_base.headers
                           INTERFACE
                           -Wthread-safety)
endif()

set(gfxstream_common_base_srcs
    AlignedBuf.cpp
    CpuTime.cpp
    FileUtils.cpp
    FunctorThread.cpp
    LayoutResolver.cpp
    MessageChannel.cpp
    PathUtils.cpp
    StringFormat.cpp
    System.cpp
    Tracing.cpp
    )

set(gfxstream_common_base_posix_srcs
    SharedMemory_posix.cpp
    Thread_pthread.cpp)

set(gfxstream_common_base_windows_srcs
    msvc.cpp
    SharedMemory_win32.cpp
    Thread_win32.cpp
    Win32UnicodeString.cpp)

if(GFXSTREAM_USE_LZ4)
    list(APPEND gfxstream_common_base_srcs
            CompressingStream.cpp
            DecompressingStream.cpp)
endif()

if(APPLE)
    set(gfxstream_common_base_platform_srcs
        ${gfxstream_common_base_posix_srcs}
        system-native-mac.mm)
elseif(WIN32)
    set(gfxstream_common_base_platform_srcs
        ${gfxstream_common_base_windows_srcs})
else()
    set(gfxstream_common_base_platform_srcs
        ${gfxstream_common_base_posix_srcs})
endif()

set(gfxstream_common_base_srcs
    ${gfxstream_common_base_srcs}
    ${gfxstream_common_base_platform_srcs})


add_library(gfxstream_common_base ${gfxstream_common_base_srcs})
if (BUILD_SHARED_LIBS)
    set_target_properties(
        gfxstream_common_base
        PROPERTIES
        VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}
        SOVERSION ${VERSION_MAJOR})
endif()

target_compile_definitions(gfxstream_common_base PRIVATE)

if (WIN32)
    set(gfxstream_common_base_platform_deps
        Shlwapi)
elseif (QNX)
    set(gfxstream_common_base_platform_deps
        dl)
elseif(LINUX)
    set(gfxstream_common_base_platform_deps
        dl
        rt)
elseif(APPLE)
    set(gfxstream_common_base_platform_deps
        "-framework Foundation"
        "-framework AppKit"
        "-framework IOKit")
endif()

target_link_libraries(
    gfxstream_common_base
    PUBLIC
    gfxstream_common_base.headers
    PRIVATE
    ${gfxstream_common_base_platform_deps})

if (NOT MSVC)
    target_compile_options(
        gfxstream_common_base PRIVATE -fvisibility=default)
endif()

if(GFXSTREAM_USE_PERFETTO)
    target_compile_definitions(gfxstream_common_base
                                PRIVATE
                                "USE_PERFETTO_TRACING")
    target_link_libraries(gfxstream_common_base
                            PRIVATE
                            perfetto-tracing-only)
endif()

if(GFXSTREAM_USE_LZ4)
    target_link_libraries(gfxstream_common_base
                            PRIVATE
                            lz4_static)
endif()

if (APPLE)
    set(gfxstream_common_base_test_srcs
        "")
    set(gfxstream_common_base_test_deps
        "")
elseif (WIN32)
    set(gfxstream_common_base_test_srcs
        Win32UnicodeString_unittest.cpp)
    set(gfxstream_common_base_test_deps
        gfxstream_common_base_windows_compat)
else()
    set(gfxstream_common_base_test_srcs
        "")
    set(gfxstream_common_base_test_deps
        "")
endif()

if (ENABLE_VKCEREAL_TESTS)
    # Tests
    add_executable(gfxstream_common_base_unittests
                   AlignedBuf_unittest.cpp
                   ArraySize_unittest.cpp
                   LayoutResolver_unittest.cpp
                   LruCache_unittest.cpp
                   ManagedDescriptor_unittest.cpp
                   Optional_unittest.cpp
                   StringFormat_unittest.cpp
                   TypeTraits_unittest.cpp
                   WorkerThread_unittest.cpp
                   HybridEntityManager_unittest.cpp
                   ${gfxstream_common_base_test_srcs})

    target_link_libraries(gfxstream_common_base_unittests
                          PUBLIC
                          gfxstream_common_base
                          ${gfxstream_common_base_test_deps}
                          gmock
                          gtest_main)

endif()
