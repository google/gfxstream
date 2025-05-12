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
    renderControl_dec
    renderControl_dec.cpp
    renderControl_server_context.cpp)
target_link_libraries(
    renderControl_dec
    PUBLIC
    gfxstream_host_decoder_common
    PRIVATE
    gfxstream_common_base
    gfxstream_egl_headers
    gfxstream_host_logging
    gfxstream_host_iostream
    )
target_include_directories(
    renderControl_dec
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/include
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common)
