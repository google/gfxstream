if (APPLE)
    set(apigen-codec-common-platform-sources)
elseif (WIN32)
    set(apigen-codec-common-platform-sources)
elseif (QNX)
    set(apigen-codec-common-platform-sources)
else()
    set(apigen-codec-common-platform-sources X11Support.cpp)
endif()

add_library(
    apigen-codec-common
    ChecksumCalculator.cpp
    ChecksumCalculatorThreadInfo.cpp
    glUtils.cpp
    ${apigen-codec-common-platform-sources})
if (NOT MSVC)
    target_compile_options(
        apigen-codec-common PRIVATE -fvisibility=hidden)
endif()
target_link_libraries(
    apigen-codec-common
    PUBLIC
    aemu-base.headers
    gfxstream_common_base
    gfxstream_host_library
    gfxstream_host_logging
    gfxstream_opengl_headers
    )
target_include_directories(
    apigen-codec-common
    PUBLIC
    ${GFXSTREAM_REPO_ROOT}/host/apigen-codec-common
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}/host)
