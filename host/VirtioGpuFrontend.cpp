// Copyright (C) 2024 The Android Open Source Project
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

#include "VirtioGpuFrontend.h"

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include <filesystem>
#include <fcntl.h>
// X11 defines status as a preprocessor define which messes up
// anyone with a `Status` type.
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#endif  // ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

#include <vulkan/vulkan.h>

#include "FrameBuffer.h"
#include "FrameworkFormats.h"
#include "VkCommonOperations.h"
#include "aemu/base/files/StdioStream.h"
#include "aemu/base/memory/SharedMemory.h"
#include "aemu/base/threads/WorkerThread.h"
#include "gfxstream/host/address_space_operations.h"
// TODO: remove after moving save/load interface to ops.
#include "gfxstream/host/address_space_graphics.h"
#include "gfxstream/host/Tracing.h"
#include "virtgpu_gfxstream_protocol.h"

namespace gfxstream {
namespace host {
namespace {

using android::base::DescriptorType;
using android::base::SharedMemory;
#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
using gfxstream::host::snapshot::VirtioGpuContextSnapshot;
using gfxstream::host::snapshot::VirtioGpuFrontendSnapshot;
using gfxstream::host::snapshot::VirtioGpuResourceSnapshot;
#endif  // ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

struct VirtioGpuCmd {
    uint32_t op;
    uint32_t cmdSize;
    unsigned char buf[0];
} __attribute__((packed));

static uint64_t convert32to64(uint32_t lo, uint32_t hi) {
    return ((uint64_t)lo) | (((uint64_t)hi) << 32);
}

}  // namespace

class CleanupThread {
   public:
    using GenericCleanup = std::function<void()>;

    CleanupThread()
        : mWorker([](CleanupTask task) {
              return std::visit(
                  [](auto&& work) {
                      using T = std::decay_t<decltype(work)>;
                      if constexpr (std::is_same_v<T, GenericCleanup>) {
                          work();
                          return android::base::WorkerProcessingResult::Continue;
                      } else if constexpr (std::is_same_v<T, Exit>) {
                          return android::base::WorkerProcessingResult::Stop;
                      }
                  },
                  std::move(task));
          }) {
        mWorker.start();
    }

    ~CleanupThread() { stop(); }

    // CleanupThread is neither copyable nor movable.
    CleanupThread(const CleanupThread& other) = delete;
    CleanupThread& operator=(const CleanupThread& other) = delete;
    CleanupThread(CleanupThread&& other) = delete;
    CleanupThread& operator=(CleanupThread&& other) = delete;

    void enqueueCleanup(GenericCleanup command) { mWorker.enqueue(std::move(command)); }

    void waitForPendingCleanups() {
        std::promise<void> pendingCleanupsCompletedSignal;
        std::future<void> pendingCleanupsCompltedWaitable = pendingCleanupsCompletedSignal.get_future();
        enqueueCleanup([&]() { pendingCleanupsCompletedSignal.set_value(); });
        pendingCleanupsCompltedWaitable.wait();
    }

    void stop() {
        mWorker.enqueue(Exit{});
        mWorker.join();
    }

