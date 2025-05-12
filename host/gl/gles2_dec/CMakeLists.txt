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
    gles2_dec
    gles2_dec.cpp
    gles2_server_context.cpp
    GLESv2Decoder.cpp)
target_link_libraries(
    gles2_dec
    PUBLIC
    gfxstream_host_decoder_common
    gfxstream_host_iostream
    GLSnapshot
    ${GFXSTREAM_BASE_LIB}
    PRIVATE
    gfxstream_common_base
    gfxstream_egl_headers
    gfxstream_host_backend
    gfxstream_host_library
    gfxstream_host_logging
    )
target_include_directories(
    gles2_dec
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common)
