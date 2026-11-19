// Copyright (C) 2026 The Android Open Source Project
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

#include "virtio_gpu_resource.h"

#include <cerrno>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

#include <gtest/gtest.h>

#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/address_space_operations.h"
#include "render-utils/address_space_operations.h"
#include "virtio_gpu_format_utils.h"
#include "virtio_gpu_frontend.h"
#include "virtio_gpu_pipe.h"

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include <atomic>
#include <filesystem>
#endif

namespace gfxstream {
namespace host {
namespace {

TEST(VirtioGpuFormatUtilsSecurityTest, GetTransferOffsetAndSize64BitNoOverflow) {
    // 65536 * 65536 * 4 = 16 GiB (wraps 32-bit uint32_t to 0 without size_t widening)
    const size_t offset = GetTransferOffset(VIRGL_FORMAT_R8G8B8A8_UNORM,
                                            /*totalWidth=*/65536,
                                            /*totalHeight=*/65536,
                                            /*x=*/0,
                                            /*y=*/65535,
                                            /*w=*/65536,
                                            /*h=*/1);
    EXPECT_EQ(offset, static_cast<size_t>(65535ULL) * 65536ULL * 4ULL);

    const size_t rgbSize = GetTransferSize(VIRGL_FORMAT_R8G8B8A8_UNORM,
                                           /*totalWidth=*/65536,
                                           /*totalHeight=*/65536,
                                           /*x=*/0,
                                           /*y=*/0,
                                           /*w=*/65536,
                                           /*h=*/65536);
    EXPECT_EQ(rgbSize, 65536ULL * 65536ULL * 4ULL);

    const size_t yuvSize = GetTransferSize(VIRGL_FORMAT_YV12,
                                           /*totalWidth=*/65536,
                                           /*totalHeight=*/65536,
                                           /*x=*/0,
                                           /*y=*/0,
                                           /*w=*/65536,
                                           /*h=*/65536);
    EXPECT_GT(yuvSize, 0x100000000ULL);

    // Zero width/height must never underflow (h - 1U)
    EXPECT_EQ(GetTransferSize(VIRGL_FORMAT_R8G8B8A8_UNORM, 16, 16, 0, 0, 16, 0), 0U);
    EXPECT_EQ(GetTransferSize(VIRGL_FORMAT_R8G8B8A8_UNORM, 0, 16, 0, 0, 16, 16), 0U);
}

TEST(VirtioGpuFormatUtilsTest, GetGfxstreamFormatFromDrmFormat) {
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_ABGR8888),
              GfxstreamFormat::R8G8B8A8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_XBGR8888),
              GfxstreamFormat::R8G8B8X8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_ARGB8888),
              GfxstreamFormat::B8G8R8A8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_XRGB8888),
              GfxstreamFormat::B8G8R8A8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_BGR888), GfxstreamFormat::R8G8B8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_RGB888), GfxstreamFormat::R8G8B8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_RGB565), GfxstreamFormat::R5G6B5_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_RG88), GfxstreamFormat::R8G8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_GR88), GfxstreamFormat::R8G8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_R8), GfxstreamFormat::R8_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_R16), GfxstreamFormat::R16_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_ABGR16161616F),
              GfxstreamFormat::R16G16B16A16_FLOAT);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_ABGR2101010),
              GfxstreamFormat::R10G10B10A2_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_XBGR2101010),
              GfxstreamFormat::R10G10B10A2_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_ARGB2101010),
              GfxstreamFormat::B10G10R10A2_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_XRGB2101010),
              GfxstreamFormat::B10G10R10A2_UNORM);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_NV12), GfxstreamFormat::NV12);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_NV21), GfxstreamFormat::NV21);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_P010), GfxstreamFormat::P010);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_P210), GfxstreamFormat::P210);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_YUV420), GfxstreamFormat::YV21);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(DRM_FORMAT_YVU420), GfxstreamFormat::YV12);
    EXPECT_EQ(GetGfxstreamFormatFromDrmFormat(0), std::nullopt);
}

