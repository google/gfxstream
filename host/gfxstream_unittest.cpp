// Copyright (C) 2020 The Android Open Source Project
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

#include <gtest/gtest.h>

#include <vector>

#include "gfxstream/common/testing/graphics_test_environment.h"
#include "gfxstream/host/iostream.h"
#include "gfxstream/host/testing/OSWindow.h"
#include "gfxstream/system/System.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "virtgpu_gfxstream_protocol.h"
#include "virtio_gpu_format_utils.h"

using gfxstream::base::sleepMs;

class GfxStreamBackendTest : public ::testing::Test {
   private:
    static void sWriteFence(void* cookie, struct stream_renderer_fence* fence) {
        uint32_t current = *(uint32_t*)cookie;
        if (current < fence->fence_id) *(uint64_t*)(cookie) = fence->fence_id;
    }

   protected:
    uint32_t cookie;
    static const bool useWindow;
    std::vector<stream_renderer_param> streamRendererParams;
    std::vector<stream_renderer_param> minimumRequiredParams;
    static constexpr uint32_t width = 256;
    static constexpr uint32_t height = 256;
    static std::unique_ptr<OSWindow> window;
    static constexpr int rendererFlags = STREAM_RENDERER_FLAGS_USE_GLES_BIT;
    static constexpr int surfacelessFlags = STREAM_RENDERER_FLAGS_USE_SURFACELESS_BIT;

    GfxStreamBackendTest()
        : cookie(0),
          streamRendererParams{{STREAM_RENDERER_PARAM_USER_DATA,
                                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&cookie))},
                               {STREAM_RENDERER_PARAM_FENCE_CALLBACK,
                                static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&sWriteFence))},
                               {STREAM_RENDERER_PARAM_RENDERER_FLAGS, surfacelessFlags},
                               {STREAM_RENDERER_PARAM_WIN0_WIDTH, width},
                               {STREAM_RENDERER_PARAM_WIN0_HEIGHT, height}},
          minimumRequiredParams{{STREAM_RENDERER_PARAM_USER_DATA,
                                 static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&cookie))},
                                {STREAM_RENDERER_PARAM_FENCE_CALLBACK,
                                 static_cast<uint64_t>(reinterpret_cast<uintptr_t>(&sWriteFence))},
                                {STREAM_RENDERER_PARAM_RENDERER_FLAGS, surfacelessFlags}} {}

    static void SetUpTestSuite() {
        ASSERT_TRUE(gfxstream::testing::SetupGraphicsTestEnvironment());
        if (useWindow) {
            window.reset(CreateOSWindow());
        }
    }

    static void TearDownTestSuite() { window.reset(nullptr); }

    void SetUp() override {
        if (!gfxstream::testing::IsGraphicsTestEnvironmentProvidingGlesDriver()) {
            GTEST_SKIP() << "Skipping GLES test in gles_none test environment.";
        }
        gfxstream::base::setEnvironmentVariable("ANDROID_GFXSTREAM_EGL", "1");
        if (useWindow) {
            window->initialize("GfxStreamBackendTestWindow", width, height);
            window->setVisible(true);
            window->messageLoop();
        }
    }

    void TearDown() override {
        if (IsSkipped()) {
            return;
        }
        // Ensure background threads aren't mid-initialization.
        sleepMs(100);
        if (useWindow) {
            window->destroy();
        }
        stream_renderer_teardown();
    }
};

std::unique_ptr<OSWindow> GfxStreamBackendTest::window = nullptr;

const bool GfxStreamBackendTest::useWindow =
    gfxstream::base::getEnvironmentVariable("ANDROID_EMU_TEST_WITH_WINDOW") == "1";

TEST_F(GfxStreamBackendTest, Init) {
    stream_renderer_init(streamRendererParams.data(), streamRendererParams.size());
}

TEST_F(GfxStreamBackendTest, InitOpenGLWindow) {
    if (!useWindow) {
        return;
    }

    std::vector<stream_renderer_param> glParams = streamRendererParams;
    for (auto& param : glParams) {
        if (param.key == STREAM_RENDERER_PARAM_RENDERER_FLAGS) {
            param.value = rendererFlags;
        }
    }
    stream_renderer_init(glParams.data(), glParams.size());
    gfxstream_backend_setup_window(window->getFramebufferNativeWindow(), 0, 0, width, height, width,
                                   height);
}

