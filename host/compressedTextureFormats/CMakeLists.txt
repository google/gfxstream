if (ASTC_CPU_DECODING)
    set(astc-cpu-decompressor-sources AstcCpuDecompressorImpl.cpp)
else()
    set(astc-cpu-decompressor-sources AstcCpuDecompressorNoOp.cpp)
endif()

add_library(
    gfxstream-compressedTextures
    ${astc-cpu-decompressor-sources})

target_link_libraries(
    gfxstream-compressedTextures
    PRIVATE
    gfxstream_etc)

if(ASTC_CPU_DECODING)
    if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
        target_link_libraries(gfxstream-compressedTextures PRIVATE astcdec-native-static)
    else()
        target_link_libraries(gfxstream-compressedTextures PRIVATE astcdec-avx2-static)
    endif()
endif()


if (ENABLE_VKCEREAL_TESTS)
    add_executable(
        gfxstream-compressedTextures_unittests
        AstcCpuDecompressor_unittest.cpp)

    target_link_libraries(
        gfxstream-compressedTextures_unittests
        PRIVATE
        aemu-base.headers
        gfxstream-compressedTextures
        ${GFXSTREAM_BASE_LIB}
        gtest_main
        gmock_main)

    gtest_discover_tests(gfxstream-compressedTextures_unittests)
endif()
