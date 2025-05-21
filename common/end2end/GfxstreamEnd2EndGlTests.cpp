// Copyright (C) 2023 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "GfxstreamEnd2EndTests.h"

namespace gfxstream {
namespace tests {
namespace {

using testing::Eq;
using testing::Gt;
using testing::HasSubstr;
using testing::IsEmpty;
using testing::IsTrue;
using testing::Le;
using testing::Not;

MATCHER_P(IsOkWith, expected, std::string(" equals ") + expected.ToString()) {
    const auto& actual = arg;

    if (actual.ok() && actual.value().r == expected.r && actual.value().g == expected.g &&
        actual.value().b == expected.b && actual.value().a == expected.a) {
        return true;
    }

    if (actual.ok()) {
        *result_listener << "actual: " << actual.value().ToString();
    } else {
        *result_listener << "actual: {" << " error: " << actual.error() << " };";
    }
    return false;
}

MATCHER_P4(IsOkWithRGBA, r, g, b, a,
           std::string(" equals ") + PixelR8G8B8A8(r, g, b, a).ToString()) {
    const auto& actual = arg;

    if (actual.ok() && actual.value().r == r && actual.value().g == g && actual.value().b == b &&
        actual.value().a == a) {
        return true;
    }

    if (actual.ok()) {
        *result_listener << "actual: " << actual.value().ToString();
    } else {
        *result_listener << "actual: {" << " error: " << actual.error() << " };";
    }
    return false;
}

class SimpleLatch {
   public:
    SimpleLatch(std::uint32_t count) : mCount(count) {}

    SimpleLatch(const SimpleLatch&) = delete;
    SimpleLatch& operator=(const SimpleLatch&) = delete;

    SimpleLatch(SimpleLatch&&) = delete;
    SimpleLatch& operator=(SimpleLatch&&) = delete;

    void count_down() {
        {
            std::unique_lock lock(mMutex);
            --mCount;
        }
        mConditionVariable.notify_all();
    }

    void wait() {
        std::unique_lock lock(mMutex);
        mConditionVariable.wait(lock, [this] { return mCount == 0; });
    }

   private:
    std::mutex mMutex;
    std::condition_variable mConditionVariable;
    std::uint32_t mCount;
};

class GfxstreamEnd2EndGlTest : public GfxstreamEnd2EndTest {
   protected:
    Result<PixelR8G8B8A8> GetPixelAt(GLint x, GLint y) {
        if (!mGl) {
            return gfxstream::unexpected("GL not available, running with `with_gl = false`?");
        }

        GLubyte rgba[4] = {0, 0, 0, 0};
        mGl->glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);

        if (GLenum error = mGl->glGetError(); error != GL_NO_ERROR) {
            return gfxstream::unexpected("Failed to glReadPixels() with error " +
                                             std::to_string(error));
        }

        return PixelR8G8B8A8(x, y, rgba[0], rgba[1], rgba[2], rgba[3]);
    }

    void SetUp() override {
        GfxstreamEnd2EndTest::SetUp();

        SetUpEglContextAndSurface(2, mSurfaceWidth, mSurfaceHeight, &mDisplay, &mContext,
                                  &mSurface);
    }

    void TearDown() override {
        TearDownEglContextAndSurface(mDisplay, mContext, mSurface);

        GfxstreamEnd2EndTest::TearDown();
    }

    int mSurfaceWidth = 32;
    int mSurfaceHeight = 32;
    EGLDisplay mDisplay;
    EGLContext mContext;
    EGLSurface mSurface;
};

TEST_P(GfxstreamEnd2EndGlTest, BasicViewport) {
    GLint viewport[4] = {};
    mGl->glGetIntegerv(GL_VIEWPORT, viewport);

    EXPECT_THAT(viewport[0], Eq(0));
    EXPECT_THAT(viewport[1], Eq(0));
    EXPECT_THAT(viewport[2], Eq(mSurfaceWidth));
    EXPECT_THAT(viewport[3], Eq(mSurfaceHeight));
}

TEST_P(GfxstreamEnd2EndGlTest, CreateWindowSurface) {
    // clang-format off
    static const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE,
    };
    // clang-format on

    int numConfigs = 0;
    ASSERT_THAT(mGl->eglChooseConfig(mDisplay, configAttributes, nullptr, 1, &numConfigs),
                IsTrue());
    ASSERT_THAT(numConfigs, Gt(0));

    EGLConfig config = nullptr;
    ASSERT_THAT(mGl->eglChooseConfig(mDisplay, configAttributes, &config, 1, &numConfigs),
                IsTrue());
    ASSERT_THAT(config, Not(Eq(nullptr)));

    // clang-format off
    static const EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE,
    };
    // clang-format on

    EGLContext context = mGl->eglCreateContext(mDisplay, config, EGL_NO_CONTEXT, contextAttribs);
    ASSERT_THAT(context, Not(Eq(EGL_NO_CONTEXT)));

    constexpr const int width = 32;
    constexpr const int height = 32;

    auto anw = mAnwHelper->createNativeWindowForTesting(mGralloc.get(), width, height);

    EGLSurface surface = mGl->eglCreateWindowSurface(mDisplay, config, anw, nullptr);
    ASSERT_THAT(surface, Not(Eq(EGL_NO_SURFACE)));

    ASSERT_THAT(mGl->eglMakeCurrent(mDisplay, surface, surface, context), IsTrue());

    constexpr const int iterations = 120;
    for (int i = 0; i < iterations; i++) {
        mGl->glViewport(0, 0, width, height);
        mGl->glClearColor(1.0f, 0.0f, static_cast<float>(i) / static_cast<float>(iterations), 1.0f);
        mGl->glClear(GL_COLOR_BUFFER_BIT);
        mGl->glFinish();
        mGl->eglSwapBuffers(mDisplay, surface);
    }

    ASSERT_THAT(mGl->eglDestroyContext(mDisplay, context), IsTrue());
    ASSERT_THAT(mGl->eglDestroySurface(mDisplay, surface), IsTrue());

    mAnwHelper->release(anw);
}

