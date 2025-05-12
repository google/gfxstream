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
    GLSnapshot
    GLSnapshot.cpp)
target_link_libraries(
    GLSnapshot
    PUBLIC
    gfxstream_host_decoder_common
    gfxstream_openglesdispatch)
target_include_directories(
    GLSnapshot
    PUBLIC
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gl/OpenGLESDispatch
    ${GFXSTREAM_REPO_ROOT}/host/gl/glsnapshot)
