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
    GLES_V2_translator_static
    angle_shader_parser.cpp
    gles_v2_context.cpp
    gles_v2_imp.cpp
    gles_v2_validate.cpp
    program_data.cpp
    sampler_data.cpp
    shader_parser.cpp
    shader_validator.cpp
    transform_feedback_data.cpp)
if (NOT MSVC)
    target_compile_options(
        GLES_V2_translator_static
        PRIVATE
        -fvisibility=hidden)
endif()
target_compile_options(
    GLES_V2_translator_static
    PRIVATE
    -Wno-macro-redefined)
target_link_libraries(
    GLES_V2_translator_static
    PRIVATE
    GLcommon
    gfxstream_host_decoder_common
    gfxstream_host_backend
    gfxstream_common_logging)
