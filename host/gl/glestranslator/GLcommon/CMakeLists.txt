add_library(
  GLcommon
  rgtc.cpp
  FramebufferData.cpp
  GLBackgroundLoader.cpp
  GLDispatch.cpp
  GLESbuffer.cpp
  GLEScontext.cpp
  GLESpointer.cpp
  GLESvalidate.cpp
  GLutils.cpp
  NamedObject.cpp
  ObjectData.cpp
  ObjectNameSpace.cpp
  PaletteTexture.cpp
  RangeManip.cpp
  SaveableTexture.cpp
  ScopedGLState.cpp
  ShareGroup.cpp
  TextureData.cpp
  TextureUtils.cpp)
target_include_directories(
    GLcommon PUBLIC
    ${GFXSTREAM_REPO_ROOT}
    ${GFXSTREAM_REPO_ROOT}/host
    ${GFXSTREAM_REPO_ROOT}/host/apigen-codec-common
    ${GFXSTREAM_REPO_ROOT}/host/gl/glestranslator/GLcommon/include)
target_link_libraries(
    GLcommon
    PUBLIC
    aemu-base.headers
    gfxstream_host_logging
    gfxstream_host_snapshot.headers
    gfxstream-compressedTextures
    gfxstream_egl_headers
    gfxstream_etc_headers
    gfxstream_openglesdispatch
    PRIVATE
    gfxstream_etc)
if (NOT MSVC)
    target_compile_options(GLcommon PRIVATE -fvisibility=hidden)
endif()
target_compile_options(GLcommon PUBLIC -Wno-inconsistent-missing-override)

if (LINUX)
    target_link_libraries(GLcommon PRIVATE "-ldl" "-Wl,-Bsymbolic")
endif()

# android_add_test(TARGET GLcommon_unittests SRC # cmake-format: sortable
#                                                Etc2_unittest.cpp)
# target_link_libraries(GLcommon_unittests PUBLIC GLcommon gmock_main)
# target_link_libraries(GLcommon_unittests PRIVATE emugl_base)
# android_target_link_libraries(GLcommon_unittests linux-x86_64
#                               PRIVATE "-ldl" "-Wl,-Bsymbolic")
# android_target_link_libraries(GLcommon_unittests windows
#                               PRIVATE "gdi32::gdi32" "-Wl,--add-stdcall-alias")