TEST_P(GfxstreamEnd2EndGlTest, SwitchContext) {
    ASSERT_THAT(mGl->eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT),
                IsTrue());
    for (int i = 0; i < 100; i++) {
        ASSERT_THAT(mGl->eglMakeCurrent(mDisplay, mSurface, mSurface, mContext), IsTrue());
        ASSERT_THAT(mGl->eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT),
                    IsTrue());
    }
}

TEST_P(GfxstreamEnd2EndGlTest, MappedMemory) {
    constexpr GLsizei kBufferSize = 64;

    ScopedGlBuffer buffer(*mGl);
    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glBufferData(GL_ARRAY_BUFFER, kBufferSize, 0, GL_DYNAMIC_DRAW);

    std::vector<uint8_t> bufferData(kBufferSize);
    for (uint8_t i = 0; i < kBufferSize; ++i) {
        bufferData[i] = i;
    }

    {
        auto* mappedBufferData = reinterpret_cast<uint8_t*>(
            mGl->glMapBufferRange(GL_ARRAY_BUFFER, 0, kBufferSize,
                                  GL_MAP_WRITE_BIT | GL_MAP_FLUSH_EXPLICIT_BIT));

        for (uint8_t i = 0; i < kBufferSize; ++i) {
            mappedBufferData[i] = bufferData[i];
        }

        mGl->glFlushMappedBufferRange(GL_ARRAY_BUFFER, 0, kBufferSize);
        mGl->glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    {
        auto* mappedBufferData = reinterpret_cast<uint8_t*>(
            mGl->glMapBufferRange(GL_ARRAY_BUFFER, 0, kBufferSize, GL_MAP_READ_BIT));

        for (uint8_t i = 0; i < kBufferSize; ++i) {
            EXPECT_THAT(mappedBufferData[i], Eq(bufferData[i]));
        }

        mGl->glUnmapBuffer(GL_ARRAY_BUFFER);
    }

    mGl->glBindBuffer(GL_ARRAY_BUFFER, 0);
}

TEST_P(GfxstreamEnd2EndGlTest, ContextStrings) {
    EGLDisplay display = mGl->eglGetDisplay(EGL_DEFAULT_DISPLAY);
    ASSERT_THAT(display, Not(Eq(EGL_NO_DISPLAY)));

    int versionMajor = 0;
    int versionMinor = 0;
    ASSERT_THAT(mGl->eglInitialize(display, &versionMajor, &versionMinor), IsTrue());

    ASSERT_THAT(mGl->eglBindAPI(EGL_OPENGL_ES_API), IsTrue());

    // clang-format off
    static const EGLint configAttributes[] = {
        EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE,
    };
    // clang-format on

    int numConfigs = 0;
    ASSERT_THAT(mGl->eglChooseConfig(display, configAttributes, nullptr, 1, &numConfigs), IsTrue());
    ASSERT_THAT(numConfigs, Gt(0));

    EGLConfig config = nullptr;
    ASSERT_THAT(mGl->eglChooseConfig(display, configAttributes, &config, 1, &numConfigs), IsTrue());
    ASSERT_THAT(config, Not(Eq(nullptr)));

    // clang-format off
    static const EGLint gles1ContextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 1,
        EGL_NONE,
    };
    // clang-format on

    EGLContext gles1Context = mGl->eglCreateContext(display, config, EGL_NO_CONTEXT, gles1ContextAttribs);
    ASSERT_THAT(gles1Context, Not(Eq(EGL_NO_CONTEXT)));

    // clang-format off
    static const EGLint gles2ContextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
    // clang-format on

    EGLContext gles2Context = mGl->eglCreateContext(display, config, EGL_NO_CONTEXT, gles2ContextAttribs);
    ASSERT_THAT(gles2Context, Not(Eq(EGL_NO_CONTEXT)));

    constexpr const int width = 32;
    constexpr const int height = 32;

    // clang-format off
    static const EGLint surfaceAttributes[] = {
        EGL_WIDTH,  width,
        EGL_HEIGHT, height,
        EGL_NONE,
    };
    // clang-format on

    EGLSurface surface = mGl->eglCreatePbufferSurface(display, config, surfaceAttributes);
    ASSERT_THAT(surface, Not(Eq(EGL_NO_SURFACE)));

    {
        ASSERT_THAT(mGl->eglMakeCurrent(display, surface, surface, gles2Context), IsTrue());
        const auto versionString = (const char*)mGl->glGetString(GL_VERSION);
        const auto extensionString = (const char*)mGl->glGetString(GL_EXTENSIONS);
        EXPECT_THAT(versionString, HasSubstr("ES 3"));
        EXPECT_THAT(extensionString, Not(HasSubstr("OES_draw_texture")));
    }
    {
        ASSERT_THAT(mGl->eglMakeCurrent(display, surface, surface, gles1Context), IsTrue());
        const auto versionString = (const char*)mGl->glGetString(GL_VERSION);
        const auto extensionString = (const char*)mGl->glGetString(GL_EXTENSIONS);
        EXPECT_THAT(versionString, HasSubstr("ES-CM"));
        EXPECT_THAT(extensionString, HasSubstr("OES_draw_texture"));
    }
    {
        ASSERT_THAT(mGl->eglMakeCurrent(display, surface, surface, gles2Context), IsTrue());
        const auto versionString = (const char*)mGl->glGetString(GL_VERSION);
        const auto extensionString = (const char*)mGl->glGetString(GL_EXTENSIONS);
        EXPECT_THAT(versionString, HasSubstr("ES 3"));
        EXPECT_THAT(extensionString, Not(HasSubstr("OES_draw_texture")));
    }

    ASSERT_THAT(mGl->eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), IsTrue());
    ASSERT_THAT(mGl->eglDestroyContext(display, gles1Context), IsTrue());
    ASSERT_THAT(mGl->eglDestroyContext(display, gles2Context), IsTrue());
    ASSERT_THAT(mGl->eglDestroySurface(display, surface), IsTrue());
}

