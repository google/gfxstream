add_library(OpenglRender_vulkan_cereal
    common/goldfish_vk_extension_structs.cpp
    common/goldfish_vk_marshaling.cpp
    common/goldfish_vk_reserved_marshaling.cpp
    common/goldfish_vk_deepcopy.cpp
    common/goldfish_vk_dispatch.cpp
    common/goldfish_vk_transform.cpp )
target_compile_definitions(OpenglRender_vulkan_cereal PRIVATE -DVK_GOOGLE_gfxstream)
if (WIN32)
    target_compile_definitions(OpenglRender_vulkan_cereal PRIVATE -DVK_USE_PLATFORM_WIN32_KHR)
endif()
target_link_libraries(
    OpenglRender_vulkan_cereal
    PUBLIC
    aemu-base.headers
    gfxstream_features
    gfxstream_host_logging
    gfxstream_host_health
    gfxstream_vulkan_headers
    PRIVATE
    gfxstream_utils.headers)

target_include_directories(OpenglRender_vulkan_cereal
                           PUBLIC
                           .
                           PRIVATE
                           common/
                           ..
                           ../..
                           ../../../include)

