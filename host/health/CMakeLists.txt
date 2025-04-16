add_library(
    gfxstream_host_health
    HealthMonitor.cpp)

target_include_directories(
    gfxstream_host_health
    PUBLIC
    include)

target_link_libraries(
    gfxstream_host_health
    PUBLIC
    gfxstream_common_base
    gfxstream_host_logging
    gfxstream_host_metrics)