   private:
    struct Exit {};
    using CleanupTask = std::variant<GenericCleanup, Exit>;
    android::base::WorkerThread<CleanupTask> mWorker;
};

VirtioGpuFrontend::VirtioGpuFrontend() = default;

int VirtioGpuFrontend::init(RendererPtr renderer,
                            void* cookie, const gfxstream::host::FeatureSet& features,
                            stream_renderer_fence_callback fence_callback) {
    GFXSTREAM_DEBUG("cookie: %p", cookie);
    mRenderer = renderer;
    mCookie = cookie;
    mFeatures = features;
    mFenceCallback = fence_callback;
    mVirtioGpuTimelines = VirtioGpuTimelines::create(getFenceCompletionCallback());

#if !defined(_WIN32)
    mPageSize = getpagesize();
#endif

    mCleanupThread.reset(new CleanupThread());

    return 0;
}

void VirtioGpuFrontend::teardown() {
    destroyVirtioGpuObjects();

    mCleanupThread.reset();

    if (mRenderer) {
        mRenderer->finish();

        bool success = mRenderer->destroyOpenGLSubwindow();
        if (!success) {
            GFXSTREAM_WARNING("Failed to destroy renderer window.");
        }

        mRenderer->stop(/*wait*/true);
        mRenderer.reset();
    }
}

int VirtioGpuFrontend::createContext(VirtioGpuCtxId contextId, uint32_t nlen, const char* name,
                                     uint32_t contextInit) {
    std::string contextName(name, nlen);

    GFXSTREAM_DEBUG("ctxid: %u len: %u name: %s", contextId, nlen, contextName.c_str());

    auto contextOpt = VirtioGpuContext::Create(mRenderer, contextId, contextName, contextInit);
    if (!contextOpt) {
        GFXSTREAM_ERROR("Failed to create context %u.", contextId);
        return -EINVAL;
    }
    mContexts[contextId] = std::move(*contextOpt);
    return 0;
}

VirtioGpuTimelines::FenceCompletionCallback VirtioGpuFrontend::getFenceCompletionCallback() {
    // Forwards fence completions from VirtioGpuTimelines to the client (VMM).
    return [this](const VirtioGpuTimelines::Ring& ring, VirtioGpuTimelines::FenceId fenceId) {
        struct stream_renderer_fence fence = {0};
        fence.fence_id = fenceId;
        fence.flags = STREAM_RENDERER_FLAG_FENCE;
        if (const auto* contextSpecificRing = std::get_if<VirtioGpuRingContextSpecific>(&ring)) {
            fence.flags |= STREAM_RENDERER_FLAG_FENCE_RING_IDX;
            fence.ctx_id = contextSpecificRing->mCtxId;
            fence.ring_idx = contextSpecificRing->mRingIdx;
        }
        mFenceCallback(mCookie, &fence);
    };
}

int VirtioGpuFrontend::destroyContext(VirtioGpuCtxId contextId) {
    GFXSTREAM_DEBUG("ctxid: %u", contextId);

    auto contextIt = mContexts.find(contextId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("failed to destroy context %d: context not found", contextId);
        return -EINVAL;
    }
    auto& context = contextIt->second;

    context.Destroy(get_gfxstream_address_space_ops());

    mContexts.erase(contextIt);
    return 0;
}

#define DECODE(variable, type, input) \
    type variable = {};               \
    memcpy(&variable, input, sizeof(type));

int VirtioGpuFrontend::addressSpaceProcessCmd(VirtioGpuCtxId ctxId, uint32_t* dwords) {
    DECODE(header, gfxstream::gfxstreamHeader, dwords)

    auto contextIt = mContexts.find(ctxId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("ctx id %u not found", ctxId);
        return -EINVAL;
    }
    auto& context = contextIt->second;

    switch (header.opCode) {
        case GFXSTREAM_CONTEXT_CREATE: {
            DECODE(contextCreate, gfxstream::gfxstreamContextCreate, dwords)

            auto resourceIt = mResources.find(contextCreate.resourceId);
            if (resourceIt == mResources.end()) {
                GFXSTREAM_ERROR("ASG coherent resource %u not found", contextCreate.resourceId);
                return -EINVAL;
            }
            auto& resource = resourceIt->second;

            return context.CreateAddressSpaceGraphicsInstance(get_gfxstream_address_space_ops(),
                                                              resource);
        }
        case GFXSTREAM_CONTEXT_PING: {
            DECODE(contextPing, gfxstream::gfxstreamContextPing, dwords)

            return context.PingAddressSpaceGraphicsInstance(get_gfxstream_address_space_ops(),
                                                            contextPing.resourceId);
        }
        default:
            break;
    }

    return 0;
}

int VirtioGpuFrontend::submitCmd(struct stream_renderer_command* cmd) {
    if (!cmd) return -EINVAL;

    void* buffer = reinterpret_cast<void*>(cmd->cmd);

    VirtioGpuRing ring = VirtioGpuRingGlobal{};
    GFXSTREAM_DEBUG("ctx: % u, ring: %s buffer: %p dwords: %d", cmd->ctx_id,
                    to_string(ring).c_str(), buffer, cmd->cmd_size);

    if (!buffer) {
        GFXSTREAM_ERROR("error: buffer null");
        return -EINVAL;
    }

    if (cmd->cmd_size < 4) {
        GFXSTREAM_ERROR("error: not enough bytes (got %d)", cmd->cmd_size);
        return -EINVAL;
    }

    DECODE(header, gfxstream::gfxstreamHeader, buffer);
    switch (header.opCode) {
        case GFXSTREAM_CONTEXT_CREATE:
        case GFXSTREAM_CONTEXT_PING:
        case GFXSTREAM_CONTEXT_PING_WITH_RESPONSE: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_CONTEXT_[CREATE|PING]");

            if (addressSpaceProcessCmd(cmd->ctx_id, (uint32_t*)buffer)) {
                return -EINVAL;
            }
            break;
        }
        case GFXSTREAM_CREATE_EXPORT_SYNC: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_CREATE_EXPORT_SYNC");

            // Make sure the context-specific ring is used
            ring = VirtioGpuRingContextSpecific{
                .mCtxId = cmd->ctx_id,
                .mRingIdx = 0,
            };

            DECODE(exportSync, gfxstream::gfxstreamCreateExportSync, buffer)

            uint64_t sync_handle = convert32to64(exportSync.syncHandleLo, exportSync.syncHandleHi);

            GFXSTREAM_DEBUG("wait for gpu ring %s", to_string(ring).c_str());
            auto taskId = mVirtioGpuTimelines->enqueueTask(ring);
#if GFXSTREAM_ENABLE_HOST_GLES
            gfxstream::FrameBuffer::getFB()->asyncWaitForGpuWithCb(
                sync_handle, [this, taskId] { mVirtioGpuTimelines->notifyTaskCompletion(taskId); });
#endif
            break;
        }
        case GFXSTREAM_CREATE_EXPORT_SYNC_VK:
        case GFXSTREAM_CREATE_IMPORT_SYNC_VK: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_CREATE_[IMPORT|EXPORT]_SYNC_VK");

            // The guest sync export assumes fence context support and always uses
            // VIRTGPU_EXECBUF_RING_IDX. With this, the task created here must use
            // the same ring as the fence created for the virtio gpu command or the
            // fence may be signaled without properly waiting for the task to complete.
            ring = VirtioGpuRingContextSpecific{
                .mCtxId = cmd->ctx_id,
                .mRingIdx = 0,
            };

