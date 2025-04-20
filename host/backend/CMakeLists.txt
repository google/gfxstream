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
        display_surface.cpp
        display_surface_user.cpp
        dma_device.cpp
        sync_device.cpp
        )
    target_link_libraries(
        gfxstream_host_backend
        PUBLIC
        gfxstream_common_utils
        gfxstream_host_backend.headers
        gfxstream_host_logging
        )
endif()

