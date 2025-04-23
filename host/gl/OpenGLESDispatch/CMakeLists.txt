add_library(
    gfxstream_openglesdispatch.headers
    INTERFACE)
target_link_libraries(
    gfxstream_openglesdispatch.headers
    INTERFACE
    gfxstream_opengl_headers)
target_include_directories(
    gfxstream_openglesdispatch.headers
    INTERFACE
    include)

add_library(
    gfxstream_openglesdispatch
    EGLDispatch.cpp
    gles1_stubbed_in_translator_namespace.cpp
    gles2_stubbed_in_translator_namespace.cpp
    GLESv1Dispatch.cpp
    GLESv2Dispatch.cpp
    OpenGLDispatchLoader.cpp
    StaticDispatch.cpp)

if (NOT MSVC)
    target_compile_options(gfxstream_openglesdispatch
                           PRIVATE
                           -fvisibility=default)
endif()


target_link_libraries(
    gfxstream_openglesdispatch
    PUBLIC
    gfxstream_openglesdispatch.headers
    gles1_dec
    gles2_dec
    apigen-codec-common
    PRIVATE
    aemu-base.headers
    EGL_translator_static)

