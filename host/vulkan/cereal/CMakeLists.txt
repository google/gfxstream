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

add_library(OpenglRender_vulkan_cereal
    common/goldfish_vk_extension_structs.cpp
    common/goldfish_vk_marshaling.cpp
    common/goldfish_vk_reserved_marshaling.cpp
    common/goldfish_vk_deepcopy.cpp
    common/goldfish_vk_dispatch.cpp
    common/goldfish_vk_transform.cpp )
target_compile_definitions(OpenglRender_vulkan_cereal PRIVATE -DVK_GOOGLE_gfxstream)
if (WIN32)
    target_compile_definitions(OpenglRender_vulkan_cereal PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
endif()
target_link_libraries(
    OpenglRender_vulkan_cereal
    PUBLIC
    gfxstream_common_logging
    gfxstream_features
    gfxstream_host_backend
    gfxstream_host_decoder_common
    gfxstream_host_health
    gfxstream_vulkan_headers
    )

target_include_directories(OpenglRender_vulkan_cereal
                           PUBLIC
                           .
                           PRIVATE
                           common/
                           ..
                           ../..
                           ../../../include)

