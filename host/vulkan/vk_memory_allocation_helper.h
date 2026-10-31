// Copyright (C) 2026 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#endif

#ifdef __APPLE__
#include <vulkan/vulkan_beta.h>
typedef void* MTLResource_id;
#endif

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "external_memory.h"
#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/features.h"
#include "gfxstream/host/handle.h"
#include "gfxstream/memory/SharedMemory.h"
#include "gfxstream/memory/heap_memory.h"
#include "vk_emulated_physical_device_memory.h"
#include "vk_utils.h"

namespace gfxstream {
namespace host {
namespace vk {

// Describes the mutually exclusive strategy selected for a vkAllocateMemory call.
enum class MemoryAllocationKind {
    // Standard driver-allocated memory (device-local or non-emulated host-visible).
    kDriverAllocated,
    // Importing backing memory from a host ColorBuffer (VkImportColorBufferGOOGLE).
    kImportColorBuffer,
    // Importing backing memory from a host Buffer (VkImportBufferGOOGLE).
    kImportBuffer,
    // Importing a guest-created dmabuf handle (STREAM_BLOB_MEM_GUEST).
    kImportGuestBlobHandle,
    // Allocating host SharedMemory and importing via host pointer (SystemBlob) or udmabuf.
    kHostVisibleSharedMemory,
    // Driver-allocated exportable memory (ExternalBlob via VkExportMemoryAllocateInfo).
    kHostVisibleExternalBlob,
    // Allocating aligned HeapMemory and importing via VK_EXT_external_memory_host.
    kHostVisibleHeapMemory,
};

// Concrete declarative strategy descriptors produced by Phase 1 (SelectStrategy) and consumed
// by Phase 2 (ExecuteStrategy).
struct DriverAllocatedStrategy {};

struct ImportColorBufferStrategy {
    uint32_t colorBufferHandle = 0;
    ExternalMemory::Mode mode = ExternalMemory::Mode::NotSupported;
    bool supportsExternalMemoryImport = false;
    bool invalidateColorBuffer = false;
};

struct ImportBufferStrategy {
    uint32_t bufferHandle = 0;
    ExternalMemory::Mode mode = ExternalMemory::Mode::NotSupported;
    bool supportsExternalMemoryImport = false;
};

struct ImportGuestBlobStrategy {
    uint32_t virtioGpuContextId = 0;
    uint64_t blobId = 0;
};

struct HostVisibleSharedMemoryStrategy {
    VkDeviceSize alignment = 4096;
    VkDeviceSize size = 0;
    bool allocateAsUdmabuf = false;
};

struct HostVisibleExternalBlobStrategy {
    VkExternalMemoryHandleTypeFlags handleTypes = 0;
};

struct HostVisibleHeapMemoryStrategy {
    VkDeviceSize alignment = 4096;
    VkDeviceSize size = 0;
};

using MemoryAllocationStrategy = std::variant<
    DriverAllocatedStrategy,
    ImportColorBufferStrategy,
    ImportBufferStrategy,
    ImportGuestBlobStrategy,
    HostVisibleSharedMemoryStrategy,
    HostVisibleExternalBlobStrategy,
    HostVisibleHeapMemoryStrategy>;

using HostMemoryBacking = std::variant<
    std::monostate,
    gfxstream::base::SharedMemory,
    gfxstream::base::HeapMemory>;

// Owns the optional external memory import/export pNext node and any OS descriptor whose lifetime
// must span the vkAllocateMemory call (e.g. Win32 NT handles, or POSIX file descriptors until the
// driver takes ownership after a successful import).
struct ExternalMemoryChainNode {
    ManagedDescriptor ownedDescriptor;
    std::variant<
        std::monostate,
        VkExportMemoryAllocateInfo,
        VkImportMemoryHostPointerInfoEXT
#if defined(_WIN32)
        , VkImportMemoryWin32HandleInfoKHR
#else
        , VkImportMemoryFdInfoKHR
#if defined(__APPLE__)
        , VkImportMemoryMetalHandleInfoEXT
#elif defined(__QNX__)
        , VkImportScreenBufferInfoQNX
#elif defined(__ANDROID__)
        , VkImportAndroidHardwareBufferInfoANDROID
#endif
#endif
    > pNextStruct;
};

struct ImportedColorBufferAllocationInfo {
    VkDeviceSize allocationSize = 0;
    uint32_t hostMemoryTypeIndex = 0;
    bool dedicatedAllocation = false;
    void* mappedPtr = nullptr;
};

struct ImportedBufferAllocationInfo {
    VkDeviceSize allocationSize = 0;
    uint32_t hostMemoryTypeIndex = 0;
    bool dedicatedAllocation = false;
};

// Per-vkAllocateMemory() call inputs gathered from the target VkDevice/VkPhysicalDevice,
// dynamic decoder state, and any imported host ColorBuffer/Buffer metadata.
struct VkMemoryAllocationInputs {
    const VkMemoryAllocateInfo* pAllocateInfo = nullptr;
    const EmulatedPhysicalDeviceMemoryProperties* memoryProperties = nullptr;
    uint32_t virtioGpuContextId = 0;
    bool deviceHasDmaBufExt = false;
    bool deviceUsesAstcCpuDecompression = false;
    bool isSnapshotLoading = false;
    std::optional<ImportedColorBufferAllocationInfo> importedColorBufferInfo;
    std::optional<ImportedBufferAllocationInfo> importedBufferInfo;
};

// Narrow collaborator seam used only during Phase 2 (executeStrategy) to materialize OS handles,
// shared/heap memory, and host pointer properties after a strategy has been selected.
class VkExternalMemoryProvider {
   public:
    virtual ~VkExternalMemoryProvider() = default;