TEST_P(GfxstreamEnd2EndGlTest, FramebufferFetchShader) {
    const std::string extensionsString = (const char*)mGl->glGetString(GL_EXTENSIONS);
    ASSERT_THAT(extensionsString, Not(IsEmpty()));

    const bool supportsFramebufferFetch =
        extensionsString.find("GL_EXT_shader_framebuffer_fetch") != std::string::npos;

    const std::string shaderSource = R"(#version 300 es
#extension GL_EXT_shader_framebuffer_fetch : require
precision highp float;
in vec3 color_varying;
out vec4 fragColor;
void main() {
    fragColor = vec4(color_varying, 1.0);
}
    )";
    auto result = SetUpShader(GL_FRAGMENT_SHADER, shaderSource);
    if (result.ok()) {
        ASSERT_THAT(supportsFramebufferFetch, Eq(GL_TRUE));
    } else {
        ASSERT_THAT(supportsFramebufferFetch, Eq(GL_FALSE));
    }
}

TEST_P(GfxstreamEnd2EndGlTest, ConstantMatrixShader) {
    const std::string shaderSource = R"(#version 300 es
precision mediump float;
in highp vec4 dEQP_Position;
out vec2 out0;

void main() {
    const mat4x2 matA = mat4x2( 2.0,  4.0,   8.0,  16.0,
                               32.0, 64.0, 128.0, 256.0);
    const mat4x2 matB = mat4x2(1.0 /  2.0, 1.0 /  4.0, 1.0 /   8.0, 1.0 /  16.0,
                               1.0 / 32.0, 1.0 / 64.0, 1.0 / 128.0, 1.0 / 256.0);
    mat4x2 result = matrixCompMult(matA, matB);

    out0 = result * vec4(1.0, 1.0, 1.0, 1.0);
    gl_Position = dEQP_Position;
}
    )";

    auto result = SetUpShader(GL_VERTEX_SHADER, shaderSource);
    ASSERT_THAT(result, IsOk());
}

TEST_P(GfxstreamEnd2EndGlTest, Draw) {
    const std::string vertSource = R"(#version 300 es
precision highp float;

layout (location = 0) in vec2 pos;
layout (location = 1) in vec3 color;

uniform mat4 transform;

out vec3 color_varying;

void main() {
    gl_Position = transform * vec4(pos, 0.0, 1.0);
    color_varying = (transform * vec4(color, 1.0)).xyz;
}
    )";

    const std::string fragSource = R"(#version 300 es
precision highp float;

in vec3 color_varying;

out vec4 fragColor;

void main() {
    fragColor = vec4(color_varying, 1.0);
}
    )";

    ScopedGlProgram program = GFXSTREAM_ASSERT(SetUpProgram(vertSource, fragSource));

    GLint transformUniformLocation = mGl->glGetUniformLocation(program, "transform");
    mGl->glEnableVertexAttribArray(0);
    mGl->glEnableVertexAttribArray(1);

    struct VertexAttributes {
        float position[2];
        float color[3];
    };
    const VertexAttributes vertexAttrs[] = {
        // clang-format off
        { { -0.5f, -0.5f,}, { 0.2, 0.1, 0.9, }, },
        { {  0.5f, -0.5f,}, { 0.8, 0.3, 0.1, }, },
        { {  0.0f,  0.5f,}, { 0.1, 0.9, 0.6, }, },
        // clang-format on
    };

    ScopedGlBuffer buffer(*mGl);
    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertexAttrs), vertexAttrs, GL_STATIC_DRAW);

    mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes), 0);
    mGl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes), (GLvoid*)offsetof(VertexAttributes, color));

    mGl->glUseProgram(program);

    mGl->glViewport(0, 0, 1, 1);

    mGl->glClearColor(0.2f, 0.2f, 0.3f, 0.0f);
    mGl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    const float matrix[16] = {
        // clang-format off
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
        // clang-format on
    };

    constexpr uint32_t kDrawIterations = 200;
    for (uint32_t i = 0; i < kDrawIterations; i++) {
        mGl->glUniformMatrix4fv(transformUniformLocation, 1, GL_FALSE, matrix);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    mGl->glFinish();
    mGl->glBindBuffer(GL_ARRAY_BUFFER, 0);
    mGl->glUseProgram(0);
}

