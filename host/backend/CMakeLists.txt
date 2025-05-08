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

if (NOT TARGET gfxstream_host_backend)
    add_library(
        gfxstream_host_backend.headers
        INTERFACE)
    target_include_directories(
        gfxstream_host_backend.headers
        INTERFACE
        include)
    target_link_libraries(
        gfxstream_host_backend.headers
        INTERFACE
        gfxstream_backend_headers)

    add_library(
        gfxstream_host_backend
        address_space_operations.cpp
        display_operations.cpp
        display_surface.cpp
        display_surface_user.cpp
        dma_device.cpp
        external_object_manager.cpp
        file_stream.cpp
        graphics_driver_lock.cpp
        guest_operations.cpp
        mem_stream.cpp
        renderer_operations.cpp
        stream_utils.cpp
        sync_device.cpp
        vm_operations.cpp
        window_operations.cpp
        )
    target_link_libraries(
        gfxstream_host_backend
        PUBLIC
        gfxstream_common_utils
        gfxstream_host_backend.headers
        PRIVATE
        gfxstream_host_address_space
        gfxstream_host_logging
        )
endif()