            DECODE(exportSyncVK, gfxstream::gfxstreamCreateExportSyncVK, buffer)

            uint64_t device_handle =
                convert32to64(exportSyncVK.deviceHandleLo, exportSyncVK.deviceHandleHi);

            uint64_t fence_handle =
                convert32to64(exportSyncVK.fenceHandleLo, exportSyncVK.fenceHandleHi);

            GFXSTREAM_DEBUG("wait for gpu ring %s", to_string(ring).c_str());
            auto taskId = mVirtioGpuTimelines->enqueueTask(ring);
            gfxstream::FrameBuffer::getFB()->asyncWaitForGpuVulkanWithCb(
                device_handle, fence_handle,
                [this, taskId] { mVirtioGpuTimelines->notifyTaskCompletion(taskId); });
            break;
        }
        case GFXSTREAM_CREATE_QSRI_EXPORT_VK: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_CREATE_QSRI_EXPORT_VK");

            // The guest QSRI export assumes fence context support and always uses
            // VIRTGPU_EXECBUF_RING_IDX. With this, the task created here must use
            // the same ring as the fence created for the virtio gpu command or the
            // fence may be signaled without properly waiting for the task to complete.
            ring = VirtioGpuRingContextSpecific{
                .mCtxId = cmd->ctx_id,
                .mRingIdx = 0,
            };

            DECODE(exportQSRI, gfxstream::gfxstreamCreateQSRIExportVK, buffer)

            uint64_t image_handle =
                convert32to64(exportQSRI.imageHandleLo, exportQSRI.imageHandleHi);

            GFXSTREAM_DEBUG("wait for gpu vk qsri ring %u image 0x%llx", to_string(ring).c_str(),
                            (unsigned long long)image_handle);
            auto taskId = mVirtioGpuTimelines->enqueueTask(ring);
            gfxstream::FrameBuffer::getFB()->asyncWaitForGpuVulkanQsriWithCb(
                image_handle,
                [this, taskId] { mVirtioGpuTimelines->notifyTaskCompletion(taskId); });
            break;
        }
        case GFXSTREAM_RESOURCE_CREATE_3D: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_RESOURCE_CREATE_3D");

            DECODE(create3d, gfxstream::gfxstreamResourceCreate3d, buffer)
            struct stream_renderer_resource_create_args rc3d = {0};

            rc3d.target = create3d.target;
            rc3d.format = create3d.format;
            rc3d.bind = create3d.bind;
            rc3d.width = create3d.width;
            rc3d.height = create3d.height;
            rc3d.depth = create3d.depth;
            rc3d.array_size = create3d.arraySize;
            rc3d.last_level = create3d.lastLevel;
            rc3d.nr_samples = create3d.nrSamples;
            rc3d.flags = create3d.flags;

            auto contextIt = mContexts.find(cmd->ctx_id);
            if (contextIt == mContexts.end()) {
                GFXSTREAM_ERROR("ctx id %u is not found", cmd->ctx_id);
                return -EINVAL;
            }
            auto& context = contextIt->second;

            return context.AddPendingBlob(create3d.blobId, rc3d);
        }
        case GFXSTREAM_ACQUIRE_SYNC: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_ACQUIRE_SYNC");

            DECODE(acquireSync, gfxstream::gfxstreamAcquireSync, buffer);

            auto contextIt = mContexts.find(cmd->ctx_id);
            if (contextIt == mContexts.end()) {
                GFXSTREAM_ERROR("ctx id %u is not found", cmd->ctx_id);
                return -EINVAL;
            }
            auto& context = contextIt->second;
            return context.AcquireSync(acquireSync.syncId);
        }
        case GFXSTREAM_PLACEHOLDER_COMMAND_VK: {
            GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_STREAM_RENDERER_CATEGORY,
                                  "GFXSTREAM_PLACEHOLDER_COMMAND_VK");

            // Do nothing, this is a placeholder command
            break;
        }
        default:
            return -EINVAL;
    }

    return 0;
}

int VirtioGpuFrontend::createFence(uint64_t fence_id, const VirtioGpuRing& ring) {
    GFXSTREAM_DEBUG("fenceid: %llu ring: %s", (unsigned long long)fence_id,
                    to_string(ring).c_str());

    mVirtioGpuTimelines->enqueueFence(ring, fence_id);

    return 0;
}

int VirtioGpuFrontend::acquireContextFence(uint32_t contextId, uint64_t fenceId) {
    auto contextIt = mContexts.find(contextId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("failed to acquire context %u fence: context not found", contextId);
        return -EINVAL;
    }
    auto& context = contextIt->second;

    auto syncInfoOpt = context.TakeSync();
    if (!syncInfoOpt) {
        GFXSTREAM_ERROR("failed to acquire context %u fence: no sync acquired", contextId);
        return -EINVAL;
    }

    mSyncMap[fenceId] = std::make_shared<gfxstream::SyncDescriptorInfo>(std::move(*syncInfoOpt));

    return 0;
}

void VirtioGpuFrontend::poll() { mVirtioGpuTimelines->poll(); }

int VirtioGpuFrontend::createResource(struct stream_renderer_resource_create_args* args,
                                      struct iovec* iov, uint32_t num_iovs) {
    auto resourceOpt = VirtioGpuResource::Create(args, iov, num_iovs);
    if (!resourceOpt) {
        GFXSTREAM_ERROR("Failed to create resource %u.", args->handle);
        return -EINVAL;
    }
    mResources[args->handle] = std::move(*resourceOpt);
    return 0;
}