TEST_P(GfxstreamEnd2EndGlTest, ProgramBinaryWithAHB) {
    const uint32_t width = 2;
    const uint32_t height = 2;
    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    GFXSTREAM_ASSERT(FillAhb(ahb, PixelR8G8B8A8(0, 0, 128, 255)));

    const EGLint ahbImageAttribs[] = {
        // clang-format off
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
        // clang-format on
    };
    EGLImageKHR ahbImage = mGl->eglCreateImageKHR(mDisplay, EGL_NO_CONTEXT,
                                                  EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
    ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

    ScopedGlTexture ahbTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, ahbTexture);
    mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, ahbImage);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    GLenum programBinaryFormat = GL_NONE;
    std::vector<uint8_t> programBinaryData;
    {
        const std::string vertSource = R"(#version 300 es

            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;

            out vec2 vTex;

            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string fragSource = R"(#version 300 es

            precision highp float;

            uniform float uMultiplier;
            uniform sampler2D uTexture;

            in vec2 vTex;

            out vec4 oColor;

            void main() {
                oColor = texture(uTexture, vTex) * uMultiplier;
            })";

        ScopedGlProgram program = GFXSTREAM_ASSERT(SetUpProgram(vertSource, fragSource));

        GLint programBinaryLength = 0;
        mGl->glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &programBinaryLength);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        programBinaryData.resize(programBinaryLength);

        GLint readProgramBinaryLength = 0;
        mGl->glGetProgramBinary(program, programBinaryLength, &readProgramBinaryLength,
                                &programBinaryFormat, programBinaryData.data());
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(programBinaryLength, Eq(readProgramBinaryLength));
    }

    ScopedGlProgram program =
        GFXSTREAM_ASSERT(SetUpProgram(programBinaryFormat, programBinaryData));
    ASSERT_THAT(program, Not(Eq(0)));

    GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

    GLint multiplierUniformLoc = mGl->glGetUniformLocation(program, "uMultiplier");
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(multiplierUniformLoc, Not(Eq(-1)));

    const GLsizei kFramebufferWidth = 4;
    const GLsizei kFramebufferHeight = 4;
    ScopedGlFramebuffer framebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    ScopedGlTexture framebufferTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, framebufferTexture);
    mGl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFramebufferWidth, kFramebufferHeight, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, nullptr);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                framebufferTexture, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    mGl->glBindTexture(GL_TEXTURE_2D, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    struct VertexAttributes {
        float pos[2];
        float tex[2];
    };
    const VertexAttributes vertexAttrs[] = {
        // clang-format off
        { { -1.0f, -1.0f,}, { 0.0f, 0.0f }, },
        { {  3.0f, -1.0f,}, { 2.0f, 0.0f }, },
        { { -1.0f,  3.0f,}, { 0.0f, 2.0f }, },
        // clang-format on
    };
    ScopedGlBuffer buffer(*mGl);
    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertexAttrs), vertexAttrs, GL_STATIC_DRAW);

    mGl->glUseProgram(program);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glEnableVertexAttribArray(0);
    mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                               (GLvoid*)offsetof(VertexAttributes, pos));
    mGl->glEnableVertexAttribArray(1);
    mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                               (GLvoid*)offsetof(VertexAttributes, tex));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glActiveTexture(GL_TEXTURE0);
    mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
    mGl->glUniform1i(textureUniformLoc, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glUniform1f(multiplierUniformLoc, 2.0f);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    for (int x = 0; x < kFramebufferWidth; x++) {
        for (int y = 0; y < kFramebufferHeight; y++) {
            EXPECT_THAT(GetPixelAt(x, y), IsOkWithRGBA(0, 0, 255, 255));
        }
    }

    mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TEST_P(GfxstreamEnd2EndGlTest, ProgramBinaryWithTexture) {
    const GLsizei kTextureWidth = 2;
    const GLsizei kTextureHeight = 2;
    const GLubyte kTextureData[16] = {
        // clang-format off
        0, 0, 128, 255,   0, 0, 128, 255,

        0, 0, 128, 255,   0, 0, 128, 255,
        // clang-format on
    };
    ScopedGlTexture texture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, texture);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    mGl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kTextureWidth, kTextureHeight, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, kTextureData);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    GLenum programBinaryFormat = GL_NONE;
    std::vector<uint8_t> programBinaryData;
    {
        const std::string vertSource = R"(#version 300 es

            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;

            out vec2 vTex;

            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string fragSource = R"(#version 300 es

            precision highp float;

            uniform float uMultiplier;
            uniform sampler2D uTexture;

            in vec2 vTex;

            out vec4 oColor;

            void main() {
                oColor = texture(uTexture, vTex) * uMultiplier;
            })";

        ScopedGlProgram program = GFXSTREAM_ASSERT(SetUpProgram(vertSource, fragSource));

        GLint programBinaryLength = 0;
        mGl->glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &programBinaryLength);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        programBinaryData.resize(programBinaryLength);

        GLint readProgramBinaryLength = 0;
        mGl->glGetProgramBinary(program, programBinaryLength, &readProgramBinaryLength,
                                &programBinaryFormat, programBinaryData.data());
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(programBinaryLength, Eq(readProgramBinaryLength));
    }

    ScopedGlProgram program =
        GFXSTREAM_ASSERT(SetUpProgram(programBinaryFormat, programBinaryData));
    ASSERT_THAT(program, Not(Eq(0)));

    GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

    GLint multiplierUniformLoc = mGl->glGetUniformLocation(program, "uMultiplier");
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(multiplierUniformLoc, Not(Eq(-1)));

    const GLsizei kFramebufferWidth = 4;
    const GLsizei kFramebufferHeight = 4;
    ScopedGlFramebuffer framebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    ScopedGlTexture framebufferTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, framebufferTexture);
    mGl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFramebufferWidth, kFramebufferHeight, 0, GL_RGBA,
                      GL_UNSIGNED_BYTE, nullptr);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                framebufferTexture, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    mGl->glBindTexture(GL_TEXTURE_2D, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    struct VertexAttributes {
        float pos[2];
        float tex[2];
    };
    const VertexAttributes vertexAttrs[] = {
        // clang-format off
        { { -1.0f, -1.0f,}, { 0.0f, 0.0f }, },
        { {  3.0f, -1.0f,}, { 2.0f, 0.0f }, },
        { { -1.0f,  3.0f,}, { 0.0f, 2.0f }, },
        // clang-format on
    };
    ScopedGlBuffer buffer(*mGl);
    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertexAttrs), vertexAttrs, GL_STATIC_DRAW);

    mGl->glUseProgram(program);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    mGl->glEnableVertexAttribArray(0);
    mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                               (GLvoid*)offsetof(VertexAttributes, pos));
    mGl->glEnableVertexAttribArray(1);
    mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                               (GLvoid*)offsetof(VertexAttributes, tex));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glActiveTexture(GL_TEXTURE0);
    mGl->glBindTexture(GL_TEXTURE_2D, texture);
    mGl->glUniform1i(textureUniformLoc, 0);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glUniform1f(multiplierUniformLoc, 2.0f);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    for (int x = 0; x < kFramebufferWidth; x++) {
        for (int y = 0; y < kFramebufferHeight; y++) {
            EXPECT_THAT(GetPixelAt(x, y), IsOkWithRGBA(0, 0, 255, 255));
        }
    }

    mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

TEST_P(GfxstreamEnd2EndGlTest, AhbTextureUploadAndReadback) {
    const uint32_t width = 2;
    const uint32_t height = 2;

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    const auto uploadPixel = PixelR8G8B8A8(55, 66, 77, 88);
    const auto uploadPixels = Fill(width, height, uploadPixel);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    // Initialize AHB with `lockPixel`
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // Update AHB with `uploadPixel` via texture upload:
    {
        const EGLint ahbImageAttribs[] = {
            // clang-format off
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_NONE,
            // clang-format on
        };
        EGLImageKHR ahbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture ahbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                             uploadPixels.data());
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        EGLSync uploadFence = mGl->eglCreateSyncKHR(mDisplay, EGL_SYNC_FENCE_KHR, nullptr);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->glFlush();

        ahbTexture.Reset();

        mGl->eglClientWaitSyncKHR(mDisplay, uploadFence, /*flags=*/0,
                                  /*timeout=2 seconds*/ 2 * 1000 * 1000 * 1000);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroySyncKHR(mDisplay, uploadFence);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroyImageKHR(mDisplay, ahbImage);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));
    }

    // Blit from AHB to an additional framebuffer and readback:
    {
        const EGLint ahbImageAttribs[] = {
            // clang-format off
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_NONE,
            // clang-format on
        };
        EGLImageKHR ahbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture ahbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        ScopedGlFramebuffer readbackFramebuffer(*mGl);
        mGl->glBindFramebuffer(GL_FRAMEBUFFER, readbackFramebuffer);
        mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ahbTexture,
                                    0);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                EXPECT_THAT(GetPixelAt(x, y), IsOkWith(uploadPixel));
            }
        }

        mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