TEST(VirtioGpuFrontendImportResourceTest, ValidatesImportArgumentsAndHandlesExistingResource) {
    VirtioGpuFrontend frontend;

    struct stream_renderer_handle dummyHandle = {
        .os_handle = -1,
        .handle_type = STREAM_HANDLE_TYPE_MEM_DMABUF,
    };
    struct stream_renderer_import_data dummyData = {
        .flags = 0,
        .size = 4096,
    };

    // Null import_handle or import_data must return -EINVAL.
    EXPECT_EQ(frontend.importResource(1, nullptr, &dummyData), -EINVAL);
    EXPECT_EQ(frontend.importResource(1, &dummyHandle, nullptr), -EINVAL);

    // Null arguments to VirtioGpuResource::Create must return std::nullopt.
    EXPECT_FALSE(VirtioGpuResource::Create(1, nullptr, &dummyData).has_value());
    EXPECT_FALSE(VirtioGpuResource::Create(1, &dummyHandle, nullptr).has_value());

    // STREAM_RENDERER_IMPORT_FLAG_RESOURCE_EXISTS on non-existent resource must return -EINVAL.
    struct stream_renderer_import_data existsData = {
        .flags = STREAM_RENDERER_IMPORT_FLAG_RESOURCE_EXISTS,
        .size = 4096,
    };
    EXPECT_EQ(frontend.importResource(999, &dummyHandle, &existsData), -EINVAL);

    // Create a PIPE resource with handle 1.
    struct stream_renderer_resource_create_args createArgs = {
        .handle = 1,
        .target = 0,
        .format = VIRGL_FORMAT_R8_UNORM,
        .bind = VIRGL_BIND_CUSTOM,
        .width = 64,
        .height = 1,
        .depth = 1,
        .array_size = 1,
    };
    ASSERT_EQ(frontend.createResource(&createArgs, nullptr, 0), 0);

#if !defined(_WIN32)
    // Importing an already-existing resource without STREAM_RENDERER_IMPORT_FLAG_RESOURCE_EXISTS
    // succeeds (returns 0) and closes the transferred file descriptor.
    int pipeFds[2] = {-1, -1};
    ASSERT_EQ(pipe(pipeFds), 0);
    close(pipeFds[1]);

    struct stream_renderer_handle transferredHandle = {
        .os_handle = pipeFds[0],
        .handle_type = STREAM_HANDLE_TYPE_MEM_DMABUF,
    };
    EXPECT_EQ(frontend.importResource(1, &transferredHandle, &dummyData), 0);
    EXPECT_EQ(fcntl(pipeFds[0], F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
#endif
}

TEST(VirtioGpuResourceSecurityTest, RejectsPipeTransferReadAndWriteOutOfBoundsAndOverflow) {
    struct stream_renderer_resource_create_args args = {
        .handle = 1,
        .target = 0,                      // PIPE_BUFFER
        .format = VIRGL_FORMAT_R8_UNORM,
        .bind = VIRGL_BIND_CUSTOM,        // VirtioGpuResourceType::PIPE
        .width = 0x2000,
        .height = 1,
        .depth = 1,
        .array_size = 1,
    };

    // Attach only 4 KiB linear backing buffer.
    std::vector<uint8_t> backing(4096, 0x41);
    struct iovec iov = {.iov_base = backing.data(), .iov_len = backing.size()};

    auto resOpt = VirtioGpuResource::Create(&args, &iov, 1);
    ASSERT_TRUE(resOpt.has_value());
    VirtioGpuResource& res = *resOpt;

    res.SetHostPipe(std::make_shared<VirtioGpuPipe>(/*renderer=*/nullptr, /*id=*/1));

    // Null box must reject cleanly with -EINVAL.
    EXPECT_EQ(res.TransferRead(0, nullptr, std::nullopt), -EINVAL);
    EXPECT_EQ(res.TransferWrite(0, nullptr, std::nullopt), -EINVAL);

    // 1. Direct OOB offset (box.x = 0x10000000, box.w = 0x1000 > mLinear.size() = 0x1000)
    stream_renderer_box oobBox = {.x = 0x10000000, .y = 0, .z = 0, .w = 0x1000, .h = 1, .d = 1};
    EXPECT_EQ(res.TransferRead(0, &oobBox, std::nullopt), -EINVAL);
    EXPECT_EQ(res.TransferWrite(0, &oobBox, std::nullopt), -EINVAL);

    // 2. 32-bit uint32_t overflow (box.x = 0xFFFFF800, box.w = 0x1000 -> x + w wraps u32 to 0x800!)
    stream_renderer_box overflowBox = {
        .x = 0xFFFFF800U, .y = 0, .z = 0, .w = 0x1000U, .h = 1, .d = 1};
    EXPECT_EQ(res.TransferRead(0, &overflowBox, std::nullopt), -EINVAL);
    EXPECT_EQ(res.TransferWrite(0, &overflowBox, std::nullopt), -EINVAL);

    // 3. Exact boundary OOB (box.x = 2048, box.w = 2049 -> 4097 > 4096)
    stream_renderer_box offByOneBox = {.x = 2048, .y = 0, .z = 0, .w = 2049, .h = 1, .d = 1};
    EXPECT_EQ(res.TransferRead(0, &offByOneBox, std::nullopt), -EINVAL);
    EXPECT_EQ(res.TransferWrite(0, &offByOneBox, std::nullopt), -EINVAL);

    // 4. AttachIov size_t overflow must reject cleanly and clear mLinear/mIovs
    struct iovec overflowIovs[2] = {
        {.iov_base = backing.data(), .iov_len = 4096},
        {.iov_base = backing.data(), .iov_len = std::numeric_limits<size_t>::max()},
    };
    res.AttachIov(overflowIovs, 2);
    stream_renderer_box smallBox = {.x = 0, .y = 0, .z = 0, .w = 16, .h = 1, .d = 1};
    EXPECT_EQ(res.TransferRead(0, &smallBox, std::nullopt), -EINVAL);
    EXPECT_EQ(res.TransferWrite(0, &smallBox, std::nullopt), -EINVAL);
}

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
TEST(VirtioGpuResourceSecurityTest, RejectsUndersizedLinearBackingOnBufferAndColorBufferTransfers) {
    std::vector<uint8_t> smallBacking(4096, 0x42);
    struct iovec iov = {.iov_base = smallBacking.data(), .iov_len = smallBacking.size()};
    stream_renderer_box tinyBox = {.x = 0, .y = 0, .z = 0, .w = 16, .h = 1, .d = 1};

    // 1. BUFFER resource with width=65536, height=65536 (4 GiB -> u32 wraps width*height to 0!)
    // Without 64-bit bounds check in BOTH ReadFromBufferToLinear and WriteToBufferFromLinear,
    // step-2 would pass requiredSize=0 / skip check and dereference FrameBuffer::getFB() OOB!
    snapshot::VirtioGpuResourceSnapshot bufSnap;
    bufSnap.set_id(10);
    bufSnap.set_type(static_cast<snapshot::VirtioGpuResourceType>(VirtioGpuResourceType::BUFFER));
    auto* bufCreate = bufSnap.mutable_create_args();
    bufCreate->set_id(10);
    bufCreate->set_target(0);
    bufCreate->set_format(VIRGL_FORMAT_R8_UNORM);
    bufCreate->set_bind(1U << 4);
    bufCreate->set_width(65536);
    bufCreate->set_height(65536);
    bufCreate->set_depth(1);
    bufCreate->set_array_size(1);

    auto bufResOpt = VirtioGpuResource::Restore(bufSnap);
    ASSERT_TRUE(bufResOpt.has_value());
    bufResOpt->AttachIov(&iov, 1);
    EXPECT_EQ(bufResOpt->TransferRead(0, &tinyBox, std::nullopt), -EINVAL);
    EXPECT_EQ(bufResOpt->TransferWrite(0, &tinyBox, std::nullopt), -EINVAL);

    // 2. COLOR_BUFFER resource with width=65536, height=65536 RGBA8 (16 GiB -> u32 wraps to 0!)
    // Without 64-bit GetTransferSize + bounds check in BOTH ReadFromColorBufferToLinear and
    // WriteToColorBufferFromLinear, step-2 would read/write 16 GiB past mLinear!
    snapshot::VirtioGpuResourceSnapshot cbSnap;
    cbSnap.set_id(11);
    cbSnap.set_type(
        static_cast<snapshot::VirtioGpuResourceType>(VirtioGpuResourceType::COLOR_BUFFER));
    auto* cbCreate = cbSnap.mutable_create_args();
    cbCreate->set_id(11);
    cbCreate->set_target(2);
    cbCreate->set_format(VIRGL_FORMAT_R8G8B8A8_UNORM);
    cbCreate->set_bind(VIRGL_BIND_SAMPLER_VIEW);
    cbCreate->set_width(65536);
    cbCreate->set_height(65536);
    cbCreate->set_depth(1);
    cbCreate->set_array_size(1);

    auto cbResOpt = VirtioGpuResource::Restore(cbSnap);
    ASSERT_TRUE(cbResOpt.has_value());
    cbResOpt->AttachIov(&iov, 1);
    EXPECT_EQ(cbResOpt->TransferRead(0, &tinyBox, std::nullopt), -EINVAL);
    EXPECT_EQ(cbResOpt->TransferWrite(0, &tinyBox, std::nullopt), -EINVAL);
}

class StubSnapshotRenderer final : public gfxstream::Renderer {
  public:
    bool allowPauseAll = true;
    bool allowSave = true;
    int pauseAllCalls = 0;
    int resumeAllCalls = 0;
    int saveCalls = 0;
    std::function<void()> onSave;

    RenderChannelPtr createRenderChannel(Stream*, uint32_t) override { return nullptr; }
    void* addressSpaceGraphicsConsumerCreate(const AsgConsumerCreateInfo&, Stream*) override {
        return nullptr;
    }
    void addressSpaceGraphicsConsumerDestroy(void*) override {}
    void addressSpaceGraphicsConsumerPreSave(void*) override {}
    void addressSpaceGraphicsConsumerSave(void*, Stream*) override {}
    void addressSpaceGraphicsConsumerPostSave(void*) override {}
    void addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(void*) override {}
    void addressSpaceGraphicsConsumerReloadRingConfig(void*) override {}
    HardwareStrings getHardwareStrings() override { return {}; }
    bool getVulkanEmulationDeviceInfo(char**, char**, uint32_t*, uint32_t*, uint32_t*, uint32_t*,
                                      uint32_t*, uint64_t*) override {
        return false;
    }
    void setPostCallback(OnPostCallback, void*, bool, uint32_t) override {}
    void addListener(FrameBufferChangeEventListener*) override {}
    void removeListener(FrameBufferChangeEventListener*) override {}
    bool asyncReadbackSupported() override { return false; }
    ReadPixelsCallback getReadPixelsCallback() override { return nullptr; }
    FlushReadPixelPipeline getFlushReadPixelPipeline() override { return nullptr; }
    bool showOpenGLSubwindow(FBNativeWindowType, int, int, int, int, int, int, float, float, bool,
                             bool) override {
        return false;
    }
    bool destroyOpenGLSubwindow() override { return true; }
    void setOpenGLDisplayRotation(float) override {}
    void setOpenGLDisplayTranslation(float, float) override {}
    void repaintOpenGLDisplay() override {}
    bool hasGuestPostedAFrame() override { return false; }
    void resetGuestPostedAFrame() override {}
    void setScreenMask(int, int, const uint8_t*) override {}
    void setScreenBackground(int, int, const uint8_t*) override {}
    void setDisplayLayout(int, int, const Rect&) override {}
    void setMultiDisplay(uint32_t, int32_t, int32_t, uint32_t, uint32_t, uint32_t, bool) override {
    }
    void setMultiDisplayColorBuffer(uint32_t, uint32_t) override {}
    void onGuestGraphicsProcessCreate(uint64_t) override {}
    void cleanupProcGLObjects(uint64_t) override {}
    bool waitForProcessCleanup() override { return true; }
    AndroidVirtioGpuOps* getVirtioGpuOps() override { return nullptr; }
    void stop(bool) override {}
    void finish() override {}
    bool pauseAllPreSave() override {
        ++pauseAllCalls;
        return allowPauseAll;
    }
    void resumeAll() override { ++resumeAllCalls; }
    bool save(Stream* stream, const ITextureSaverPtr&) override {
        ++saveCalls;
        if (onSave) {
            onSave();
        }
        if (!allowSave) {
            return false;
        }
        stream->putByte(1);
        return !stream->hasErrors();
    }
    bool load(Stream*, const ITextureLoaderPtr&) override { return true; }
    void fillGLESUsages(android_studio::EmulatorGLESUsages*) override {}
    int getScreenshot(unsigned int, unsigned int*, unsigned int*, uint8_t*, size_t*, int, int, int,
                      int, Rect) override {
        return 0;
    }
    void preLoad() override {}
    void postLoad() override {}
    void setVsyncHz(int) override {}
    void setDisplayConfigs(int, int, int, int, int) override {}
    void setDisplayActiveConfig(int) override {}
    const void* getEglDispatch() override { return nullptr; }
    const void* getGles2Dispatch() override { return nullptr; }
    void setShouldSkipDraw(bool) override {}
    bool getShouldSkipDraw() const override { return false; }
};

TEST(VirtioGpuFrontendSnapshotTest, SnapshotCoordinatesAsgPrePostSaveAndHandlesErrors) {
    struct AsgState {
        int dummyConsumer = 0;
        bool failWaitForPause = false;
        bool clearDuringAsgSave = false;
        bool asgPaused = false;
        bool asgPausedDuringRendererSave = false;
        int preSaveCalls = 0;
        int postSaveCalls = 0;
        int cancelCalls = 0;
    };
    auto asgState = std::make_shared<AsgState>();

    struct CleanupGuard {
        ~CleanupGuard() {
            GetAsgOperations().clear();
            AddressSpaceGraphicsContext::clear();
            AddressSpaceGraphicsContext::setConsumer({});
        }
    } cleanupGuard;

    AddressSpaceGraphicsContext::setConsumer(ConsumerInterface{
        .create = [asgState](const AsgConsumerCreateInfo&, Stream*) -> ConsumerHandle {
            return &asgState->dummyConsumer;
        },
        .destroy = [](ConsumerHandle) {},
        .preSave = [asgState](ConsumerHandle) {
            ++asgState->preSaveCalls;
            asgState->asgPaused = true;
        },
        .globalPreSave = []() {},
        .save = [asgState](ConsumerHandle, Stream* stream) {
            stream->putByte(0x5A);
        },
        .globalPostSave = []() {},
        .postSave = [asgState](ConsumerHandle) {
            ++asgState->postSaveCalls;
            asgState->asgPaused = false;
        },
        .postLoad = [](ConsumerHandle) {},
        .globalPreLoad = []() {},
        .reloadRingConfig = [](ConsumerHandle) {},
        .waitForPause = [asgState](ConsumerHandle) -> bool {
            return !asgState->failWaitForPause;
        },
        .cancelPreSave = [asgState](ConsumerHandle) {
            ++asgState->cancelCalls;
            asgState->asgPaused = false;
        },
    });

    auto ops = GetAsgOperations();
    std::vector<char> backing(kAsgConsumerRingStorageSize + kAsgWriteBufferSize, 0);
    AddressSpaceCreateInfo createInfo = {};
    createInfo.handle = ops.gen_handle();
    createInfo.type = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS;
    createInfo.createRenderThread = true;
    createInfo.externalAddr = backing.data();
    createInfo.externalAddrSize = backing.size();
    ops.create_instance(createInfo);

    auto renderer = std::make_shared<StubSnapshotRenderer>();
    renderer->onSave = [asgState] {
        asgState->asgPausedDuringRendererSave = asgState->asgPaused;
    };

    VirtioGpuFrontend frontend;
    ASSERT_EQ(frontend.init(renderer, nullptr, FeatureSet{}, [](void*, stream_renderer_fence*) {}),
              0);

    const std::filesystem::path baseTemp =
        std::filesystem::temp_directory_path() /
        ("gfxstream_vgpu_snap_test_" +
         std::to_string(reinterpret_cast<uintptr_t>(&frontend)));
    std::filesystem::remove_all(baseTemp);
    ASSERT_TRUE(std::filesystem::create_directories(baseTemp));
    struct DirCleanup {
        std::filesystem::path dir;
        ~DirCleanup() { std::filesystem::remove_all(dir); }
    } dirCleanup{baseTemp};

    // 1. Happy path: ASG is pre-saved before snapshotRenderer() and post-saved via ScopedResume.
    EXPECT_EQ(frontend.snapshot(baseTemp.string().c_str()), 0);
    EXPECT_TRUE(asgState->asgPausedDuringRendererSave);
    EXPECT_FALSE(asgState->asgPaused);
    EXPECT_EQ(asgState->preSaveCalls, 1);
    EXPECT_EQ(asgState->postSaveCalls, 1);
    EXPECT_EQ(renderer->pauseAllCalls, 1);
    EXPECT_EQ(renderer->resumeAllCalls, 1);

    // 2. Renderer pauseAllPreSave() failure returns -ETIMEDOUT without touching ASG preSave.
    renderer->allowPauseAll = false;
    EXPECT_EQ(frontend.snapshot(baseTemp.string().c_str()), -ETIMEDOUT);
    EXPECT_EQ(asgState->preSaveCalls, 1);
    renderer->allowPauseAll = true;

    // 3. ASG pre_save failure calls renderer->resumeAll() and aborts before snapshotRenderer().
    asgState->failWaitForPause = true;
    const int resumeBeforePreSaveFail = renderer->resumeAllCalls;
    const int saveBeforePreSaveFail = renderer->saveCalls;
    EXPECT_NE(frontend.snapshot(baseTemp.string().c_str()), 0);
    EXPECT_EQ(renderer->resumeAllCalls, resumeBeforePreSaveFail + 1);
    EXPECT_EQ(renderer->saveCalls, saveBeforePreSaveFail);
    EXPECT_EQ(asgState->cancelCalls, 1);
    asgState->failWaitForPause = false;

    // 4. Null-stream error in snapshotRenderer() (missing directory) returns -EIO and ScopedResume
    //    still runs ASG post_save + renderer->resumeAll().
    const std::filesystem::path missingDir = baseTemp / "nonexistent_subdir" / "nested";
    const int postSavesBeforeMissing = asgState->postSaveCalls;
    EXPECT_EQ(frontend.snapshot(missingDir.string().c_str()), -EIO);
    EXPECT_EQ(asgState->postSaveCalls, postSavesBeforeMissing + 1);
    EXPECT_FALSE(asgState->asgPaused);

    // 5. Null-stream error in snapshotAsg() (gfxstream_asg.bin occupied by a directory) returns
    //    -EIO after snapshotRenderer + snapshotFrontend succeed, and ScopedResume still unparks ASG.
    const std::filesystem::path asgFailDir = baseTemp / "asg_open_fail";
    ASSERT_TRUE(std::filesystem::create_directories(asgFailDir / "gfxstream_asg.bin"));
    const int postSavesBeforeAsgOpenFail = asgState->postSaveCalls;
    EXPECT_EQ(frontend.snapshot(asgFailDir.string().c_str()), -EIO);
    EXPECT_EQ(asgState->postSaveCalls, postSavesBeforeAsgOpenFail + 1);
    EXPECT_FALSE(asgState->asgPaused);

    // 6. Error in scopedResume.finish() (clearing ASG state mid-snapshot resets SaveState so
    //    scopedResume.finish()'s post_save() fails) propagates from snapshot() while still invoking
    //    renderer->resumeAll().
    const std::filesystem::path okDir2 = baseTemp / "post_save_fail";
    ASSERT_TRUE(std::filesystem::create_directories(okDir2));
    renderer->onSave = [ops] {
        ops.clear();
    };
    const int resumeBeforeFinishFail = renderer->resumeAllCalls;
    EXPECT_NE(frontend.snapshot(okDir2.string().c_str()), 0);
    EXPECT_EQ(renderer->resumeAllCalls, resumeBeforeFinishFail + 1);
}
#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

}  // namespace
}  // namespace host
}  // namespace gfxstream
