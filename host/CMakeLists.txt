# Copyright 2025 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

add_library(
        gfxstream_backend_headers
        INTERFACE)
target_include_directories(
        gfxstream_backend_headers
        INTERFACE
        include)

add_subdirectory(address_space)
add_subdirectory(backend)
add_subdirectory(features)
add_subdirectory(health)
add_subdirectory(iostream)
add_subdirectory(library)
add_subdirectory(metrics)
add_subdirectory(snapshot)
add_subdirectory(tracing)
add_subdirectory(decoder_common)
add_subdirectory(compressed_textures)
add_subdirectory(renderdoc)

add_library(
    gfxstream_backend.headers
    INTERFACE
    )
target_link_libraries(
    gfxstream_backend.headers
    INTERFACE
    gfxstream_features.headers
    gfxstream_host_decoder_common
    gfxstream_host_health
    gfxstream_host_snapshot.headers
    gfxstream_host_renderdoc
    )
target_include_directories(
    gfxstream_backend.headers
    INTERFACE
    include
    )


add_subdirectory(gl)
add_subdirectory(renderControl_dec)
add_subdirectory(vulkan)


if(CONFIG_AEMU)
    add_compile_definitions(GFXSTREAM_BUILD_WITH_SNAPSHOT_SUPPORT)
    add_compile_definitions(CONFIG_AEMU)
endif()

# Stream server core
set(stream-server-core-sources
    Buffer.cpp
    ChannelStream.cpp
    ColorBuffer.cpp
    FrameBuffer.cpp
    Hwc2.cpp
    PostWorker.cpp
    PostWorkerGl.cpp
    ReadBuffer.cpp
    RenderChannelImpl.cpp
    RenderControl.cpp
    RendererImpl.cpp
    RenderLibImpl.cpp
    RenderThread.cpp
    RenderThreadInfo.cpp
    RenderWindow.cpp
    RingStream.cpp
    SyncThread.cpp
    VirtioGpuContext.cpp
    VirtioGpuFrontend.cpp
    VirtioGpuPipe.cpp
    VirtioGpuResource.cpp
    VirtioGpuRingBlob.cpp
    VirtioGpuTimelines.cpp
    VsyncThread.cpp)
if (APPLE)
    set(stream-server-core-platform-sources NativeSubWindow_cocoa.mm)
elseif (WIN32)
    set(stream-server-core-platform-sources NativeSubWindow_win32.cpp)
elseif (QNX)
    set(stream-server-core-platform-sources NativeSubWindow_qnx.cpp)
else()
    set(stream-server-core-platform-sources NativeSubWindow_x11.cpp)
endif()




# Compile everything as a static library first so that unit tests can call non-exported functions
add_library(
        gfxstream_backend_static
        STATIC
        ${stream-server-core-sources}
        ${stream-server-core-platform-sources}
)
target_link_libraries(
        gfxstream_backend_static
        PUBLIC
        ${GFXSTREAM_HOST_COMMON_LIB}
        gfxstream_backend.headers
        gfxstream_common_base
        gfxstream_common_logging
        gfxstream_drm_headers
        gfxstream_features
        gfxstream_glm_headers
        gfxstream_host_address_space
        gfxstream_host_backend
        gfxstream_host_decoder_common
        gfxstream_host_health
        gfxstream_host_iostream
        gfxstream_host_library
        gfxstream_host_metrics
        gfxstream_host_renderdoc
        gfxstream_host_snapshot.headers
        gfxstream_host_tracing
        gfxstream_opengl_headers
        gfxstream_openglesdispatch
        gfxstream-gl-server
        gfxstream-vulkan-server
        GLES_CM_translator_static
        renderControl_dec
        )

target_include_directories(
        gfxstream_backend_static
        PUBLIC
        ${GFXSTREAM_REPO_ROOT}
        ${GFXSTREAM_REPO_ROOT}/host
        ${GFXSTREAM_REPO_ROOT}/host/include
        ${GFXSTREAM_REPO_ROOT}/host/gfxstream_host_decoder_common
        ${GFXSTREAM_REPO_ROOT}/host/gl
        ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/include
        ${GFXSTREAM_REPO_ROOT}/host/vulkan
        ${GFXSTREAM_REPO_ROOT}/host/vulkan/cereal/common
        )

