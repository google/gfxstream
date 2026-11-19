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
#include "gfxstream/host/hwc2.h"
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
#include "common/gles_pointer.h"
#include "host/gl/gles2_dec/gles2_opcodes.h"
#undef OP_last
#include "host/gl/emulated_egl_fence_sync.h"
#include "host/gl/emulation_gl.h"
#include "host/gl/yuv_converter.h"
#include "host/vulkan/trivial_stream.h"
#include "host/renderControl_dec/renderControl_opcodes.h"
#undef OP_last
#include "render_channel_impl.h"
#include "render_thread_info.h"
#include "renderer_impl.h"
#include "gfxstream/host/address_space_device.h"
#include "render-utils/address_space_operations.h"

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
        if (!gfxstream::testing::IsGraphicsTestEnvironmentProvidingGlesDriver()) {
            GTEST_SKIP() << "Skipping GLES test in gles_none test environment.";
        }
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
        if (!mFb) {
            return;
        }
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
        if (!gfxstream::testing::IsGraphicsTestEnvironmentProvidingGlesDriver()) {
            GTEST_SKIP() << "Skipping GLES test in gles_none test environment.";
        }
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
        if (IsSkipped()) {
            return;
        }
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

TEST_F(RenderThreadDeadlockTest, ReloadProcessResourcesAfterRcSetPuid) {
    auto fb = FrameBuffer::getFB();
    ASSERT_NE(nullptr, fb);

    const uint32_t contextId = 12347;
    const uint64_t collidingPuid = 1;
    fb->createGraphicsProcessResources(contextId);
    fb->createGraphicsProcessResources(collidingPuid);

    auto channel = std::make_unique<RenderChannelImpl>(nullptr, contextId);
    ASSERT_NE(nullptr, channel->renderThread());

    // First packet on a RenderChannel starts with a 4-byte flags header.
    RenderChannel::Buffer initAndSetPuid;
    const uint32_t flags = 0;
    const uint32_t setPuidOpcode = OP_rcSetPuid;
    const uint32_t setPuidPacketLen = 16;
    initAndSetPuid.resize(sizeof(flags) + setPuidPacketLen);
    std::memcpy(initAndSetPuid.data(), &flags, sizeof(flags));
    std::memcpy(initAndSetPuid.data() + 4, &setPuidOpcode, sizeof(setPuidOpcode));
    std::memcpy(initAndSetPuid.data() + 8, &setPuidPacketLen, sizeof(setPuidPacketLen));
    std::memcpy(initAndSetPuid.data() + 12, &collidingPuid, sizeof(collidingPuid));
    ASSERT_EQ(RenderChannel::IoResult::Ok, channel->tryWrite(std::move(initAndSetPuid)));

    auto sendRcGetRendererVersionAndWait = [&]() {
        RenderChannel::Buffer req;
        const uint32_t opcode = OP_rcGetRendererVersion;
        const uint32_t packetLen = 8;
        req.resize(packetLen);
        std::memcpy(req.data(), &opcode, sizeof(opcode));
        std::memcpy(req.data() + 4, &packetLen, sizeof(packetLen));
        ASSERT_EQ(RenderChannel::IoResult::Ok, channel->tryWrite(std::move(req)));

        size_t bytesRead = 0;
        const auto deadlineUs = gfxstream::base::getUnixTimeUs() + 3'000'000;
        while (bytesRead < sizeof(GLint)) {
            RenderChannel::Buffer resp;
            ASSERT_EQ(RenderChannel::IoResult::Ok, channel->readBefore(&resp, deadlineUs));
            bytesRead += resp.size();
        }
    };

    sendRcGetRendererVersionAndWait();

    channel->renderThread()->pausePreSnapshot();
    MemStream stream;
    channel->onSave(&stream);
    channel->renderThread()->resume();

    // Trigger mNeedReloadProcessResources in the render loop.
    sendRcGetRendererVersionAndWait();

    // Destroying the colliding pipe puid must not block on this channel's RenderThread,
    // nor cause this channel's RenderThread to exit.
    fb->signalGuestProcessDead(collidingPuid);
    EXPECT_TRUE(fb->waitForAllPendingCleanups(std::chrono::milliseconds(500)));
    EXPECT_FALSE(channel->renderThread()->isFinished());

    fb->signalGuestProcessDead(contextId);
    channel->stop();
    channel.reset();
    EXPECT_TRUE(fb->waitForAllPendingCleanups(std::chrono::milliseconds(500)));
}

TEST_F(RenderThreadDeadlockTest, AsgRenderThreadCancelPausePreSnapshotAndAbortRecovery) {
    auto fb = FrameBuffer::getFB();
    ASSERT_NE(nullptr, fb);

    const uint32_t contextId = 12348;
    fb->createGraphicsProcessResources(contextId);

    struct SharedState {
        std::mutex mu;
        std::condition_variable cv;
        int unavailableCalls = 0;
        int pauseStatusReturned = 0;
        RenderThread* renderThread = nullptr;
    };
    auto state = std::make_shared<SharedState>();

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [state](const AsgConsumerCreateInfo& info, Stream* loadStream) -> ConsumerHandle {
            AsgConsumerCreateInfo wrappedInfo = info;
            auto origOnUnavailable = info.callbacks.onUnavailableRead;
            wrappedInfo.callbacks.onUnavailableRead = [state, origOnUnavailable]() {
                {
                    std::lock_guard<std::mutex> lock(state->mu);
                    ++state->unavailableCalls;
                    state->cv.notify_all();
                }
                const auto status = origOnUnavailable();
                if (status == AsgOnUnavailableReadStatus::kPauseForSnapshot) {
                    std::lock_guard<std::mutex> lock(state->mu);
                    ++state->pauseStatusReturned;
                    state->cv.notify_all();
                }
                return status;
            };
            auto* thread = new RenderThread(wrappedInfo, loadStream);
            {
                std::lock_guard<std::mutex> lock(state->mu);
                state->renderThread = thread;
            }
            thread->start();
            return static_cast<ConsumerHandle>(thread);
        },
        .destroy = [fb](ConsumerHandle consumer) {
            auto* thread = static_cast<RenderThread*>(consumer);
            fb->signalGuestProcessDead(contextId);
            thread->waitForFinished();
            {
                gfxstream::base::AutoLock driverLock(*graphicsDriverLock());
                thread->sendExitSignal();
                thread->wait();
            }
            delete thread;
        },
        .preSave = [](ConsumerHandle consumer) {
            static_cast<RenderThread*>(consumer)->pausePreSnapshot();
        },
        .globalPreSave = []() {},
        .save = [](ConsumerHandle consumer, Stream* stream) {
            static_cast<RenderThread*>(consumer)->save(stream);
        },
        .globalPostSave = []() {},
        .postSave = [](ConsumerHandle consumer) {
            static_cast<RenderThread*>(consumer)->resume();
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle consumer) {
            static_cast<RenderThread*>(consumer)->addressSpaceGraphicsReloadRingConfig();
        },
        .waitForPause = [](ConsumerHandle consumer) -> bool {
            static_cast<RenderThread*>(consumer)->waitForPausePreSnapshot();
            return true;
        },
        .cancelPreSave = [](ConsumerHandle consumer) {
            static_cast<RenderThread*>(consumer)->cancelPausePreSnapshot();
        },
    });

    auto asgMemory = std::make_shared<std::vector<char>>(
        kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    AddressSpaceCreateInfo createInfo = {};
    createInfo.handle = 1;
    createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    createInfo.createRenderThread = true;
    createInfo.externalAddr = asgMemory->data();
    createInfo.externalAddrSize = asgMemory->size();
    createInfo.virtioGpuContextId = contextId;
    auto asg = std::make_unique<AddressSpaceGraphicsContext>(createInfo);

    // Wait for RenderThread::main() to enter its startup flags read loop (onUnavailableRead).
    {
        std::unique_lock<std::mutex> lock(state->mu);
        ASSERT_TRUE(state->cv.wait_for(lock, std::chrono::seconds(5),
                                       [&] { return state->unavailableCalls >= 1; }));
        ASSERT_NE(state->renderThread, nullptr);
    }

    // 1. cancelPausePreSnapshot() on SnapshotState::Empty is a safe no-op.
    state->renderThread->cancelPausePreSnapshot();
    EXPECT_FALSE(state->renderThread->isFinished());

    // 2. Startup flags loop: exercise preSave -> waitForPreSave (SnapshotState::Finished) ->
    //    cancelPreSave, plus rapid preSave -> cancelPreSave (StartSaving / abort recovery).
    ASSERT_TRUE(asg->preSave());
    ASSERT_TRUE(asg->waitForPreSave());
    asg->cancelPreSave();
    EXPECT_FALSE(state->renderThread->isFinished());

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(asg->preSave());
        if (i % 2 == 0) {
            std::this_thread::yield();
        }
        asg->cancelPreSave();
    }
    EXPECT_FALSE(state->renderThread->isFinished());

    // 3. Send startup 4-byte flags + OP_rcGetRendererVersion over ASG ring so RenderThread enters
    //    its main packet loop (attach view-only without mutating ringStorage while RenderThread runs).
    char* ringStorage = asgMemory->data();
    char* writeBuffer = ringStorage + kAsgConsumerRingStorageSize;
    struct asg_context guestCtx = {};
    guestCtx.to_host = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, to_host));
    guestCtx.to_host_large_xfer.ring = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, to_host_large_xfer));
    guestCtx.from_host_large_xfer.ring = reinterpret_cast<struct ring_buffer*>(
        ringStorage + offsetof(struct asg_ring_storage, from_host_large_xfer));
    guestCtx.buffer = writeBuffer;
    guestCtx.buffer_size = kAsgWriteBufferSize;
    guestCtx.host_state = reinterpret_cast<asg_host_state*>(&guestCtx.to_host->state);
    guestCtx.ring_config = reinterpret_cast<asg_ring_config*>(guestCtx.to_host->config);
    ring_buffer_init_view_only(&guestCtx.to_host_large_xfer.view,
                               reinterpret_cast<uint8_t*>(writeBuffer), kAsgWriteBufferSize);
    ring_buffer_init_view_only(&guestCtx.from_host_large_xfer.view,
                               reinterpret_cast<uint8_t*>(writeBuffer), kAsgWriteBufferSize);

    uint32_t writeOffset = 0;
    auto sendRingBytes = [&](const void* data, uint32_t len) {
        std::memcpy(writeBuffer + writeOffset, data, len);
        asg_type1_xfer xfer = {.offset = writeOffset, .size = len};
        writeOffset += len;
        ASSERT_EQ(1, ring_buffer_write(guestCtx.to_host, &xfer, sizeof(xfer), 1));
        AddressSpaceDevicePingInfo notifyPing = {.metadata = ASG_NOTIFY_AVAILABLE};
        asg->perform(&notifyPing);
    };
    auto waitRingReply = [&](size_t expectedBytes) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (ring_buffer_available_read(guestCtx.from_host_large_xfer.ring,
                                          &guestCtx.from_host_large_xfer.view) < expectedBytes) {
            ASSERT_LT(std::chrono::steady_clock::now(), deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::vector<uint8_t> reply(expectedBytes, 0);
        ASSERT_EQ(1, ring_buffer_view_read(guestCtx.from_host_large_xfer.ring,
                                           &guestCtx.from_host_large_xfer.view,
                                           reply.data(), reply.size(), 1));
    };

    struct {
        uint32_t flags = 0;
        uint32_t opcode = OP_rcGetRendererVersion;
        uint32_t packetLen = 8;
    } firstMsg;
    static_assert(sizeof(firstMsg) == 12);
    sendRingBytes(&firstMsg, sizeof(firstMsg));
    waitRingReply(sizeof(GLint));

    // Wait for RenderThread::main() to block in onUnavailableRead() inside the main packet loop.
    int callsAfterFirstReply = 0;
    {
        std::lock_guard<std::mutex> lock(state->mu);
        callsAfterFirstReply = state->unavailableCalls;
    }
    {
        AddressSpaceDevicePingInfo wakePing = {.metadata = ASG_NOTIFY_AVAILABLE};
        asg->perform(&wakePing);
        std::unique_lock<std::mutex> lock(state->mu);
        ASSERT_TRUE(state->cv.wait_for(
            lock, std::chrono::seconds(5),
            [&] { return state->unavailableCalls > callsAfterFirstReply; }));
    }

    // 4. Main packet loop: exercise back-to-back preSave -> waitForPreSave -> cancelPreSave ->
    //    preSave -> waitForPreSave -> postSave (ensuring doSnapshotOp() exits before a subsequent
    //    pausePreSnapshot() starts) plus rapid preSave -> cancelPreSave abort recovery.
    for (int i = 0; i < 6; ++i) {
        ASSERT_TRUE(asg->preSave());
        ASSERT_TRUE(asg->waitForPreSave());
        asg->cancelPreSave();
        ASSERT_TRUE(asg->preSave());
        ASSERT_TRUE(asg->waitForPreSave());
        ASSERT_TRUE(asg->postSave());
    }
    EXPECT_FALSE(state->renderThread->isFinished());

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(asg->preSave());
        if (i % 2 == 0) {
            std::this_thread::yield();
        }
        asg->cancelPreSave();
    }
    EXPECT_FALSE(state->renderThread->isFinished());

    // 5. Verify RenderThread remains alive and decodes a second OP_rcGetRendererVersion packet
    //    (exercising mNeedReloadProcessResources reload).
    struct {
        uint32_t opcode = OP_rcGetRendererVersion;
        uint32_t packetLen = 8;
    } secondMsg;
    sendRingBytes(&secondMsg, sizeof(secondMsg));
    waitRingReply(sizeof(GLint));
    EXPECT_FALSE(state->renderThread->isFinished());

    asg.reset();
    AddressSpaceGraphicsContext::clear();
    AddressSpaceGraphicsContext::setConsumer({});
    EXPECT_TRUE(fb->waitForAllPendingCleanups(std::chrono::milliseconds(500)));
}

