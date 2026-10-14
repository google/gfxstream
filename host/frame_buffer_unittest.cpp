// Copyright (C) 2018 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "frame_buffer.h"

#include <memory>
#include <thread>

#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/files/PathUtils.h"
#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/color_buffer_interface.h"
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/features.h"
#include "gfxstream/host/file_stream.h"
#include "gfxstream/host/gfxstream_format.h"
#include "gfxstream/host/mem_stream.h"
#include "gfxstream/host/testing/GLSnapshotTesting.h"
#include "gfxstream/host/testing/GLTestUtils.h"
#include "gfxstream/host/testing/OSWindow.h"
#include "gfxstream/host/testing/SampleApplication.h"
#include "gfxstream/host/testing/ShaderUtils.h"
#include "gfxstream/host/window_operations.h"
#include "gfxstream/system/System.h"
#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/graphics_driver_lock.h"
#include "host/gl/emulation_gl.h"
#include "host/gl/yuv_converter.h"
#include "render_channel_impl.h"
#include "render_thread_info.h"

#ifdef _MSC_VER
#include "gfxstream/msvc.h"
#else
#include <sys/time.h>
#endif

namespace gfxstream {
namespace host {
namespace {

using gfxstream::host::FeatureSet;
using gfxstream::host::StdioStream;
using gl::EGLDispatch;
using gl::GLESApi_3_0;
using gl::LazyLoadedEGLDispatch;
using gl::LazyLoadedGLESv2Dispatch;

class FrameBufferTest : public ::testing::Test {
  public:
    FrameBufferTest() = default;

  protected:
    static void SetUpTestSuite() {
        ASSERT_THAT(gfxstream::testing::SetupGraphicsTestEnvironment(), ::testing::IsTrue())
            << "Failed to configured graphics test environment!";
    }

    void SetUp() override {
        const EGLDispatch* egl = LazyLoadedEGLDispatch::get();
        ASSERT_NE(nullptr, egl);
        ASSERT_NE(nullptr, LazyLoadedGLESv2Dispatch::get());

        bool useHostGpu = shouldUseHostGpu();
        mWindow = createOrGetTestWindow(mXOffset, mYOffset, mWidth, mHeight);
        mUseSubWindow = mWindow != nullptr;

        FeatureSet features = {};
        features.EglOnEgl.setEnabled(!useHostGpu);

        if (mUseSubWindow) {
            ASSERT_NE(nullptr, mWindow->getFramebufferNativeWindow());

            EXPECT_TRUE(
                FrameBuffer::initialize(mWidth, mHeight, features, mUseSubWindow));
            mFb = FrameBuffer::getFB();
            EXPECT_NE(nullptr, mFb);

            mFb->setupSubWindow(
                (FBNativeWindowType)(uintptr_t)
                mWindow->getFramebufferNativeWindow(),
                0, 0,
                mWidth, mHeight, mWidth, mHeight,
                mWindow->getDevicePixelRatio(), 0, false, false);
            mWindow->messageLoop();
        } else {
            gfxstream::base::setEnvironmentVariable("ANDROID_EMU_HEADLESS", "1");
            EXPECT_TRUE(
                FrameBuffer::initialize(mWidth, mHeight, features, mUseSubWindow));
            mFb = FrameBuffer::getFB();
            ASSERT_NE(nullptr, mFb);
        }
        EXPECT_EQ(EGL_SUCCESS, egl->eglGetError());

        mRenderThreadInfo = new RenderThreadInfo();
        mRenderThreadInfo->initGl(mFb->getEmulationGl());
    }

    virtual void TearDown() override {
        FrameBuffer::finalize();
        mFb = nullptr;

        delete mRenderThreadInfo;
        EXPECT_EQ(EGL_SUCCESS, LazyLoadedEGLDispatch::get()->eglGetError())
                << "FrameBufferTest TearDown found EGL error";
    }

    void saveSnapshot() {
        mSnapshotStream = std::make_unique<MemStream>();
        mSnapshotTextureSaverLoader = std::make_shared<InMemoryTextureSaverLoader>();
        mFb->onSave(mSnapshotStream.get(), mSnapshotTextureSaverLoader);

    }

    void loadSnapshot() {
        // unbind so load will destroy previous objects
        mFb->bindContext(0, 0, 0);

        mSnapshotStream->rewind();
        mFb->onLoad(mSnapshotStream.get(), mSnapshotTextureSaverLoader);

    }

    bool mUseSubWindow = false;
    OSWindow* mWindow = nullptr;
    FrameBuffer* mFb = nullptr;
    RenderThreadInfo* mRenderThreadInfo = nullptr;

    std::unique_ptr<MemStream> mSnapshotStream;
    std::shared_ptr<InMemoryTextureSaverLoader> mSnapshotTextureSaverLoader;

    int mWidth = 256;
    int mHeight = 256;
    int mXOffset= 400;
    int mYOffset= 400;
};

// Tests that framebuffer initialization and finalization works.
TEST_F(FrameBufferTest, FrameBufferBasic) {
}

// Tests the creation of a single color buffer for the framebuffer.
TEST_F(FrameBufferTest, CreateColorBuffer) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    // FramBuffer::finalize handles color buffer destruction here
}

// Tests both creation and closing a color buffer.
TEST_F(FrameBufferTest, CreateCloseColorBuffer) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    mFb->closeColorBuffer(handle);
}

// Tests create, open, and close color buffer.
TEST_F(FrameBufferTest, CreateOpenCloseColorBuffer) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));
    mFb->closeColorBuffer(handle);
}

// Tests that the color buffer can be update with a test pattern and that
// the test pattern can be read back from the color buffer.
TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forRead.data(),
                         forRead.size());

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(), forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_BGRA8888) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::B8G8R8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::B8G8R8A8_UNORM,
                           forUpdate.data());

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::B8G8R8A8_UNORM,
                         forRead.data(), forRead.size());

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(), forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_ReadYUV420) {
    HandleType handle = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::YV21);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead = createTestPatternRGBA8888(mWidth, mHeight);
    memset(forRead.data(), 0x0, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));
    memset(forRead.data(), 0xff, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_ReadNV12) {
    HandleType handle = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::NV12);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead = createTestPatternRGBA8888(mWidth, mHeight);
    memset(forRead.data(), 0x0, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));
    memset(forRead.data(), 0xff, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_ReadNV12TOYUV420) {
    // nv12
    mWidth = 8;
    mHeight = 8;
    HandleType handle_nv12 = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::NV12);
    EXPECT_NE((HandleType)0, handle_nv12);
    EXPECT_EQ(0, mFb->openColorBuffer(handle_nv12));

    uint8_t forUpdate[] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
                           2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3,
                           2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3, 2, 3};

    uint8_t golden[] = {
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
            1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
            3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
    };

    mFb->updateColorBuffer(handle_nv12, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate);

    // yuv420
    HandleType handle_yuv420 = mFb->createColorBuffer(
            mWidth, mHeight, GfxstreamFormat::YV21);
    EXPECT_NE((HandleType)0, handle_yuv420);
    EXPECT_EQ(0, mFb->openColorBuffer(handle_yuv420));

    uint32_t textures[2] = {1, 2};

    mFb->swapTexturesAndUpdateColorBuffer(handle_nv12, 0, 0, mWidth, mHeight,
                                          GL_RGBA, GL_UNSIGNED_BYTE,
                                          FRAMEWORK_FORMAT_NV12, textures);
    mFb->swapTexturesAndUpdateColorBuffer(handle_yuv420, 0, 0, mWidth, mHeight,
                                          GL_RGBA, GL_UNSIGNED_BYTE,
                                          FRAMEWORK_FORMAT_NV12, textures);

    uint8_t forRead[sizeof(golden)];
    memset(forRead, 0x0, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle_yuv420, 0, 0, mWidth, mHeight, forRead,
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(
            ImageMatches(mWidth, mHeight * 3 / 2, 1, mWidth, golden, forRead));

    mFb->closeColorBuffer(handle_nv12);
    mFb->closeColorBuffer(handle_yuv420);
}

TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_ReadYV12) {
    mWidth = 20 * 16;
    HandleType handle = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::YV12);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead = createTestPatternRGBA8888(mWidth, mHeight);
    memset(forRead.data(), 0x0, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));
    memset(forRead.data(), 0xff, mWidth * mHeight * 3 / 2);
    mFb->readColorBufferYUV(handle, 0, 0, mWidth, mHeight, forRead.data(),
                            mWidth * mHeight * 3 / 2);

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, ColorBufferGlReplaceContentsRejectsUndersizedBuffer) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    // 100x100 YV12 requires 16-byte (or 32-byte under minigbm) luma/chroma row alignment
    // (>= 17,600 bytes), whereas an unaligned 4:2:0 buffer is only 15,000 bytes (b/511631253).
    constexpr uint32_t kYv12Width = 100;
    constexpr uint32_t kYv12Height = 100;
    gl::YUVConverter yv12Converter(kYv12Width, kYv12Height, GfxstreamFormat::YV12);
    const uint32_t yv12NeededBytes = yv12Converter.getDataSize();
    constexpr size_t kUnalignedYv12Bytes = kYv12Width * kYv12Height * 3 / 2;
    EXPECT_GT(yv12NeededBytes, kUnalignedYv12Bytes);

    // P010 uses 2 bytes per sample (3 * w * h bytes total).
    gl::YUVConverter p010Converter(16, 16, GfxstreamFormat::P010);
    EXPECT_EQ(p010Converter.getDataSize(), 16u * 16u * 3u);

    auto yv12Cb = mFb->getEmulationGl()->createColorBuffer(
        kYv12Width, kYv12Height, GfxstreamFormat::YV12, /*handle=*/1001);
    ASSERT_NE(nullptr, yv12Cb);

    std::vector<uint8_t> undersizedBuf(kUnalignedYv12Bytes, 0);
    EXPECT_FALSE(yv12Cb->replaceContents(undersizedBuf.data(), undersizedBuf.size()));

    std::vector<uint8_t> fullBuf(yv12NeededBytes, 0);
    EXPECT_TRUE(yv12Cb->replaceContents(fullBuf.data(), fullBuf.size()));
}

// bug: 110105029
// Tests that color buffer updates should not fail if there is a format change.
// Needed to accomodate format-changing behavior from the guest gralloc.
TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_FormatChange) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forRead.data(),
                         forRead.size());

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(),
                             forRead.data()));

    mFb->closeColorBuffer(handle);
}

// Regression test for b/462496183: ensure compatibility with older guests that
// are not using virtio gpu and are instead still using GLenums for format params.
TEST_F(FrameBufferTest, CreateOpenUpdateCloseColorBuffer_RGB565) {
    constexpr const uint32_t kWidth = 1024;
    constexpr const uint32_t kHeight = 1024;

    HandleType colorBufferHandle =
        mFb->createColorBuffer(kWidth, kHeight, GfxstreamFormat::R5G6B5_UNORM);
    ASSERT_THAT(colorBufferHandle, ::testing::Ne(0));

    for (int i = 0; i < 10; i++) {
        constexpr const uint32_t kBpp = 2;
        std::vector<uint8_t> buffer(kWidth * kHeight * kBpp);

        mFb->readColorBufferDeprecated(colorBufferHandle, 0, 0, kWidth, kHeight,
                                       GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buffer.data(),
                                       buffer.size());
        mFb->updateColorBufferDeprecated(colorBufferHandle, 0, 0, kWidth, kHeight,
                                         GL_RGB, GL_UNSIGNED_SHORT_5_6_5, buffer.data());
    }

    mFb->closeColorBuffer(colorBufferHandle);
}

TEST_F(FrameBufferTest, ReadColorBufferDeprecatedRejectsUndersizedBuffer) {
    HandleType handle = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM,
                           forUpdate.data());

    const size_t fullBytes = static_cast<size_t>(mWidth) * mHeight * 4;
    const size_t smallBytes = 16;
    constexpr uint8_t kSentinel = 0xAA;
    std::vector<uint8_t> bufWithGuard(fullBytes, kSentinel);

    // Passing pixelsSize smaller than width * height * bpp must not write past pixelsSize
    // (b/288556299).
    mFb->readColorBufferDeprecated(handle, 0, 0, mWidth, mHeight, GL_RGBA, GL_UNSIGNED_BYTE,
                                   bufWithGuard.data(), smallBytes);

    for (size_t i = smallBytes; i < bufWithGuard.size(); ++i) {
        ASSERT_EQ(kSentinel, bufWithGuard[i]) << "OOB write at byte " << i;
    }

    mFb->closeColorBuffer(handle);
}

// Tests obtaining EGL configs from FrameBuffer.
TEST_F(FrameBufferTest, Configs) {
    EGLint numConfigs = 0;
    EGLint numAttribs = 0;
    mFb->getNumConfigs(&numConfigs, &numAttribs);
    EXPECT_GE(numConfigs, 0);

    std::vector<GLuint> buffer((numConfigs + 1) * numAttribs, 0);
    mFb->getConfigs(buffer.size(), buffer.data());
}

// Tests creating GL context from FrameBuffer.
TEST_F(FrameBufferTest, CreateEmulatedEglContext) {
    HandleType handle = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    EXPECT_NE((HandleType)0, handle);
}

// Tests creating window surface from FrameBuffer.
TEST_F(FrameBufferTest, CreateEmulatedEglWindowSurface) {
    HandleType handle = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_NE((HandleType)0, handle);
}

// Tests eglMakeCurrent from FrameBuffer.
TEST_F(FrameBufferTest, CreateBindEmulatedEglContext) {
    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_TRUE(mFb->bindContext(context, surface, surface));
}

// A basic blit test that simulates what the guest system does in one pass
// of draw + eglSwapBuffers:
// 1. Draws in OpenGL with glClear.
// 2. Calls flushEmulatedEglWindowSurfaceColorBuffer(), which is the "backing operation" of
// ANativeWindow::queueBuffer in the guest.
// 3. Calls post() with the resulting color buffer, the backing operation of fb device "post"
// in the guest.
TEST_F(FrameBufferTest, BasicBlit) {
    auto gl = LazyLoadedGLESv2Dispatch::get();

    HandleType colorBuffer =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);

    EXPECT_TRUE(mFb->bindContext(context, surface, surface));
    EXPECT_TRUE(mFb->setEmulatedEglWindowSurfaceColorBuffer(surface, colorBuffer));

    float colors[3][4] = {
        { 1.0f, 0.0f, 0.0f, 1.0f},
        { 0.0f, 1.0f, 0.0f, 1.0f},
        { 0.0f, 0.0f, 1.0f, 1.0f},
    };

    for (int i = 0; i < 3; i++) {
        float* color = colors[i];

        gl->glClearColor(color[0], color[1], color[2], color[3]);
        gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        mFb->flushEmulatedEglWindowSurfaceColorBuffer(surface);

        TestTexture targetBuffer =
            createTestTextureRGBA8888SingleColor(
                mWidth, mHeight, color[0], color[1], color[2], color[3]);

        TestTexture forRead =
            createTestTextureRGBA8888SingleColor(
                mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);

        mFb->readColorBuffer(colorBuffer, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM,
                             forRead.data(), forRead.size());

        EXPECT_TRUE(
            ImageMatches(
                mWidth, mHeight, 4, mWidth,
                targetBuffer.data(), forRead.data()));

        if (mUseSubWindow) {
            mFb->post(colorBuffer);
            mWindow->messageLoop();
        }
    }

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->closeColorBuffer(colorBuffer);
    mFb->closeColorBuffer(colorBuffer);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