TEST_P(GfxstreamEnd2EndGlTest, AhbTextureUploadAndBlit) {
    const uint32_t width = 2;
    const uint32_t height = 2;

    ScopedGlTexture blitFramebufferTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, blitFramebufferTexture);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mGl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    mGl->glBindTexture(GL_TEXTURE_2D, 0);

    ScopedGlFramebuffer blitFramebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, blitFramebuffer);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                blitFramebufferTexture, 0);

    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    const auto uploadPixel = PixelR8G8B8A8(55, 66, 77, 88);
    const auto uploadPixels = Fill(width, height, uploadPixel);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    // Initialize AHB with `lockPixel`
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // Update AHB with `uploadPixel` via texture upload:
    {
        const EGLint ahbImageAttribs[] = {
            // clang-format off
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_NONE,
            // clang-format on
        };
        EGLImageKHR ahbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture ahbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahbImage);
        mGl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                             uploadPixels.data());
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        EGLSync uploadFence = mGl->eglCreateSyncKHR(mDisplay, EGL_SYNC_FENCE_KHR, nullptr);

        mGl->glFlush();

        ahbTexture.Reset();

        mGl->eglClientWaitSyncKHR(mDisplay, uploadFence, /*flags=*/0,
                                  /*timeout=2 seconds*/ 2 * 1000 * 1000 * 1000);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroySyncKHR(mDisplay, uploadFence);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroyImageKHR(mDisplay, ahbImage);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));
    }

    // Blit from AHB to an additional framebuffer and readback:
    {
        const std::string blitTextureVertSource = R"(#version 300 es

            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;

            out vec2 vTex;

            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string blitTextureFragSource = R"(#version 300 es

            precision highp float;

            uniform sampler2D uTexture;

            in vec2 vTex;

            out vec4 oColor;

            void main() {
                oColor = texture(uTexture, vTex);
            })";

        ScopedGlProgram program =
            GFXSTREAM_ASSERT(SetUpProgram(blitTextureVertSource, blitTextureFragSource));

        GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

        struct VertexAttributes {
            float pos[2];
            float tex[2];
        };
        const std::vector<VertexAttributes> fullscreenTriVerts = {
            // clang-format off
            { .pos = { -1.0f, -1.0f }, .tex = { 0.0f, 0.0f }, },
            { .pos = {  3.0f, -1.0f }, .tex = { 2.0f, 0.0f }, },
            { .pos = { -1.0f,  3.0f }, .tex = { 0.0f, 2.0f }, },
            // clang-format on
        };
        ScopedGlBuffer buffer(*mGl);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(VertexAttributes) * fullscreenTriVerts.size(),
                          fullscreenTriVerts.data(), GL_STATIC_DRAW);

        mGl->glUseProgram(program);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        mGl->glViewport(0, 0, width, height);
        mGl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGl->glClear(GL_COLOR_BUFFER_BIT);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glEnableVertexAttribArray(0);
        mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, pos));
        mGl->glEnableVertexAttribArray(1);
        mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, tex));
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        const EGLint ahbImageAttribs[] = {
            // clang-format off
            EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
            EGL_NONE,
            // clang-format on
        };
        EGLImageKHR ahbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture ahbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
        mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glActiveTexture(GL_TEXTURE0);
        mGl->glBindTexture(GL_TEXTURE_2D, ahbTexture);
        mGl->glUniform1i(textureUniformLoc, 0);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glFinish();

        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                EXPECT_THAT(GetPixelAt(x, y), IsOkWith(uploadPixel));
            }
        }

        mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

