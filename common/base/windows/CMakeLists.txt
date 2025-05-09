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

if(TARGET gfxstream_common_base_windows_compat)
  return()
endif()

if(INCLUDE_ANDROID_CMAKE)
  # This is a posix wrapper for windows-msvc build.
  android_nasm_compile(TARGET
                       setjmp_asm_li
                       LICENSE
                       Apache-2.0
                       SRC
                       src/setjmp.asm)

  android_add_library(TARGET
                      gfxstream_common_base_windows_compat
                      LICENSE
                      Apache-2.0
                      SRC
                      "")
  target_link_libraries(gfxstream_common_base_windows_compat
                        PRIVATE
                        setjmp_asm_lib)
else()
  add_library(gfxstream_common_base_windows_compat)
endif()

target_sources(
    gfxstream_common_base_windows_compat
    PRIVATE
    src/asprintf.c
    src/files.cpp
    src/msvc-posix.c
    src/pread.cpp
    src/time.cpp)

# Msvc redefines macro's to inject compatibility.
target_compile_options(
    gfxstream_common_base_windows_compat
    PUBLIC
    "-Wno-macro-redefined"
    "-Wno-deprecated-declarations" # A lot of the POSIX names are deprecated...
    )

target_include_directories(
    gfxstream_common_base_windows_compat
    PUBLIC
    includes)
