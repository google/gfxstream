set(egl-translator-common-sources
    ClientAPIExts.cpp
    EglConfig.cpp
    EglContext.cpp
    EglDisplay.cpp
    EglGlobalInfo.cpp
    EglImp.cpp
    EglPbufferSurface.cpp
    EglSurface.cpp
    EglThreadInfo.cpp
    EglValidate.cpp
    EglWindowSurface.cpp
    ShaderCache.cpp
    ThreadInfo.cpp)
set(egl-translator-windows-sources
    CoreProfileConfigs_windows.cpp EglOsApi_egl.cpp EglOsApi_wgl.cpp)
set(egl-translator-darwin-sources
    EglOsApi_darwin.cpp EglOsApi_egl.cpp MacNative.m
    MacPixelFormatsAttribs.m)
set(egl-translator-linux-sources
    CoreProfileConfigs_linux.cpp EglOsApi_egl.cpp EglOsApi_glx.cpp X11ErrorHandler.cpp)
set(egl-translator-qnx-sources
    EglOsApi_egl.cpp)
if (WIN32)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-windows-sources})
elseif (APPLE)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-darwin-sources})
elseif (QNX)
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-qnx-sources})
else()
    add_library(
        EGL_translator_static
        ${egl-translator-common-sources}
        ${egl-translator-linux-sources})
endif()

target_link_libraries(
    EGL_translator_static
    PUBLIC
    GLcommon
    apigen-codec-common
    PRIVATE
    aemu-base.headers
    aemu-host-common.headers
    gfxstream_common_utils
    gfxstream_host_backend
    )
target_link_libraries(EGL_translator_static PUBLIC GLES_CM_translator_static GLES_V2_translator_static)
if (NOT MSVC)
    target_compile_options(EGL_translator_static PRIVATE -fvisibility=hidden)
endif()
target_compile_options(
    EGL_translator_static
    PRIVATE -Wno-inconsistent-missing-override -Wno-macro-redefined)

if (APPLE)
    target_compile_options(EGL_translator_static PRIVATE -Wno-deprecated-declarations)
    target_compile_options(EGL_translator_static PRIVATE "-Wno-deprecated-declarations")
endif()

if (WIN32)
elseif (APPLE)
    target_link_libraries(EGL_translator_static PUBLIC "-framework AppKit -framework IOSurface")
elseif (QNX)
    target_link_libraries(EGL_translator_static PUBLIC "-lscreen -lregex -lEGL -lGLESv2")
else()
    target_link_libraries(EGL_translator_static PUBLIC "-ldl -lpthread")
endif()