if (APPLE)
    target_link_libraries(gfxstream_backend_static PUBLIC "-framework AppKit -framework QuartzCore -framework IOSurface")
endif()
if (WIN32)
    target_link_libraries(gfxstream_backend_static PRIVATE D3d9.lib)
endif()

# Suppress some warnings
target_compile_options(
    gfxstream_backend_static
    PRIVATE
    -Wall
    -Wextra
    # TODO: renable
    # -Werror
    -Wno-missing-field-initializers
    -Wno-unused-parameter
    -Wno-unused-private-field
    -Wno-return-type-c-linkage
    -Wno-extern-c-compat
    -DGFXSTREAM_ENABLE_HOST_GLES=1
    )

# gfxstream_backend.dll
add_library(
    gfxstream_backend
    SHARED
    render_api.cpp
    virtio-gpu-gfxstream-renderer.cpp)

target_link_libraries(
    gfxstream_backend
    PUBLIC
    gfxstream_common_utils
    gfxstream_features
    gfxstream_host_backend
    gfxstream_host_tracing
    gfxstream_backend_static
    PRIVATE
    )

if (APPLE)
    set_target_properties(gfxstream_backend
        PROPERTIES
        LINK_FLAGS  "-undefined dynamic_lookup -flat_namespace")
endif()

if (LINUX)
    # Make sure we embed an build-id that can be used by crashpad.
    target_link_options(gfxstream_backend PRIVATE "LINKER:--build-id=sha1")
endif()


if(CONFIG_AEMU)
    android_install_shared_library(TARGET gfxstream_backend)
else()
    install(
      TARGETS gfxstream_backend RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX} LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX})
endif()

# Testing libraries
add_subdirectory(testlibs)

if (ENABLE_VKCEREAL_TESTS)

set(LIST_OF_TESTS)
function(discover_tests test_name)
if (WIN32)
    gtest_discover_tests(
        ${test_name}
        ${ARGN})
    list(APPEND LIST_OF_TESTS ${test_name})
    set(LIST_OF_TESTS ${LIST_OF_TESTS} PARENT_SCOPE)
else()
    gtest_discover_tests(
        ${test_name}
        ${ARGN}
    )