TEST_F(RenderThreadDeadlockTest, RendererImplAsgFailedLoadCleansUpParkedPostLoadThreads) {
    auto fb = FrameBuffer::getFB();
    ASSERT_NE(nullptr, fb);

    const uint32_t ctxId1 = 12351;
    const uint32_t ctxId2 = 12352;
    fb->createGraphicsProcessResources(ctxId1);
    fb->createGraphicsProcessResources(ctxId2);

    auto renderer = std::make_shared<RendererImpl>();
    std::vector<char> mem1(kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    std::vector<char> mem2(kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    struct CleanupGuard {
        FrameBuffer* fb = nullptr;
        uint32_t id1 = 0;
        uint32_t id2 = 0;
        ~CleanupGuard() {
            GetAsgOperations().clear();
            AddressSpaceGraphicsContext::clear();
            AddressSpaceGraphicsContext::setConsumer({});
            gfxstream_address_space_set_load_resources({});
            if (fb) {
                fb->signalGuestProcessDead(id1);
                fb->signalGuestProcessDead(id2);
                fb->waitForAllPendingCleanups(std::chrono::milliseconds(500));
            }
        }
    } guard{fb, ctxId1, ctxId2};

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [renderer](const AsgConsumerCreateInfo& info, Stream* loadStream) {
            return renderer->addressSpaceGraphicsConsumerCreate(info, loadStream);
        },
        .destroy = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerDestroy(consumer);
        },
        .preSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerPreSave(consumer);
        },
        .globalPreSave = [renderer]() { renderer->pauseAllPreSave(); },
        .save = [renderer](void* consumer, Stream* stream) {
            renderer->addressSpaceGraphicsConsumerSave(consumer, stream);
        },
        .globalPostSave = [renderer]() { renderer->resumeAll(); },
        .postSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerPostSave(consumer);
        },
        .postLoad = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(consumer);
        },
        .globalPreLoad = []() {},
        .reloadRingConfig = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerReloadRingConfig(consumer);
        },
        .waitForPause = [renderer](void* consumer) {
            return renderer->addressSpaceGraphicsConsumerWaitForPause(consumer);
        },
        .cancelPreSave = [renderer](void* consumer) {
            renderer->addressSpaceGraphicsConsumerCancelPreSave(consumer);
        },
    });

    auto ops = GetAsgOperations();

    auto makeCtx = [&](uint32_t ctxId, std::vector<char>* mem) {
        AddressSpaceCreateInfo ci = {};
        ci.handle = ops.gen_handle();
        ci.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
        ci.createRenderThread = true;
        ci.externalAddr = mem->data();
        ci.externalAddrSize = mem->size();
        ci.virtioGpuContextId = ctxId;
        ops.create_instance(ci);
        return ci.handle;
    };
    const uint32_t h1 = makeCtx(ctxId1, &mem1);
    const uint32_t h2 = makeCtx(ctxId2, &mem2);

    MemStream saved;
    ASSERT_EQ(gfxstream_address_space_save_memory_state(&saved), 0);
    ASSERT_GT(saved.writtenSize(), 16u);

    // Truncate inside the second context's RenderThread payload so the first context loads,
    // starts a parked RenderThread, and registers in mAdditionalPostLoadRenderThreads, while the
    // second context fails with stream->hasErrors() and triggers full rollback + resumeAll().
    MemStream truncated(saved.writtenSize() - 4);
    ASSERT_EQ(truncated.write(saved.buffer().data(), saved.writtenSize() - 4),
              static_cast<ssize_t>(saved.writtenSize() - 4));
    truncated.rewind();

    AddressSpaceDeviceLoadResources loadResources;
    loadResources.contextExternalMemoryMap[h1] = {mem1.data(), mem1.size()};
    loadResources.contextExternalMemoryMap[h2] = {mem2.data(), mem2.size()};
    ASSERT_EQ(gfxstream_address_space_set_load_resources(std::move(loadResources)), 0);

    EXPECT_NE(gfxstream_address_space_load_memory_state(&truncated), 0);
    renderer->resumeAll();

    // Corrupting the inner RenderThread mStream (ReadBuffer m_validData > m_size) while keeping
    // the outer saveStream length intact must cause RenderThread::loadSnapshot() to detect
    // mStream->hasErrors() and mark the RenderThread finished instead of restoring corrupt state.
    MemStream corruptInner;
    corruptInner.putByte(1);
    MemStream badPayload;
    badPayload.putBe32(64);        // ReadBuffer m_size
    badPayload.putBe32(0x7fffffff); // Invalid ReadBuffer m_validData > m_size
    saveStream(&corruptInner, badPayload);
    corruptInner.rewind();

    std::atomic<int> unavailableReadCalls{0};
    AsgConsumerCreateInfo directInfo = {};
    directInfo.version = 1;
    directInfo.ring_storage = mem1.data();
    directInfo.buffer = mem1.data() + kAsgConsumerRingStorageSize;
    directInfo.buffer_size = kAsgWriteBufferSize;
    directInfo.buffer_flush_interval = kAsgWriteStepSize;
    directInfo.callbacks.onUnavailableRead = [&unavailableReadCalls] {
        ++unavailableReadCalls;
        return AsgOnUnavailableReadStatus::kExit;
    };
    directInfo.virtioGpuContextId = ctxId1;
    void* badConsumer = renderer->addressSpaceGraphicsConsumerCreate(directInfo, &corruptInner);
    ASSERT_NE(badConsumer, nullptr);
    renderer->addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(badConsumer);
    renderer->resumeAll();
    auto* badThread = static_cast<RenderThread*>(badConsumer);
    badThread->waitForFinished();
    EXPECT_TRUE(badThread->isFinished());
    EXPECT_EQ(unavailableReadCalls.load(), 0);
    renderer->addressSpaceGraphicsConsumerDestroy(badConsumer);
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

    // Verify fallback thread context and surface tracking when puid == 0
    HandleType threadContext = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_2);
    EXPECT_NE((HandleType)0, threadContext);
    HandleType threadSurface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    EXPECT_NE((HandleType)0, threadSurface);
    EXPECT_TRUE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
    mFb->drainGlRenderThreadResources();
    EXPECT_FALSE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
}