TEST_F(GfxStreamBackendTest, SimpleFlush) {
    stream_renderer_init(streamRendererParams.data(), streamRendererParams.size());

    const uint32_t res_id = 8;
    struct stream_renderer_resource_create_args create_resource_args = {
        .handle = res_id,
        .target = 2,  // PIPE_TEXTURE_2D
        .format = VIRGL_FORMAT_R8G8B8A8_UNORM,
        .bind = VIRGL_BIND_SAMPLER_VIEW | VIRGL_BIND_SCANOUT | VIRGL_BIND_SHARED,
        .width = width,
        .height = height,
        .depth = 1,
        .array_size = 1,
        .last_level = 0,
        .nr_samples = 0,
        .flags = 0,
    };
    EXPECT_EQ(stream_renderer_resource_create(&create_resource_args, NULL, 0), 0);
    // R8G8B8A8 is used, so 4 bytes per pixel
    auto fb = std::make_unique<uint32_t[]>(width * height);
    EXPECT_NE(fb, nullptr);
    stream_renderer_flush(res_id);
}

// Tests compile and link only.
TEST_F(GfxStreamBackendTest, DISABLED_ApiCallLinkTest) {
    stream_renderer_init(streamRendererParams.data(), streamRendererParams.size());

    const uint32_t res_id = 8;
    struct stream_renderer_resource_create_args create_resource_args = {
        .handle = res_id,
        .target = 2,  // PIPE_TEXTURE_2D
        .format = VIRGL_FORMAT_R8G8B8A8_UNORM,
        .bind = VIRGL_BIND_SAMPLER_VIEW | VIRGL_BIND_SCANOUT | VIRGL_BIND_SHARED,
        .width = width,
        .height = height,
        .depth = 1,
        .array_size = 1,
        .last_level = 0,
        .nr_samples = 0,
        .flags = 0,
    };
    EXPECT_EQ(stream_renderer_resource_create(&create_resource_args, NULL, 0), 0);
    struct stream_renderer_command cmd;
    cmd.ctx_id = 0;
    cmd.cmd = nullptr;
    cmd.cmd_size = 0;

    // R8G8B8A8 is used, so 4 bytes per pixel
    auto fb = std::make_unique<uint32_t[]>(width * height);
    EXPECT_NE(fb, nullptr);
    stream_renderer_flush(res_id);
    stream_renderer_resource_unref(0);
    stream_renderer_context_create(0, 0, NULL, 0);
    stream_renderer_context_destroy(0);
    stream_renderer_submit_cmd(&cmd);
    stream_renderer_transfer_read_iov(0, 0, 0, 0, 0, 0, 0, 0, 0);
    stream_renderer_transfer_write_iov(0, 0, 0, 0, 0, 0, 0, 0, 0);

    stream_renderer_get_cap_set(0, 0, 0);
    stream_renderer_fill_caps(0, 0, 0);

    stream_renderer_resource_attach_iov(0, 0, 0);
    stream_renderer_resource_detach_iov(0, 0, 0);
    stream_renderer_create_fence(NULL);
    stream_renderer_ctx_attach_resource(0, 0);
    stream_renderer_ctx_detach_resource(0, 0);
    stream_renderer_resource_get_info(0, 0);
}

TEST_F(GfxStreamBackendTest, MinimumRequiredParameters) {
    // Only the minimum required parameters.
    int initResult =
        stream_renderer_init(minimumRequiredParams.data(), minimumRequiredParams.size());
    EXPECT_EQ(initResult, 0);
}