int VirtioGpuFrontend::importResource(uint32_t res_handle,
                                      const struct stream_renderer_handle* import_handle,
                                      const struct stream_renderer_import_data* import_data) {
    if (!import_handle) {
        GFXSTREAM_ERROR("import_handle was not provided in call to importResource for handle: %d",
                        res_handle);
        return -EINVAL;
    } else if (import_data && (import_data->flags & STREAM_RENDERER_IMPORT_FLAG_RESOURCE_EXISTS)) {
        auto resourceIt = mResources.find(res_handle);
        if (resourceIt == mResources.end()) {
            GFXSTREAM_ERROR(
                "import_data::flags specified STREAM_RENDERER_IMPORT_FLAG_RESOURCE_EXISTS, but "
                "internal resource does not already exist",
                res_handle);
            return -EINVAL;
        }
        return resourceIt->second.ImportHandle(import_handle, import_data);
    } else {
        auto resourceOpt = VirtioGpuResource::Create(res_handle, import_handle, import_data);
        if (!resourceOpt) {
            GFXSTREAM_ERROR("Failed to create resource %u, with import_handle/import_data",
                            res_handle);
            return -EINVAL;
        }
        mResources[res_handle] = std::move(*resourceOpt);
        return 0;
    }
}

void VirtioGpuFrontend::unrefResource(uint32_t resourceId) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) return;
    auto& resource = resourceIt->second;

    auto attachedContextIds = resource.GetAttachedContexts();
    for (auto contextId : attachedContextIds) {
        detachResource(contextId, resourceId);
    }

    resource.Destroy();

    mResources.erase(resourceIt);
}

int VirtioGpuFrontend::attachIov(int resourceId, struct iovec* iov, int num_iovs) {
    GFXSTREAM_DEBUG("resource:%d numiovs: %d", resourceId, num_iovs);

    auto it = mResources.find(resourceId);
    if (it == mResources.end()) {
        GFXSTREAM_ERROR("failed to attach iov: resource %u not found.", resourceId);
        return ENOENT;
    }
    auto& resource = it->second;
    resource.AttachIov(iov, num_iovs);
    return 0;
}

void VirtioGpuFrontend::detachIov(int resourceId) {
    GFXSTREAM_DEBUG("resource:%d", resourceId);

    auto it = mResources.find(resourceId);
    if (it == mResources.end()) {
        GFXSTREAM_ERROR("failed to detach iov: resource %u not found.", resourceId);
        return;
    }
    auto& resource = it->second;
    resource.DetachIov();
}

namespace {

std::optional<std::vector<struct iovec>> AsVecOption(struct iovec* iov, int iovec_cnt) {
    if (iovec_cnt > 0) {
        std::vector<struct iovec> ret;
        ret.reserve(iovec_cnt);
        for (int i = 0; i < iovec_cnt; i++) {
            ret.push_back(iov[i]);
        }
        return ret;
    }
    return std::nullopt;
}

}  // namespace

int VirtioGpuFrontend::transferReadIov(int resId, uint64_t offset, stream_renderer_box* box,
                                       struct iovec* iov, int iovec_cnt) {
    auto it = mResources.find(resId);
    if (it == mResources.end()) {
        GFXSTREAM_ERROR("Failed to transfer: failed to find resource %d.", resId);
        return EINVAL;
    }
    auto& resource = it->second;
    return resource.TransferRead(offset, box, AsVecOption(iov, iovec_cnt));
}

int VirtioGpuFrontend::transferWriteIov(int resId, uint64_t offset, stream_renderer_box* box,
                                        struct iovec* iov, int iovec_cnt) {
    auto it = mResources.find(resId);
    if (it == mResources.end()) {
        GFXSTREAM_ERROR("Failed to transfer: failed to find resource %d.", resId);
        return EINVAL;
    }
    auto& resource = it->second;
    return resource.TransferWrite(offset, box, AsVecOption(iov, iovec_cnt));
}

void VirtioGpuFrontend::getCapset(uint32_t set, uint32_t* max_size) {
    switch (set) {
        case VIRTGPU_CAPSET_GFXSTREAM_VULKAN:
            *max_size = sizeof(struct gfxstream::vulkanCapset);
            break;
        case VIRTGPU_CAPSET_GFXSTREAM_GLES:
            *max_size = sizeof(struct gfxstream::glesCapset);
            break;
        case VIRTGPU_CAPSET_GFXSTREAM_COMPOSER:
            *max_size = sizeof(struct gfxstream::composerCapset);
            break;
        default:
            GFXSTREAM_ERROR("Incorrect capability set specified (%u)", set);
    }
}