TEST_F(FrameBufferTest, CreateEmulatedEglFenceSyncWithoutCurrentContext) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    auto& glInfo = *mRenderThreadInfo->m_glInfo;
    ASSERT_EQ(nullptr, glInfo.currContext);
    ASSERT_FALSE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());

    uint64_t sync = 0;
    mFb->createEmulatedEglFenceSync(EGL_SYNC_FENCE_KHR, false, &sync);
    EXPECT_NE(0u, sync);

    EXPECT_NE(nullptr, glInfo.currContext);
    EXPECT_EQ(1u, glInfo.m_contextSet.size());
    EXPECT_EQ(1u, glInfo.m_windowSet.size());

    gl::EmulatedEglFenceSync* fenceSync = gl::EmulatedEglFenceSync::getFromHandle(sync);
    ASSERT_NE(nullptr, fenceSync);
    fenceSync->decRef();

    mFb->drainGlRenderThreadResources();
    EXPECT_EQ(nullptr, glInfo.currContext);
    EXPECT_TRUE(glInfo.m_contextSet.empty());
    EXPECT_TRUE(glInfo.m_windowSet.empty());
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

TEST_F(FrameBufferTest, HeadlessPostAndDrainRenderThreadSurfaces) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType postCb = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, postCb);
    EXPECT_EQ(0, mFb->openColorBuffer(postCb));
    EXPECT_TRUE(mFb->post(postCb));
    EXPECT_EQ(postCb, mFb->getLastPostedColorBuffer());
    mFb->closeColorBuffer(postCb);

    mRenderThreadInfo->setPuid(0);
    HandleType surfaceCb = mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, surfaceCb);
    EXPECT_EQ(0, mFb->openColorBuffer(surfaceCb));

    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
    EXPECT_TRUE(mFb->setEmulatedEglWindowSurfaceColorBuffer(surface, surfaceCb));
    EXPECT_EQ(surfaceCb, mFb->getEmulatedEglWindowSurfaceColorBufferHandle(surface));
    mFb->closeColorBuffer(surfaceCb);

    mFb->drainGlRenderThreadSurfaces();
    EXPECT_FALSE(mFb->getEmulationGl()->hasContextsOrWindowSurfaces());
    EXPECT_EQ((HandleType)0, mFb->getEmulatedEglWindowSurfaceColorBufferHandle(surface));
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
    // Use a large stride (0x40000000) with a client pointer so that totalVerts (3) is within
    // INT32_MAX / 16, but neededBytes ((3 - 1) * 0x40000000 + 12) exceeds INT32_MAX.
    gl1.glVertexPointerWithDataSize(3, GL_FLOAT, 0x40000000, smallVerts, sizeof(smallVerts));
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

TEST_F(FrameBufferTest, Gles3InvalidateFramebufferWithZeroAttachments) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx3 = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_0);
    ASSERT_NE((HandleType)0, ctx3);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx3, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    gl2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    (void)gl2.glGetError();

    // A count of 0 is a legal no-op. It used to index an empty std::vector, which hardened libc++
    // turns into a trap that kills the emulator (b/582922278). The decoder always passes a
    // non-null pointer, even for an empty list, so try that as well as a null one.
    const GLenum unusedAttachment = GL_COLOR;
    gl2.glInvalidateFramebuffer(GL_FRAMEBUFFER, 0, nullptr);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    gl2.glInvalidateFramebuffer(GL_FRAMEBUFFER, 0, &unusedAttachment);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    gl2.glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 0, nullptr, 0, 0, 1, 1);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    gl2.glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 0, &unusedAttachment, 0, 0, 1, 1);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());

    // A negative count is GL_INVALID_VALUE.
    gl2.glInvalidateFramebuffer(GL_FRAMEBUFFER, -1, nullptr);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());
    gl2.glInvalidateSubFramebuffer(GL_FRAMEBUFFER, -1, nullptr, 0, 0, 1, 1);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());

    // Non-empty lists still reach the driver intact: GL_COLOR/GL_DEPTH/GL_STENCIL are translated
    // for the emulated default framebuffer, and user FBO attachments are passed through as is.
    const GLenum defaultFbAttachments[] = {GL_COLOR, GL_DEPTH, GL_STENCIL};
    gl2.glInvalidateFramebuffer(GL_FRAMEBUFFER, 3, defaultFbAttachments);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    GLuint fbo = 0;
    gl2.glGenFramebuffers(1, &fbo);
    gl2.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    const GLenum fboAttachment = GL_COLOR_ATTACHMENT0;
    gl2.glInvalidateSubFramebuffer(GL_FRAMEBUFFER, 1, &fboAttachment, 0, 0, 1, 1);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    gl2.glBindFramebuffer(GL_FRAMEBUFFER, 0);
    gl2.glDeleteFramebuffers(1, &fbo);

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx3);
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

TEST_F(FrameBufferTest, Gles2CrossVaoClientPointerDataRetainedWithoutUaf) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_0);
    ASSERT_NE((HandleType)0, ctx);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    GLuint vaos[2] = {0, 0};
    gl2.glGenVertexArrays(2, vaos);
    ASSERT_NE(0u, vaos[0]);
    ASSERT_NE(0u, vaos[1]);

    GLfloat smallVerts[12] = {
        0.0f, 0.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
    };
    gl2.glBindVertexArray(vaos[0]);
    gl2.glEnableVertexAttribArray(0);
    gl2.glVertexAttribPointerData(&gl2, 0, 4, GL_FLOAT, GL_FALSE, 0, smallVerts,
                                  sizeof(smallVerts));

    // Switch to vao[1] and upload a larger buffer to attribute 0, forcing m_contextData
    // to reallocate its per-attribute staging buffer for index 0.
    std::vector<GLfloat> largeVerts(4096, 0.5f);
    gl2.glBindVertexArray(vaos[1]);
    gl2.glEnableVertexAttribArray(0);
    gl2.glVertexAttribPointerData(&gl2, 0, 4, GL_FLOAT, GL_FALSE, 0, largeVerts.data(),
                                  static_cast<GLuint>(largeVerts.size() * sizeof(GLfloat)));

    // Rebinding vao[0] and drawing must use vao[0]'s owned copy rather than a dangling
    // pointer into m_contextData's old buffer.
    gl2.glBindVertexArray(vaos[0]);
    (void)gl2.glGetError();
    gl2.glDrawArrays(GL_TRIANGLES, 0, 3);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());

    gl2.glBindVertexArray(0);
    gl2.glDeleteVertexArrays(2, vaos);
    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, GlesPointerOnLoadClampsValuesAndPreservesStreamAlignment) {
    MemStream stream;
    stream.putBe32(4);                      // m_size
    stream.putBe32(GL_FLOAT);               // m_type
    stream.putBe32(0);                      // m_stride
    stream.putByte(0);                      // m_enabled
    stream.putByte(0);                      // m_normalize
    stream.putByte(GLESpointer::VALUE);     // m_attribType
    stream.putBe32(0);                      // m_bufferName
    stream.putBe32(0);                      // m_buffOffset
    stream.putByte(0);                      // m_isInt
    stream.putBe32(0);                      // m_divisor
    stream.putBe32(0);                      // m_bindingIndex
    stream.putBe32(0);                      // m_reloffset

    constexpr uint32_t kExcessValueCount = 10;
    stream.putBe32(kExcessValueCount);      // m_valueCount (> 4)
    for (uint32_t i = 0; i < kExcessValueCount; ++i) {
        GLfloat val = static_cast<GLfloat>(i + 1);
        stream.write(&val, sizeof(GLfloat));
    }
    constexpr uint32_t kSentinel = 0x12345678u;
    stream.putBe32(kSentinel);

    GLESpointer ptr(&stream);
    EXPECT_EQ(4u, ptr.getValueCount());
    const GLfloat* values = ptr.getValues();
    ASSERT_NE(nullptr, values);
    EXPECT_FLOAT_EQ(1.0f, values[0]);
    EXPECT_FLOAT_EQ(2.0f, values[1]);
    EXPECT_FLOAT_EQ(3.0f, values[2]);
    EXPECT_FLOAT_EQ(4.0f, values[3]);
    EXPECT_EQ(kSentinel, stream.getBe32());

    // Negative serialized m_dataSize (0xFFFFFFFF / -1) on an ARRAY pointer must not throw
    // std::bad_alloc and must reset dataSize / dataCapacity to 0 and arrayData to nullptr.
    MemStream negArrayStream;
    negArrayStream.putBe32(4);                      // m_size
    negArrayStream.putBe32(GL_FLOAT);               // m_type
    negArrayStream.putBe32(0);                      // m_stride
    negArrayStream.putByte(0);                      // m_enabled
    negArrayStream.putByte(0);                      // m_normalize
    negArrayStream.putByte(GLESpointer::ARRAY);     // m_attribType
    negArrayStream.putBe32(0);                      // m_bufferName
    negArrayStream.putBe32(0xFFFFFFFFu);            // m_dataSize (-1 when signed)
    negArrayStream.putBe32(0);                      // m_buffOffset
    negArrayStream.putByte(0);                      // m_isInt
    negArrayStream.putBe32(0);                      // m_divisor
    negArrayStream.putBe32(0);                      // m_bindingIndex
    negArrayStream.putBe32(0);                      // m_reloffset
    negArrayStream.putBe32(0);                      // m_valueCount

    GLESpointer negArrayPtr(&negArrayStream);
    EXPECT_EQ(GLESpointer::ARRAY, negArrayPtr.getAttribType());
    EXPECT_EQ(0, negArrayPtr.getDataSize());
    EXPECT_EQ(0u, negArrayPtr.getDataCapacity());
    EXPECT_EQ(nullptr, negArrayPtr.getArrayData());
    EXPECT_EQ(nullptr, negArrayPtr.getData());
}

