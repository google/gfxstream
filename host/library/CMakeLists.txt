add_library(
    gfxstream_host_library
    SharedLibrary.cpp)

target_include_directories(
    gfxstream_host_library
    PUBLIC
    include)

target_link_libraries(
    gfxstream_host_library
    PUBLIC
    gfxstream_common_base
    gfxstream_host_logging)