if (NOT TARGET gfxstream_host_backend)
    add_library(
        gfxstream_host_backend.headers
        INTERFACE)
    target_include_directories(
        gfxstream_host_backend.headers
        INTERFACE
        include)

    add_library(
        gfxstream_host_backend
        display_surface.cpp
        display_surface_user.cpp
        )
    target_link_libraries(
        gfxstream_host_backend
        PUBLIC
        gfxstream_common_utils
        gfxstream_host_backend.headers
        gfxstream_host_logging
        )
endif()