    virtual std::optional<ExternalHandleInfo> dupColorBufferExtMemoryHandle(
        uint32_t colorBufferHandle) = 0;
    virtual void* getColorBufferHostPointer(uint32_t colorBufferHandle) = 0;

    virtual std::optional<ExternalHandleInfo> dupBufferExtMemoryHandle(
        uint32_t bufferHandle) = 0;
    virtual void* getBufferHostPointer(uint32_t bufferHandle) = 0;

#if defined(__APPLE__)
    virtual MTLResource_id getColorBufferMetalMemoryHandle(uint32_t colorBufferHandle) = 0;
    virtual MTLResource_id getBufferMetalMemoryHandle(uint32_t bufferHandle) = 0;
#endif
#if defined(__QNX__)
    virtual screen_buffer_t getColorBufferScreenBufferQnxHandle(uint32_t colorBufferHandle) = 0;
#endif
    virtual VkResult getMemoryHostPointerProperties(
        void* hostPtr, VkMemoryHostPointerPropertiesEXT* outProps) = 0;

    virtual std::optional<DescriptorType> removeBlobDescriptor(uint32_t virtioGpuContextId,
                                                               uint64_t blobId) = 0;

    virtual std::optional<DescriptorType> createUdmabufHandle(
        gfxstream::base::SharedMemory& memory) = 0;
};

// Value object holding the resolved host VkMemoryAllocateInfo plan, the selected allocation
// strategy, all pNext structs it chains to, and any backing host allocation that must be
// transferred into MemoryInfo upon success.
struct VkMemoryAllocationPlan {
    // Pure helper that validates `memoryTypeBits` returned by vkGetMemoryHostPointerPropertiesEXT
    // against `hostMemoryInfo.index` and falls back to the first supported HOST_VISIBLE host memory
    // type while keeping `index` and `memoryType.propertyFlags` (and thus `computeCaching()`) in
    // lockstep.
    VkResult ApplyHostPointerMemoryTypeBits(
        uint32_t memoryTypeBits,
        const EmulatedPhysicalDeviceMemoryProperties& memoryProperties);

    MemoryAllocationStrategy strategy = DriverAllocatedStrategy{};
    VkDeviceSize allocationSize = 0;

    // Single source of truth for both the host memoryTypeIndex and its VkMemoryPropertyFlags,
    // preventing index and propertyFlags from falling out of sync when a strategy switches
    // the host memory type.
    EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo hostMemoryInfo = {};

    // Sanitized pNext structs (copied by value; chained only when materializing VkMemoryAllocateInfo).
    std::optional<VkMemoryAllocateFlagsInfo> allocFlagsInfo;
    std::optional<VkMemoryOpaqueCaptureAddressAllocateInfo> opaqueCaptureAddressInfo;
    std::optional<VkMemoryDedicatedAllocateInfo> dedicatedAllocInfo;
    ExternalMemoryChainNode externalMemoryNode;

