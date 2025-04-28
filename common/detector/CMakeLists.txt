
if(BUILD_GRAPHICS_DETECTOR)
    find_package(Protobuf REQUIRED)
    find_package(Threads REQUIRED)

    protobuf_generate_cpp(
        LIBGFXSTREAM_GRAPHICS_DETECTOR_SRCS
        LIBGFXSTREAM_GRAPHICS_DETECTOR_HDRS
        GraphicsDetector.proto
        )

    add_library(
        libgfxstream_graphics_detector_proto
        STATIC
        ${LIBGFXSTREAM_GRAPHICS_DETECTOR_SRCS}
        )

    target_link_libraries(
        libgfxstream_graphics_detector_proto
        PUBLIC
        libprotobuf.a
        )

    target_include_directories(
        libgfxstream_graphics_detector_proto
        PUBLIC
        $<TARGET_PROPERTY:libgfxstream_graphics_detector_proto,BINARY_DIR>
        )

    set_property(
        TARGET
        libgfxstream_graphics_detector_proto
        PROPERTY
        CXX_STANDARD 17
        )

    add_executable(
        gfxstream_graphics_detector
        DetectGraphics.cpp
        Egl.cpp
        Gles.cpp
        Image.cpp
        Lib.cpp
        GraphicsDetector.cpp
        GraphicsDetectorGl.cpp
        GraphicsDetectorVk.cpp
        GraphicsDetectorVkExternalMemoryHost.cpp
        GraphicsDetectorVkPrecisionQualifiersOnYuvSamplers.cpp
        Subprocess.cpp
        Vulkan.cpp
        )

    target_include_directories(
        gfxstream_graphics_detector
        PRIVATE
        .
        )

    target_link_libraries(
        gfxstream_graphics_detector
        PRIVATE
        libgfxstream_graphics_detector_proto
        gfxstream_vulkan_headers
        Threads::Threads
        ${CMAKE_DL_LIBS}
        )

    set_property(
        TARGET
        gfxstream_graphics_detector
        PROPERTY
        CXX_STANDARD 17
        )

endif()