TEST_P(GfxstreamEnd2EndGlTest, MultiThreadedAhbTextureUploadAndReadback) {
    const uint32_t width = 2;
    const uint32_t height = 2;

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    const auto uploadPixel = PixelR8G8B8A8(55, 66, 77, 88);
    const auto uploadPixels = Fill(width, height, uploadPixel);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const EGLint ahbImageAttribs[] = {
        // clang-format off
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
        // clang-format on
    };
    EGLImageKHR ahbImage = mGl->eglCreateImageKHR(mDisplay, EGL_NO_CONTEXT,
                                                  EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
    ASSERT_THAT(ahbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

    std::vector<PixelR8G8B8A8> readbackPixels;

    SimpleLatch readbackThreadInitialized{1};
    SimpleLatch readbackThreadCanReadback{1};
    SimpleLatch readbackThreadDidReadback{1};
    SimpleLatch readbackThreadCanCleanup{1};
    std::thread readbackThread([&]() {
        EGLContext readbackThreadContext;
        EGLSurface readbackThreadSurface;
        SetUpEglContextAndSurface(2, 32, 32, &mDisplay, &readbackThreadContext,
                                  &readbackThreadSurface);
        {
            // Create a framebuffer for blitting the AHB into and reading back the blitted results
            // from:
            ScopedGlTexture readbackTexture(*mGl);
            mGl->glBindTexture(GL_TEXTURE_2D, readbackTexture);
            mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            mGl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
            mGl->glBindTexture(GL_TEXTURE_2D, 0);
            ScopedGlFramebuffer readbackFramebuffer(*mGl);
            mGl->glBindFramebuffer(GL_FRAMEBUFFER, readbackFramebuffer);
            mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                        readbackTexture, 0);
            ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
            ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

            // Create fullscreen triangle vertex buffer:
            struct VertexAttributes {
                float pos[2];
                float tex[2];
            };
            const std::vector<VertexAttributes> fullscreenTriVerts = {
                // clang-format off
                { .pos = { -1.0f, -1.0f }, .tex = { 0.0f, 0.0f }, },
                { .pos = {  3.0f, -1.0f }, .tex = { 2.0f, 0.0f }, },
                { .pos = { -1.0f,  3.0f }, .tex = { 0.0f, 2.0f }, },
                // clang-format on
            };
            ScopedGlBuffer buffer(*mGl);
            mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
            mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(VertexAttributes) * fullscreenTriVerts.size(),
                              fullscreenTriVerts.data(), GL_STATIC_DRAW);

            const std::string vertSource = R"(#version 300 es
                    layout (location = 0) in vec2 pos;
                    layout (location = 1) in vec2 tex;
                    out vec2 vTex;
                    void main() {
                        gl_Position = vec4(pos, 0.0, 1.0);
                        vTex = tex;
                    })";
            const std::string fragSource = R"(#version 300 es
                    precision highp float;
                    uniform sampler2D uTexture;
                    in vec2 vTex;
                    out vec4 oColor;
                    void main() {
                        oColor = texture(uTexture, vTex);
                    })";
            ScopedGlProgram program = GFXSTREAM_ASSERT(SetUpProgram(vertSource, fragSource));
            ASSERT_THAT(program, Not(Eq(0)));

            GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
            ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
            ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

            readbackThreadInitialized.count_down();

            readbackThreadCanReadback.wait();
            {
                EGLImageKHR readbackAhbImage = mGl->eglCreateImageKHR(
                    mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
                ASSERT_THAT(readbackAhbImage, Not(Eq(EGL_NO_IMAGE_KHR)));
                ScopedGlTexture readbackAhbTexture(*mGl);
                mGl->glBindTexture(GL_TEXTURE_2D, readbackAhbTexture);
                mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, readbackAhbImage);
                ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

                mGl->glUseProgram(program);
                ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
                mGl->glViewport(0, 0, width, height);
                mGl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                mGl->glClear(GL_COLOR_BUFFER_BIT);
                mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
                mGl->glEnableVertexAttribArray(0);
                mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                           (GLvoid*)offsetof(VertexAttributes, pos));
                mGl->glEnableVertexAttribArray(1);
                mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                           (GLvoid*)offsetof(VertexAttributes, tex));
                ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

                mGl->glActiveTexture(GL_TEXTURE0);
                mGl->glBindTexture(GL_TEXTURE_2D, readbackAhbTexture);
                mGl->glUniform1i(textureUniformLoc, 0);
                ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
                mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
                ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
                mGl->glFinish();

                std::vector<uint8_t> readbackBytes(width * height * 4);
                mGl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
                mGl->glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                                  readbackBytes.data());

                const uint8_t* readbackBytesPtr = readbackBytes.data();
                for (uint32_t y = 0; y < height; y++) {
                    for (uint32_t x = 0; x < width; x++) {
                        PixelR8G8B8A8 readbackPixel;
                        readbackPixel.x = x;
                        readbackPixel.y = y;
                        readbackPixel.r = *readbackBytesPtr;
                        ++readbackBytesPtr;
                        readbackPixel.g = *readbackBytesPtr;
                        ++readbackBytesPtr;
                        readbackPixel.b = *readbackBytesPtr;
                        ++readbackBytesPtr;
                        readbackPixel.a = *readbackBytesPtr;
                        ++readbackBytesPtr;
                        readbackPixels.push_back(readbackPixel);
                    }
                }
            }
            readbackThreadDidReadback.count_down();
        }
        readbackThreadCanCleanup.wait();
        TearDownEglContextAndSurface(mDisplay, readbackThreadContext, readbackThreadSurface);
    });

    EGLSync uploadCompleteFence = EGL_NO_SYNC;

    SimpleLatch uploadThreadInitialized{1};
    SimpleLatch uploadThreadStartUpload{1};
    SimpleLatch uploadThreadStartedUpload{1};
    SimpleLatch uploadThreadCanCleanup{1};
    std::thread uploadThread([&]() {
        EGLContext uploadThreadContext;
        EGLSurface uploadThreadSurface;
        SetUpEglContextAndSurface(2, 32, 32, &mDisplay, &uploadThreadContext, &uploadThreadSurface);
        uploadThreadInitialized.count_down();

        {
            // Update AHB with `uploadPixel` via texture upload:
            uploadThreadStartUpload.wait();
            {
                ScopedGlTexture uploadTexture(*mGl);
                mGl->glBindTexture(GL_TEXTURE_2D, uploadTexture);
                mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, ahbImage);
                mGl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA,
                                     GL_UNSIGNED_BYTE, uploadPixels.data());
                ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

                uploadCompleteFence = mGl->eglCreateSyncKHR(mDisplay, EGL_SYNC_FENCE_KHR, nullptr);
                ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));
                ASSERT_THAT(uploadCompleteFence, Not(Eq(EGL_NO_SYNC)));

                mGl->glFlush();
            }
            uploadThreadStartedUpload.count_down();
        }

        uploadThreadCanCleanup.wait();
        TearDownEglContextAndSurface(mDisplay, uploadThreadContext, uploadThreadSurface);
    });

    readbackThreadInitialized.wait();
    uploadThreadInitialized.wait();

    // "MainThread" updates the AHB with `lockPixel` via Gralloc->Lock():
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // "UploadThread" updates the AHB with `uploadPixel` via GL texture upload:
    uploadThreadStartUpload.count_down();

    // "MainThread" waits on upload fence:
    {
        uploadThreadStartedUpload.wait();

        ASSERT_THAT(uploadCompleteFence, Not(Eq(EGL_NO_SYNC)));
        mGl->eglClientWaitSyncKHR(mDisplay, uploadCompleteFence, /*flags=*/0,
                                  /*timeout=2 seconds*/ 2 * 1000 * 1000 * 1000);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroySyncKHR(mDisplay, uploadCompleteFence);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroyImageKHR(mDisplay, ahbImage);
    }

    // "ReadbackThread" blits the AHB contents to an internal framebuffer and performs readback:
    readbackThreadCanReadback.count_down();

    // Check readback results:
    readbackThreadDidReadback.wait();
    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {
            EXPECT_THAT(readbackPixels[(y * width) + x], Eq(uploadPixel));
        }
    }

    readbackThreadCanCleanup.count_down();
    readbackThread.join();

    uploadThreadCanCleanup.count_down();
    uploadThread.join();
}