// Tests that snapshot works with an empty FrameBuffer.
TEST_F(FrameBufferTest, SnapshotSmokeTest) {
    saveSnapshot();
    loadSnapshot();
}

// Tests that the snapshot restores the clear color state, by changing the clear
// color in between save and load. If this fails, it means failure to restore a
// number of different states from GL contexts.
TEST_F(FrameBufferTest, SnapshotPreserveColorClear) {
    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_TRUE(mFb->bindContext(context, surface, surface));

    auto gl = LazyLoadedGLESv2Dispatch::get();
    gl->glClearColor(1, 1, 1, 1);
    gl->glClear(GL_COLOR_BUFFER_BIT);
    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE, {1, 1, 1, 1}));

    saveSnapshot();

    gl->glClearColor(0.5, 0.5, 0.5, 0.5);
    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE,
                                      {0.5, 0.5, 0.5, 0.5}));

    loadSnapshot();
    EXPECT_TRUE(mFb->bindContext(context, surface, surface));

    EXPECT_TRUE(compareGlobalGlFloatv(gl, GL_COLOR_CLEAR_VALUE, {1, 1, 1, 1}));
}

// Tests that snapshot works to save the state of a single ColorBuffer; we
// upload a test pattern to the ColorBuffer, take a snapshot, load it, and
// verify that the contents are the same.
TEST_F(FrameBufferTest, SnapshotSingleColorBuffer) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    saveSnapshot();
    loadSnapshot();

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forRead.data(),
                         forRead.size());

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(), forRead.data()));

    mFb->closeColorBuffer(handle);
}

// bug: 111360779
// Tests that the ColorBuffer is successfully updated even if a reformat happens
// on restore; the reformat may mess up the texture restore logic.
// In ColorBuffer::subUpdate, this test is known to fail if touch() is moved after the
// reformat.
TEST_F(FrameBufferTest, SnapshotColorBufferSubUpdateRestore) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);

    saveSnapshot();
    loadSnapshot();

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forRead.data(),
                         forRead.size());

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(), forRead.data()));

    mFb->closeColorBuffer(handle);
}

// Tests rate of draw calls with no guest/host communication, but with translator.
static constexpr uint32_t kDrawCallLimit = 50000;

TEST_F(FrameBufferTest, DrawCallRate) {
    HandleType colorBuffer =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);

    EXPECT_TRUE(mFb->bindContext(context, surface, surface));
    EXPECT_TRUE(mFb->setEmulatedEglWindowSurfaceColorBuffer(surface, colorBuffer));

    auto gl = LazyLoadedGLESv2Dispatch::get();

    constexpr char vshaderSrc[] = R"(#version 300 es
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
    constexpr char fshaderSrc[] = R"(#version 300 es
    precision highp float;

    in vec3 color_varying;

    out vec4 fragColor;

    void main() {
        fragColor = vec4(color_varying, 1.0);
    }
    )";

    GLuint program = compileAndLinkShaderProgram(vshaderSrc, fshaderSrc);

    GLint transformLoc = gl->glGetUniformLocation(program, "transform");

    struct VertexAttributes {
        float position[2];
        float color[3];
    };

    const VertexAttributes vertexAttrs[] = {
        { { -0.5f, -0.5f,}, { 0.2, 0.1, 0.9, }, },
        { { 0.5f, -0.5f,}, { 0.8, 0.3, 0.1,}, },
        { { 0.0f, 0.5f,}, { 0.1, 0.9, 0.6,}, },
    };

    GLuint buffer;
    gl->glGenBuffers(1, &buffer);
    gl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
    gl->glBufferData(GL_ARRAY_BUFFER, sizeof(vertexAttrs), vertexAttrs,
                     GL_STATIC_DRAW);

    gl->glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          sizeof(VertexAttributes), 0);
    gl->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                              sizeof(VertexAttributes),
                              (GLvoid*)offsetof(VertexAttributes, color));
    gl->glEnableVertexAttribArray(0);
    gl->glEnableVertexAttribArray(1);

    gl->glUseProgram(program);

    gl->glClearColor(0.2f, 0.2f, 0.3f, 0.0f);
    gl->glViewport(0, 0, 1, 1);

    float matrix[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    uint32_t drawCount = 0;

    auto cpuTimeStart = gfxstream::base::cpuTime();

    while (drawCount < kDrawCallLimit) {
        gl->glUniformMatrix4fv(transformLoc, 1, GL_FALSE, matrix);
        gl->glBindBuffer(GL_ARRAY_BUFFER, buffer);
        gl->glDrawArrays(GL_TRIANGLES, 0, 3);
        ++drawCount;
    }

    gl->glFinish();

    auto cpuTime = gfxstream::base::cpuTime() - cpuTimeStart;

    uint64_t duration_us = cpuTime.wall_time_us;
    // uint64_t duration_cpu_us = cpuTime.usageUs();

    float ms = duration_us / 1000.0f;
    float sec = duration_us / 1000000.0f;
    float drawCallHz = (float)kDrawCallLimit / sec;

    printf("Drew %u times in %f ms. Rate: %f Hz\n", kDrawCallLimit, ms, drawCallHz);

    // android::perflogger::logDrawCallOverheadTest(
    //     (const char*)gl->glGetString(GL_VENDOR),
    //     (const char*)gl->glGetString(GL_RENDERER),
    //     (const char*)gl->glGetString(GL_VERSION),
    //     "drawArrays", "decoder",
    //     kDrawCallLimit,
    //     (long)drawCallHz,
    //     duration_us,
    //     duration_cpu_us);

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->closeColorBuffer(colorBuffer);
    mFb->closeColorBuffer(colorBuffer);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

// Tests rate of draw calls with only the host driver and no translator.
TEST_F(FrameBufferTest, HostDrawCallRate) {
    HandleType colorBuffer =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);

    EXPECT_TRUE(mFb->bindContext(context, surface, surface));
    EXPECT_TRUE(mFb->setEmulatedEglWindowSurfaceColorBuffer(surface, colorBuffer));

    auto gl = LazyLoadedGLESv2Dispatch::get();

    uint64_t duration_us, duration_cpu_us;
    gl->glTestHostDriverPerformance(kDrawCallLimit, &duration_us, &duration_cpu_us);

    float ms = duration_us / 1000.0f;
    float sec = duration_us / 1000000.0f;
    float drawCallHz = kDrawCallLimit / sec;

    printf("Drew %u times in %f ms. Rate: %f Hz\n", kDrawCallLimit, ms, drawCallHz);

    // android::perflogger::logDrawCallOverheadTest(
    //     (const char*)gl->glGetString(GL_VENDOR),
    //     (const char*)gl->glGetString(GL_RENDERER),
    //     (const char*)gl->glGetString(GL_VERSION),
    //     "drawArrays", "host",
    //     kDrawCallLimit,
    //     (long)drawCallHz,
    //     duration_us,
    //     duration_cpu_us);

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->closeColorBuffer(colorBuffer);
    mFb->closeColorBuffer(colorBuffer);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

// Tests Vulkan interop query.
TEST_F(FrameBufferTest, VulkanInteropQuery) {
    auto egl = LazyLoadedEGLDispatch::get();

    EXPECT_NE(nullptr, egl->eglQueryVulkanInteropSupportANDROID);

    EGLBoolean supported =
        egl->eglQueryVulkanInteropSupportANDROID();

    // Disregard the result for now
    (void)supported;
}

// Tests ColorBuffer with GL_BGRA input.
TEST_F(FrameBufferTest, CreateColorBufferBGRA) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::B8G8R8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    // FramBuffer::finalize handles color buffer destruction here
}