TEST_F(GfxStreamBackendTest, MissingRequiredParameter) {
    for (size_t i = 0; i < minimumRequiredParams.size(); ++i) {
        // For each parameter, remove it and try to init, expecting failure.
        std::vector<stream_renderer_param> insufficientParams = minimumRequiredParams;
        insufficientParams.erase(insufficientParams.begin() + i);
        int initResult = stream_renderer_init(insufficientParams.data(), insufficientParams.size());
        EXPECT_NE(initResult, 0);

        // Ensure background threads aren't mid-initialization.
        sleepMs(100);
        stream_renderer_teardown();
    }

    // Initialize once more for the teardown function.
    int initResult = stream_renderer_init(streamRendererParams.data(), streamRendererParams.size());
    EXPECT_EQ(initResult, 0);
}

TEST_F(GfxStreamBackendTest, RenderThreadTracking) {
    int initResult = stream_renderer_init(streamRendererParams.data(), streamRendererParams.size());
    EXPECT_EQ(initResult, 0);
    struct GfxstreamMetrics metrics = {};
    EXPECT_EQ(stream_renderer_get_metrics(&metrics), 0);
    const uint32_t initialRenderThreads = metrics.render_thread_count;
    EXPECT_GT(initialRenderThreads, 0);

    {
        const uint32_t context_id = 1;
        EXPECT_EQ(stream_renderer_context_create(context_id, 0, nullptr, 0), 0);

        // When `blob_id` is 0, creating a pipe resource which is just for communication:
        const uint32_t blob_resource_id = 1;
        const struct stream_renderer_create_blob create_blob = {
            .blob_mem = STREAM_BLOB_MEM_HOST3D,
            .blob_flags = STREAM_BLOB_FLAG_USE_MAPPABLE,
            .blob_id = 0,
            .size = 2 * 1024 * 1024,
        };
        EXPECT_EQ(stream_renderer_create_blob(context_id, blob_resource_id, &create_blob, nullptr,
                                              0, nullptr),
                  0);

        const struct gfxstream::gfxstreamContextCreate contextCreate = {
            .hdr =
                {
                    .opCode = GFXSTREAM_CONTEXT_CREATE,
                },
            .resourceId = blob_resource_id,
        };
        struct stream_renderer_command cmd = {
            .ctx_id = context_id,
            .cmd_size = sizeof(contextCreate),
            .cmd = reinterpret_cast<uint8_t*>(
                const_cast<struct gfxstream::gfxstreamContextCreate*>(&contextCreate)),
            .num_in_fences = 0,
            .fences = nullptr,
        };
        EXPECT_EQ(stream_renderer_submit_cmd(&cmd), 0);

        EXPECT_EQ(stream_renderer_get_metrics(&metrics), 0);
        EXPECT_EQ(metrics.render_thread_count, initialRenderThreads + 1);

        stream_renderer_context_destroy(context_id);
        stream_renderer_resource_unref(blob_resource_id);
    }

    EXPECT_EQ(stream_renderer_get_metrics(&metrics), 0);
    EXPECT_EQ(metrics.render_thread_count, initialRenderThreads);
}

namespace {
class FakeIOStream : public gfxstream::IOStream {
   public:
    explicit FakeIOStream(size_t bufSize) : IOStream(bufSize) {}

    void* allocBuffer(size_t minSize) override {
        lastRequestedMinSize = minSize;
        return &dummyByte;
    }
    int commitBuffer(size_t size) override { return 0; }
    int writeFully(const void* buf, size_t len) override { return 0; }
    const unsigned char* readFully(void* buf, size_t len) override { return nullptr; }
    void* getDmaForReading(uint64_t guest_paddr) override { return nullptr; }
    void unlockDma(uint64_t guest_paddr) override {}

    size_t lastRequestedMinSize = 0;
    unsigned char dummyByte = 0;

   protected:
    const unsigned char* readRaw(void* buf, size_t* inout_len) override { return nullptr; }
    void onSave(gfxstream::Stream* stream) override {}
    unsigned char* onLoad(gfxstream::Stream* stream) override { return nullptr; }
};
}  // namespace

TEST(IOStreamTest, AllocDoesNotTruncateSizeToInt) {
    FakeIOStream stream(16);
    const size_t largeLen = 0x80000004ULL;
    unsigned char* ptr = stream.alloc(largeLen);
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(stream.lastRequestedMinSize, largeLen);
}