TEST_P(GfxstreamEnd2EndGlTest, AhbTextureUploadAndExternalOesBlit) {
    const uint32_t width = 2;
    const uint32_t height = 2;

    ScopedGlTexture readbackTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, readbackTexture);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mGl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    mGl->glBindTexture(GL_TEXTURE_2D, 0);
    ScopedGlFramebuffer readbackFramebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, readbackFramebuffer);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                readbackTexture, 0);
    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    const auto uploadPixel = PixelR8G8B8A8(55, 66, 77, 88);
    const auto uploadPixels = Fill(width, height, uploadPixel);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const EGLint ahbImageAttribs[] = {
        // clang-format off
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
        // clang-format on
    };

    // Initialize AHB with `lockPixel`
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // Update AHB with `uploadPixel` via texture upload:
    {
        EGLImageKHR uploadAhbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(uploadAhbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture uploadAhbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_2D, uploadAhbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, uploadAhbImage);
        mGl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE,
                             uploadPixels.data());
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        EGLSync uploadFence = mGl->eglCreateSyncKHR(mDisplay, EGL_SYNC_FENCE_KHR, nullptr);

        mGl->glFlush();

        uploadAhbTexture.Reset();

        mGl->eglClientWaitSyncKHR(mDisplay, uploadFence, /*flags=*/0,
                                  /*timeout=2 seconds*/ 2 * 1000 * 1000 * 1000);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroySyncKHR(mDisplay, uploadFence);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));

        mGl->eglDestroyImageKHR(mDisplay, uploadAhbImage);
        ASSERT_THAT(mGl->eglGetError(), Eq(EGL_SUCCESS));
    }

    // Blit from AHB to an additional framebuffer for readback:
    {
        const std::string blitTextureVertSource = R"(#version 300 es
            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;
            out vec2 vTex;
            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string blitTextureFragSource = R"(#version 300 es
            #extension GL_OES_EGL_image_external
            precision highp float;
            uniform samplerExternalOES uTexture;
            in vec2 vTex;
            out vec4 oColor;
            void main() {
                oColor = texture(uTexture, vTex);
            })";

        ScopedGlProgram program =
            GFXSTREAM_ASSERT(SetUpProgram(blitTextureVertSource, blitTextureFragSource));

        GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

        struct VertexAttributes {
            float pos[2];
            float tex[2];
        };
        const std::vector<VertexAttributes> fullscreenTriVerts = {
            // clang-format off
            { .pos = { -1.0f, -1.0f }, .tex = { 0.0f, 0.0f }, },
            { .pos = {  3.0f, -1.0f }, .tex = { 2.0f, 0.0f }, },
            { .pos = { -1.0f,  3.0f }, .tex = { 0.0f, 2.0f }, },
            // clang-format on
        };
        ScopedGlBuffer buffer(*mGl);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(VertexAttributes) * fullscreenTriVerts.size(),
                          fullscreenTriVerts.data(), GL_STATIC_DRAW);

        mGl->glUseProgram(program);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        mGl->glViewport(0, 0, width, height);
        mGl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGl->glClear(GL_COLOR_BUFFER_BIT);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glEnableVertexAttribArray(0);
        mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, pos));
        mGl->glEnableVertexAttribArray(1);
        mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, tex));
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        EGLImageKHR blitAhbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(blitAhbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture blitAhbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, blitAhbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glActiveTexture(GL_TEXTURE0);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        mGl->glUniform1i(textureUniformLoc, 0);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glFinish();
    }

    // Readback and compare:
    {
        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                EXPECT_THAT(GetPixelAt(x, y), IsOkWith(uploadPixel));
            }
        }

        mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

TEST_P(GfxstreamEnd2EndGlTest, AhbExternalOesTextureBlit) {
    const uint32_t width = 2;
    const uint32_t height = 2;

    ScopedGlTexture readbackTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, readbackTexture);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mGl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    mGl->glBindTexture(GL_TEXTURE_2D, 0);
    ScopedGlFramebuffer readbackFramebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, readbackFramebuffer);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                readbackTexture, 0);
    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const EGLint ahbImageAttribs[] = {
        // clang-format off
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
        // clang-format on
    };

    // Initialize AHB with `lockPixel`
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // Blit from AHB to an additional framebuffer and readback:
    {
        const std::string blitTextureVertSource = R"(#version 300 es

            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;

            out vec2 vTex;

            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string blitTextureFragSource = R"(#version 300 es
            #extension GL_OES_EGL_image_external

            precision highp float;

            uniform samplerExternalOES uTexture;

            in vec2 vTex;

            out vec4 oColor;

            void main() {
                oColor = texture(uTexture, vTex);
            })";

        ScopedGlProgram program =
            GFXSTREAM_ASSERT(SetUpProgram(blitTextureVertSource, blitTextureFragSource));

        GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

        struct VertexAttributes {
            float pos[2];
            float tex[2];
        };
        const std::vector<VertexAttributes> fullscreenTriVerts = {
            // clang-format off
            { .pos = { -1.0f, -1.0f }, .tex = { 0.0f, 0.0f }, },
            { .pos = {  3.0f, -1.0f }, .tex = { 2.0f, 0.0f }, },
            { .pos = { -1.0f,  3.0f }, .tex = { 0.0f, 2.0f }, },
            // clang-format on
        };
        ScopedGlBuffer buffer(*mGl);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(VertexAttributes) * fullscreenTriVerts.size(),
                          fullscreenTriVerts.data(), GL_STATIC_DRAW);

        mGl->glUseProgram(program);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        mGl->glViewport(0, 0, width, height);
        mGl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGl->glClear(GL_COLOR_BUFFER_BIT);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glEnableVertexAttribArray(0);
        mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, pos));
        mGl->glEnableVertexAttribArray(1);
        mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, tex));
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        EGLImageKHR blitAhbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(blitAhbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture blitAhbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, blitAhbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glActiveTexture(GL_TEXTURE0);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        mGl->glUniform1i(textureUniformLoc, 0);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glFinish();

        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                EXPECT_THAT(GetPixelAt(x, y), IsOkWith(lockPixel));
            }
        }

        mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