TEST_F(FrameBufferTest, GlesPointerSetArrayAndCopyMoveSemantics) {
    GLfloat verts[4] = {1.0f, 2.0f, 3.0f, 4.0f};

    // 1. setArray with data != nullptr && dataSize == 0 retains m_data without copying.
    GLESpointer unownedPtr;
    unownedPtr.setArray(4, GL_FLOAT, 0, verts, 0);
    EXPECT_EQ(verts, unownedPtr.getArrayData());
    EXPECT_EQ(verts, unownedPtr.getData());
    EXPECT_EQ(0, unownedPtr.getDataSize());

    // 2. setArray with data == nullptr && dataSize > 0 resets m_dataSize to 0 and m_data to nullptr.
    GLESpointer nullPtr;
    nullPtr.setArray(4, GL_FLOAT, 0, nullptr, 64);
    EXPECT_EQ(nullptr, nullPtr.getArrayData());
    EXPECT_EQ(nullptr, nullPtr.getData());
    EXPECT_EQ(0, nullPtr.getDataSize());
    EXPECT_EQ(0u, nullPtr.getDataCapacity());

    // Null-array onSave/onLoad round-trip must write/read size 0 cleanly.
    MemStream nullStream;
    nullPtr.onSave(&nullStream);
    GLESpointer loadedNullPtr(&nullStream);
    EXPECT_EQ(nullptr, loadedNullPtr.getData());
    EXPECT_EQ(0, loadedNullPtr.getDataSize());

    // 3. Copy constructor and copy assignment re-bind m_data to the new instance's m_ownData.
    GLESpointer copyAssigned;
    {
        GLESpointer original;
        original.setArray(4, GL_FLOAT, 0, verts, sizeof(verts));
        ASSERT_NE(nullptr, original.getData());
        EXPECT_NE(verts, original.getData());

        GLESpointer copyConstructed(original);
        EXPECT_NE(original.getData(), copyConstructed.getData());
        EXPECT_EQ(0, memcmp(copyConstructed.getData(), verts, sizeof(verts)));

        copyAssigned = original;
        EXPECT_NE(original.getData(), copyAssigned.getData());
        EXPECT_EQ(0, memcmp(copyAssigned.getData(), verts, sizeof(verts)));

        // Overwrite original; copies must retain their own buffer and data.
        GLfloat otherVerts[4] = {9.0f, 8.0f, 7.0f, 6.0f};
        original.setArray(4, GL_FLOAT, 0, otherVerts, sizeof(otherVerts));
        EXPECT_EQ(0, memcmp(copyConstructed.getData(), verts, sizeof(verts)));
    }
    EXPECT_EQ(0, memcmp(copyAssigned.getData(), verts, sizeof(verts)));

    // 4. Move constructor and move assignment transfer ownership and point m_data to the new buffer.
    GLESpointer moveConstructed(std::move(copyAssigned));
    EXPECT_EQ(nullptr, copyAssigned.getData());
    EXPECT_EQ(0, copyAssigned.getDataSize());
    ASSERT_NE(nullptr, moveConstructed.getData());
    EXPECT_EQ(static_cast<GLsizei>(sizeof(verts)), moveConstructed.getDataSize());
    EXPECT_EQ(0, memcmp(moveConstructed.getData(), verts, sizeof(verts)));

    GLESpointer moveAssigned;
    moveAssigned = std::move(moveConstructed);
    EXPECT_EQ(nullptr, moveConstructed.getData());
    EXPECT_EQ(0, moveConstructed.getDataSize());
    ASSERT_NE(nullptr, moveAssigned.getData());
    EXPECT_EQ(static_cast<GLsizei>(sizeof(verts)), moveAssigned.getDataSize());
    EXPECT_EQ(0, memcmp(moveAssigned.getData(), verts, sizeof(verts)));

    // 5. Reconfiguring an ARRAY pointer via setValue clears owned array data and preserves VALUE state across copy/move.
    GLfloat constVals[4] = {10.0f, 20.0f, 30.0f, 40.0f};
    moveAssigned.setValue(4, constVals);
    EXPECT_EQ(GLESpointer::VALUE, moveAssigned.getAttribType());
    EXPECT_EQ(nullptr, moveAssigned.getArrayData());
    EXPECT_EQ(0, moveAssigned.getDataSize());
    EXPECT_EQ(moveAssigned.getValues(), moveAssigned.getData());

    GLESpointer valueCopied(moveAssigned);
    EXPECT_EQ(GLESpointer::VALUE, valueCopied.getAttribType());
    EXPECT_EQ(nullptr, valueCopied.getArrayData());
    EXPECT_EQ(0, valueCopied.getDataSize());
    EXPECT_EQ(valueCopied.getValues(), valueCopied.getData());

    GLESpointer valueMoved(std::move(valueCopied));
    EXPECT_EQ(GLESpointer::VALUE, valueMoved.getAttribType());
    EXPECT_EQ(nullptr, valueMoved.getArrayData());
    EXPECT_EQ(0, valueMoved.getDataSize());
    EXPECT_EQ(valueMoved.getValues(), valueMoved.getData());
}