endif()
endfunction()
    # Backend unit tests
    add_executable(
        gfxstream_backend_unittests
        gfxstream_unittest.cpp)
    target_link_libraries(
        gfxstream_backend_unittests
        PRIVATE
        gfxstream_backend
        gfxstream_host_testing_oswindow_support
        ${GFXSTREAM_BASE_LIB}
        gtest_main)
    discover_tests(gfxstream_backend_unittests)

    # Basic opengl rendering tests##################################################
    add_executable(
        OpenglRender_unittests
        FrameBuffer_unittest.cpp
        VsyncThread_unittest.cpp
        tests/GLES1Dispatch_unittest.cpp
        tests/DefaultFramebufferBlit_unittest.cpp
        tests/TextureDraw_unittest.cpp
        tests/StalePtrRegistry_unittest.cpp
        )
    target_link_libraries(
        OpenglRender_unittests
        PRIVATE
        gfxstream_host_testing_support
        gfxstream_backend_static
        gfxstream_common_base
        gfxstream_host_backend
        gmock
        gtest_main)
    if (LINUX)
        target_compile_definitions(
            OpenglRender_unittests
            PRIVATE GFXSTREAM_HAS_X11=1)
        target_link_libraries(
            OpenglRender_unittests
            PRIVATE x11_testing_support)
    endif()
    discover_tests(OpenglRender_unittests)

    # Snapshot tests################################################################
    add_executable(
        OpenglRender_snapshot_unittests
        tests/GLSnapshotBuffers_unittest.cpp
        tests/GLSnapshotFramebufferControl_unittest.cpp
        tests/GLSnapshotFramebuffers_unittest.cpp
        tests/GLSnapshotMultisampling_unittest.cpp
        tests/GLSnapshotPixelOperations_unittest.cpp
        tests/GLSnapshotPixels_unittest.cpp
        tests/GLSnapshotPrograms_unittest.cpp
        tests/GLSnapshotRasterization_unittest.cpp
        tests/GLSnapshotRenderbuffers_unittest.cpp
        tests/GLSnapshotRendering_unittest.cpp
        tests/GLSnapshotShaders_unittest.cpp
        tests/GLSnapshotTextures_unittest.cpp
        tests/GLSnapshotTransformation_unittest.cpp
        tests/GLSnapshotVertexAttributes_unittest.cpp
        tests/GLSnapshot_unittest.cpp)
    target_link_libraries(
        OpenglRender_snapshot_unittests
        PRIVATE
        gfxstream_host_testing_support
        gfxstream_backend_static
        gfxstream_common_base
        gfxstream_host_backend
        gtest_main)
    discover_tests(OpenglRender_snapshot_unittests)

    # Vulkan tests##################################################################
    add_executable(
        Vulkan_unittests
        VirtioGpuTimelinesTests.cpp
        vulkan/CompositorVk_unittest.cpp
        vulkan/DisplayVk_unittest.cpp
        vulkan/SwapChainStateVk_unittest.cpp
        vulkan/VkDecoderGlobalState_unittest.cpp
        vulkan/VkFormatUtils_unittest.cpp
        vulkan/VkQsriTimeline_unittest.cpp
        vulkan/VkUtilsTests.cpp
        vulkan/Vulkan_unittest.cpp
    )
    target_link_libraries(
        Vulkan_unittests
        PRIVATE
        gfxstream_host_testing_support
        gfxstream_host_testing_oswindow_support
        gfxstream_backend_static
        gfxstream-vulkan-server
        gtest
        gtest_main
        gmock)
    if (APPLE)
        target_compile_definitions(
            Vulkan_unittests
            PRIVATE
            -DVK_USE_PLATFORM_METAL_EXT)
        target_link_libraries(
            Vulkan_unittests
            PUBLIC
            "-framework AppKit")
    endif()
    discover_tests(
            Vulkan_unittests
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    if (APPLE)
        target_compile_definitions(Vulkan_unittests PRIVATE -DVK_USE_PLATFORM_METAL_EXT)
    elseif (QNX)
        target_compile_definitions(Vulkan_unittests PRIVATE -DVK_USE_PLATFORM_SCREEN_QNX)
    elseif (UNIX)
        target_compile_definitions(Vulkan_unittests PRIVATE -DVK_USE_PLATFORM_XCB_KHR)
    endif()


    file(GLOB Vulkan_unittests_datafiles "vulkan/testdata/*.png")
    add_custom_command(TARGET Vulkan_unittests POST_BUILD
                       COMMAND ${CMAKE_COMMAND} -E make_directory
                            ${CMAKE_BINARY_DIR}/testdata
                       COMMAND ${CMAKE_COMMAND} -E copy_if_different
                            ${Vulkan_unittests_datafiles}
                            ${CMAKE_BINARY_DIR}/testdata)

    add_executable(
            Vulkan_integrationtests
            vulkan/testing/VkDecoderTestDispatch.h
            vulkan/testing/VulkanTestHelper.cpp
    )
    target_link_libraries(
            Vulkan_integrationtests
            PRIVATE
            gfxstream_backend_static
            gfxstream_common_base
            gfxstream-gl-server
            gfxstream-vulkan-server
            gfxstream_host_testing_support
            gtest
            gtest_main
            gmock)
    discover_tests(Vulkan_integrationtests)
endif()
if (WIN32)
    set(BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}")
    configure_file(../toolchain/cmake/SetWin32TestEnvironment.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/SetWin32TestEnvironment.cmake @ONLY)
    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES ${CMAKE_CURRENT_BINARY_DIR}/SetWin32TestEnvironment.cmake)
endif()
