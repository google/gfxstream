# EMU defines its own host common but then also includes the
# host directory (which includes this directory). Avoid a double
# include/define.
if (NOT TARGET gfxstream-gl-host-common)
    add_library(gfxstream-gl-host-common.headers INTERFACE)
    target_include_directories(
        gfxstream-gl-host-common.headers
        INTERFACE include ${GFXSTREAM_REPO_ROOT}/include)
    target_link_libraries(
        gfxstream-gl-host-common.headers
        INTERFACE
        aemu-host-common.headers
        aemu-base.headers
        gfxstream_features.headers
        gfxstream_host_snapshot.headers
        gfxstream_egl_headers)

    if (APPLE)
        set(gl-host-common-opengles-platform-sources
            opengl/macTouchOpenGL.m
            opengl/NativeGpuInfo_darwin.cpp)
    elseif (WIN32)
        set(gl-host-common-opengles-platform-sources
            opengl/NativeGpuInfo_windows.cpp)
    else()
        set(gl-host-common-opengles-platform-sources
            opengl/NativeGpuInfo_linux.cpp)
    endif()

    if (BUILD_STANDALONE)
        add_library(
            gfxstream-gl-host-common
            STATIC

            # emugl glue
            opengl/misc.cpp

            # general opengles host stuff, incl process pipe
            # and opengl es pipe
            opengles.cpp
            opengl/EmuglBackendList.cpp
            # opengl/EmuglBackendList_unittest.cpp
            opengl/emugl_config.cpp
            # opengl/emugl_config_unittest.cpp
            opengl/GLProcessPipe.cpp
            opengl/gpuinfo.cpp
            # opengl/gpuinfo_unittest.cpp
            opengl/logger.cpp
            opengl/OpenglEsPipe.cpp
            ${gl-host-common-opengles-platform-sources}
            )
        target_link_libraries(
            gfxstream-gl-host-common
            PUBLIC
            gfxstream-gl-host-common.headers
            gfxstream_host_logging)
    endif()
endif()
