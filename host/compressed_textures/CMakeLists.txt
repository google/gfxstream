if (ASTC_CPU_DECODING)
    set(astc-cpu-decompressor-sources AstcCpuDecompressorImpl.cpp)
else()
    set(astc-cpu-decompressor-sources AstcCpuDecompressorNoOp.cpp)
endif()

add_library(
    gfxstream_host_compressed_textures
    ${astc-cpu-decompressor-sources})

target_link_libraries(
    gfxstream_host_compressed_textures
    PRIVATE
    gfxstream_etc)

target_include_directories(
    gfxstream_host_compressed_textures
    PUBLIC
    include)

if(ASTC_CPU_DECODING)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        target_link_libraries(
            gfxstream_host_compressed_textures
            PRIVATE
            astcdec-native-static)
    else()
        target_link_libraries(
            gfxstream_host_compressed_textures
            PRIVATE
            astcdec-avx2-static)
    endif()
endif()

if (ENABLE_VKCEREAL_TESTS)
    add_executable(
        gfxstream_host_compressed_textures_unittests
        AstcCpuDecompressor_unittest.cpp)

    target_link_libraries(
        gfxstream_host_compressed_textures_unittests
        PRIVATE
        gfxstream_host_compressed_textures
        ${GFXSTREAM_BASE_LIB}
        gtest_main
        gmock_main)

    gtest_discover_tests(gfxstream_host_compressed_textures_unittests)
endif()
