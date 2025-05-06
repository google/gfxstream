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
#pragma once

#include <inttypes.h>

#include <atomic>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

#include "gfxstream/Compiler.h"
#include "gfxstream/ManagedDescriptor.hpp"
#include "gfxstream/ThreadAnnotations.h"

// A global mapping from opaque host memory IDs to host virtual
// addresses/sizes.  This is so that the guest doesn't have to know the host
// virtual address to be able to map them. However, we do also provide a
// mechanism for obtaining the offsets into page for such buffers (as the guest
// does need to know those).
//
// This is currently used only in conjunction with virtio-gpu-next and Vulkan /
// address space device, though there are possible other consumers of this, so
// it becomes a global object. It exports methods into VmOperations.

using gfxstream::base::DescriptorType;
using gfxstream::base::ManagedDescriptor;

namespace gfxstream {

// Caching types
#define MAP_CACHE_MASK 0x0f
#define MAP_CACHE_NONE 0x00
#define MAP_CACHE_CACHED 0x01
#define MAP_CACHE_UNCACHED 0x02
#define MAP_CACHE_WC 0x03

#define STREAM_HANDLE_TYPE_MEM_OPAQUE_FD 0x1
#define STREAM_HANDLE_TYPE_MEM_DMABUF 0x2
#define STREAM_HANDLE_TYPE_MEM_OPAQUE_WIN32 0x3
#define STREAM_HANDLE_TYPE_MEM_SHM 0x4
#define STREAM_HANDLE_TYPE_MEM_ZIRCON 0x5

#define STREAM_HANDLE_TYPE_SIGNAL_OPAQUE_FD 0x10
#define STREAM_HANDLE_TYPE_SIGNAL_SYNC_FD 0x20
#define STREAM_HANDLE_TYPE_SIGNAL_OPAQUE_WIN32 0x30
#define STREAM_HANDLE_TYPE_SIGNAL_ZIRCON 0x40
#define STREAM_HANDLE_TYPE_SIGNAL_EVENT_FD 0x50

#define STREAM_HANDLE_TYPE_PLATFORM_SCREEN_BUFFER_QNX 0x01000000
#define STREAM_HANDLE_TYPE_PLATFORM_EGL_NATIVE_PIXMAP 0x02000000

typedef int64_t ExternalHandleType;

struct ExternalHandleInfo {
    ExternalHandleType handle;
    uint32_t streamHandleType;

#ifdef _WIN32
    ManagedDescriptor toManagedDescriptor() const {
        return ManagedDescriptor(static_cast<DescriptorType>(reinterpret_cast<void*>(handle)));
    }
#else
    ManagedDescriptor toManagedDescriptor() const {
        return ManagedDescriptor(static_cast<DescriptorType>(handle));
    }
    int getFd() const { return static_cast<int>(handle); }
    ExternalHandleType dupFd() const { return static_cast<ExternalHandleType>(dup(getFd())); }
#endif
};

// A struct describing the information about host memory associated
// with a host memory id. Used with virtio-gpu-next.
struct HostMemInfo {
    void* addr;
    uint32_t caching;
};

struct GenericDescriptorInfo {
    ManagedDescriptor descriptor;
    uint32_t streamHandleType;
};

struct VulkanInfo {
    uint32_t memoryIndex;
    uint8_t deviceUUID[16];
    uint8_t driverUUID[16];
};

struct BlobDescriptorInfo {
    GenericDescriptorInfo descriptorInfo;
    uint32_t caching;
    std::optional<VulkanInfo> vulkanInfoOpt;
};

using SyncDescriptorInfo = GenericDescriptorInfo;

class ExternalObjectManager {
   public:
    ExternalObjectManager() = default;

    static ExternalObjectManager* get();

    void addMapping(uint32_t ctx_id, uint64_t blobId, void* addr, uint32_t caching);
    std::optional<HostMemInfo> removeMapping(uint32_t ctx_id, uint64_t blobId);

    void addBlobDescriptorInfo(uint32_t ctx_id, uint64_t blobId, ManagedDescriptor descriptor,
                               uint32_t streamHandleType, uint32_t caching,
                               std::optional<VulkanInfo> vulkanInfoOpt);
    std::optional<BlobDescriptorInfo> removeBlobDescriptorInfo(uint32_t ctx_id, uint64_t blobId);

    void addSyncDescriptorInfo(uint32_t ctx_id, uint64_t syncId, ManagedDescriptor descriptor,
                               uint32_t streamHandleType);
    std::optional<SyncDescriptorInfo> removeSyncDescriptorInfo(uint32_t ctx_id, uint64_t syncId);

    void addResourceExternalHandleInfo(uint32_t resHandle,
                                       const ExternalHandleInfo& externalHandleInfo);
    std::optional<ExternalHandleInfo> removeResourceExternalHandleInfo(uint32_t resHandle);

   private:
    // Only for pairs of std::hash-able types for simplicity.
    // You can of course template this struct to allow other hash functions
    struct pair_hash {
        template <class T1, class T2>
        std::size_t operator()(const std::pair<T1, T2>& p) const {
            auto h1 = std::hash<T1>{}(p.first);
            auto h2 = std::hash<T2>{}(p.second);

            // Mainly for demonstration purposes, i.e. works but is overly simple
            // In the real world, use sth. like boost.hash_combine
            return h1 ^ h2;
        }
    };

    std::mutex mMutex;
    std::unordered_map<std::pair<uint32_t, uint64_t>, HostMemInfo, pair_hash> mHostMemInfos
        GUARDED_BY(mMutex);
    std::unordered_map<std::pair<uint32_t, uint64_t>, BlobDescriptorInfo, pair_hash>
        mBlobDescriptorInfos GUARDED_BY(mMutex);
    std::unordered_map<std::pair<uint32_t, uint64_t>, SyncDescriptorInfo, pair_hash>
        mSyncDescriptorInfos GUARDED_BY(mMutex);
    std::unordered_map<uint32_t, ExternalHandleInfo> mResourceExternalHandleInfos
        GUARDED_BY(mMutex);

    DISALLOW_COPY_ASSIGN_AND_MOVE(ExternalObjectManager);
};

}  // namespace gfxstream
