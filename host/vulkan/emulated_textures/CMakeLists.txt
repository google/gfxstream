add_library(
    emulated_textures
    "AstcTexture.cpp"
    "CompressedImageInfo.cpp"
    "GpuDecompressionPipeline.cpp"
    )

target_link_libraries(
    emulated_textures
    PUBLIC
    OpenglRender_vulkan_cereal
    gfxstream_vulkan_headers
    gfxstream_host_compressed_textures
    gfxstream_host_logging
    gfxstream_host_health
    PRIVATE
    gfxstream_utils.headers)

target_include_directories(
    emulated_textures
    PRIVATE
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/include
    ${GFXSTREAM_REPO_ROOT}/host/vulkan
    ${GFXSTREAM_REPO_ROOT}/host/vulkan/cereal/common)
