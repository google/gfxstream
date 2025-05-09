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
#include <optional>
#include <unordered_set>

#include "gfxstream/host/external_object_manager.h"
#include "VirtioGpu.h"
#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "VirtioGpuResourceSnapshot.pb.h"
#endif  // GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
#include "VirtioGpuPipe.h"
#include "VirtioGpuRingBlob.h"
#include "gfxstream/host/Features.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer-unstable.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"

namespace gfxstream {
namespace host {

// LINT.IfChange(virtio_gpu_resource_type)
enum class VirtioGpuResourceType {
    UNKNOWN = 0,
    // Used as a communication channel between the guest and the host
    // which does not need an allocation on the host GPU.
    PIPE = 1,
    // Used as a GPU data buffer.
    BUFFER = 2,
    // Used as a GPU texture.
    COLOR_BUFFER = 3,
    // Used as a blob and not known to FrameBuffer.
    BLOB = 4,
};
// LINT.ThenChange(VirtioGpuResourceSnapshot.proto:virtio_gpu_resource_type)

class VirtioGpuResource {
   public:
    VirtioGpuResource() {}

    static std::optional<VirtioGpuResource> Create(
        const struct stream_renderer_resource_create_args* args, struct iovec* iov,
        uint32_t num_iovs);

    static std::optional<VirtioGpuResource> Create(
        uint32_t res_handle, const struct stream_renderer_handle* import_handle,
        const struct stream_renderer_import_data* import_data);

    static std::optional<VirtioGpuResource> Create(
        const gfxstream::host::FeatureSet& features, uint32_t pageSize, uint32_t contextId,
        uint32_t resourceId, const struct stream_renderer_resource_create_args* createArgs,
        const struct stream_renderer_create_blob* createBlobArgs,
        const struct stream_renderer_handle* handle);

    int Destroy();

    int ImportHandle(const stream_renderer_handle* handle,
                     const stream_renderer_import_data* import_data);

    VirtioGpuResourceId GetId() const { return mId; }

    void AttachIov(struct iovec* iov, uint32_t num_iovs);
    void DetachIov();

    void AttachToContext(VirtioGpuContextId contextId);
    void DetachFromContext(VirtioGpuContextId contextId);
    std::unordered_set<VirtioGpuContextId> GetAttachedContexts() const;

    int Map(void** outAddress, uint64_t* outSize);

    int GetInfo(struct stream_renderer_resource_info* outInfo) const;

    int GetVulkanInfo(struct stream_renderer_vulkan_info* outInfo) const;

    int GetCaching(uint32_t* outHvaCaching) const;

    void SetHostPipe(std::shared_ptr<VirtioGpuPipe> pipe) { mHostPipe = pipe; }

    // Corresponds to Virtio GPU "TransferFromHost" commands and VMM requests to
    // copy into display buffers.
    int TransferRead(uint64_t offset, stream_renderer_box* box,
                     std::optional<std::vector<struct iovec>> iovs = std::nullopt);

    // Corresponds to Virtio GPU "TransferToHost" commands.
    int TransferWrite(uint64_t offset, stream_renderer_box* box,
                      std::optional<std::vector<struct iovec>> iovs = std::nullopt);

    int ExportBlob(struct stream_renderer_handle* outHandle);

    std::shared_ptr<RingBlob> ShareRingBlob();

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT
    std::optional<gfxstream::host::snapshot::VirtioGpuResourceSnapshot> Snapshot() const;

    static std::optional<VirtioGpuResource> Restore(
        const gfxstream::host::snapshot::VirtioGpuResourceSnapshot& snapshot);
#endif

   private:
    int ReadFromPipeToLinear(uint64_t offset, stream_renderer_box* box);
    int WriteToPipeFromLinear(uint64_t offset, stream_renderer_box* box);

    int ReadFromBufferToLinear(uint64_t offset, stream_renderer_box* box);
    int WriteToBufferFromLinear(uint64_t offset, stream_renderer_box* box);

    int ReadFromColorBufferToLinear(uint64_t offset, stream_renderer_box* box);
    int WriteToColorBufferFromLinear(uint64_t offset, stream_renderer_box* box);

    // If `iovs` provided, copy from this resource's linear buffer to the given `iovs`.
    // Otherwise, copy from this resource's linear buffer into its previously attached
    // iovs.
    int TransferToIov(uint64_t offset, const stream_renderer_box* box,
                      std::optional<std::vector<struct iovec>> iovs = std::nullopt);

    // If `iovs` provided, copy from the given `iovs` to this resources linear buffer.
    // Otherwise, copy from this resource's previously attached iovs into its linear
    // buffer.
    int TransferFromIov(uint64_t offset, const stream_renderer_box* box,
                        std::optional<std::vector<struct iovec>> iovs = std::nullopt);

    enum TransferDirection {
        IOV_TO_LINEAR = 0,
        LINEAR_TO_IOV = 1,
    };
    int TransferWithIov(uint64_t offset, const stream_renderer_box* box,
                        const std::vector<struct iovec>& iovs, TransferDirection direction);

    // LINT.IfChange(virtio_gpu_resource)
    VirtioGpuResourceId mId = -1;
    VirtioGpuResourceType mResourceType = VirtioGpuResourceType::UNKNOWN;
    std::optional<struct stream_renderer_resource_create_args> mCreateArgs;
    std::optional<struct stream_renderer_create_blob> mCreateBlobArgs;
    std::vector<struct iovec> mIovs;
    std::vector<char> mLinear;
    std::shared_ptr<VirtioGpuPipe> mHostPipe; // Snapshot/restore handled by context
    std::optional<VirtioGpuContextId> mLatestAttachedContext;
    std::unordered_set<VirtioGpuContextId> mAttachedToContexts;

    // If this resource is a blob resource, the source of the external memory.
    //
    //   * For ring blobs, blobs that are used soley for guest and host
    //     communication, the external memory is allocated by this resource
    //     in the frontend.
    //
    //   * For non ring blobs, the memory from the backend as either an external
    //     memory handle (`BlobDescriptorInfo`) or a raw mapping.
    using RingBlobMemory = std::shared_ptr<RingBlob>;
    using ExternalMemoryInfo = std::shared_ptr<BlobDescriptorInfo>;
    using ExternalMemoryMapping = HostMemInfo;

    using BlobMemory = std::variant<RingBlobMemory, ExternalMemoryInfo, ExternalMemoryMapping>;
    std::optional<BlobMemory> mBlobMemory;
    // LINT.ThenChange(VirtioGpuResourceSnapshot.proto:virtio_gpu_resource)
};

}  // namespace host
}  // namespace gfxstream