void VirtioGpuFrontend::fillCaps(uint32_t set, void* caps) {
    switch (set) {
        case VIRTGPU_CAPSET_GFXSTREAM_VULKAN: {
            struct gfxstream::vulkanCapset* capset =
                reinterpret_cast<struct gfxstream::vulkanCapset*>(caps);

            memset(capset, 0, sizeof(*capset));

            capset->protocolVersion = 1;
            capset->ringSize = 12288;
            capset->bufferSize = 1048576;

            auto* fb = gfxstream::FrameBuffer::getFB();
            if (fb->hasEmulationVk()) {
                const auto info = fb->getEmulationVk().getRepresentativeColorBufferMemoryTypeInfo();
                capset->colorBufferMemoryIndex = info.guestMemoryTypeIndex;
                capset->deferredMapping = 1;
            }

            if (mFeatures.VulkanBatchedDescriptorSetUpdate.enabled) {
                capset->vulkanBatchedDescriptorSetUpdate=1;
            }
            capset->noRenderControlEnc = 1;
            capset->blobAlignment = mPageSize;

#if GFXSTREAM_UNSTABLE_VULKAN_DMABUF_WINSYS
            capset->alwaysBlob = 1;
#endif

#if GFXSTREAM_UNSTABLE_VULKAN_EXTERNAL_SYNC
            capset->externalSync = 1;
#endif

            memset(capset->virglSupportedFormats, 0, sizeof(capset->virglSupportedFormats));

            struct FormatWithName {
                uint32_t format;
                const char* name;
            };
#define MAKE_FORMAT_AND_NAME(x) \
    {                           \
        x, #x                   \
    }
            static const FormatWithName kPossibleFormats[] = {
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_B5G6R5_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_B8G8R8A8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_B8G8R8X8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_NV12),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_P010),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R10G10B10A2_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R16_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R16G16B16A16_FLOAT),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R8G8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R8G8B8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R8G8B8A8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_R8G8B8X8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_YV12),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_Z16_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_Z24_UNORM_S8_UINT),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_Z24X8_UNORM),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_Z32_FLOAT_S8X24_UINT),
                MAKE_FORMAT_AND_NAME(VIRGL_FORMAT_Z32_FLOAT),
            };
#undef MAKE_FORMAT_AND_NAME

            GFXSTREAM_INFO("Format support:");
            for (std::size_t i = 0; i < std::size(kPossibleFormats); i++) {
                const FormatWithName& possibleFormat = kPossibleFormats[i];

                GLenum possibleFormatGl = virgl_format_to_gl(possibleFormat.format);
                const bool supported =
                    gfxstream::FrameBuffer::getFB()->isFormatSupported(possibleFormatGl);

                GFXSTREAM_INFO(" %s: %s", possibleFormat.name,
                               (supported ? "supported" : "unsupported"));
                set_virgl_format_supported(capset->virglSupportedFormats, possibleFormat.format,
                                           supported);
            }
            break;
        }
        case VIRTGPU_CAPSET_GFXSTREAM_GLES: {
            struct gfxstream::glesCapset* capset =
                reinterpret_cast<struct gfxstream::glesCapset*>(caps);

            capset->protocolVersion = 1;
            capset->ringSize = 12288;
            capset->bufferSize = 1048576;
            capset->blobAlignment = mPageSize;
            break;
        }
        case VIRTGPU_CAPSET_GFXSTREAM_COMPOSER: {
            struct gfxstream::composerCapset* capset =
                reinterpret_cast<struct gfxstream::composerCapset*>(caps);

            capset->protocolVersion = 1;
            capset->ringSize = 12288;
            capset->bufferSize = 1048576;
            capset->blobAlignment = mPageSize;
            break;
        }
        default:
            GFXSTREAM_ERROR("Incorrect capability set specified");
    }
}

void VirtioGpuFrontend::attachResource(uint32_t contextId, uint32_t resourceId) {
    GFXSTREAM_DEBUG("ctxid: %u resid: %u", contextId, resourceId);

    auto contextIt = mContexts.find(contextId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("failed to attach resource %u to context %u: context not found.",
                        resourceId, contextId);
        return;
    }
    auto& context = contextIt->second;

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("failed to attach resource %u to context %u: resource not found.",
                        resourceId, contextId);
        return;
    }
    auto& resource = resourceIt->second;

    context.AttachResource(resource);
}

void VirtioGpuFrontend::detachResource(uint32_t contextId, uint32_t resourceId) {
    GFXSTREAM_DEBUG("ctxid: %u resid: %u", contextId, resourceId);

    auto contextIt = mContexts.find(contextId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("failed to detach resource %u to context %u: context not found.",
                        resourceId, contextId);
        return;
    }
    auto& context = contextIt->second;

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("failed to attach resource %u to context %u: resource not found.",
                        resourceId, contextId);
        return;
    }
    auto& resource = resourceIt->second;

    auto resourceAsgOpt = context.TakeAddressSpaceGraphicsHandle(resourceId);
    if (resourceAsgOpt) {
        mCleanupThread->enqueueCleanup(
            [asgBlob = resource.ShareRingBlob(), asgHandle = *resourceAsgOpt]() {
                get_gfxstream_address_space_ops().destroy_handle(asgHandle);
            });
    }

    context.DetachResource(resource);
}

int VirtioGpuFrontend::getResourceInfo(uint32_t resourceId,
                                       struct stream_renderer_resource_info* info) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    if (!info) {
        GFXSTREAM_ERROR("Failed to get info: invalid info struct.");
        return EINVAL;
    }

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("Failed to get info: failed to find resource %d.", resourceId);
        return ENOENT;
    }
    auto& resource = resourceIt->second;
    return resource.GetInfo(info);
}

void VirtioGpuFrontend::flushResource(uint32_t res_handle) {
    auto taskId = mVirtioGpuTimelines->enqueueTask(VirtioGpuRingGlobal{});
    gfxstream::FrameBuffer::getFB()->postWithCallback(
        res_handle, [this, taskId](std::shared_future<void> waitForGpu) {
            waitForGpu.wait();
            mVirtioGpuTimelines->notifyTaskCompletion(taskId);
        });
}

