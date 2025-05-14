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

add_subdirectory(cereal)
add_subdirectory(emulated_textures)

if(CONFIG_AEMU)
    add_compile_definitions(GFXSTREAM_BUILD_WITH_SNAPSHOT_SUPPORT)
    add_compile_definitions(CONFIG_AEMU)
endif()

add_library(gfxstream-vulkan-server
            BorrowedImageVk.cpp
            BufferVk.cpp
            ColorBufferVk.cpp
            CompositorVk.cpp
            DependencyGraph.cpp
            DeviceLostHelper.cpp
            DeviceOpTracker.cpp
            DisplayVk.cpp
            DisplaySurfaceVk.cpp
            DebugUtilsHelper.cpp
            PostWorkerVk.cpp
            SwapChainStateVk.cpp
            RenderThreadInfoVk.cpp
            VkAndroidNativeBuffer.cpp
            VkCommonOperations.cpp
            VkDecoder.cpp
            VkDecoderGlobalState.cpp
            VkDecoderSnapshot.cpp
            VkDecoderSnapshotUtils.cpp
            VkEmulatedPhysicalDeviceMemory.cpp
            VkEmulatedPhysicalDeviceQueue.cpp
            VkFormatUtils.cpp
            VkReconstruction.cpp
            VulkanBoxedHandles.cpp
            VulkanDispatch.cpp
            VulkanHandleMapping.cpp
            VulkanStream.cpp
            vk_util.cpp)
set_source_files_properties(VkDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(VkSubDecoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(VkDecoderSnapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)

target_link_libraries(gfxstream-vulkan-server PUBLIC
        gfxstream_host_decoder_common
        emulated_textures
        gfxstream_common_base
        gfxstream_features
        gfxstream_host_backend
        gfxstream_host_health
        gfxstream_host_iostream
        gfxstream_host_library
        gfxstream_host_logging
        gfxstream_host_renderdoc
        gfxstream_host_snapshot.headers
        gfxstream_host_tracing
        gfxstream_openglesdispatch.headers
        gfxstream_vulkan_headers
        gfxstream_host_compressed_textures
        OpenglRender_vulkan_cereal
        PRIVATE
        gfxstream_egl_headers
        )

if (WIN32)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
elseif (APPLE)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_METAL_EXT)
elseif (QNX)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_SCREEN_QNX)
endif()

if (NOT MSVC)
    target_compile_options(gfxstream-vulkan-server PRIVATE -fvisibility=hidden)
endif()
target_compile_options(gfxstream-vulkan-server
    PRIVATE
    -Wall
    -Wextra
    # TODO: renable
    #-Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    )

target_include_directories(gfxstream-vulkan-server
                           PUBLIC
                           ${GFXSTREAM_REPO_ROOT}/vulkan
                           ${GFXSTREAM_REPO_ROOT}/vulkan/cereal
                           PRIVATE
                           ${GFXSTREAM_REPO_ROOT}
                           ${GFXSTREAM_REPO_ROOT}/host
                           ${GFXSTREAM_REPO_ROOT}/host/tracing
                           ${GFXSTREAM_REPO_ROOT}/host/vulkan
                           ${GFXSTREAM_REPO_ROOT}/host/vulkan/cereal/common
                           ${GFXSTREAM_REPO_ROOT}/third_party/glm/include)