TEST_F(FrameBufferTest, Gles3GetIntegeriVAndVertexAttribMaxIndexBoundsChecked) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_1);
    ASSERT_NE((HandleType)0, ctx);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    const GLenum kIndexedTargets[] = {
        GL_TRANSFORM_FEEDBACK_BUFFER_BINDING,
        GL_UNIFORM_BUFFER_BINDING,
        GL_ATOMIC_COUNTER_BUFFER_BINDING,
        GL_SHADER_STORAGE_BUFFER_BINDING,
    };
    for (GLenum target : kIndexedTargets) {
        GLint val32 = -1;
        gl2.glGetIntegeri_v(target, 0x7fffffffu, &val32);
        EXPECT_EQ(0, val32);

        GLint64 val64 = -1;
        gl2.glGetInteger64i_v(target, 0x7fffffffu, &val64);
        EXPECT_EQ(0, val64);
    }

    // Attribute index 16 (== kMaxVertexAttributes) must not index past the 16-element
    // VAO attribute vector.
    constexpr GLuint kOutOfBoundsAttrib = 16;
    GLfloat verts[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    (void)gl2.glGetError();
    gl2.glEnableVertexAttribArray(kOutOfBoundsAttrib);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());
    gl2.glDisableVertexAttribArray(kOutOfBoundsAttrib);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());
    gl2.glVertexAttribPointerData(&gl2, kOutOfBoundsAttrib, 4, GL_FLOAT, GL_FALSE, 0, verts,
                                  sizeof(verts));
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());
    gl2.glVertexAttribBinding(kOutOfBoundsAttrib, 0);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());
    gl2.glVertexAttribFormat(kOutOfBoundsAttrib, 4, GL_FLOAT, GL_FALSE, 0);
    EXPECT_EQ((GLenum)GL_INVALID_VALUE, gl2.glGetError());

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles2DetachShaderWithProgramHandleDoesNotConfuseTypes) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_1);
    ASSERT_NE((HandleType)0, ctx);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    GLuint prog1 = gl2.glCreateProgram_dec(&gl2);
    GLuint prog2 = gl2.glCreateProgram_dec(&gl2);
    ASSERT_NE(0u, prog1);
    ASSERT_NE(0u, prog2);
    (void)gl2.glGetError();

    // 1. Attempting to attach a program handle as a shader must fail with GL_INVALID_OPERATION.
    gl2.glAttachShader_dec(&gl2, prog1, prog2);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    // 2. Attempting to detach a program handle as a shader must fail with GL_INVALID_OPERATION.
    gl2.glDetachShader_dec(&gl2, prog1, prog2);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    // 3. Attaching and detaching a valid shader handle exercises s_detachShader cleanly.
    GLuint vs = gl2.glCreateShader_dec(&gl2, GL_VERTEX_SHADER);
    ASSERT_NE(0u, vs);
    gl2.glAttachShader_dec(&gl2, prog1, vs);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());
    gl2.glDetachShader_dec(&gl2, prog1, vs);
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.glGetError());

    // 4. Passing a shader handle instead of a program handle to program APIs must be
    // rejected with GL_INVALID_OPERATION without type confusion.
    gl2.glUseProgram_dec(&gl2, vs);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    (void)gl2.glGetUniformLocation_dec(&gl2, vs, "u_test");
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    gl2.glProgramUniform1i_dec(&gl2, vs, 0, 1);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.glGetError());

    gl2.glDeleteShader_dec(&gl2, vs);
    gl2.glDeleteProgram_dec(&gl2, prog1);
    gl2.glDeleteProgram_dec(&gl2, prog2);
    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles2RawArrayOfPointersOpcodesRejectedWithInvalidOperation) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_0);
    ASSERT_NE((HandleType)0, ctx);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    vk::TrivialStream ioStream;
    ChecksumCalculator checksumCalc;
    (void)gl2.getGLerror();

    struct RawPointerArrayCase {
        uint32_t opcode;
        uint32_t lastWord;
    };
    const RawPointerArrayCase kCases[] = {
        {OP_glShaderSource, 0},                                   // size_length = 0
        {OP_glGetUniformIndices, 0},                              // size_uniformIndices = 0
        {OP_glTransformFeedbackVaryings, GL_INTERLEAVED_ATTRIBS}, // bufferMode
    };
    for (const auto& testCase : kCases) {
        uint32_t packet[8] = {
            testCase.opcode,
            32,                  // packetLen
            1,                   // shader / program
            1,                   // count / uniformCount
            8,                   // byte size of pointer array
            0xdeadbeefu,         // bogus pointer low 32 bits
            0xdeadbeefu,         // bogus pointer high 32 bits
            testCase.lastWord,
        };
        EXPECT_EQ(sizeof(packet), gl2.decode(packet, sizeof(packet), &ioStream, &checksumCalc));
        EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.getGLerror());
        EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());
    }

    // OP_glCreateShaderProgramv with bogus guest pointer address 0xdeadbeefdeadbeef.
    // Also verify OP_glGetError over the wire drains the pending GL_INVALID_OPERATION.
    {
        uint32_t packet[7] = {
            OP_glCreateShaderProgramv,
            28,                // packetLen
            GL_VERTEX_SHADER,  // type
            1,                 // count
            8,                 // size_strings
            0xdeadbeefu,       // bogus pointer low 32 bits
            0xdeadbeefu,       // bogus pointer high 32 bits
        };
        EXPECT_EQ(sizeof(packet), gl2.decode(packet, sizeof(packet), &ioStream, &checksumCalc));
        EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.getGLerror());
        EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());

        EXPECT_EQ(sizeof(packet), gl2.decode(packet, sizeof(packet), &ioStream, &checksumCalc));
        uint32_t getErrPacket[2] = {OP_glGetError, 8};
        EXPECT_EQ(sizeof(getErrPacket),
                  gl2.decode(getErrPacket, sizeof(getErrPacket), &ioStream, &checksumCalc));
        EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());
    }

    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, Gles2MapAndUnmapBufferDirectRejectedWithInvalidOperation) {
    if (!mFb->hasEmulationGl()) {
        GTEST_SKIP() << "GL emulation not enabled.";
    }

    HandleType ctx = mFb->createEmulatedEglContext(0, 0, gl::GLESApi_3_0);
    ASSERT_NE((HandleType)0, ctx);
    HandleType surface = mFb->createEmulatedEglWindowSurface(0, mWidth, mHeight);
    ASSERT_NE((HandleType)0, surface);
    EXPECT_TRUE(mFb->bindContext(ctx, surface, surface));

    auto& gl2 = mRenderThreadInfo->m_glInfo->m_gl2Dec;
    GLuint vbo = 0;
    gl2.glGenBuffers_dec(&gl2, 1, &vbo);
    ASSERT_NE(0u, vbo);
    gl2.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    uint8_t initData[64] = {};
    gl2.glBufferData(GL_ARRAY_BUFFER, sizeof(initData), initData, GL_DYNAMIC_DRAW);
    (void)gl2.getGLerror();

    uint64_t mappedAddr = gl2.glMapBufferRangeDirect(&gl2, GL_ARRAY_BUFFER, 0, sizeof(initData),
                                                     GL_MAP_WRITE_BIT, 0x100000);
    EXPECT_EQ(0u, mappedAddr);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.getGLerror());
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());

    GLboolean unmapRes = GL_TRUE;
    gl2.glUnmapBufferDirect(&gl2, GL_ARRAY_BUFFER, 0, sizeof(initData), GL_MAP_WRITE_BIT,
                            0x100000, 0xdeadbeef0000ULL, &unmapRes);
    EXPECT_EQ((GLboolean)GL_FALSE, unmapRes);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.getGLerror());
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());

    gl2.glFlushMappedBufferRangeDirect(&gl2, GL_ARRAY_BUFFER, 0, sizeof(initData),
                                       GL_MAP_WRITE_BIT);
    EXPECT_EQ((GLenum)GL_INVALID_OPERATION, gl2.getGLerror());
    EXPECT_EQ((GLenum)GL_NO_ERROR, gl2.getGLerror());

    gl2.glBindBuffer(GL_ARRAY_BUFFER, 0);
    gl2.glDeleteBuffers_dec(&gl2, 1, &vbo);
    EXPECT_TRUE(mFb->bindContext(0, 0, 0));
    mFb->destroyEmulatedEglContext(ctx);
    mFb->destroyEmulatedEglWindowSurface(surface);
}

TEST_F(FrameBufferTest, ComposePostsTargetColorBufferWhenNeedPostIsTrue) {
    HandleType targetHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, targetHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(targetHandle));

    struct PostContext {
        int count = 0;
        int width = 0;
        int height = 0;
    } postCtx;

    mFb->setPostCallback(
        [](void* context, uint32_t /*displayId*/, int width, int height, int /*ydir*/,
           int /*format*/, int /*type*/, unsigned char* /*pixels*/) {
            auto* ctx = static_cast<PostContext*>(context);
            ++ctx->count;
            ctx->width = width;
            ctx->height = height;
        },
        &postCtx, /*displayId=*/0, /*useBgraReadback=*/false);

    std::vector<uint8_t> composeBuf(sizeof(ComposeDevice_v2) + sizeof(ComposeLayer), 0);
    auto* composeDev = reinterpret_cast<ComposeDevice_v2*>(composeBuf.data());
    composeDev->version = 2;
    composeDev->displayId = 0;
    composeDev->targetHandle = targetHandle;
    composeDev->numLayers = 1;
    composeDev->layer[0].cbHandle = 0;
    composeDev->layer[0].composeMode = HWC2_COMPOSITION_SOLID_COLOR;
    composeDev->layer[0].displayFrame = {0, 0, mWidth, mHeight};
    composeDev->layer[0].crop = {0.0f, 0.0f, static_cast<float>(mWidth),
                                 static_cast<float>(mHeight)};
    composeDev->layer[0].blendMode = HWC2_BLEND_MODE_NONE;
    composeDev->layer[0].alpha = 1.0f;
    composeDev->layer[0].color = {255, 0, 0, 255};
    composeDev->layer[0].transform = HWC_TRANSFORM_NONE;

    mFb->resetGuestPostedAFrame();
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/false));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(0, postCtx.count);

    std::vector<uint8_t> readPixels(static_cast<size_t>(mWidth) * mHeight * 4, 0);
    mFb->readColorBuffer(targetHandle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM,
                         readPixels.data(), readPixels.size());
    EXPECT_EQ(255u, readPixels[0]);
    EXPECT_EQ(0u, readPixels[1]);
    EXPECT_EQ(0u, readPixels[2]);
    EXPECT_EQ(255u, readPixels[3]);

    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    EXPECT_TRUE(mFb->hasGuestPostedAFrame());
    if (!mUseSubWindow) {
        if (mFb->asyncReadbackSupported()) {
            mFb->flushReadPipeline(0);
        }
        EXPECT_GE(postCtx.count, 1);
        EXPECT_EQ(mWidth, postCtx.width);
        EXPECT_EQ(mHeight, postCtx.height);
    }

    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/0);
    mFb->closeColorBuffer(targetHandle);
}