int VirtioGpuFrontend::createBlob(uint32_t contextId, uint32_t resourceId,
                                  const struct stream_renderer_create_blob* createBlobArgs,
                                  const struct stream_renderer_handle* handle) {
    auto contextIt = mContexts.find(contextId);
    if (contextIt == mContexts.end()) {
        GFXSTREAM_ERROR("failed to create blob resource %u: context %u missing.", resourceId,
                        contextId);
        return -EINVAL;
    }
    auto& context = contextIt->second;

    auto createArgs = context.TakePendingBlob(createBlobArgs->blob_id);
    if (createArgs) {
        createArgs->handle = resourceId;
    }

    auto resourceOpt =
        VirtioGpuResource::Create(mFeatures, mPageSize, contextId, resourceId,
                                  createArgs ? &*createArgs : nullptr, createBlobArgs, handle);
    if (!resourceOpt) {
        GFXSTREAM_ERROR("failed to create blob resource %u.", resourceId);
        return -EINVAL;
    }
    mResources[resourceId] = std::move(*resourceOpt);
    return 0;
}

int VirtioGpuFrontend::resourceMap(uint32_t resourceId, void** hvaOut, uint64_t* sizeOut) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    if (mFeatures.ExternalBlob.enabled) {
        GFXSTREAM_ERROR("Failed to map resource: external blob enabled.");
        return -EINVAL;
    }

    auto it = mResources.find(resourceId);
    if (it == mResources.end()) {
        if (hvaOut) *hvaOut = nullptr;
        if (sizeOut) *sizeOut = 0;

        GFXSTREAM_ERROR("Failed to map resource: unknown resource id %d.", resourceId);
        return -EINVAL;
    }

    auto& resource = it->second;
    return resource.Map(hvaOut, sizeOut);
}

int VirtioGpuFrontend::resourceUnmap(uint32_t resourceId) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    auto it = mResources.find(resourceId);
    if (it == mResources.end()) {
        GFXSTREAM_ERROR("Failed to map resource: unknown resource id %d.", resourceId);
        return -EINVAL;
    }

    // TODO(lfy): Good place to run any registered cleanup callbacks.
    // No-op for now.
    return 0;
}

void* VirtioGpuFrontend::platformCreateSharedEglContext() {
    void* ptr = nullptr;
#if GFXSTREAM_ENABLE_HOST_GLES
    ptr = gfxstream::FrameBuffer::getFB()->platformCreateSharedEglContext();
#endif
    return ptr;
}

int VirtioGpuFrontend::platformDestroySharedEglContext(void* context) {
    bool success = false;
#if GFXSTREAM_ENABLE_HOST_GLES
    success = gfxstream::FrameBuffer::getFB()->platformDestroySharedEglContext(context);
#endif
    return success ? 0 : -1;
}

int VirtioGpuFrontend::resourceMapInfo(uint32_t resourceId, uint32_t* map_info) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("Failed to get resource map info: unknown resource %d.", resourceId);
        return -EINVAL;
    }

    const auto& resource = resourceIt->second;
    return resource.GetCaching(map_info);
}

int VirtioGpuFrontend::exportBlob(uint32_t resourceId, struct stream_renderer_handle* handle) {
    GFXSTREAM_DEBUG("resource: %u", resourceId);

    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("Failed to export blob: unknown resource %d.", resourceId);
        return -EINVAL;
    }
    auto& resource = resourceIt->second;
    return resource.ExportBlob(handle);
}

int VirtioGpuFrontend::exportFence(uint64_t fenceId, struct stream_renderer_handle* handle) {
    auto it = mSyncMap.find(fenceId);
    if (it == mSyncMap.end()) {
        return -EINVAL;
    }

    auto& entry = it->second;
    DescriptorType rawDescriptor;
    auto rawDescriptorOpt = entry->descriptor.release();
    if (rawDescriptorOpt)
        rawDescriptor = *rawDescriptorOpt;
    else
        return -EINVAL;

    handle->handle_type = entry->streamHandleType;

#ifdef _WIN32
    handle->os_handle = static_cast<int64_t>(reinterpret_cast<intptr_t>(rawDescriptor));
#else
    handle->os_handle = static_cast<int64_t>(rawDescriptor);
#endif

    return 0;
}

int VirtioGpuFrontend::vulkanInfo(uint32_t resourceId,
                                  struct stream_renderer_vulkan_info* vulkanInfo) {
    auto resourceIt = mResources.find(resourceId);
    if (resourceIt == mResources.end()) {
        GFXSTREAM_ERROR("failed to get vulkan info: failed to find resource %d", resourceId);
        return -EINVAL;
    }
    auto& resource = resourceIt->second;
    return resource.GetVulkanInfo(vulkanInfo);
}

