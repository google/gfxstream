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
        guest_operations.cpp
        renderer_operations.cpp
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

