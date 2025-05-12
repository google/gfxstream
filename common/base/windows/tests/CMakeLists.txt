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

if(WINDOWS_MSVC_X86_64)
  android_nasm_compile(
    TARGET hello_world_asm_lib NODISTRIBUTE SRC # cmake-format: sortable
                                                yasm/hello_world.asm)
  android_add_test(
    TARGET hello_world_asm_test SRC # cmake-format: sortable
                                    yasm/hello_world_unittest.cpp)
  target_link_libraries(hello_world_asm_test PRIVATE hello_world_asm_lib
                                                     gmock_main)
endif()

add_subdirectory(compiler)
