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

#pragma once

#include <stdint.h>

#include <memory>
#include <unordered_map>

extern "C" {
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "host-common/goldfish_pipe.h"
}  // extern "C"

#include "VirtioGpu.h"
#include "VirtioGpuContext.h"
#include "VirtioGpuFormatUtils.h"
#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "VirtioGpuFrontendSnapshot.pb.h"
#endif
#include "VirtioGpuResource.h"
#include "VirtioGpuTimelines.h"
#include "gfxstream/host/Features.h"
#include "host-common/address_space_device.h"

namespace gfxstream {
namespace host {

class CleanupThread;

class VirtioGpuFrontend {
   public:
    VirtioGpuFrontend();

    int init(void* cookie, const gfxstream::host::FeatureSet& features,
             stream_renderer_fence_callback fence_callback);

    void teardown();

    int createContext(VirtioGpuContextId ctx_id, uint32_t nlen, const char* name,
                      uint32_t context_init);

    int destroyContext(VirtioGpuContextId handle);

    int addressSpaceProcessCmd(VirtioGpuContextId ctxId, uint32_t* dwords);

    int submitCmd(struct stream_renderer_command* cmd);

    int createFence(uint64_t fence_id, const VirtioGpuRing& ring);

    int acquireContextFence(uint32_t ctx_id, uint64_t fenceId);

    void poll();

    int createResource(struct stream_renderer_resource_create_args* args, struct iovec* iov,
                       uint32_t num_iovs);
    int importResource(uint32_t res_handle, const struct stream_renderer_handle* import_handle,
                       const struct stream_renderer_import_data* import_data);
    void unrefResource(uint32_t toUnrefId);

    int attachIov(int resId, iovec* iov, int num_iovs);
    void detachIov(int resId);

    int transferReadIov(int resId, uint64_t offset, stream_renderer_box* box, struct iovec* iov,
                        int iovec_cnt);
    int transferWriteIov(int resId, uint64_t offset, stream_renderer_box* box, struct iovec* iov,
                         int iovec_cnt);

    void getCapset(uint32_t set, uint32_t* max_size);
    void fillCaps(uint32_t set, void* caps);

    void attachResource(uint32_t ctxId, uint32_t resId);
    void detachResource(uint32_t ctxId, uint32_t toUnrefId);

    int getResourceInfo(uint32_t resId, struct stream_renderer_resource_info* info);

    void flushResource(uint32_t res_handle);

    int createRingBlob(VirtioGpuResource& entry, uint32_t res_handle,
                       const struct stream_renderer_create_blob* create_blob,
                       const struct stream_renderer_handle* handle);

    int createBlob(uint32_t ctx_id, uint32_t res_handle,
                   const struct stream_renderer_create_blob* create_blob,
                   const struct stream_renderer_handle* handle);

    int resourceMap(uint32_t resourceId, void** hvaOut, uint64_t* sizeOut);
    int resourceUnmap(uint32_t res_handle);

    void* platformCreateSharedEglContext();

    int platformDestroySharedEglContext(void* context);

    int resourceMapInfo(uint32_t resourceId, uint32_t* map_info);

    int exportBlob(uint32_t resourceId, struct stream_renderer_handle* handle);

    int exportFence(uint64_t fenceId, struct stream_renderer_handle* handle);
    int vulkanInfo(uint32_t res_handle, struct stream_renderer_vulkan_info* vulkan_info);

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    int snapshot(const char* directory);
    int restore(const char* directory);
#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

#ifdef CONFIG_AEMU
    void setServiceOps(const GoldfishPipeServiceOps* ops);
#endif  // CONFIG_AEMU

   private:
    VirtioGpuTimelines::FenceCompletionCallback getFenceCompletionCallback();

    int destroyVirtioGpuObjects();

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    int snapshotRenderer(const char* directory);
    int snapshotFrontend(const char* directory);
    int snapshotAsg(const char* directory);

    int restoreRenderer(const char* directory);
    int restoreFrontend(const char* directory);
    int restoreAsg(const char* directory);
#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

    int resetPipe(VirtioGpuContextId contextId, GoldfishHostPipe* hostPipe);

    const GoldfishPipeServiceOps* ensureAndGetServiceOps();

    void* mCookie = nullptr;
    gfxstream::host::FeatureSet mFeatures;
    stream_renderer_fence_callback mFenceCallback;
    uint32_t mPageSize = 4096;
    struct ::address_space_device_control_ops* mAddressSpaceDeviceControlOps = nullptr;

    const GoldfishPipeServiceOps* mServiceOps = nullptr;

    // State that is preserved across snapshots:
    //
    // LINT.IfChange(virtio_gpu_frontend)
    std::unordered_map<VirtioGpuContextId, VirtioGpuContext> mContexts;
    std::unordered_map<VirtioGpuResourceId, VirtioGpuResource> mResources;
    std::unordered_map<uint64_t, std::shared_ptr<SyncDescriptorInfo>> mSyncMap;
    // When we wait for gpu or wait for gpu vulkan, the next (and subsequent)
    // fences created for that context should not be signaled immediately.
    // Rather, they should get in line.
    std::unique_ptr<VirtioGpuTimelines> mVirtioGpuTimelines = nullptr;
    // LINT.ThenChange(VirtioGpuFrontend.h:virtio_gpu_frontend)

    std::unique_ptr<CleanupThread> mCleanupThread;
};

}  // namespace host
}  // namespace gfxstream