#if !defined(CONFIG_AEMU)
// Without a subwindow or post callback (the virtio-gpu / stream_renderer path
// and every non-emulator target), compose(post=true) must only compose: it
// never posts.
TEST_F(FrameBufferTest, ComposeWithNeedPostDoesNotPostWithoutPostCallback) {
    if (mUseSubWindow) {
        GTEST_SKIP() << "Subwindow mode posts directly to m_subWin without m_onPost";
    }
    HandleType targetHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, targetHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(targetHandle));

    std::vector<uint8_t> composeBuf(sizeof(ComposeDevice_v2) + sizeof(ComposeLayer), 0);
    auto* composeDev = reinterpret_cast<ComposeDevice_v2*>(composeBuf.data());
    composeDev->version = 2;
    composeDev->displayId = 0;
    composeDev->targetHandle = targetHandle;
    composeDev->numLayers = 1;
    composeDev->layer[0].cbHandle = 0;
    composeDev->layer[0].composeMode = HWC2_COMPOSITION_SOLID_COLOR;
    composeDev->layer[0].displayFrame = {0, 0, mWidth, mHeight};
    composeDev->layer[0].crop = {0.0f, 0.0f, static_cast<float>(mWidth),
                                 static_cast<float>(mHeight)};
    composeDev->layer[0].blendMode = HWC2_BLEND_MODE_NONE;
    composeDev->layer[0].alpha = 1.0f;
    composeDev->layer[0].color = {0, 255, 0, 255};
    composeDev->layer[0].transform = HWC_TRANSFORM_NONE;

    mFb->resetGuestPostedAFrame();
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());

    // The composition itself still happened.
    std::vector<uint8_t> readPixels(static_cast<size_t>(mWidth) * mHeight * 4, 0);
    mFb->readColorBuffer(targetHandle, 0, 0, mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM,
                         readPixels.data(), readPixels.size());
    EXPECT_EQ(0u, readPixels[0]);
    EXPECT_EQ(255u, readPixels[1]);
    EXPECT_EQ(0u, readPixels[2]);
    EXPECT_EQ(255u, readPixels[3]);

    mFb->closeColorBuffer(targetHandle);
}
#endif

TEST_F(FrameBufferTest, ComposePostsDisplay0AndDisplay6ToSeparatePostCallbacks) {
    const int kOuterWidth = 128;
    const int kOuterHeight = 320;
    HandleType innerHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, innerHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(innerHandle));

    HandleType outerHandle =
        mFb->createColorBuffer(kOuterWidth, kOuterHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, outerHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(outerHandle));

    uint32_t outerDisplayId = 6;
    ASSERT_EQ(0, mFb->createDisplay(&outerDisplayId));
    ASSERT_EQ(6u, outerDisplayId);
    ASSERT_EQ(0, mFb->setDisplayPose(6, 0, 0, kOuterWidth, kOuterHeight, 420));

    struct PostContext {
        int count = 0;
        uint32_t displayId = 99;
        int width = 0;
        int height = 0;
    } innerCtx, outerCtx;

    auto onPostCb = +[](void* context, uint32_t displayId, int width, int height, int /*ydir*/,
                        int /*format*/, int /*type*/, unsigned char* /*pixels*/) {
        auto* ctx = static_cast<PostContext*>(context);
        ++ctx->count;
        ctx->displayId = displayId;
        ctx->width = width;
        ctx->height = height;
    };

    mFb->setPostCallback(onPostCb, &innerCtx, /*displayId=*/0, /*useBgraReadback=*/false);
    mFb->setPostCallback(onPostCb, &outerCtx, /*displayId=*/6, /*useBgraReadback=*/false);

    std::vector<uint8_t> composeBuf(sizeof(ComposeDevice_v2) + sizeof(ComposeLayer), 0);
    auto* composeDev = reinterpret_cast<ComposeDevice_v2*>(composeBuf.data());
    composeDev->version = 2;
    composeDev->displayId = 6;
    composeDev->targetHandle = outerHandle;
    composeDev->numLayers = 1;
    composeDev->layer[0].cbHandle = 0;
    composeDev->layer[0].composeMode = HWC2_COMPOSITION_SOLID_COLOR;
    composeDev->layer[0].displayFrame = {0, 0, kOuterWidth, kOuterHeight};
    composeDev->layer[0].crop = {0.0f, 0.0f, static_cast<float>(kOuterWidth),
                                 static_cast<float>(kOuterHeight)};
    composeDev->layer[0].blendMode = HWC2_BLEND_MODE_NONE;
    composeDev->layer[0].alpha = 1.0f;
    composeDev->layer[0].color = {0, 255, 0, 255};
    composeDev->layer[0].transform = HWC_TRANSFORM_NONE;

    mFb->resetGuestPostedAFrame();
    // Not a pixel-fold device: only display 0 is ever posted.
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());

    struct ScopedDisplayOps {
        gfxstream_multi_display_ops saved = get_gfxstream_multi_display_operations();
        ~ScopedDisplayOps() { set_gfxstream_multi_display_operations(saved); }
    } scopedOps;
    gfxstream_multi_display_ops pixelFoldOps = scopedOps.saved;
    pixelFoldOps.is_pixel_fold = []() { return true; };
    set_gfxstream_multi_display_operations(pixelFoldOps);

    // Virtual display 1 does not post onto either physical console buffer.
    composeDev->displayId = 1;
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());

    // Outer display 6 posts exclusively to m_onPost[6] and primes its PBO ring on the very
    // first frame so static outer screens do not wait 3 frames before firing doPostCallback.
    composeDev->displayId = 6;
    composeDev->targetHandle = outerHandle;
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    EXPECT_TRUE(mFb->hasGuestPostedAFrame());
    if (!mUseSubWindow) {
        EXPECT_EQ(0, innerCtx.count);
        EXPECT_EQ(1, outerCtx.count);
        EXPECT_EQ(6u, outerCtx.displayId);
        EXPECT_EQ(kOuterWidth, outerCtx.width);
        EXPECT_EQ(kOuterHeight, outerCtx.height);

        // A subsequent non-primed frame marks hasUnflushedFrame; flushReadPipeline(6) delivers it
        // once and a repeat flush is a no-op.
        EXPECT_TRUE(mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(),
                                 /*post=*/true));
        if (mFb->asyncReadbackSupported()) {
            mFb->flushReadPipeline(0);
            mFb->flushReadPipeline(6);
            const int countAfterFlush = outerCtx.count;
            EXPECT_GE(countAfterFlush, 2);
            mFb->flushReadPipeline(6);
            EXPECT_EQ(countAfterFlush, outerCtx.count);
        }
        EXPECT_EQ(0, innerCtx.count);
    }

    // Inner display 0 posts exclusively to m_onPost[0], leaving m_onPost[6] untouched.
    const int outerCountBeforeInner = outerCtx.count;
    composeDev->displayId = 0;
    composeDev->targetHandle = innerHandle;
    composeDev->layer[0].displayFrame = {0, 0, mWidth, mHeight};
    composeDev->layer[0].crop = {0.0f, 0.0f, static_cast<float>(mWidth),
                                 static_cast<float>(mHeight)};
    EXPECT_TRUE(
        mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(), /*post=*/true));
    if (!mUseSubWindow) {
        if (mFb->asyncReadbackSupported()) {
            mFb->flushReadPipeline(0);
            mFb->flushReadPipeline(6);
        }
        EXPECT_GE(innerCtx.count, 1);
        EXPECT_EQ(0u, innerCtx.displayId);
        EXPECT_EQ(mWidth, innerCtx.width);
        EXPECT_EQ(mHeight, innerCtx.height);
        EXPECT_EQ(outerCountBeforeInner, outerCtx.count);
    }

    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/6);
    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/0);
    mFb->destroyDisplay(6);
    mFb->closeColorBuffer(outerHandle);
    mFb->closeColorBuffer(innerHandle);
}

