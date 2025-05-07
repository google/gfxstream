if (APPLE)
    set(gfxstream_host_decoder_common-platform-sources)
elseif (WIN32)
    set(gfxstream_host_decoder_common-platform-sources)
elseif (QNX)
    set(gfxstream_host_decoder_common-platform-sources)
else()
    set(gfxstream_host_decoder_common-platform-sources X11Support.cpp)
endif()

add_library(
    gfxstream_host_decoder_common
    ChecksumCalculator.cpp
    ChecksumCalculatorThreadInfo.cpp
    glUtils.cpp
    ${gfxstream_host_decoder_common-platform-sources})
if (NOT MSVC)
    target_compile_options(
        gfxstream_host_decoder_common PRIVATE -fvisibility=hidden)
endif()
target_link_libraries(
    gfxstream_host_decoder_common
    PUBLIC
    gfxstream_backend_headers
    gfxstream_common_base
    gfxstream_host_library
    gfxstream_host_logging
    gfxstream_opengl_headers
    gfxstream_x11_headers
    )
target_include_directories(
    gfxstream_host_decoder_common
    PUBLIC
    .
    include
    )