int VirtioGpuFrontend::destroyVirtioGpuObjects() {
    {
        std::vector<VirtioGpuResourceId> resourceIds;
        resourceIds.reserve(mResources.size());
        for (auto& [resourceId, resource] : mResources) {
            const auto contextIds = resource.GetAttachedContexts();
            for (const VirtioGpuContextId contextId : contextIds) {
                detachResource(contextId, resourceId);
            }
            resourceIds.push_back(resourceId);
        }
        for (const VirtioGpuResourceId resourceId : resourceIds) {
            unrefResource(resourceId);
        }
        mResources.clear();
    }
    {
        std::vector<VirtioGpuContextId> contextIds;
        contextIds.reserve(mContexts.size());
        for (const auto& [contextId, _] : mContexts) {
            contextIds.push_back(contextId);
        }
        for (const VirtioGpuContextId contextId : contextIds) {
            destroyContext(contextId);
        }
        mContexts.clear();
    }

    if (mCleanupThread) {
        mCleanupThread->waitForPendingCleanups();
    }

    return 0;
}

void VirtioGpuFrontend::setupWindow(void* nativeWindowHandle,
                                    int32_t windowX,
                                    int32_t windowY,
                                    int32_t windowWidth,
                                    int32_t windowHeight,
                                    int32_t framebufferWidth,
                                    int32_t framebufferHeight) {
    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to setup window: renderer not available.");
        return;
    }

    bool success = mRenderer->showOpenGLSubwindow((FBNativeWindowType)(uintptr_t)nativeWindowHandle,
                                                  windowX,
                                                  windowY,
                                                  windowWidth,
                                                  windowHeight,
                                                  framebufferWidth,
                                                  framebufferHeight,
                                                  /*dpr=*/1.0f,
                                                  /*rotation=*/0,
                                                  /*deleteExisting=*/false,
                                                  /*hideWindow=*/false);
    if (!success) {
        GFXSTREAM_ERROR("Failed to setup window: show subwindow failed.");
    }
}

void VirtioGpuFrontend::setScreenMask(int width,
                                      int height,
                                      const unsigned char* rgbaData) {
    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to set screen mask: renderer not available.");
        return;
    }

    mRenderer->setScreenMask(width, height, rgbaData);
}

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

static constexpr const char kSnapshotBasenameAsg[] = "gfxstream_asg.bin";
static constexpr const char kSnapshotBasenameFrontend[] = "gfxstream_frontend.txtproto";
static constexpr const char kSnapshotBasenameRenderer[] = "gfxstream_renderer.bin";

int VirtioGpuFrontend::snapshotRenderer(const char* directory) {
    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameRenderer;

    android::base::StdioStream stream(fopen(snapshotPath.c_str(), "wb"),
                                      android::base::StdioStream::kOwner);
    android::snapshot::SnapshotSaveStream saveStream{
        .stream = &stream,
    };

    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to snapshot renderer: renderer not available.");
        return -EINVAL;
    }
    mRenderer->save(saveStream.stream, saveStream.textureSaver);

    return 0;
}

int VirtioGpuFrontend::snapshotFrontend(const char* directory) {
    gfxstream::host::snapshot::VirtioGpuFrontendSnapshot snapshot;

    for (const auto& [contextId, context] : mContexts) {
        auto contextSnapshotOpt = context.Snapshot();
        if (!contextSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot context %d", contextId);
            return -1;
        }
        (*snapshot.mutable_contexts())[contextId] = std::move(*contextSnapshotOpt);
    }
    for (const auto& [resourceId, resource] : mResources) {
        auto resourceSnapshotOpt = resource.Snapshot();
        if (!resourceSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot resource %d", resourceId);
            return -1;
        }
        (*snapshot.mutable_resources())[resourceId] = std::move(*resourceSnapshotOpt);
    }

    if (mVirtioGpuTimelines) {
        auto timelinesSnapshotOpt = mVirtioGpuTimelines->Snapshot();
        if (!timelinesSnapshotOpt) {
            GFXSTREAM_ERROR("Failed to snapshot timelines.");
            return -1;
        }
        snapshot.mutable_timelines()->Swap(&*timelinesSnapshotOpt);
    }

    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameFrontend;
    int snapshotFd = open(snapshotPath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0660);
    if (snapshotFd < 0) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to open %s", snapshotPath.c_str());
        return -1;
    }
    google::protobuf::io::FileOutputStream snapshotOutputStream(snapshotFd);
    snapshotOutputStream.SetCloseOnDelete(true);
    if (!google::protobuf::TextFormat::Print(snapshot, &snapshotOutputStream)) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to serialize to stream.");
        return -1;
    }

    return 0;
}

int VirtioGpuFrontend::snapshotAsg(const char* directory) {
    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameAsg;

    android::base::StdioStream stream(fopen(snapshotPath.c_str(), "wb"),
                                      android::base::StdioStream::kOwner);
    android::snapshot::SnapshotLoadStream saveStream{
        .stream = &stream,
    };

    int ret = gfxstream_address_space_save_memory_state(saveStream.stream);
    if (ret) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to save ASG state.");
        return ret;
    }
    return 0;
}

int VirtioGpuFrontend::snapshot(const char* directory) {
    GFXSTREAM_DEBUG("directory:%s", directory);

    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to restore renderer: renderer not available.");
        return -EINVAL;
    }
    mRenderer->pauseAllPreSave();

    int ret = snapshotRenderer(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to snapshot renderer.");
        return ret;
    }

    ret = snapshotFrontend(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to snapshot frontend.");
        return ret;
    }

    ret = snapshotAsg(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to save snapshot: failed to snapshot ASG device.");
        return ret;
    }

    GFXSTREAM_DEBUG("directory:%s - done!", directory);
    return 0;
}

