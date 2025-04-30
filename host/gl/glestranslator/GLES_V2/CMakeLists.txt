add_library(
    GLES_V2_translator_static
    ANGLEShaderParser.cpp
    GLESv2Context.cpp
    GLESv2Imp.cpp
    GLESv2Validate.cpp
    ProgramData.cpp
    SamplerData.cpp
    ShaderParser.cpp
    ShaderValidator.cpp
    TransformFeedbackData.cpp)
if (NOT MSVC)
    target_compile_options(GLES_V2_translator_static PRIVATE -fvisibility=hidden)
endif()
target_compile_options(GLES_V2_translator_static PRIVATE -Wno-macro-redefined)
target_link_libraries(
    GLES_V2_translator_static PRIVATE
    GLcommon
    apigen-codec-common
    gfxstream_host_logging)
