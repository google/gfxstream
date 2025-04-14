if (NOT TARGET gfxstream_host_iostream)
    add_library(
        gfxstream_host_iostream
        INTERFACE)
    target_include_directories(
        gfxstream_host_iostream
        INTERFACE
        include)
endif()
