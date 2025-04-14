add_subdirectory(gl-host-common)

# GLES decoder
add_subdirectory(gles1_dec)
add_subdirectory(gles2_dec)

# GLES dispatch based on Translator
add_subdirectory(OpenGLESDispatch)

# GLES translator
add_subdirectory(glestranslator)
add_subdirectory(glsnapshot)

add_library(gfxstream-gl-server
            BufferGl.cpp
            ColorBufferGl.cpp
            CompositorGl.cpp
            DebugGl.cpp
            DisplayGl.cpp
            DisplaySurfaceGl.cpp
            EmulatedEglConfig.cpp
            EmulatedEglContext.cpp
            EmulatedEglFenceSync.cpp
            EmulatedEglImage.cpp
            EmulatedEglWindowSurface.cpp
            EmulationGl.cpp
            GLESVersionDetector.cpp
            ReadbackWorkerGl.cpp
            TextureDraw.cpp
            TextureResize.cpp
            YUVConverter.cpp)

target_link_libraries(
        gfxstream-gl-server
        PUBLIC
        aemu-base.headers
        gfxstream_features
        gfxstream-gl-host-common.headers
        gfxstream_vulkan_headers
        gfxstream_egl_headers
        gfxstream_host_backend
        gfxstream_host_logging
        gles1_dec
        gles2_dec
        GLcommon
        GLSnapshot
        OpenGLESDispatch)

if(BUILD_STANDALONE)
    target_link_libraries(
        gfxstream-gl-server
        PRIVATE
        gfxstream-gl-host-common)
endif()

target_include_directories(gfxstream-gl-server
                           PRIVATE
                           ${GFXSTREAM_REPO_ROOT}
                           ${GFXSTREAM_REPO_ROOT}/gldispatch/include
                           ${GFXSTREAM_REPO_ROOT}/include
                           ${GFXSTREAM_REPO_ROOT}/host
                           ${GFXSTREAM_REPO_ROOT}/host/gl
                           ${GFXSTREAM_REPO_ROOT}/third-party/glm/include)