// Test ColorBuffer with GL_RGBA, but read back as GL_BGRA, so that R/B are switched.
// TODO: This doesn't work on NVIDIA EGL, it issues GL_INVALID_OPERATION if the format doesn't
// match.
TEST_F(FrameBufferTest, DISABLED_ReadColorBufferSwitchRedBlue) {
    HandleType handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->openColorBuffer(handle));

    TestTexture forUpdate = createTestPatternRGBA8888(mWidth, mHeight);
    mFb->updateColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate.data());

    TestTexture forRead =
        createTestTextureRGBA8888SingleColor(mWidth, mHeight, 0.0f, 0.0f, 0.0f, 0.0f);
    // Switch red and blue
    mFb->readColorBuffer(handle, 0, 0, mWidth, mHeight, GfxstreamFormat::B8G8R8A8_UNORM,
                         forRead.data(), forRead.size());

    // Switch them back, so we get the original image
    uint8_t* forReadBytes = forRead.data();

    for (int row = 0; row < mHeight; ++row) {
        for (int col = 0; col < mWidth; ++col) {
            uint8_t* pixel = forReadBytes + mWidth * 4 * row + col * 4;
            // In RGBA8:
            //    3 2 1 0
            // 0xAABBGGRR on little endian systems
            // R component: pixel[0]
            // B component: pixel[2]
            uint8_t r = pixel[0];
            uint8_t b = pixel[2];
            pixel[0] = b;
            pixel[2] = r;
        }
    }

    EXPECT_TRUE(ImageMatches(mWidth, mHeight, 4, mWidth, forUpdate.data(), forRead.data()));

    mFb->closeColorBuffer(handle);
}

TEST_F(FrameBufferTest, CreateMultiDisplay) {
    uint32_t id = 1;
    mFb->createDisplay(&id);
    EXPECT_EQ(0, mFb->createDisplay(&id));
    EXPECT_EQ(0, mFb->destroyDisplay(id));
}

TEST_F(FrameBufferTest, BindMultiDisplayColorBuffer) {
    uint32_t id = 2;
    EXPECT_EQ(0, mFb->createDisplay(&id));
    uint32_t handle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_NE((HandleType)0, handle);
    EXPECT_EQ(0, mFb->setDisplayColorBuffer(2, handle));
    uint32_t getHandle = 0;
    mFb->getDisplayColorBuffer(id, &getHandle);
    EXPECT_EQ(handle, getHandle);
    uint32_t getId = 0;
    mFb->getColorBufferDisplay(handle, &getId);
    EXPECT_EQ(id, getId);
    mFb->closeColorBuffer(handle);
    EXPECT_EQ(0, mFb->destroyDisplay(id));
}

TEST_F(FrameBufferTest, SetMultiDisplayPosition) {
    uint32_t id = FrameBuffer::s_invalidIdMultiDisplay;
    mFb->createDisplay(&id);
    EXPECT_NE((uint32_t)0, id);
    uint32_t w = mWidth / 2, h = mHeight / 2;
    EXPECT_EQ(0, mFb->setDisplayPose(id, -1, -1, w, h));
    int32_t x, y;
    uint32_t width, height;
    EXPECT_EQ(0, mFb->getDisplayPose(id, &x, &y, &width, &height));
    EXPECT_EQ(w, width);
    EXPECT_EQ(h, height);
    EXPECT_EQ(0, mFb->destroyDisplay(id));
}

TEST_F(FrameBufferTest, SetMultiDisplayColorTransform) {
    uint32_t id = FrameBuffer::s_invalidIdMultiDisplay;
    mFb->createDisplay(&id);
    EXPECT_NE((uint32_t)0, id);
    const float colorTransform[16] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    EXPECT_EQ(0, mFb->setDisplayColorTransform(id, colorTransform));
    float colorTransformReadback[16] = {};
    EXPECT_EQ(0, mFb->getDisplayColorTransform(id, colorTransformReadback));
    for (int i = 0; i < 16; i++) {
        EXPECT_EQ(colorTransform[i], colorTransformReadback[i]);
    }
    EXPECT_EQ(0, mFb->destroyDisplay(id));
}

