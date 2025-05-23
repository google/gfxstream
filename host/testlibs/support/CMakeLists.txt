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
    gfxstream_host_testing_support
    GLSnapshotTestDispatch.cpp
    GLSnapshotTesting.cpp
    GLSnapshotTestStateUtils.cpp
    GLTestUtils.cpp
    HelloTriangleImp.cpp
    OpenGLTestContext.cpp
    SampleApplication.cpp
    ShaderUtils.cpp
    )
target_include_directories(
    gfxstream_host_testing_support
    PUBLIC
    include
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/GLES_CM
    ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/include
    ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common
    ${GFXSTREAM_REPO_ROOT}/host/vulkan
    )
target_link_libraries(
    gfxstream_host_testing_support
    PUBLIC
    gfxstream_common_image
    gfxstream_backend_static
    gfxstream_host_testing_oswindow_support
    gfxstream_stb
    gtest
    )

if (LINUX)
    add_library(
        x11_testing_support
        X11TestingSupport.cpp
        )
    target_link_libraries(
        x11_testing_support
        PUBLIC
        gfxstream_common_base
        gfxstream_opengl_headers
        )
    target_include_directories(
        x11_testing_support
        PUBLIC
        ${GFXSTREAM_REPO_ROOT}/host/decoder_common/include
        )
endif()