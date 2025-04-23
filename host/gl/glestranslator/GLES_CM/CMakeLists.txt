add_library(
    GLES_CM_translator_static
    CoreProfileEngine.cpp GLEScmContext.cpp GLEScmImp.cpp GLEScmUtils.cpp
    GLEScmValidate.cpp)
if (NOT MSVC)
    target_compile_options(GLES_CM_translator_static PRIVATE -fvisibility=hidden)
endif()
target_compile_options(GLES_CM_translator_static PRIVATE -Wno-macro-redefined)
target_link_libraries(
    GLES_CM_translator_static PRIVATE
    GLcommon
    apigen-codec-common
    aemu-base.headers
    aemu-host-common.headers
    gfxstream_host_logging
    gfxstream_host_snapshot.headers
    gfxstream_egl_headers)
target_include_directories(
    GLES_CM_translator_static PRIVATE
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host/
    ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/include
    ${GFXSTREAM_REPO_ROOT}/third-party/glm/include)