TEST_F(FrameBufferTest, Display0PreconfiguredModesSwitchWithoutReallocationAndSkipUnconfiguredSize) {
    if (mUseSubWindow) {
        GTEST_SKIP() << "Post callbacks are not delivered with a sub-window; nothing to assert.";
    }
    const int kUnfoldedWidth = 320;
    const int kUnfoldedHeight = 280;
    const int kTabletWidth = 300;
    const int kTabletHeight = 200;
    const int kDesktopWidth = 360;
    const int kDesktopHeight = 260;
    const int kUnconfiguredWidth = 140;
    const int kUnconfiguredHeight = 310;

    mFb->setDisplayConfigs(0, mWidth, mHeight, 420, 420);
    mFb->setDisplayConfigs(1, kUnfoldedWidth, kUnfoldedHeight, 420, 420);
    mFb->setDisplayConfigs(2, kTabletWidth, kTabletHeight, 240, 240);
    mFb->setDisplayActiveConfig(0);

    HandleType phoneHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, phoneHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(phoneHandle));

    HandleType unfoldedHandle =
        mFb->createColorBuffer(kUnfoldedWidth, kUnfoldedHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, unfoldedHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(unfoldedHandle));

    HandleType tabletHandle =
        mFb->createColorBuffer(kTabletWidth, kTabletHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, tabletHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(tabletHandle));

    HandleType desktopHandle =
        mFb->createColorBuffer(kDesktopWidth, kDesktopHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, desktopHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(desktopHandle));

    HandleType unconfiguredHandle = mFb->createColorBuffer(kUnconfiguredWidth, kUnconfiguredHeight,
                                                           GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, unconfiguredHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(unconfiguredHandle));

    struct PostContext {
        int count = 0;
        uint32_t displayId = 99;
        int width = 0;
        int height = 0;
    } postCtx;

    auto onPostCb = +[](void* context, uint32_t displayId, int width, int height, int /*ydir*/,
                        int /*format*/, int /*type*/, unsigned char* /*pixels*/) {
        auto* ctx = static_cast<PostContext*>(context);
        ++ctx->count;
        ctx->displayId = displayId;
        ctx->width = width;
        ctx->height = height;
    };

    mFb->setPostCallback(onPostCb, &postCtx, /*displayId=*/0, /*useBgraReadback=*/false);
    // Registering a larger display 0 config after setPostCallback() also pre-expands m_onPost[0].
    mFb->setDisplayConfigs(3, kDesktopWidth, kDesktopHeight, 160, 160);

    auto composeDisplay0 = [&](HandleType targetHandle, int w, int h) {
        std::vector<uint8_t> composeBuf(sizeof(ComposeDevice_v2) + sizeof(ComposeLayer), 0);
        auto* composeDev = reinterpret_cast<ComposeDevice_v2*>(composeBuf.data());
        composeDev->version = 2;
        composeDev->displayId = 0;
        composeDev->targetHandle = targetHandle;
        composeDev->numLayers = 1;
        composeDev->layer[0].cbHandle = 0;
        composeDev->layer[0].composeMode = HWC2_COMPOSITION_SOLID_COLOR;
        composeDev->layer[0].displayFrame = {0, 0, w, h};
        composeDev->layer[0].crop = {0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h)};
        composeDev->layer[0].blendMode = HWC2_BLEND_MODE_NONE;
        composeDev->layer[0].alpha = 1.0f;
        composeDev->layer[0].color = {0, 128, 255, 255};
        composeDev->layer[0].transform = HWC_TRANSFORM_NONE;
        return mFb->compose(static_cast<uint32_t>(composeBuf.size()), composeBuf.data(),
                            /*post=*/true);
    };

    EXPECT_TRUE(composeDisplay0(phoneHandle, mWidth, mHeight));
    if (!mUseSubWindow) {
        EXPECT_EQ(1, postCtx.count);
        EXPECT_EQ(0u, postCtx.displayId);
        EXPECT_EQ(mWidth, postCtx.width);
        EXPECT_EQ(mHeight, postCtx.height);
    }

    // Switching to the larger preconfigured unfolded mode primes immediately on its first frame.
    EXPECT_TRUE(composeDisplay0(unfoldedHandle, kUnfoldedWidth, kUnfoldedHeight));
    if (!mUseSubWindow) {
        EXPECT_EQ(2, postCtx.count);
        EXPECT_EQ(0u, postCtx.displayId);
        EXPECT_EQ(kUnfoldedWidth, postCtx.width);
        EXPECT_EQ(kUnfoldedHeight, postCtx.height);
    }

    // Switching to the preconfigured tablet mode also delivers immediately on its first frame.
    EXPECT_TRUE(composeDisplay0(tabletHandle, kTabletWidth, kTabletHeight));
    if (!mUseSubWindow) {
        EXPECT_EQ(3, postCtx.count);
        EXPECT_EQ(0u, postCtx.displayId);
        EXPECT_EQ(kTabletWidth, postCtx.width);
        EXPECT_EQ(kTabletHeight, postCtx.height);
    }

    // Switching to config 3 (registered after setPostCallback) also delivers immediately.
    EXPECT_TRUE(composeDisplay0(desktopHandle, kDesktopWidth, kDesktopHeight));
    if (!mUseSubWindow) {
        EXPECT_EQ(4, postCtx.count);
        EXPECT_EQ(0u, postCtx.displayId);
        EXPECT_EQ(kDesktopWidth, postCtx.width);
        EXPECT_EQ(kDesktopHeight, postCtx.height);
    }

    // An unconfigured size is rejected and does not overwrite m_onPost[0].
    const int countBeforeUnconfigured = postCtx.count;
    EXPECT_TRUE(composeDisplay0(unconfiguredHandle, kUnconfiguredWidth, kUnconfiguredHeight));
    if (!mUseSubWindow) {
        EXPECT_EQ(countBeforeUnconfigured, postCtx.count);
        EXPECT_EQ(kDesktopWidth, postCtx.width);
        EXPECT_EQ(kDesktopHeight, postCtx.height);
    }

    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/0);
    mFb->closeColorBuffer(unconfiguredHandle);
    mFb->closeColorBuffer(desktopHandle);
    mFb->closeColorBuffer(tabletHandle);
    mFb->closeColorBuffer(unfoldedHandle);
    mFb->closeColorBuffer(phoneHandle);
}

TEST_F(FrameBufferTest, PostSecondaryDisplayDoesNotOverwritePrimaryDisplay0) {
    const int kSecondaryWidth = 160;
    const int kSecondaryHeight = 120;
    const uint32_t kSecondaryDisplayId = 7;

    // Untracked primary ColorBuffer defaults to display 0.
    HandleType untrackedPrimaryHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, untrackedPrimaryHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(untrackedPrimaryHandle));

    HandleType primaryHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, primaryHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(primaryHandle));
    std::vector<uint8_t> primaryPixels(static_cast<size_t>(mWidth) * mHeight * 4, 0);
    for (size_t i = 0; i < primaryPixels.size(); i += 4) {
        primaryPixels[i + 0] = 255;
        primaryPixels[i + 1] = 0;
        primaryPixels[i + 2] = 0;
        primaryPixels[i + 3] = 255;
    }
    EXPECT_TRUE(mFb->updateColorBuffer(primaryHandle, 0, 0, mWidth, mHeight,
                                       GfxstreamFormat::R8G8B8A8_UNORM, primaryPixels.data()));

    HandleType secondaryHandle =
        mFb->createColorBuffer(kSecondaryWidth, kSecondaryHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, secondaryHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(secondaryHandle));
    std::vector<uint8_t> secondaryPixels(
        static_cast<size_t>(kSecondaryWidth) * kSecondaryHeight * 4, 0);
    for (size_t i = 0; i < secondaryPixels.size(); i += 4) {
        secondaryPixels[i + 0] = 0;
        secondaryPixels[i + 1] = 0;
        secondaryPixels[i + 2] = 255;
        secondaryPixels[i + 3] = 255;
    }
    EXPECT_TRUE(mFb->updateColorBuffer(secondaryHandle, 0, 0, kSecondaryWidth, kSecondaryHeight,
                                       GfxstreamFormat::R8G8B8A8_UNORM, secondaryPixels.data()));

    uint32_t createdSecondaryId = kSecondaryDisplayId;
    ASSERT_EQ(0, mFb->createDisplay(&createdSecondaryId));
    ASSERT_EQ(kSecondaryDisplayId, createdSecondaryId);
    ASSERT_EQ(
        0, mFb->setDisplayPose(kSecondaryDisplayId, 0, 0, kSecondaryWidth, kSecondaryHeight, 160));

    struct PostContext {
        int count = 0;
        uint32_t displayId = 99;
    } primaryPostCtx, secondaryPostCtx;

    auto onPostCb = +[](void* context, uint32_t displayId, int /*width*/, int /*height*/,
                        int /*ydir*/, int /*format*/, int /*type*/, unsigned char* /*pixels*/) {
        auto* ctx = static_cast<PostContext*>(context);
        ++ctx->count;
        ctx->displayId = displayId;
    };

    mFb->setPostCallback(onPostCb, &primaryPostCtx, /*displayId=*/0, /*useBgraReadback=*/false);
    mFb->setPostCallback(onPostCb, &secondaryPostCtx, kSecondaryDisplayId,
                         /*useBgraReadback=*/false);

    // Invalid handles fail closed and still fire postWithCallback's completion callback.
    mFb->resetGuestPostedAFrame();
    constexpr HandleType kInvalidHandle = 0xDEADu;
    EXPECT_FALSE(mFb->post(kInvalidHandle));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());
    bool invalidCallbackFired = false;
    mFb->postWithCallback(kInvalidHandle, [&](std::shared_future<void> waitForGpu) {
        waitForGpu.wait();
        invalidCallbackFired = true;
    });
    EXPECT_TRUE(invalidCallbackFired);
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());

    // Bind secondary display first so MultiDisplay has an active secondary display when testing
    // untracked primary ColorBuffers and Case 2 multi-display readbacks.
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(kSecondaryDisplayId, secondaryHandle));

    // Untracked primary ColorBuffer defaults to display 0 even when secondary displays exist.
    EXPECT_TRUE(mFb->post(untrackedPrimaryHandle));
    EXPECT_TRUE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(untrackedPrimaryHandle, mFb->getLastPostedColorBuffer());

    // Tracked primary ColorBuffer (rcSetDisplayColorBuffer(0, ...) + rcFBPost) posts to display 0.
    mFb->resetGuestPostedAFrame();
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(0, primaryHandle));
    EXPECT_TRUE(mFb->post(primaryHandle));
    EXPECT_TRUE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(primaryHandle, mFb->getLastPostedColorBuffer());
    if (!mUseSubWindow && mFb->asyncReadbackSupported()) {
        mFb->flushReadPipeline(0);
        mFb->flushReadPipeline(kSecondaryDisplayId);
    }

    const int primaryCountBeforeSecondaryPost = primaryPostCtx.count;
    const int secondaryCountBeforeSecondaryPost = secondaryPostCtx.count;

    // Simulate HWC3 CLIENT composition on passenger display 7: rcSetDisplayColorBuffer + rcFBPost.
    mFb->resetGuestPostedAFrame();
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(kSecondaryDisplayId, secondaryHandle));
    EXPECT_TRUE(mFb->post(secondaryHandle));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(primaryHandle, mFb->getLastPostedColorBuffer());

    // postWithCallback on a secondary display also completes its callback without clobbering 0.
    bool secondaryCallbackFired = false;
    mFb->postWithCallback(secondaryHandle, [&](std::shared_future<void> waitForGpu) {
        waitForGpu.wait();
        secondaryCallbackFired = true;
    });
    EXPECT_TRUE(secondaryCallbackFired);
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(primaryHandle, mFb->getLastPostedColorBuffer());

    if (!mUseSubWindow) {
        if (mFb->asyncReadbackSupported()) {
            mFb->flushReadPipeline(0);
            mFb->flushReadPipeline(kSecondaryDisplayId);
        }
        EXPECT_EQ(primaryCountBeforeSecondaryPost, primaryPostCtx.count);
        EXPECT_EQ(secondaryCountBeforeSecondaryPost, secondaryPostCtx.count);
    }

    uint32_t boundSecondaryCb = 0;
    ASSERT_EQ(0, mFb->getDisplayColorBuffer(kSecondaryDisplayId, &boundSecondaryCb));
    EXPECT_EQ(secondaryHandle, boundSecondaryCb);

    std::vector<uint8_t> readPrimary(static_cast<size_t>(mWidth) * mHeight * 4, 0);
    mFb->readColorBuffer(mFb->getLastPostedColorBuffer(), 0, 0, mWidth, mHeight,
                         GfxstreamFormat::R8G8B8A8_UNORM, readPrimary.data(), readPrimary.size());
    EXPECT_EQ(255u, readPrimary[0]);
    EXPECT_EQ(0u, readPrimary[2]);

    std::vector<uint8_t> readSecondary(static_cast<size_t>(kSecondaryWidth) * kSecondaryHeight * 4,
                                       0);
    mFb->readColorBuffer(boundSecondaryCb, 0, 0, kSecondaryWidth, kSecondaryHeight,
                         GfxstreamFormat::R8G8B8A8_UNORM, readSecondary.data(),
                         readSecondary.size());
    EXPECT_EQ(0u, readSecondary[0]);
    EXPECT_EQ(255u, readSecondary[2]);

