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

#include "VirtioGpuContext.h"

#include "gfxstream/host/logging.h"
#include "host-common/AddressSpaceService.h"

namespace gfxstream {
namespace host {

/*static*/
std::optional<VirtioGpuContext> VirtioGpuContext::Create(RendererPtr renderer,
                                                         VirtioGpuContextId contextId,
                                                         const std::string& contextName,
                                                         uint32_t capsetId) {
    VirtioGpuContext context = {};
    context.mRenderer = renderer;
    context.mId = contextId;
    context.mName = contextName;
    context.mCapsetId = capsetId;
    context.mHostPipe = std::make_shared<VirtioGpuPipe>(renderer, contextId);

    renderer->onGuestGraphicsProcessCreate(contextId);

    return context;
}

int VirtioGpuContext::Destroy(const struct address_space_device_control_ops* asgOps) {
    for (const auto& [_, handle] : mAddressSpaceHandles) {
        // Note: this can hang as is but this has only been observed to
        // happen during shutdown. See b/329287602#comment8.
        asgOps->destroy_handle(handle);
    }

    mRenderer->cleanupProcGLObjects(mId);

    return 0;
}

void VirtioGpuContext::AttachResource(VirtioGpuResource& resource) {
    // Associate the host pipe of the resource entry with the host pipe of
    // the context entry.  That is, the last context to call attachResource
    // wins if there is any conflict.
    resource.AttachToContext(mId);
    resource.SetHostPipe(mHostPipe);

    mAttachedResources.insert(resource.GetId());
}

void VirtioGpuContext::DetachResource(VirtioGpuResource& resource) {
    mAttachedResources.erase(resource.GetId());
    resource.DetachFromContext(mId);
}

const std::unordered_set<VirtioGpuResourceId>& VirtioGpuContext::GetAttachedResources() const {
    return mAttachedResources;
}

void VirtioGpuContext::SetHostPipe(std::shared_ptr<VirtioGpuPipe> pipe) { mHostPipe = pipe; }

int VirtioGpuContext::AcquireSync(uint64_t syncId) {
    if (mLatestSync) {
        GFXSTREAM_ERROR("failed to acquire sync %" PRIu64 " on context %u: sync already present?",
                        syncId, mId);
        return -EINVAL;
    }

    auto descriptorOpt = ExternalObjectManager::get()->removeSyncDescriptorInfo(mId, syncId);
    if (!descriptorOpt) {
        GFXSTREAM_ERROR("failed to acquire sync %" PRIu64 " on context %u: sync not found.", syncId,
                        mId);
        return -EINVAL;
    }

    mLatestSync = std::move(*descriptorOpt);
    return 0;
}

std::optional<SyncDescriptorInfo> VirtioGpuContext::TakeSync() {
    if (!mLatestSync) {
        return std::nullopt;
    }

    auto info = std::move(*mLatestSync);
    mLatestSync.reset();
    return std::move(info);
}

int VirtioGpuContext::CreateAddressSpaceGraphicsInstance(
    const struct address_space_device_control_ops* asgOps, VirtioGpuResource& resource) {
    const VirtioGpuResourceId resourceId = resource.GetId();

    void* resourceHva = nullptr;
    uint64_t resourceHvaSize = 0;
    if (resource.Map(&resourceHva, &resourceHvaSize) != 0) {
        GFXSTREAM_ERROR("failed to create ASG instance on context %d: failed to map resource %u",
                        mId, resourceId);
        return -EINVAL;
    }

    const std::string asgName = mName + "-" + std::to_string(resourceId);

    // Note: resource ids can not be used as ASG handles because ASGs may outlive the
    // containing resource due asynchronous ASG destruction.
    const uint32_t asgId = asgOps->gen_handle();

    struct AddressSpaceCreateInfo createInfo = {
        .handle = asgId,
        .type = android::emulation::VirtioGpuGraphics,
        .createRenderThread = true,
        .externalAddr = resourceHva,
        .externalAddrSize = resourceHvaSize,
        .virtioGpuContextId = mId,
        .virtioGpuCapsetId = mCapsetId,
        .contextName = asgName.c_str(),
        .contextNameSize = static_cast<uint32_t>(asgName.size()),
    };
    asgOps->create_instance(createInfo);

    mAddressSpaceHandles[resourceId] = asgId;
    return 0;
}

const std::unordered_map<VirtioGpuResourceId, uint32_t>& VirtioGpuContext::AsgInstances() const {
    return mAddressSpaceHandles;
}

std::optional<uint32_t> VirtioGpuContext::TakeAddressSpaceGraphicsHandle(
    VirtioGpuResourceId resourceId) {
    auto asgIt = mAddressSpaceHandles.find(resourceId);
    if (asgIt == mAddressSpaceHandles.end()) {
        return std::nullopt;
    }

    auto asgId = asgIt->second;
    mAddressSpaceHandles.erase(asgIt);
    return asgId;
}

int VirtioGpuContext::PingAddressSpaceGraphicsInstance(
    const struct address_space_device_control_ops* asgOps, VirtioGpuResourceId resourceId) {
    auto asgIt = mAddressSpaceHandles.find(resourceId);
    if (asgIt == mAddressSpaceHandles.end()) {
        GFXSTREAM_ERROR("failed to ping ASG instance on context %u resource %d: ASG not found.",
                        mId, resourceId);
        return -EINVAL;
    }
    auto asgId = asgIt->second;

    struct android::emulation::AddressSpaceDevicePingInfo ping = {0};
    ping.metadata = ASG_NOTIFY_AVAILABLE;
    asgOps->ping_at_hva(asgId, &ping);

    return 0;
}

int VirtioGpuContext::AddPendingBlob(uint32_t blobId,
                                     struct stream_renderer_resource_create_args blobArgs) {
    auto [_, inserted] = mPendingBlobs.try_emplace(blobId, blobArgs);
    if (!inserted) {
        GFXSTREAM_ERROR("failed to add pending blob %u to context %u: blob ID already in use?",
                        blobId, mId);
        return -EINVAL;
    }
    return 0;
}

std::optional<struct stream_renderer_resource_create_args> VirtioGpuContext::TakePendingBlob(
    uint32_t blobId) {
    auto it = mPendingBlobs.find(blobId);
    if (it == mPendingBlobs.end()) {
        return std::nullopt;
    }

    auto args = it->second;
    mPendingBlobs.erase(it);
    return args;
}

#ifdef GFXSTREAM_BUILD_WITH_SNAPSHOT_FRONTEND_SUPPORT

using gfxstream::host::snapshot::VirtioGpuContextSnapshot;

std::optional<VirtioGpuContextSnapshot> VirtioGpuContext::Snapshot() const {
    VirtioGpuContextSnapshot contextSnapshot;
    contextSnapshot.set_id(mId);
    contextSnapshot.set_name(mName);
    contextSnapshot.set_capset(mCapsetId);
    contextSnapshot.mutable_attached_resources()->Add(mAttachedResources.begin(),
                                                      mAttachedResources.end());
    contextSnapshot.mutable_resource_asgs()->insert(mAddressSpaceHandles.begin(),
                                                    mAddressSpaceHandles.end());
    // TODO(b/369615058): Handle mHostPipe.
    return contextSnapshot;
}

/*static*/
std::optional<VirtioGpuContext> VirtioGpuContext::Restore(
    RendererPtr renderer,
    const VirtioGpuContextSnapshot& contextSnapshot) {
    VirtioGpuContext context = {};
    context.mRenderer = renderer;
    context.mId = contextSnapshot.id();
    context.mName = contextSnapshot.name();
    context.mCapsetId = contextSnapshot.capset();
    context.mAttachedResources.insert(contextSnapshot.attached_resources().begin(),
                                      contextSnapshot.attached_resources().end());
    context.mAddressSpaceHandles.insert(contextSnapshot.resource_asgs().begin(),
                                        contextSnapshot.resource_asgs().end());
    // TODO(b/369615058): Handle mHostPipe.
    return context;
}

#endif

}  // namespace host
}  // namespace gfxstream