int VirtioGpuFrontend::restoreRenderer(const char* directory) {
    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameRenderer;

    android::base::StdioStream stream(fopen(snapshotPath.c_str(), "rb"),
                                      android::base::StdioStream::kOwner);
    android::snapshot::SnapshotLoadStream loadStream{
        .stream = &stream,
    };

    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to restore renderer: renderer not available.");
        return -EINVAL;
    }
    mRenderer->load(loadStream.stream, loadStream.textureLoader);

    return 0;
}

int VirtioGpuFrontend::restoreFrontend(const char* directory) {
    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameFrontend;

    gfxstream::host::snapshot::VirtioGpuFrontendSnapshot snapshot;
    {
        int snapshotFd = open(snapshotPath.c_str(), O_RDONLY);
        if (snapshotFd < 0) {
            GFXSTREAM_ERROR("Failed to restore snapshot: failed to open %s", snapshotPath.c_str());
            return -1;
        }
        google::protobuf::io::FileInputStream snapshotInputStream(snapshotFd);
        snapshotInputStream.SetCloseOnDelete(true);
        if (!google::protobuf::TextFormat::Parse(&snapshotInputStream, &snapshot)) {
            GFXSTREAM_ERROR("Failed to restore snapshot: failed to parse from file.");
            return -1;
        }
    }

    mContexts.clear();
    mResources.clear();

    for (const auto& [contextId, contextSnapshot] : snapshot.contexts()) {
        auto contextOpt = VirtioGpuContext::Restore(mRenderer, contextSnapshot);
        if (!contextOpt) {
            GFXSTREAM_ERROR("Failed to restore context %d", contextId);
            return -1;
        }
        mContexts.emplace(contextId, std::move(*contextOpt));
    }
    for (const auto& [resourceId, resourceSnapshot] : snapshot.resources()) {
        auto resourceOpt = VirtioGpuResource::Restore(resourceSnapshot);
        if (!resourceOpt) {
            GFXSTREAM_ERROR("Failed to restore resource %d", resourceId);
            return -1;
        }
        mResources.emplace(resourceId, std::move(*resourceOpt));
    }

    mVirtioGpuTimelines =
        VirtioGpuTimelines::Restore(getFenceCompletionCallback(), snapshot.timelines());
    if (!mVirtioGpuTimelines) {
        GFXSTREAM_ERROR("Failed to restore timelines.");
        return -1;
    }

    return 0;
}

int VirtioGpuFrontend::restoreAsg(const char* directory) {
    const std::filesystem::path snapshotDirectory = std::string(directory);
    const std::filesystem::path snapshotPath = snapshotDirectory / kSnapshotBasenameAsg;

    android::base::StdioStream stream(fopen(snapshotPath.c_str(), "rb"),
                                      android::base::StdioStream::kOwner);
    android::snapshot::SnapshotLoadStream loadStream{
        .stream = &stream,
    };

    // Gather external memory info that the ASG device needs to reload.
    AddressSpaceDeviceLoadResources asgLoadResources;
    for (const auto& [contextId, context] : mContexts) {
        for (const auto [resourceId, asgId] : context.AsgInstances()) {
            auto resourceIt = mResources.find(resourceId);
            if (resourceIt == mResources.end()) {
                GFXSTREAM_ERROR("Failed to restore ASG device: context %" PRIu32
                                " claims resource %" PRIu32 " is used for ASG %" PRIu32
                                " but resource not found.",
                                contextId, resourceId, asgId);
                return -1;
            }
            auto& resource = resourceIt->second;

            void* mappedAddr = nullptr;
            uint64_t mappedSize = 0;

            int ret = resource.Map(&mappedAddr, &mappedSize);
            if (ret) {
                GFXSTREAM_ERROR("Failed to restore ASG device: failed to map resource %" PRIu32,
                                resourceId);
                return -1;
            }

            asgLoadResources.contextExternalMemoryMap[asgId] = {
                .externalAddress = mappedAddr,
                .externalAddressSize = mappedSize,
            };
        }
    }

    int ret = gfxstream_address_space_set_load_resources(asgLoadResources);
    if (ret) {
        GFXSTREAM_ERROR("Failed to restore ASG device: failed to set ASG load resources.");
        return ret;
    }

    ret = gfxstream_address_space_load_memory_state(loadStream.stream);
    if (ret) {
        GFXSTREAM_ERROR("Failed to restore ASG device: failed to restore ASG state.");
        return ret;
    }
    return 0;
}

int VirtioGpuFrontend::restore(const char* directory) {
    GFXSTREAM_DEBUG("directory:%s", directory);

    destroyVirtioGpuObjects();

    int ret = restoreRenderer(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to load snapshot: failed to load renderer.");
        return ret;
    }

    ret = restoreFrontend(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to load snapshot: failed to load frontend.");
        return ret;
    }

    ret = restoreAsg(directory);
    if (ret) {
        GFXSTREAM_ERROR("Failed to load snapshot: failed to load ASG device.");
        return ret;
    }

    if (!mRenderer) {
        GFXSTREAM_ERROR("Failed to restore: renderer not available.");
        return -EINVAL;
    }
    mRenderer->resumeAll();

    GFXSTREAM_DEBUG("directory:%s - done!", directory);
    return 0;
}

#endif  // ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

}  // namespace host
}  // namespace gfxstream
