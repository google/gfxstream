add_library(
    gfxstream_host_metrics
    Metrics.cpp)

target_include_directories(
    gfxstream_host_metrics
    PUBLIC
    include)

target_link_libraries(
    gfxstream_host_metrics
    PRIVATE
    gfxstream_common_base
    gfxstream_host_logging)
