if (NOT TARGET gfxstream_host_address_space)
    add_library(
        gfxstream_host_address_space.headers
        INTERFACE)
    target_include_directories(
        gfxstream_host_address_space.headers
        INTERFACE
        include)
    target_link_libraries(
        gfxstream_host_address_space.headers
        INTERFACE
        gfxstream_backend_headers)

    add_library(
        gfxstream_host_address_space
        address_space_device.cpp
        address_space_graphics.cpp
        ring_buffer.cpp
        sub_allocator.cpp)
    target_link_libraries(
        gfxstream_host_address_space
        PUBLIC
        gfxstream_common_base
        gfxstream_host_address_space.headers
        gfxstream_host_logging)
endif()