#ifdef CONFIG_AEMU
    unsigned int scrW = 0, scrH = 0;
    std::vector<uint8_t> scrPrimary(static_cast<size_t>(mWidth) * mHeight * 4, 0);
    size_t scrPrimaryBytes = scrPrimary.size();
    ASSERT_EQ(0, mFb->getScreenshot(4, &scrW, &scrH, scrPrimary.data(), &scrPrimaryBytes,
                                    /*displayId=*/0, mWidth, mHeight, 0, {{0, 0}, {0, 0}}));
    EXPECT_EQ(255u, scrPrimary[0]);
    EXPECT_EQ(0u, scrPrimary[2]);
#endif

    mFb->setPostCallback(nullptr, nullptr, kSecondaryDisplayId);
    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/0);
    mFb->setDisplayColorBuffer(0, 0);
    mFb->destroyDisplay(kSecondaryDisplayId);
    mFb->closeColorBuffer(secondaryHandle);
    mFb->closeColorBuffer(primaryHandle);
    mFb->closeColorBuffer(untrackedPrimaryHandle);
}

TEST_F(FrameBufferTest, PostPixelFoldOuterDisplay6AndIgnoresVirtualDisplay) {
    const int kOuterDistinctWidth = 128;
    const int kOuterDistinctHeight = 320;

    HandleType innerHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, innerHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(innerHandle));

    // Same-dimension outer handle verifies explicit display 6 ownership routing without heuristics.
    HandleType outerSameSizeHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, outerSameSizeHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(outerSameSizeHandle));

    HandleType outerDistinctHandle1 = mFb->createColorBuffer(
        kOuterDistinctWidth, kOuterDistinctHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, outerDistinctHandle1);
    EXPECT_EQ(0, mFb->openColorBuffer(outerDistinctHandle1));

    HandleType outerDistinctHandle2 = mFb->createColorBuffer(
        kOuterDistinctWidth, kOuterDistinctHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, outerDistinctHandle2);
    EXPECT_EQ(0, mFb->openColorBuffer(outerDistinctHandle2));

    HandleType virtualHandle =
        mFb->createColorBuffer(mWidth, mHeight, GfxstreamFormat::R8G8B8A8_UNORM);
    ASSERT_NE((HandleType)0, virtualHandle);
    EXPECT_EQ(0, mFb->openColorBuffer(virtualHandle));

    uint32_t virtualDisplayId = 1;
    ASSERT_EQ(0, mFb->createDisplay(&virtualDisplayId));
    ASSERT_EQ(1u, virtualDisplayId);
    ASSERT_EQ(0, mFb->setDisplayPose(1, 0, 0, mWidth, mHeight, 240));

    uint32_t outerDisplayId = 6;
    ASSERT_EQ(0, mFb->createDisplay(&outerDisplayId));
    ASSERT_EQ(6u, outerDisplayId);
    ASSERT_EQ(0, mFb->setDisplayPose(6, 0, 0, mWidth, mHeight, 420));

    struct ScopedDisplayOps {
        gfxstream_multi_display_ops saved = get_gfxstream_multi_display_operations();
        ~ScopedDisplayOps() { set_gfxstream_multi_display_operations(saved); }
    } scopedOps;
    gfxstream_multi_display_ops pixelFoldOps = scopedOps.saved;
    pixelFoldOps.is_pixel_fold = []() { return true; };
    set_gfxstream_multi_display_operations(pixelFoldOps);

    struct PostContext {
        int count = 0;
        uint32_t displayId = 99;
    } innerCtx, outerCtx;

    auto onPostCb = +[](void* context, uint32_t displayId, int /*width*/, int /*height*/,
                        int /*ydir*/, int /*format*/, int /*type*/, unsigned char* /*pixels*/) {
        auto* ctx = static_cast<PostContext*>(context);
        ++ctx->count;
        ctx->displayId = displayId;
    };

    mFb->setPostCallback(onPostCb, &innerCtx, /*displayId=*/0, /*useBgraReadback=*/false);
    mFb->setPostCallback(onPostCb, &outerCtx, /*displayId=*/6, /*useBgraReadback=*/false);

    ASSERT_EQ(0, mFb->setDisplayColorBuffer(0, innerHandle));
    EXPECT_TRUE(mFb->post(innerHandle));
    EXPECT_EQ(innerHandle, mFb->getLastPostedColorBuffer());
    if (!mUseSubWindow && mFb->asyncReadbackSupported()) {
        mFb->flushReadPipeline(0);
    }
    const int innerCountBeforeVirtual = innerCtx.count;

    // Virtual display 1 CLIENT composition must not overwrite the foldable console window.
    mFb->resetGuestPostedAFrame();
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(1, virtualHandle));
    EXPECT_TRUE(mFb->post(virtualHandle));
    EXPECT_FALSE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(innerHandle, mFb->getLastPostedColorBuffer());
    if (!mUseSubWindow) {
        EXPECT_EQ(innerCountBeforeVirtual, innerCtx.count);
        EXPECT_EQ(0, outerCtx.count);
    }

    // Outer display 6 CLIENT composition posts to display 6 even when matching display 0 size.
    mFb->resetGuestPostedAFrame();
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(6, outerSameSizeHandle));
    EXPECT_TRUE(mFb->post(outerSameSizeHandle));
    EXPECT_TRUE(mFb->hasGuestPostedAFrame());
    EXPECT_EQ(outerSameSizeHandle, mFb->getLastPostedColorBuffer());
    if (!mUseSubWindow) {
        if (mFb->asyncReadbackSupported()) {
            mFb->flushReadPipeline(6);
        }
        EXPECT_EQ(innerCountBeforeVirtual, innerCtx.count);
        EXPECT_GE(outerCtx.count, 1);
        EXPECT_EQ(6u, outerCtx.displayId);
    }

    // Reconfigure outer display 6 to distinct dimensions and verify folded repost() still routes
    // to m_onPost[6] even after MultiDisplay[6] rotates to the next swapchain buffer.
    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/6);
    ASSERT_EQ(0, mFb->setDisplayPose(6, 0, 0, kOuterDistinctWidth, kOuterDistinctHeight, 420));
    outerCtx = {};
    mFb->setPostCallback(onPostCb, &outerCtx, /*displayId=*/6, /*useBgraReadback=*/false);

    ASSERT_EQ(0, mFb->setDisplayColorBuffer(6, outerDistinctHandle1));
    EXPECT_TRUE(mFb->post(outerDistinctHandle1));
    ASSERT_EQ(0, mFb->setDisplayColorBuffer(6, outerDistinctHandle2));
    const int outerCountBeforeRepost = outerCtx.count;
    EXPECT_TRUE(mFb->repost());
    if (!mUseSubWindow) {
        EXPECT_EQ(innerCountBeforeVirtual, innerCtx.count);
        EXPECT_GT(outerCtx.count, outerCountBeforeRepost);
        EXPECT_EQ(6u, outerCtx.displayId);
    }

    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/6);
    mFb->setPostCallback(nullptr, nullptr, /*displayId=*/0);
    mFb->setDisplayColorBuffer(0, 0);
    mFb->destroyDisplay(6);
    mFb->destroyDisplay(1);
    mFb->closeColorBuffer(virtualHandle);
    mFb->closeColorBuffer(outerDistinctHandle2);
    mFb->closeColorBuffer(outerDistinctHandle1);
    mFb->closeColorBuffer(outerSameSizeHandle);
    mFb->closeColorBuffer(innerHandle);
}

}  // namespace
}  // namespace host
}  // namespace gfxstream
