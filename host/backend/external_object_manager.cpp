// Copyright 2019 The Android Open Source Project
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
#include "gfxstream/host/external_object_manager.h"

#include <utility>

namespace gfxstream {

static ExternalObjectManager* sMapping() {
    static ExternalObjectManager* s = new ExternalObjectManager;
    return s;
}

// static
ExternalObjectManager* ExternalObjectManager::get() { return sMapping(); }

void ExternalObjectManager::addMapping(uint32_t ctxId, uint64_t blobId, void* addr,
                                       uint32_t caching) {
    struct HostMemInfo info = {
        .addr = addr,
        .caching = caching,
    };

    auto key = std::make_pair(ctxId, blobId);
    std::lock_guard<std::mutex> lock(mMutex);
    mHostMemInfos.insert(std::make_pair(key, info));
}

std::optional<HostMemInfo> ExternalObjectManager::removeMapping(uint32_t ctxId, uint64_t blobId) {
    auto key = std::make_pair(ctxId, blobId);
    std::lock_guard<std::mutex> lock(mMutex);
    auto found = mHostMemInfos.find(key);
    if (found != mHostMemInfos.end()) {
        std::optional<HostMemInfo> ret = found->second;
        mHostMemInfos.erase(found);
        return ret;
    }

    return std::nullopt;
}

void ExternalObjectManager::addBlobDescriptorInfo(uint32_t ctxId, uint64_t blobId,
                                                  ManagedDescriptor descriptor,
                                                  uint32_t streamHandleType, uint32_t caching,
                                                  std::optional<VulkanInfo> vulkanInfoOpt) {
    struct BlobDescriptorInfo info = {
        .descriptorInfo =
            {
                .descriptor = std::move(descriptor),
                .streamHandleType = streamHandleType,
            },
        .caching = caching,
        .vulkanInfoOpt = vulkanInfoOpt,
    };

    auto key = std::make_pair(ctxId, blobId);
    std::lock_guard<std::mutex> lock(mMutex);
    mBlobDescriptorInfos.insert(std::make_pair(key, std::move(info)));
}

std::optional<BlobDescriptorInfo> ExternalObjectManager::removeBlobDescriptorInfo(uint32_t ctxId,
                                                                                  uint64_t blobId) {
    auto key = std::make_pair(ctxId, blobId);
    std::lock_guard<std::mutex> lock(mMutex);
    auto found = mBlobDescriptorInfos.find(key);
    if (found != mBlobDescriptorInfos.end()) {
        std::optional<BlobDescriptorInfo> ret = std::move(found->second);
        mBlobDescriptorInfos.erase(found);
        return ret;
    }

    return std::nullopt;
}

void ExternalObjectManager::addSyncDescriptorInfo(uint32_t ctxId, uint64_t syncId,
                                                  ManagedDescriptor descriptor,
                                                  uint32_t streamHandleType) {
    SyncDescriptorInfo info = {
        .descriptor = std::move(descriptor),
        .streamHandleType = streamHandleType,
    };

    auto key = std::make_pair(ctxId, syncId);
    std::lock_guard<std::mutex> lock(mMutex);
    mSyncDescriptorInfos.insert(std::make_pair(key, std::move(info)));
}

std::optional<SyncDescriptorInfo> ExternalObjectManager::removeSyncDescriptorInfo(uint32_t ctxId,
                                                                                  uint64_t syncId) {
    auto key = std::make_pair(ctxId, syncId);
    std::lock_guard<std::mutex> lock(mMutex);
    auto found = mSyncDescriptorInfos.find(key);
    if (found != mSyncDescriptorInfos.end()) {
        std::optional<SyncDescriptorInfo> ret = std::move(found->second);
        mSyncDescriptorInfos.erase(found);
        return ret;
    }

    return std::nullopt;
}

void ExternalObjectManager::addResourceExternalHandleInfo(
    uint32_t resHandle, const ExternalHandleInfo& externalHandleInfo) {
    std::lock_guard<std::mutex> lock(mMutex);
    mResourceExternalHandleInfos.insert(std::make_pair(resHandle, externalHandleInfo));
}

std::optional<ExternalHandleInfo> ExternalObjectManager::removeResourceExternalHandleInfo(
    uint32_t resHandle) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto found = mResourceExternalHandleInfos.find(resHandle);
    if (found != mResourceExternalHandleInfos.end()) {
        std::optional<ExternalHandleInfo> ret = found->second;
        mResourceExternalHandleInfos.erase(found);
        return ret;
    }

    return std::nullopt;
}

}  // namespace gfxstream
