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
        aemu-base.headers
        gfxstream_backend_headers
        gfxstream_features.headers
        gfxstream_host_snapshot.headers
        gfxstream_egl_headers)

    if (BUILD_STANDALONE)
        add_library(
            gfxstream-gl-host-common
            STATIC

            # emugl glue
            opengl/misc.cpp
            opengl/EmuglBackendList.cpp
            )
        target_link_libraries(
            gfxstream-gl-host-common
            PUBLIC
            gfxstream-gl-host-common.headers
            gfxstream_host_logging)
    endif()
endif()
