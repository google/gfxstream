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
    gles1_dec
    gles1_dec.cpp
    gles1_server_context.cpp
    GLESv1Decoder.cpp)
target_link_libraries(
    gles1_dec
    PUBLIC
    gfxstream_host_decoder_common
    gfxstream_host_iostream
    GLSnapshot
    PRIVATE
    gfxstream_common_base
    gfxstream_common_logging
    gfxstream_host_library
    gfxstream_opengl_headers
    )
target_include_directories(
    gles1_dec
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common)
