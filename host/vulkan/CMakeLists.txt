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
            borrowed_image_vk.cpp
            buffer_vk.cpp
            color_buffer_vk.cpp
            compositor_vk.cpp
            dependency_graph.cpp
            device_lost_helper.cpp
            device_op_tracker.cpp
            display_vk.cpp
            display_surface_vk.cpp
            debug_utils_helper.cpp
            external_memory.cpp
            post_worker_vk.cpp
            swap_chain_state_vk.cpp
            render_thread_info_vk.cpp
            vk_android_native_buffer_operations.cpp
            vk_common_operations.cpp
            vk_decoder.cpp
            vk_decoder_global_state.cpp
            vk_decoder_snapshot.cpp
            vk_decoder_snapshot_utils.cpp
            vk_emulated_physical_device_memory.cpp
            vk_emulated_physical_device_queue.cpp
            vk_format_utils.cpp
            vk_reconstruction.cpp
            vk_utils.cpp
            vulkan_boxed_handles.cpp
            vulkan_dispatch.cpp
            vulkan_handle_mapping.cpp
            vulkan_stream.cpp
            )
set_source_files_properties(vk_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(vk_sub_decoder.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)
set_source_files_properties(vk_decoder_snapshot.cpp PROPERTIES COMPILE_FLAGS -Wno-unused-variable)

target_link_libraries(gfxstream-vulkan-server PUBLIC
        gfxstream_host_decoder_common
        emulated_textures
        gfxstream_common_base
        gfxstream_common_logging
        gfxstream_features
        gfxstream_host_common
        gfxstream_host_compressed_textures
        gfxstream_host_iostream
        gfxstream_host_library
        gfxstream_host_renderdoc
        gfxstream_host_snapshot.headers
        gfxstream_host_tracing
        gfxstream_openglesdispatch.headers
        gfxstream_vulkan_headers
        gfxstream_xcb_headers
        OpenglRender_vulkan_cereal
        PRIVATE
        gfxstream_opengl_headers
        )

if (WIN32)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
elseif (APPLE)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_METAL_EXT -DVK_USE_PLATFORM_MACOS_MVK)
elseif (QNX)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_SCREEN_QNX)
elseif (UNIX)
target_compile_definitions(gfxstream-vulkan-server PRIVATE -DVK_USE_PLATFORM_XCB_KHR)
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
                           ${GFXSTREAM_REPO_ROOT}/host/gl
                           ${GFXSTREAM_REPO_ROOT}/host/tracing
                           ${GFXSTREAM_REPO_ROOT}/host/vulkan
                           ${GFXSTREAM_REPO_ROOT}/host/vulkan/cereal/common
                           ${GFXSTREAM_REPO_ROOT}/third_party/glm/include)