    // Host backing storage to move into MemoryInfo after vkAllocateMemory succeeds.
    HostMemoryBacking backing;
    void* preMappedHostPtr = nullptr;
    bool shouldMapAfterAlloc = false;

    std::optional<HandleType> boundColorBuffer;
    std::optional<HandleType> boundBuffer;
    std::optional<uint64_t> blobId;

    MemoryAllocationKind kind() const;
    bool isHostVisible() const;
    uint32_t computeCaching() const;
    bool hasChainedAllocateStructs() const;

    // Materializes the VkMemoryAllocateInfo and its pNext chain on the stack for the duration
    // of `fn(const VkMemoryAllocateInfo&)`.
    template <typename Fn>
    decltype(auto) WithVkMemoryAllocateInfo(Fn&& fn) const {
        VkMemoryAllocateInfo hostAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = nullptr,
            .allocationSize = allocationSize,
            .memoryTypeIndex = hostMemoryInfo.index,
        };
        vk_struct_chain_iterator chainIter = vk_make_chain_iterator(&hostAllocInfo);
        auto localAllocFlagsInfo = allocFlagsInfo;
        if (localAllocFlagsInfo) {
            localAllocFlagsInfo->pNext = nullptr;
            vk_append_struct(&chainIter, &*localAllocFlagsInfo);
        }
        auto localOpaqueCaptureAddressInfo = opaqueCaptureAddressInfo;
        if (localOpaqueCaptureAddressInfo) {
            localOpaqueCaptureAddressInfo->pNext = nullptr;
            vk_append_struct(&chainIter, &*localOpaqueCaptureAddressInfo);
        }
        auto localDedicatedAllocInfo = dedicatedAllocInfo;
        if (localDedicatedAllocInfo) {
            localDedicatedAllocInfo->pNext = nullptr;
            vk_append_struct(&chainIter, &*localDedicatedAllocInfo);
        }
        auto localExternalPNext = externalMemoryNode.pNextStruct;
        std::visit(
            [&](auto& arg) {
                using T = std::decay_t<decltype(arg)>;
                if constexpr (!std::is_same_v<T, std::monostate>) {
                    arg.pNext = nullptr;
                    vk_append_struct(&chainIter, &arg);
                }
            },
            localExternalPNext);
        return std::forward<Fn>(fn)(hostAllocInfo);
    }
};

// Host and emulation memory-allocation helper initialized once for the lifetime of
// VkDecoderGlobalState::Impl. Holds the invariant host capabilities and drives the two-phase
// memory allocation pipeline (selectStrategy + executeStrategy).
struct VkMemoryAllocationHelper {
    const gfxstream::host::FeatureSet* features = nullptr;
    ExternalMemory::Mode externalMemoryMode = ExternalMemory::Mode::NotSupported;
    VkExternalMemoryHandleTypeFlagBits defaultExternalMemoryHandleType =
        static_cast<VkExternalMemoryHandleTypeFlagBits>(0);
    bool supportsExternalMemoryImport = false;
    bool supportsExternalMemoryHostProperties = false;
    VkDeviceSize minImportedHostPointerAlignment = 4096;
    bool supportsDmaBuf = false;
    size_t hostPageSize = DefaultHostPageSize();
    size_t guestPageSize = 4096;

    static size_t DefaultHostPageSize();

    bool usingDirectMapping() const {
        return features &&
               (features->GlDirectMem.enabled() || features->VirtioGpuNext.enabled());
    }

    bool canUseDmaBuf(const VkMemoryAllocationInputs& inputs) const {
        return supportsDmaBuf && inputs.deviceHasDmaBufExt;
    }

    // Phase 1: Pure, side-effect-free strategy selection. Resolves the allocation strategy,
    // size, alignment, host memory type, pNext structs, and mapping policy purely from value
    // inputs without performing any OS syscalls or mutating global state.
    VkResult selectStrategy(const VkMemoryAllocationInputs& inputs,
                            VkMemoryAllocationPlan* outPlan) const;

    // Phase 2: Executes the selected strategy to materialize any OS handles, SharedMemory, or
    // HeapMemory backing and populate `inOutPlan->externalMemoryNode`.
    VkResult executeStrategy(const VkMemoryAllocationInputs& inputs,
                             VkExternalMemoryProvider* provider,
                             VkMemoryAllocationPlan* inOutPlan) const;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