TEST_P(GfxstreamEnd2EndGlTest, AhbExternalOesTextureBlitProgramBinary) {
    if (GetParam().with_features.count("GlProgramBinaryLinkStatus") == 0) {
        GTEST_SKIP() << "Skipping test, GlProgramBinaryLinkStatus not enabled.";
    }

    const uint32_t width = 2;
    const uint32_t height = 2;

    ScopedGlTexture readbackTexture(*mGl);
    mGl->glBindTexture(GL_TEXTURE_2D, readbackTexture);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    mGl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    mGl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, width, height);
    mGl->glBindTexture(GL_TEXTURE_2D, 0);
    ScopedGlFramebuffer readbackFramebuffer(*mGl);
    mGl->glBindFramebuffer(GL_FRAMEBUFFER, readbackFramebuffer);
    mGl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                                readbackTexture, 0);
    ASSERT_THAT(mGl->glCheckFramebufferStatus(GL_FRAMEBUFFER), Eq(GL_FRAMEBUFFER_COMPLETE));
    ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

    const auto lockPixel = PixelR8G8B8A8(11, 22, 33, 44);

    auto ahb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, width, height, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const EGLint ahbImageAttribs[] = {
        // clang-format off
        EGL_IMAGE_PRESERVED_KHR, EGL_TRUE,
        EGL_NONE,
        // clang-format on
    };

    // Initialize AHB with `lockPixel`
    GFXSTREAM_ASSERT(FillAhb(ahb, lockPixel));

    // Setup blit program:
    GLenum programBinaryFormat = GL_NONE;
    std::vector<uint8_t> programBinaryData;
    {
        const std::string vertSource = R"(#version 300 es
            layout (location = 0) in vec2 pos;
            layout (location = 1) in vec2 tex;
            out vec2 vTex;
            void main() {
                gl_Position = vec4(pos, 0.0, 1.0);
                vTex = tex;
            })";

        const std::string fragSource = R"(#version 300 es
            #extension GL_OES_EGL_image_external
            precision highp float;
            uniform samplerExternalOES uTexture;
            in vec2 vTex;
            out vec4 oColor;
            void main() {
                oColor = texture(uTexture, vTex);
            })";

        ScopedGlProgram program = GFXSTREAM_ASSERT(SetUpProgram(vertSource, fragSource));

        GLint programBinaryLength = 0;
        mGl->glGetProgramiv(program, GL_PROGRAM_BINARY_LENGTH, &programBinaryLength);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        programBinaryData.resize(programBinaryLength);

        GLint readProgramBinaryLength = 0;
        mGl->glGetProgramBinary(program, programBinaryLength, &readProgramBinaryLength,
                                &programBinaryFormat, programBinaryData.data());
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(readProgramBinaryLength, Eq(programBinaryLength));
    }

    // Blit from AHB to an additional framebuffer and readback:
    {
        ScopedGlProgram program =
            GFXSTREAM_ASSERT(SetUpProgram(programBinaryFormat, programBinaryData));
        ASSERT_THAT(program, Not(Eq(0)));

        GLint textureUniformLoc = mGl->glGetUniformLocation(program, "uTexture");
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        ASSERT_THAT(textureUniformLoc, Not(Eq(-1)));

        struct VertexAttributes {
            float pos[2];
            float tex[2];
        };
        const std::vector<VertexAttributes> fullscreenTriVerts = {
            // clang-format off
            { .pos = { -1.0f, -1.0f }, .tex = { 0.0f, 0.0f }, },
            { .pos = {  3.0f, -1.0f }, .tex = { 2.0f, 0.0f }, },
            { .pos = { -1.0f,  3.0f }, .tex = { 0.0f, 2.0f }, },
            // clang-format on
        };
        ScopedGlBuffer buffer(*mGl);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glBufferData(GL_ARRAY_BUFFER, sizeof(VertexAttributes) * fullscreenTriVerts.size(),
                          fullscreenTriVerts.data(), GL_STATIC_DRAW);

        mGl->glUseProgram(program);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));
        mGl->glViewport(0, 0, width, height);
        mGl->glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        mGl->glClear(GL_COLOR_BUFFER_BIT);
        mGl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        mGl->glEnableVertexAttribArray(0);
        mGl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, pos));
        mGl->glEnableVertexAttribArray(1);
        mGl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(VertexAttributes),
                                   (GLvoid*)offsetof(VertexAttributes, tex));
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        EGLImageKHR blitAhbImage = mGl->eglCreateImageKHR(
            mDisplay, EGL_NO_CONTEXT, EGL_NATIVE_BUFFER_ANDROID, ahb, ahbImageAttribs);
        ASSERT_THAT(blitAhbImage, Not(Eq(EGL_NO_IMAGE_KHR)));

        ScopedGlTexture blitAhbTexture(*mGl);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, blitAhbImage);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glActiveTexture(GL_TEXTURE0);
        mGl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, blitAhbTexture);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        mGl->glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        mGl->glUniform1i(textureUniformLoc, 0);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glDrawArrays(GL_TRIANGLES, 0, 3);
        ASSERT_THAT(mGl->glGetError(), Eq(GL_NO_ERROR));

        mGl->glFinish();

        for (int x = 0; x < width; x++) {
            for (int y = 0; y < height; y++) {
                EXPECT_THAT(GetPixelAt(x, y), IsOkWith(lockPixel));
            }
        }

        mGl->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

INSTANTIATE_TEST_CASE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndGlTest,
                        ::testing::ValuesIn({
                            TestParams{
                                .with_gl = true,
                                .with_vk = false,
                                .with_features = {"GlProgramBinaryLinkStatus"},
                            },
                            TestParams{
                                .with_gl = true,
                                .with_vk = true,
                                .with_features = {"GlProgramBinaryLinkStatus"},
                            },
                        }),
                        &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