TEST_F(FrameBufferTest, ComposeMultiDisplay) {
    LazyLoadedGLESv2Dispatch::get();

    HandleType context = mFb->createEmulatedEglContext(0, 0, GLESApi_3_0);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_TRUE(mFb->bindContext(context, surface, surface));

    HandleType cb0 =
        mFb->createColorBuffer(mWidth/2, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    TestTexture forUpdate0 = createTestTextureRGBA8888SingleColor(mWidth/2, mHeight, 1.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_EQ(0, mFb->openColorBuffer(cb0));
    mFb->updateColorBuffer(cb0, 0, 0, mWidth/2, mHeight, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate0.data());

    uint32_t cb1 =
        mFb->createColorBuffer(mWidth/2, mHeight/2, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(0, mFb->openColorBuffer(cb1));
    TestTexture forUpdate1 = createTestTextureRGBA8888SingleColor(mWidth/2, mHeight/2, 1.0f, 0.0f, 0.0f, 1.0f);
    mFb->updateColorBuffer(cb1, 0, 0, mWidth/2, mHeight/2, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate1.data());

    uint32_t cb2 =
        mFb->createColorBuffer(mWidth/4, mHeight/2, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(0, mFb->openColorBuffer(cb2));
    TestTexture forUpdate2 = createTestTextureRGBA8888SingleColor(mWidth/4, mHeight/2, 0.0f, 1.0f, 0.0f, 1.0f);
    mFb->updateColorBuffer(cb2, 0, 0, mWidth/4, mHeight/2, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate2.data());

    uint32_t cb3 =
        mFb->createColorBuffer(mWidth/4, mHeight/4, GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(0, mFb->openColorBuffer(cb3));
    TestTexture forUpdate3 = createTestTextureRGBA8888SingleColor(mWidth/4, mHeight/4, 0.0f, 0.0f, 1.0f, 1.0f);
    mFb->updateColorBuffer(cb3, 0, 0, mWidth/4, mHeight/4, GfxstreamFormat::R8G8B8A8_UNORM, forUpdate3.data());

    FrameBuffer::DisplayInfo info[] =
    {{cb1, -1, -1, (uint32_t)mWidth/2, (uint32_t)mHeight/2, 240},
     {cb2, -1, -1, (uint32_t)mWidth/4, (uint32_t)mHeight/2, 240},
     {cb3, -1, -1, (uint32_t)mWidth/4, (uint32_t)mHeight/4, 240}};

    uint32_t ids[] = {1, 2, 3};
    for (uint32_t i = 0; i < 3 ; i++) {
        EXPECT_EQ(0, mFb->createDisplay(&ids[i]));
        EXPECT_EQ(0, mFb->setDisplayPose(ids[i],
                                         info[i].pos_x,
                                         info[i].pos_y,
                                         info[i].width,
                                         info[i].height));
        EXPECT_EQ(0, mFb->setDisplayColorBuffer(ids[i], info[i].cb));
    }

    if (mUseSubWindow) {
        mFb->post(cb0);
        mWindow->messageLoop();
    }

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->closeColorBuffer(cb0);
    mFb->closeColorBuffer(cb1);
    mFb->closeColorBuffer(cb2);
    mFb->closeColorBuffer(cb3);
    mFb->destroyDisplay(ids[0]);
    mFb->destroyDisplay(ids[1]);
    mFb->destroyDisplay(ids[2]);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

class RenderThreadDeadlockTest : public ::testing::Test {
  protected:
    static void SetUpTestSuite() {
        ASSERT_THAT(gfxstream::testing::SetupGraphicsTestEnvironment(), ::testing::IsTrue())
            << "Failed to configure graphics test environment!";
    }

    void SetUp() override {
        const EGLDispatch* egl = LazyLoadedEGLDispatch::get();
        ASSERT_NE(nullptr, egl);
        ASSERT_NE(nullptr, LazyLoadedGLESv2Dispatch::get());

        bool useHostGpu = shouldUseHostGpu();

        FeatureSet features = {};
        features.EglOnEgl.setEnabled(!useHostGpu);
        features.Vulkan.setEnabled(true);
        features.VulkanQueueSubmitWithCommands.setEnabled(true);

        gfxstream::base::setEnvironmentVariable("ANDROID_EMU_HEADLESS", "1");
        EXPECT_TRUE(FrameBuffer::initialize(256, 256, features, false));
    }

    void TearDown() override {
        AddressSpaceGraphicsContext::clear();
        AddressSpaceGraphicsContext::setConsumer({});
        FrameBuffer::finalize();
    }
};

TEST_F(RenderThreadDeadlockTest, ReproDeadlockOnTeardown) {
    auto fb = FrameBuffer::getFB();
    ASSERT_NE(nullptr, fb);
    if (!fb->hasEmulationVk()) {
        GTEST_SKIP() << "Vulkan emulation not supported on this host.";
    }

    const uint32_t contextId = 12345;
    fb->createGraphicsProcessResources(contextId);

    auto channel = std::make_unique<RenderChannelImpl>(nullptr, contextId);
    ASSERT_NE(nullptr, channel->renderThread());

    RenderChannel::Buffer buffer;
    uint32_t flags = 0;
    uint32_t opcode = 200000000; // OP_vkCreateInstance
    uint32_t packetLen = 12;
    uint32_t seqno = 2;

    buffer.resize(16);
    std::memcpy(buffer.data(), &flags, sizeof(flags));
    std::memcpy(buffer.data() + 4, &opcode, sizeof(opcode));
    std::memcpy(buffer.data() + 8, &packetLen, sizeof(packetLen));
    std::memcpy(buffer.data() + 12, &seqno, sizeof(seqno));

    channel->tryWrite(std::move(buffer));

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto teardownPromise = std::make_shared<std::promise<void>>();
    std::future<void> teardownFuture = teardownPromise->get_future();

    std::thread teardownThread(
        [fb, channel = std::move(channel), teardownPromise]() mutable {
            fb->signalGuestProcessDead(contextId);
            fb->waitForAllPendingCleanups();
            channel->stop();
            channel.reset();
            teardownPromise->set_value();
        });

    std::future_status status = teardownFuture.wait_for(std::chrono::seconds(3));
    if (status == std::future_status::timeout) {
        teardownThread.detach();
        ADD_FAILURE() << "Deadlock detected! Channel teardown timed out after 3 seconds.";
    } else {
        teardownThread.join();
    }
}

TEST_F(RenderThreadDeadlockTest, ReproAsgFullMessageChannelDeadlockOnTeardown) {
    auto fb = FrameBuffer::getFB();
    ASSERT_NE(nullptr, fb);
    if (!fb->hasEmulationVk()) {
        GTEST_SKIP() << "Vulkan emulation not supported on this host.";
    }

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [](const AsgConsumerCreateInfo& info, Stream* loadStream) -> ConsumerHandle {
            auto* thread = new RenderThread(info, loadStream);
            thread->start();
            return static_cast<void*>(thread);
        },
        .destroy = [](void* consumer) {
            auto* thread = static_cast<RenderThread*>(consumer);
            thread->waitForFinished();
            {
                gfxstream::base::AutoLock driverLock(*graphicsDriverLock());
                thread->sendExitSignal();
                thread->wait();
            }
            delete thread;
        },
        .preSave = [](void*) {},
        .globalPreSave = []() {},
        .save = [](void*, Stream*) {},
        .globalPostSave = []() {},
        .postSave = [](void*) {},
        .postLoad = [](void*) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](void*) {},
    });

    const uint32_t contextId = 12346;
    fb->createGraphicsProcessResources(contextId);

    auto asgMemory =
        std::make_shared<std::vector<uint8_t>>(kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    AddressSpaceCreateInfo createInfo = {
        .handle = 1,
        .type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS,
        .createRenderThread = true,
        .externalAddr = asgMemory->data(),
        .externalAddrSize = static_cast<uint64_t>(asgMemory->size()),
        .virtioGpuContextId = contextId,
        .virtioGpuCapsetId = 0,
        .contextName = "test-asg",
        .contextNameSize = 8,
    };
    auto asg = std::make_unique<AddressSpaceGraphicsContext>(createInfo);

    char* ringStorage = reinterpret_cast<char*>(asgMemory->data());
    char* writeBuffer = ringStorage + kAsgConsumerRingStorageSize;
    auto* toHostRing = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, to_host));

    uint32_t flags = 0;
    uint32_t opcode = 200000000;  // OP_vkCreateInstance
    uint32_t packetLen = 12;
    uint32_t seqno = 2;
    std::memcpy(writeBuffer, &flags, sizeof(flags));
    std::memcpy(writeBuffer + 4, &opcode, sizeof(opcode));
    std::memcpy(writeBuffer + 8, &packetLen, sizeof(packetLen));
    std::memcpy(writeBuffer + 12, &seqno, sizeof(seqno));

    asg_type1_xfer xfer = {
        .offset = 0,
        .size = 16,
    };
    ASSERT_EQ(1, ring_buffer_write(toHostRing, &xfer, sizeof(xfer), 1));

    // Wake up RenderThread so it enters VkDecoder::Impl::decode and spins on seqno = 2,
    // then send additional ASG_NOTIFY_AVAILABLE pings to fill mConsumerMessages (capacity 4).
    AddressSpaceDevicePingInfo pingInfo = {
        .metadata = ASG_NOTIFY_AVAILABLE,
    };
    asg->perform(&pingInfo);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    for (int i = 0; i < 6; ++i) {
        asg->perform(&pingInfo);
    }

    auto teardownPromise = std::make_shared<std::promise<void>>();
    std::future<void> teardownFuture = teardownPromise->get_future();

    std::thread teardownThread(
        [fb, asg = std::move(asg), asgMemory, teardownPromise]() mutable {
            fb->signalGuestProcessDead(contextId);
            asg.reset();
            fb->waitForAllPendingCleanups();
            AddressSpaceGraphicsContext::clear();
            teardownPromise->set_value();
        });

    std::future_status status = teardownFuture.wait_for(std::chrono::seconds(3));
    if (status == std::future_status::timeout) {
        teardownThread.detach();
        ADD_FAILURE() << "Deadlock detected! ASG teardown timed out after 3 seconds.";
    } else {
        teardownThread.join();
    }
}

// ASGs historically used a fixed capacity message queue which would lead to deadlocks
// if the queue became full but the consumer could not be awoken until the queue was
// drained (e.g. b/329287602).
TEST_F(RenderThreadDeadlockTest, CoalescedAsgConsumerStateFlags) {
    ConsumerCallbacks capturedCallbacks = {};
    bool destroyObservedExitStatus = false;

    ConsumerInterface iface = {
        .create = [&](const AsgConsumerCreateInfo& info, Stream*) -> ConsumerHandle {
            capturedCallbacks = info.callbacks;
            return reinterpret_cast<ConsumerHandle>(0x1);
        },
        .destroy = [&](ConsumerHandle) {
            if (capturedCallbacks.onUnavailableRead) {
                destroyObservedExitStatus =
                    (capturedCallbacks.onUnavailableRead() == AsgOnUnavailableReadStatus::kExit);
            }
        },
        .preSave = [](ConsumerHandle) {},
        .globalPreSave = []() {},
        .save = [](ConsumerHandle, Stream*) {},
        .globalPostSave = []() {},
        .postSave = [](ConsumerHandle) {},
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
    };
    AddressSpaceGraphicsContext::setConsumer(iface);

    std::vector<char> externalStorage(kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    AddressSpaceCreateInfo createInfo = {};
    createInfo.handle = 1;
    createInfo.type = static_cast<uint32_t>(AddressSpaceDeviceType::VirtioGpuGraphics);
    createInfo.createRenderThread = true;
    createInfo.externalAddr = externalStorage.data();
    createInfo.externalAddrSize = externalStorage.size();
    createInfo.virtioGpuContextId = 1;

    auto context = std::make_unique<AddressSpaceGraphicsContext>(createInfo);
    ASSERT_TRUE(static_cast<bool>(capturedCallbacks.onUnavailableRead));

    // Sending more than 4 ASG_NOTIFY_AVAILABLE pings without the consumer draining
    // onUnavailableRead() must not block and should coalesce into a single wakeup.
    for (int i = 0; i < 16; ++i) {
        AddressSpaceDevicePingInfo pingInfo = {};
        pingInfo.metadata = ASG_NOTIFY_AVAILABLE;
        context->perform(&pingInfo);
    }
    EXPECT_EQ(AsgOnUnavailableReadStatus::kContinue, capturedCallbacks.onUnavailableRead());

    // Subsequent notifications + snapshot transitions must not block and must preserve
    // priority ordering (PreSave -> PostSave -> Wakeup).
    for (int i = 0; i < 16; ++i) {
        AddressSpaceDevicePingInfo pingInfo = {};
        pingInfo.metadata = ASG_NOTIFY_AVAILABLE;
        context->perform(&pingInfo);
    }
    context->preSave();
    EXPECT_EQ(AsgOnUnavailableReadStatus::kPauseForSnapshot,
              capturedCallbacks.onUnavailableRead());

    context->postSave();
    EXPECT_EQ(AsgOnUnavailableReadStatus::kResumeAfterSnapshot,
              capturedCallbacks.onUnavailableRead());
    EXPECT_EQ(AsgOnUnavailableReadStatus::kContinue, capturedCallbacks.onUnavailableRead());

    // Flood notifications again and destroy context without draining wakeup; exit must take
    // precedence and never block.
    for (int i = 0; i < 16; ++i) {
        AddressSpaceDevicePingInfo pingInfo = {};
        pingInfo.metadata = ASG_NOTIFY_AVAILABLE;
        context->perform(&pingInfo);
    }
    context.reset();
    EXPECT_TRUE(destroyObservedExitStatus);

    AddressSpaceGraphicsContext::clear();
    AddressSpaceGraphicsContext::setConsumer({});
}

TEST_F(FrameBufferTest, CleanupProcGLObjects) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    const uint64_t testPuid = 42;
    mRenderThreadInfo->setPuid(testPuid);
    EXPECT_EQ(testPuid, mRenderThreadInfo->m_glInfo->m_puid);

    HandleType context = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    EXPECT_NE((HandleType)0, context);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
    EXPECT_TRUE(mFb->getEmulationGl()->hasProcOwnedResources());

    // Simulate render thread exit for this process before cleanupProcGLObjects
    mRenderThreadInfo->setPuid(0);
    mFb->signalGuestProcessDead(testPuid);
    mFb->waitForAllPendingCleanups();

    EXPECT_FALSE(mFb->getEmulationGl()->hasProcOwnedResources());
    EXPECT_FALSE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());

    // Verify fallback thread context tracking when puid == 0
    HandleType threadContext = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    EXPECT_NE((HandleType)0, threadContext);
    EXPECT_TRUE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
    mFb->getEmulationGl()->drainRenderThreadContexts();
    EXPECT_FALSE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
}

TEST_F(FrameBufferTest, ReusePuidBeforeAsyncCleanupCompletes) {
    const uint64_t testPuid = 99;
    mFb->createGraphicsProcessResources(testPuid);
    std::shared_ptr<const ProcessResources> firstProcResources = mFb->getProcessResources(testPuid);
    ASSERT_NE(nullptr, firstProcResources);
    EXPECT_FALSE(firstProcResources->isGuestProcessDead());

    mRenderThreadInfo->setPuid(testPuid);
    mRenderThreadInfo->setProcessResources(firstProcResources);

    bool firstCleanupRan = false;
    int firstKey = 1;
    mFb->registerProcessCleanupCallback(&firstKey, testPuid, [&]() {
        firstCleanupRan = true;
    });

    // Signal first process dead while its RenderThreadInfo is still active.
    mFb->signalGuestProcessDead(testPuid);
    EXPECT_TRUE(firstProcResources->isGuestProcessDead());

    // Immediately reuse the same puid for a new guest process before the first cleanup finishes.
    mFb->createGraphicsProcessResources(testPuid);
    std::shared_ptr<const ProcessResources> secondProcResources =
        mFb->getProcessResources(testPuid);
    ASSERT_NE(nullptr, secondProcResources);
    EXPECT_NE(firstProcResources, secondProcResources);
    EXPECT_FALSE(secondProcResources->isGuestProcessDead());

    bool secondCleanupRan = false;
    int secondKey = 2;
    mRenderThreadInfo->setProcessResources(secondProcResources);
    mFb->registerProcessCleanupCallback(&secondKey, testPuid, [&]() {
        secondCleanupRan = true;
    });

    // Having switched mRenderThreadInfo to secondProcResources, the first process has no active
    // RenderThreadInfo anymore, so its async cleanup can complete without touching secondProcResources.
    mFb->waitForAllPendingCleanups();
    EXPECT_TRUE(firstCleanupRan);
    EXPECT_FALSE(secondCleanupRan);
    EXPECT_EQ(secondProcResources, mFb->getProcessResources(testPuid));

    // Now clean up the second process.
    mRenderThreadInfo->setProcessResources(nullptr);
    mRenderThreadInfo->setPuid(0);
    mFb->signalGuestProcessDead(testPuid);
    mFb->waitForAllPendingCleanups();
    EXPECT_TRUE(secondCleanupRan);
}

TEST_F(FrameBufferTest, CrossVersionRebindClearsContextData) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx2 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    ASSERT_NE((HandleType)0, ctx2);
    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);

    // Bind GLES2 context, then rebind to GLES1 context and destroy GLES2 context.
    EXPECT_TRUE(mFb->bindContext(ctx2, surface, surface));
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));
    mFb->destroyEmulatedEglContext(ctx2);

    float dummyData[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    // m_gl2Dec.m_contextData must have been cleared on cross-version rebind so this does not UAF.
    mRenderThreadInfo->m_glInfo->m_gl2Dec.glVertexAttribPointerData(
        &mRenderThreadInfo->m_glInfo->m_gl2Dec, 0, 4, GL_FLOAT, GL_FALSE, 0, dummyData,
        sizeof(dummyData));

    // Reverse direction: rebind from GLES1 to a new GLES2 context and destroy GLES1 context.
    HandleType ctx2b = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    ASSERT_NE((HandleType)0, ctx2b);
    EXPECT_TRUE(mFb->bindContext(ctx2b, surface, surface));
    mFb->destroyEmulatedEglContext(ctx1);

    mRenderThreadInfo->m_glInfo->m_glDec.glVertexPointerData(
        &mRenderThreadInfo->m_glInfo->m_glDec, 4, GL_FLOAT, 0, dummyData, sizeof(dummyData));

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx2b);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, CrossVersionTranslatorCallRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx2 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    ASSERT_NE((HandleType)0, ctx2);
    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);

    // Bind GLES2 context and invoke GLES1 GET_CTX_CM() entry points via m_glDec.
    EXPECT_TRUE(mFb->bindContext(ctx2, surface, surface));
    (void)mRenderThreadInfo->m_glInfo->m_gl2Dec.glGetError();

    const GLfloat ambient[4] = {0.2f, 0.3f, 0.4f, 1.0f};
    GLfloat outParams[4] = {-1.0f, -1.0f, -1.0f, -1.0f};
    mRenderThreadInfo->m_glInfo->m_glDec.glLightfv(GL_LIGHT0, GL_AMBIENT, ambient);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, mRenderThreadInfo->m_glInfo->m_gl2Dec.glGetError());

    mRenderThreadInfo->m_glInfo->m_glDec.glGetLightfv(GL_LIGHT0, GL_AMBIENT, outParams);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, mRenderThreadInfo->m_glInfo->m_gl2Dec.glGetError());
    EXPECT_EQ(-1.0f, outParams[0]);

    // Bind GLES1 context and invoke a GLES2 GET_CTX_V2() entry point via m_gl2Dec.
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));
    (void)mRenderThreadInfo->m_glInfo->m_glDec.glGetError();

    mRenderThreadInfo->m_glInfo->m_gl2Dec.glUseProgram(0);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, mRenderThreadInfo->m_glInfo->m_glDec.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglContext(ctx2);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, DeletedVboInVertexPointerDoesNotUaf) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    GLuint vbo = 0;
    gl1.glGenBuffers(1, &vbo);
    ASSERT_NE(0u, vbo);
    gl1.glBindBuffer(GL_ARRAY_BUFFER, vbo);

    GLfixed verts[9] = {
        0, 0, 0,
        0x10000, 0, 0,
        0, 0x10000, 0,
    };
    gl1.glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    gl1.glVertexPointerOffset(&gl1, 3, GL_FIXED, 0, 0);

    // Deleting the VBO must not leave GLESpointer with a dangling GLESbuffer* pointer.
    gl1.glDeleteBuffers(1, &vbo);
    gl1.glDrawArrays(GL_TRIANGLES, 0, 3);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl1.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1DrawArraysOutOfBoundsVertexPointerRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    GLfloat smallVerts[3] = {0.0f, 0.0f, 0.0f};
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    gl1.glVertexPointerData(&gl1, 3, GL_FLOAT, 0, smallVerts, sizeof(smallVerts));
    (void)gl1.glGetError();

    // Requesting 10000 vertices from a 1-vertex buffer must be rejected.
    gl1.glDrawArrays(GL_TRIANGLES, 0, 10000);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl1.glGetError());

    // Negative first index must be rejected with GL_INVALID_VALUE.
    gl1.glDrawArrays(GL_TRIANGLES, -1, 1);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl1.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1DrawArraysNeededBytesExceedingInt32MaxRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    GLfloat smallVerts[6] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
    };
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    // Use a large stride (0x40000000) with a direct client pointer (capacity == 0) so that
    // totalVerts (3) is within INT32_MAX / 16, but neededBytes ((3 - 1) * 0x40000000 + 12)
    // exceeds INT32_MAX.
    gl1.glVertexPointer(3, GL_FLOAT, 0x40000000, smallVerts);
    (void)gl1.glGetError();

    gl1.glDrawArrays(GL_TRIANGLES, 0, 3);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl1.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1DrawElementsOutOfBoundsIboOffsetRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    GLfloat verts[9] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
    };
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    gl1.glVertexPointerData(&gl1, 3, GL_FLOAT, 0, verts, sizeof(verts));

    GLuint ibo = 0;
    gl1.glGenBuffers(1, &ibo);
    ASSERT_NE(0u, ibo);
    gl1.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    GLushort indices[3] = {0, 1, 2};
    gl1.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
    (void)gl1.glGetError();

    // Out-of-bounds byte offset into the 6-byte IBO must be rejected.
    gl1.glDrawElementsOffset(&gl1, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0x100000);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl1.glGetError());

    gl1.glDeleteBuffers(1, &ibo);
    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, DrawElementsDataExceedingDatalenRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType ctx2 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_0);
    ASSERT_NE((HandleType)0, ctx2);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);

    GLfloat verts[12] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };
    GLushort smallIndices[3] = {0, 1, 2};

    // GLES1 s_glDrawElementsData with count * 2 > datalen must return without OOB read.
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));
    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    gl1.glVertexPointerData(&gl1, 4, GL_FLOAT, 0, verts, sizeof(verts));
    gl1.glDrawElementsData(&gl1, GL_TRIANGLES, 100000, GL_UNSIGNED_SHORT, smallIndices,
                           sizeof(smallIndices));

    // GLES2/3 s_glDrawElementsData and s_glDrawElementsInstancedDataAEMU with count * 2 > datalen.
    EXPECT_TRUE(mFb->bindContext(ctx2, surface, surface));
    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    gl2.glEnableVertexAttribArray(0);
    gl2.glVertexAttribPointerData(&gl2, 0, 4, GL_FLOAT, GL_FALSE, 0, verts, sizeof(verts));
    gl2.glDrawElementsData(&gl2, GL_TRIANGLES, 100000, GL_UNSIGNED_SHORT, smallIndices,
                           sizeof(smallIndices));
    gl2.glDrawElementsInstancedDataAEMU(&gl2, GL_TRIANGLES, 100000, GL_UNSIGNED_SHORT, smallIndices,
                                        1, sizeof(smallIndices));

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglContext(ctx2);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1ConvertIndirectLargeUintIndexRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    GLfixed fixedVerts[12] = {
        0, 0, 0, 0x10000,
        0x10000, 0, 0, 0x10000,
        0, 0x10000, 0, 0x10000,
    };
    gl1.glEnableClientState(GL_VERTEX_ARRAY);
    gl1.glVertexPointerData(&gl1, 4, GL_FIXED, 0, fixedVerts, sizeof(fixedVerts));
    (void)gl1.glGetError();

    // Index 0x40000000u would overflow 32-bit attribSize * maxElements in convertIndirect.
    GLuint hugeIndices[3] = {0u, 1u, 0x40000000u};
    gl1.glDrawElementsData(&gl1, GL_TRIANGLES, 3, GL_UNSIGNED_INT, hugeIndices,
                           sizeof(hugeIndices));
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl1.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles2ValidateAtt0PreDrawLargeUintIndexDoesNotOverflow) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx2 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    ASSERT_NE((HandleType)0, ctx2);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx2, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    GLfloat verts[12] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };
    // Leave attribute 0 disabled and enable attribute 1 with a client array so
    // drawWithEmulations enters both needClientVBOSetup and needAtt0PreDrawValidation().
    gl2.glDisableVertexAttribArray(0);
    gl2.glEnableVertexAttribArray(1);
    gl2.glVertexAttribPointerData(&gl2, 1, 4, GL_FLOAT, GL_FALSE, 0, verts, sizeof(verts));
    (void)gl2.glGetError();

    // Huge GL_UNSIGNED_INT index must be rejected with GL_OUT_OF_MEMORY instead of overflowing 4 * newLen.
    GLuint hugeIndices[3] = {0u, 1u, 0x40000000u};
    gl2.glDrawElementsData(&gl2, GL_TRIANGLES, 3, GL_UNSIGNED_INT, hugeIndices,
                           sizeof(hugeIndices));
    EXPECT_EQ((GLenum)GL_OUT_OF_MEMORY, gl2.glGetError());

    // Bound IBO with non-zero byte offset must resolve against the IBO buffer rather than
    // dereferencing the integer offset as a host pointer.
    GLuint ibo = 0;
    gl2.glGenBuffers(1, &ibo);
    ASSERT_NE(0u, ibo);
    gl2.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
    GLushort paddedIndices[5] = {0xffff, 0xffff, 0, 1, 2};
    gl2.glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(paddedIndices), paddedIndices, GL_STATIC_DRAW);
    (void)gl2.glGetError();

    gl2.glDrawElementsOffset(&gl2, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 4);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());

    // Out-of-bounds IBO offset must fail with GL_INVALID_OPERATION.
    gl2.glDrawElementsOffset(&gl2, GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, 0x10000);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    gl2.glDeleteBuffers(1, &ibo);
    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx2);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1CompressedTexSubImage2DOverflowRejected) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    std::vector<uint8_t> dummyData(1024 + 64, 0);
    (void)gl1.glGetError();

    // width = 0x40000010, height = 1 would overflow 32-bit width * height * 4 in uncompressTexture.
    gl1.glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0x40000010, 1, GL_PALETTE8_RGBA8_OES,
                                  static_cast<GLsizei>(dummyData.size()), dummyData.data());
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl1.glGetError());

    // imageSize smaller than the 1024-byte palette for GL_PALETTE8_RGBA8_OES must also be rejected.
    gl1.glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_PALETTE8_RGBA8_OES, 16,
                                  dummyData.data());
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl1.glGetError());

    // level > 0 in glCompressedTexSubImage2D must allocate for the full width x height rather
    // than shifting width/height down by level and causing glTexSubImage2D to overread.
    gl1.glCompressedTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, 8, 8, GL_PALETTE8_RGBA8_OES,
                                  static_cast<GLsizei>(dummyData.size()), dummyData.data());
    (void)gl1.glGetError();

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles1GetLightAndMaterialInvalidEnumDoesNotLeakStack) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx1 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_CM);
    ASSERT_NE((HandleType)0, ctx1);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx1, surface, surface));

    auto& gl1 = mRenderThreadInfo->m_glInfo->m_glDec;
    (void)gl1.glGetError();

    // Invalid light index must fail with GL_INVALID_ENUM and leave params untouched.
    GLfixed lightParams[4] = {-12345, -12345, -12345, -12345};
    gl1.glGetLightxv(GL_LIGHT0 + 8, GL_AMBIENT, lightParams);
    EXPECT_EQ((GLenum)GL_INVALID_ENUM, gl1.glGetError());
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(-12345, lightParams[i]);
    }

    // 1-component light query (GL_SPOT_EXPONENT) must only write params[0] and not clobber params[1].
    gl1.glLightx(GL_LIGHT0, GL_SPOT_EXPONENT, 5 << 16);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl1.glGetError());
    GLfixed spotExpAndSentinel[2] = {-1, 0x77777777};
    gl1.glGetLightxv(GL_LIGHT0, GL_SPOT_EXPONENT, spotExpAndSentinel);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl1.glGetError());
    EXPECT_EQ(5 << 16, spotExpAndSentinel[0]);
    EXPECT_EQ(0x77777777, spotExpAndSentinel[1]);

    // Invalid material face must fail with GL_INVALID_ENUM and leave params untouched.
    GLfixed matParams[4] = {-54321, -54321, -54321, -54321};
    gl1.glGetMaterialxv(0, GL_AMBIENT, matParams);
    EXPECT_EQ((GLenum)GL_INVALID_ENUM, gl1.glGetError());
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(-54321, matParams[i]);
    }

    // Valid glGetMaterialxv must convert floats to fixed-point via F2X.
    gl1.glGetMaterialxv(GL_FRONT, GL_DIFFUSE, matParams);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl1.glGetError());
    EXPECT_EQ(0x10000, matParams[3]);

    // GL_AMBIENT_AND_DIFFUSE is not a valid getter parameter for glGetMaterial(f/x)v per
    // Section 6.1.3 of the GLES 1.1 spec, and must fail with GL_INVALID_ENUM leaving params untouched.
    GLfixed ambientAndDiffuseParams[4] = {-999, -999, -999, -999};
    gl1.glGetMaterialxv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, ambientAndDiffuseParams);
    EXPECT_EQ((GLenum)GL_INVALID_ENUM, gl1.glGetError());
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(-999, ambientAndDiffuseParams[i]);
    }

    // GL_EMISSION is not a valid glLightxv parameter and must fail with GL_INVALID_ENUM without
    // reading past a 1-element buffer.
    GLfixed oneFixed = 0x10000;
    gl1.glLightxv(GL_LIGHT0, GL_EMISSION, &oneFixed);
    EXPECT_EQ((GLenum)GL_INVALID_ENUM, gl1.glGetError());

    // glTexEnviv + glGetTexEnviv / glGetTexEnvfv on GL_TEXTURE_ENV_COLOR must copy all 4 elements
    // and convert properly across int/float types.
    GLint envColorIn[4] = {10, 20, 30, 40};
    gl1.glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envColorIn);
    GLint envColorOutI[4] = {};
    gl1.glGetTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, envColorOutI);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(envColorIn[i], envColorOutI[i]);
    }
    GLfixed envModeX[2] = {-1, 0x55555555};
    gl1.glGetTexEnvxv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, envModeX);
    EXPECT_EQ((GLfixed)GL_MODULATE, envModeX[0]);
    EXPECT_EQ(0x55555555, envModeX[1]);

    // glGetTexGenxvOES / glGetTexGenfvOES on GL_TEXTURE_GEN_MODE_OES must return the unscaled
    // enum value and only write params[0].
    gl1.glTexGeniOES(GL_TEXTURE_GEN_STR_OES, GL_TEXTURE_GEN_MODE_OES, GL_REFLECTION_MAP_OES);
    GLfixed texGenModeX[2] = {-1, 0x66666666};
    gl1.glGetTexGenxvOES(GL_TEXTURE_GEN_STR_OES, GL_TEXTURE_GEN_MODE_OES, texGenModeX);
    EXPECT_EQ((GLfixed)GL_REFLECTION_MAP_OES, texGenModeX[0]);
    EXPECT_EQ(0x66666666, texGenModeX[1]);

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx1);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

}  // namespace
}  // namespace host
}  // namespace gfxstream
