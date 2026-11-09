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

#include "vk_memory_allocation_helper.h"

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "external_memory.h"
#include "gfxstream/Macros.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/memory/SharedMemory.h"
#include "gfxstream/memory/heap_memory.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "vk_utils.h"
#include "vulkan_gfxstream.h"

namespace gfxstream {
namespace host {
namespace vk {

constexpr uint64_t kPageSizeforBlob = 4096;

size_t VkMemoryAllocationHelper::DefaultHostPageSize() {
#ifdef PAGE_SIZE
    return static_cast<size_t>(PAGE_SIZE);
#elif defined(_WIN32)
    return 4096;
#else
    return static_cast<size_t>(getpagesize());
#endif
}

namespace {

using gfxstream::base::HeapMemory;
using gfxstream::base::SharedMemory;

std::atomic<uint64_t> sUniqueShmemId = 0;

VkResult selectImportColorBufferStrategy(uint32_t colorBufferHandle,
                                         const VkMemoryAllocationHelper& helper,
                                         const VkMemoryAllocationInputs& inputs,
                                         VkMemoryAllocationPlan* outPlan,
                                         bool* outFellBackToNormalAlloc) {
    if (!inputs.importedColorBufferInfo) {
        if (!inputs.isSnapshotLoading) {
            GFXSTREAM_WARNING("Failed to get allocation info for ColorBuffer:%d",
                              colorBufferHandle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        // During snapshot load there could be invalidated references to color buffers.
        // Fall back to a normal guest allocation as a placeholder.
        *outFellBackToNormalAlloc = true;
        return VK_SUCCESS;
    }

    *outFellBackToNormalAlloc = false;
    const auto& importedCbInfo = *inputs.importedColorBufferInfo;

    const auto hostMemoryInfoOpt =
        inputs.memoryProperties->getHostMemoryInfoFromHostMemoryTypeIndex(
            importedCbInfo.hostMemoryTypeIndex);
    if (!hostMemoryInfoOpt) {
        GFXSTREAM_ERROR("Failed to get host memory info for ColorBuffer:%d.", colorBufferHandle);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    outPlan->allocationSize = importedCbInfo.allocationSize;
    outPlan->hostMemoryInfo = *hostMemoryInfoOpt;
    outPlan->preMappedHostPtr = importedCbInfo.mappedPtr;
    outPlan->boundColorBuffer = colorBufferHandle;

    if (!importedCbInfo.dedicatedAllocation) {
        outPlan->dedicatedAllocInfo.reset();
    }

    outPlan->strategy = ImportColorBufferStrategy{
        .colorBufferHandle = colorBufferHandle,
        .mode = helper.externalMemoryMode,
        .supportsExternalMemoryImport = helper.supportsExternalMemoryImport,
        .invalidateColorBuffer = !helper.features->GuestVulkanOnly.enabled(),
    };
    return VK_SUCCESS;
}

VkResult selectImportBufferStrategy(uint32_t bufferHandle,
                                    const VkMemoryAllocationHelper& helper,
                                    const VkMemoryAllocationInputs& inputs,
                                    VkMemoryAllocationPlan* outPlan) {
    if (!inputs.importedBufferInfo) {
        GFXSTREAM_ERROR("Failed to get Buffer:%d allocation info.", bufferHandle);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    const auto& importedBufInfo = *inputs.importedBufferInfo;

    const auto hostMemoryInfoOpt =
        inputs.memoryProperties->getHostMemoryInfoFromHostMemoryTypeIndex(
            importedBufInfo.hostMemoryTypeIndex);
    if (!hostMemoryInfoOpt) {
        GFXSTREAM_ERROR("Failed to get host memory info for Buffer:%d.", bufferHandle);
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

#if defined(__QNX__)
    if (helper.externalMemoryMode == ExternalMemory::Mode::QnxScreenBuffer) {
        GFXSTREAM_ERROR(
            "%s: VK_ERROR_OUT_OF_DEVICE_MEMORY: ExternalMemory::Mode::QnxScreenBuffer does "
            "not support memory externalization for gfxstream BufferVk objects.",
            __func__);
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
#endif

    outPlan->allocationSize = importedBufInfo.allocationSize;
    outPlan->hostMemoryInfo = *hostMemoryInfoOpt;
    outPlan->boundBuffer = bufferHandle;

    if (!importedBufInfo.dedicatedAllocation) {
        outPlan->dedicatedAllocInfo.reset();
    }

    outPlan->strategy = ImportBufferStrategy{
        .bufferHandle = bufferHandle,
        .mode = helper.externalMemoryMode,
        .supportsExternalMemoryImport = helper.supportsExternalMemoryImport,
    };
    return VK_SUCCESS;
}

VkResult selectImportGuestBlobStrategy(uint64_t blobId,
                                       const VkMemoryAllocationHelper& helper,
                                       const VkMemoryAllocationInputs& inputs,
                                       VkMemoryAllocationPlan* outPlan) {
#if defined(__ANDROID__)
    (void)helper;
    outPlan->strategy = ImportGuestBlobStrategy{
        .virtioGpuContextId = inputs.virtioGpuContextId,
        .blobId = blobId,
    };
    return VK_SUCCESS;
#elif defined(__linux__)
    if (!helper.canUseDmaBuf(inputs)) {
        GFXSTREAM_ERROR("dmabuf not supported");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }
    outPlan->strategy = ImportGuestBlobStrategy{
        .virtioGpuContextId = inputs.virtioGpuContextId,
        .blobId = blobId,
    };
    return VK_SUCCESS;
#else
    (void)blobId;
    (void)helper;
    (void)inputs;
    (void)outPlan;
    GFXSTREAM_ERROR("Guest Handle flow should not work here");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
}

VkResult selectHostVisibleSharedMemoryStrategy(const VkMemoryAllocationHelper& helper,
                                               const VkMemoryAllocationInputs& inputs,
                                               VkMemoryAllocationPlan* outPlan) {
    // A system blob is imported as a host pointer, and a driver can ask for a
    // coarser alignment than a page: Apple silicon pages are 16KB.
    uint64_t blobAlignment = kPageSizeforBlob;
    if (helper.supportsExternalMemoryHostProperties) {
        blobAlignment = std::max<uint64_t>(blobAlignment, helper.minImportedHostPointerAlignment);
    }
    if ((blobAlignment & (blobAlignment - 1)) != 0 ||
        outPlan->allocationSize > std::numeric_limits<VkDeviceSize>::max() - (blobAlignment - 1)) {
        GFXSTREAM_ERROR("Allocation size overflow or invalid alignment in shared memory strategy");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    // Ensure size is page-aligned.
    VkDeviceSize alignedSize = ALIGN(outPlan->allocationSize, blobAlignment);
    if (alignedSize != outPlan->allocationSize) {
        GFXSTREAM_WARNING("Aligning allocation size from %llu to %llu",
                          static_cast<unsigned long long>(outPlan->allocationSize),
                          static_cast<unsigned long long>(alignedSize));
    }
    outPlan->allocationSize = alignedSize;

    const bool allocateAsUdmabuf = helper.features->VulkanAllocateHostVisibleAsUdmabuf.enabled();
    if (allocateAsUdmabuf) {
#if defined(__linux__)
        if (!helper.canUseDmaBuf(inputs)) {
            GFXSTREAM_ERROR("dmabuf not supported");
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
#else
        (void)inputs;
        GFXSTREAM_ERROR("Import from shared memory should not work here");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
    }

    outPlan->strategy = HostVisibleSharedMemoryStrategy{
        .alignment = blobAlignment,
        .size = outPlan->allocationSize,
        .allocateAsUdmabuf = allocateAsUdmabuf,
    };
    return VK_SUCCESS;
}

VkResult selectHostVisibleExternalBlobStrategy(const VkMemoryAllocationHelper& helper,
                                               const VkMemoryAllocationInputs& inputs,
                                               VkMemoryAllocationPlan* outPlan) {
    VkExternalMemoryHandleTypeFlags handleTypes = helper.defaultExternalMemoryHandleType;
#ifdef __linux__
    // In AndroidAHB mode, the memory is exported as an AHardwareBuffer. Also requesting the
    // dma-buf handle type makes some drivers (e.g. Adreno) export a handle that is not a valid
    // AHardwareBuffer.
    if (helper.canUseDmaBuf(inputs) &&
        helper.externalMemoryMode != ExternalMemory::Mode::AndroidAHB) {
        handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    }
#else
    (void)inputs;
#endif

    outPlan->strategy = HostVisibleExternalBlobStrategy{
        .handleTypes = handleTypes,
    };
    outPlan->externalMemoryNode.pNextStruct = VkExportMemoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .handleTypes = handleTypes,
    };
    return VK_SUCCESS;
}

VkResult selectHostVisibleHeapMemoryStrategy(bool hasCreateBlobInfo,
                                             const VkMemoryAllocationHelper& helper,
                                             VkMemoryAllocationPlan* outPlan) {
    if (!helper.supportsExternalMemoryHostProperties) {
        GFXSTREAM_ERROR(
            "VK_EXT_EXTERNAL_MEMORY_HOST is not supported, cannot use VulkanAllocateHostMemory");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    VkDeviceSize alignmentSize =
        std::max<VkDeviceSize>(helper.minImportedHostPointerAlignment, sizeof(void*));
    if (hasCreateBlobInfo && alignmentSize < helper.guestPageSize) {
        // Align blob allocations to the page size
        alignmentSize = helper.guestPageSize;
    }
    if ((alignmentSize & (alignmentSize - 1)) != 0 ||
        outPlan->allocationSize > std::numeric_limits<VkDeviceSize>::max() - (alignmentSize - 1)) {
        GFXSTREAM_ERROR("Allocation size overflow or invalid alignment in heap memory strategy");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    outPlan->allocationSize = ALIGN(outPlan->allocationSize, alignmentSize);
    outPlan->strategy = HostVisibleHeapMemoryStrategy{
        .alignment = alignmentSize,
        .size = outPlan->allocationSize,
    };
    return VK_SUCCESS;
}

VkResult executeResourceImport(uint32_t handle,
                               bool isColorBuffer,
                               ExternalMemory::Mode externalMemoryMode,
                               bool supportsExternalMemoryImport,
                               VkExternalMemoryProvider* provider,
                               ExternalMemoryChainNode* outNode) {
    bool opaqueFd = true;

#if defined(__APPLE__)
    if (externalMemoryMode == ExternalMemory::Mode::Metal) {
        MTLResource_id metalHandle =
            isColorBuffer ? provider->getColorBufferMetalMemoryHandle(handle)
                          : provider->getBufferMetalMemoryHandle(handle);
        if (metalHandle == nullptr) {
            GFXSTREAM_ERROR(
                "%s: VK_ERROR_OUT_OF_DEVICE_MEMORY: %s 0x%x does not have Vulkan external memory "
                "backing",
                __func__, isColorBuffer ? "colorBuffer" : "buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->pNextStruct = VkImportMemoryMetalHandleInfoEXT{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT,
            .handle = metalHandle,
        };
        opaqueFd = false;
    }
#endif

#if defined(__QNX__)
    if (externalMemoryMode == ExternalMemory::Mode::QnxScreenBuffer) {
        if (!isColorBuffer) {
            GFXSTREAM_ERROR(
                "%s: VK_ERROR_OUT_OF_DEVICE_MEMORY: ExternalMemory::Mode::QnxScreenBuffer does "
                "not support memory externalization for gfxstream BufferVk objects.",
                __func__);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        screen_buffer_t screenBufferHandle =
            provider->getColorBufferScreenBufferQnxHandle(handle);
        if (screenBufferHandle == nullptr) {
            GFXSTREAM_ERROR(
                "%s: VK_ERROR_OUT_OF_DEVICE_MEMORY: colorBuffer 0x%x does not have Vulkan "
                "external memory backing",
                __func__, handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->pNextStruct = VkImportScreenBufferInfoQNX{
            .sType = VK_STRUCTURE_TYPE_IMPORT_SCREEN_BUFFER_INFO_QNX,
            .pNext = nullptr,
            .buffer = screenBufferHandle,
        };
        opaqueFd = false;
    }
#endif

    if (externalMemoryMode == ExternalMemory::Mode::HostAllocation) {
        void* hostPtr = isColorBuffer ? provider->getColorBufferHostPointer(handle)
                                      : provider->getBufferHostPointer(handle);
        if (hostPtr == nullptr) {
            GFXSTREAM_ERROR(
                "%s: VK_ERROR_OUT_OF_DEVICE_MEMORY: %s 0x%x does not have Vulkan external memory "
                "host pointer backing",
                __func__, isColorBuffer ? "colorBuffer" : "buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->pNextStruct = VkImportMemoryHostPointerInfoEXT{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            .pHostPointer = hostPtr,
        };
        opaqueFd = false;
    }

    if (opaqueFd && supportsExternalMemoryImport) {
#if defined(__ANDROID__)
        if (!isColorBuffer) {
            return VK_SUCCESS;
        }
#endif
        std::optional<ExternalHandleInfo> dupHandleInfo =
            isColorBuffer ? provider->dupColorBufferExtMemoryHandle(handle)
                          : provider->dupBufferExtMemoryHandle(handle);
        if (!dupHandleInfo) {
            GFXSTREAM_ERROR(
                "Failed to duplicate external memory handle/descriptor for %s object, with "
                "internal handle: %d",
                isColorBuffer ? "ColorBuffer" : "Buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
#if defined(_WIN32)
        // Wrap the dup'd handle in a ManagedDescriptor, and let it close the underlying HANDLE
        // when it goes out of scope. From the VkImportMemoryWin32HandleInfoKHR spec: Importing
        // memory object payloads from Windows handles does not transfer ownership of the handle
        // to the Vulkan implementation.
        const auto rawHandle =
            static_cast<DescriptorType>(reinterpret_cast<void*>(dupHandleInfo->handle));
        if (rawHandle == nullptr || rawHandle == INVALID_HANDLE_VALUE) {
            GFXSTREAM_ERROR(
                "Invalid duplicated external memory handle for %s object, with "
                "internal handle: %d",
                isColorBuffer ? "ColorBuffer" : "Buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->ownedDescriptor = ManagedDescriptor(rawHandle);
        outNode->pNextStruct = VkImportMemoryWin32HandleInfoKHR{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
            .handle = rawHandle,
            .name = L"",
        };
#elif defined(__ANDROID__)
        auto* ahb =
            static_cast<AHardwareBuffer*>(reinterpret_cast<void*>(dupHandleInfo->handle));
        if (ahb == nullptr) {
            GFXSTREAM_ERROR(
                "Invalid AHardwareBuffer external memory handle for %s object, with "
                "internal handle: %d",
                isColorBuffer ? "ColorBuffer" : "Buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->pNextStruct = VkImportAndroidHardwareBufferInfoANDROID{
            .sType = VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
            .pNext = nullptr,
            .buffer = ahb,
        };
#else
        const int rawFd = dupHandleInfo->getFd();
        if (rawFd < 0) {
            GFXSTREAM_ERROR(
                "Invalid duplicated external memory descriptor (%d) for %s object, with "
                "internal handle: %d",
                rawFd, isColorBuffer ? "ColorBuffer" : "Buffer", handle);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        outNode->ownedDescriptor = ManagedDescriptor(rawFd);
        outNode->pNextStruct = VkImportMemoryFdInfoKHR{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
            .fd = rawFd,
        };
#endif
    }

    return VK_SUCCESS;
}

VkResult executeImportGuestBlob(const ImportGuestBlobStrategy& strategy,
                                VkExternalMemoryProvider* provider,
                                ExternalMemoryChainNode* outNode) {
#if defined(__ANDROID__)
    (void)strategy;
    (void)provider;
    (void)outNode;
    return VK_SUCCESS;
#elif defined(__linux__)
    std::optional<DescriptorType> rawDescriptorOpt =
        provider->removeBlobDescriptor(strategy.virtioGpuContextId, strategy.blobId);
    if (!rawDescriptorOpt || *rawDescriptorOpt < 0) {
        GFXSTREAM_ERROR("Failed vkAllocateMemory: missing or invalid descriptor info.");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    outNode->ownedDescriptor = ManagedDescriptor(*rawDescriptorOpt);
    outNode->pNextStruct = VkImportMemoryFdInfoKHR{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = *rawDescriptorOpt,
    };
    return VK_SUCCESS;
#else
    (void)strategy;
    (void)provider;
    (void)outNode;
    GFXSTREAM_ERROR("Guest Handle flow should not work here");
    return VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
}

VkResult executeHostVisibleSharedMemory(
    const HostVisibleSharedMemoryStrategy& strategy, bool systemBlobEnabled,
    const EmulatedPhysicalDeviceMemoryProperties& memoryProperties,
    VkExternalMemoryProvider* provider, VkMemoryAllocationPlan* outPlan) {
    SharedMemory memory("shared-memory-vk-" + std::to_string(getpid()) + "-" +
                            std::to_string(sUniqueShmemId++),
                        strategy.size);

    if (strategy.allocateAsUdmabuf) {
        // 0755 = user read write
        int ret = memory.createNoMapping(0755);
        if (ret) {
            GFXSTREAM_ERROR("Failed to create shared memory, error: %d", ret);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

        auto descriptor = provider->createUdmabufHandle(memory);
        if (!descriptor.has_value()) {
            GFXSTREAM_ERROR("Failed to create handle from shared memory");
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }

#if defined(__linux__)
        if (*descriptor < 0) {
            GFXSTREAM_ERROR("Failed to create valid udmabuf fd (%d) from shared memory",
                            *descriptor);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        outPlan->externalMemoryNode.ownedDescriptor = ManagedDescriptor(*descriptor);
        outPlan->externalMemoryNode.pNextStruct = VkImportMemoryFdInfoKHR{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            .fd = *descriptor,
        };
#else
        GFXSTREAM_ERROR("Import from shared memory should not work here");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
#endif
    } else if (systemBlobEnabled) {
        int ret = memory.create(0600);
        if (ret) {
            GFXSTREAM_ERROR("Failed to create system-blob host-visible memory, error: %d", ret);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
        }
        void* mappedPtr = memory.get();
        VkDeviceSize mappedPtrAlignment =
            reinterpret_cast<uintptr_t>(mappedPtr) % strategy.alignment;
        if (mappedPtrAlignment != 0) {
            GFXSTREAM_WARNING(
                "Mapped shared memory pointer is not aligned to page size, alignment is: %llu",
                static_cast<unsigned long long>(mappedPtrAlignment));
        }

        VkMemoryHostPointerPropertiesEXT memoryHostPointerProperties = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
            .pNext = nullptr,
            .memoryTypeBits = 0,
        };
        VkResult propsRes =
            provider->getMemoryHostPointerProperties(mappedPtr, &memoryHostPointerProperties);
        if (propsRes != VK_SUCCESS) {
            GFXSTREAM_ERROR("Failed to get host pointer properties for SystemBlob: %d", propsRes);
            return propsRes;
        }

        VkResult typeRes = outPlan->ApplyHostPointerMemoryTypeBits(
            memoryHostPointerProperties.memoryTypeBits, memoryProperties);
        if (typeRes != VK_SUCCESS) {
            return typeRes;
        }

        outPlan->preMappedHostPtr = mappedPtr;
        outPlan->externalMemoryNode.pNextStruct = VkImportMemoryHostPointerInfoEXT{
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
            .pHostPointer = mappedPtr,
        };
    } else {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    outPlan->backing = std::move(memory);
    return VK_SUCCESS;
}

VkResult executeHostVisibleHeapMemory(
    const HostVisibleHeapMemoryStrategy& strategy,
    const EmulatedPhysicalDeviceMemoryProperties& memoryProperties,
    VkExternalMemoryProvider* provider,
    VkMemoryAllocationPlan* outPlan) {
    HeapMemory heapMemory(strategy.alignment, strategy.size);
    void* mappedPtr = heapMemory.getAddr();
    if (!mappedPtr) {
        GFXSTREAM_ERROR("Failed to allocate HeapMemory of size %llu with alignment %llu",
                        static_cast<unsigned long long>(strategy.size),
                        static_cast<unsigned long long>(strategy.alignment));
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    VkMemoryHostPointerPropertiesEXT memoryHostPointerProperties = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
        .pNext = nullptr,
        .memoryTypeBits = 0,
    };
    VkResult propsRes =
        provider->getMemoryHostPointerProperties(mappedPtr, &memoryHostPointerProperties);
    if (propsRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to get host pointer properties for HeapMemory: %d", propsRes);
        return propsRes;
    }

    VkResult typeRes = outPlan->ApplyHostPointerMemoryTypeBits(
        memoryHostPointerProperties.memoryTypeBits, memoryProperties);
    if (typeRes != VK_SUCCESS) {
        return typeRes;
    }

    outPlan->preMappedHostPtr = mappedPtr;
    outPlan->backing = std::move(heapMemory);
    outPlan->externalMemoryNode.pNextStruct = VkImportMemoryHostPointerInfoEXT{
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .pNext = nullptr,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
        .pHostPointer = mappedPtr,
    };
    return VK_SUCCESS;
}

}  // namespace

VkResult VkMemoryAllocationHelper::selectStrategy(
    const VkMemoryAllocationInputs& inputs,
    VkMemoryAllocationPlan* outPlan) const {
    const VkMemoryAllocateInfo* pAllocateInfo = inputs.pAllocateInfo;
    if (!pAllocateInfo || !outPlan || !inputs.memoryProperties || !features) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    *outPlan = VkMemoryAllocationPlan{};

    if (const auto* info = vk_find_struct<VkMemoryAllocateFlagsInfo>(pAllocateInfo)) {
        outPlan->allocFlagsInfo = vk_make_orphan_copy(*info);
    }
    if (const auto* info = vk_find_struct<VkMemoryOpaqueCaptureAddressAllocateInfo>(pAllocateInfo)) {
        outPlan->opaqueCaptureAddressInfo = vk_make_orphan_copy(*info);
    }
    if (const auto* info = vk_find_struct<VkMemoryDedicatedAllocateInfo>(pAllocateInfo)) {
        outPlan->dedicatedAllocInfo = vk_make_orphan_copy(*info);
    }

    const auto* importCbInfoPtr = vk_find_struct<VkImportColorBufferGOOGLE>(pAllocateInfo);
    const auto* importBufferInfoPtr = vk_find_struct<VkImportBufferGOOGLE>(pAllocateInfo);
    const auto* createBlobInfoPtr = vk_find_struct<VkCreateBlobGOOGLE>(pAllocateInfo);
    if (createBlobInfoPtr) {
        outPlan->blobId = createBlobInfoPtr->blobId;
    }

    // Stage 1: Resolve base allocation (ColorBuffer import, Buffer import, or guest request).
    bool fellBackFromColorBufferImport = false;
    if (importCbInfoPtr) {
        VkResult res = selectImportColorBufferStrategy(importCbInfoPtr->colorBuffer, *this,
                                                       inputs, outPlan,
                                                       &fellBackFromColorBufferImport);
        if (res != VK_SUCCESS) {
            return res;
        }
    } else if (importBufferInfoPtr) {
        VkResult res = selectImportBufferStrategy(importBufferInfoPtr->buffer, *this,
                                                  inputs, outPlan);
        if (res != VK_SUCCESS) {
            return res;
        }
    }

    if ((!importCbInfoPtr || fellBackFromColorBufferImport) && !importBufferInfoPtr) {
        outPlan->strategy = DriverAllocatedStrategy{};
        outPlan->allocationSize = pAllocateInfo->allocationSize;
        if (!usingDirectMapping()) {
            // We copy bytes 1 page at a time from the guest to the host if we are not using
            // direct mapping. This means we can end up writing over memory we did not intend.
            // E.g. swiftshader just allocated with malloc, which can have data stored between
            // allocations.
            const VkDeviceSize pageSize = static_cast<VkDeviceSize>(hostPageSize);
            if (pageSize == 0 || (pageSize & (pageSize - 1)) != 0 ||
                outPlan->allocationSize > std::numeric_limits<VkDeviceSize>::max() - pageSize) {
                GFXSTREAM_ERROR("Allocation size overflow when aligning to host page size");
                return VK_ERROR_OUT_OF_DEVICE_MEMORY;
            }
            outPlan->allocationSize += pageSize;
            outPlan->allocationSize &= ~(pageSize - 1);
        }

        const auto hostMemoryInfoOpt =
            inputs.memoryProperties->getHostMemoryInfoFromGuestMemoryTypeIndex(
                pAllocateInfo->memoryTypeIndex);
        if (!hostMemoryInfoOpt) {
            GFXSTREAM_ERROR("Failed to get host memory info.");
            return VK_ERROR_INCOMPATIBLE_DRIVER;
        }
        outPlan->hostMemoryInfo = *hostMemoryInfoOpt;
    }

    // Stage 2: Host-visible memory emulation strategies (only when not importing a
    // ColorBuffer/Buffer).
    if (outPlan->kind() == MemoryAllocationKind::kDriverAllocated &&
        !fellBackFromColorBufferImport && outPlan->isHostVisible()) {
        VkResult hostVisRes = VK_SUCCESS;
        if (createBlobInfoPtr && createBlobInfoPtr->blobMem == STREAM_BLOB_MEM_GUEST &&
            (createBlobInfoPtr->blobFlags & STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE)) {
            hostVisRes =
                selectImportGuestBlobStrategy(createBlobInfoPtr->blobId, *this, inputs, outPlan);
        } else if (features->SystemBlob.enabled() ||
                   features->VulkanAllocateHostVisibleAsUdmabuf.enabled()) {
            hostVisRes = selectHostVisibleSharedMemoryStrategy(*this, inputs, outPlan);
        } else if (features->ExternalBlob.enabled()) {
            hostVisRes = selectHostVisibleExternalBlobStrategy(*this, inputs, outPlan);
        } else if (features->VulkanAllocateHostMemory.enabled() &&
                   !outPlan->hasChainedAllocateStructs()) {
            hostVisRes = selectHostVisibleHeapMemoryStrategy(
                /*hasCreateBlobInfo=*/createBlobInfoPtr != nullptr, *this, outPlan);
        }
        if (hostVisRes != VK_SUCCESS) {
            return hostVisRes;
        }
    }

    bool skipHostMapForAndroidAhb = false;
    if (const auto* importCb = std::get_if<ImportColorBufferStrategy>(&outPlan->strategy)) {
#if defined(__ANDROID__)
        skipHostMapForAndroidAhb =
            importCb->mode != ExternalMemory::Mode::HostAllocation &&
            importCb->supportsExternalMemoryImport;
#else
        skipHostMapForAndroidAhb =
            importCb->mode == ExternalMemory::Mode::AndroidAHB &&
            importCb->supportsExternalMemoryImport;
#endif
    }

    outPlan->shouldMapAfterAlloc =
        outPlan->isHostVisible() &&
        (!features->ExternalBlob.enabled() || inputs.deviceUsesAstcCpuDecompression) &&
        !outPlan->blobId.has_value() && !skipHostMapForAndroidAhb;

    return VK_SUCCESS;
}

VkResult VkMemoryAllocationPlan::ApplyHostPointerMemoryTypeBits(
    uint32_t memoryTypeBits,
    const EmulatedPhysicalDeviceMemoryProperties& memoryProperties) {
    if (memoryTypeBits == 0) {
        GFXSTREAM_ERROR("Cannot find suitable memory type for VulkanAllocateHostMemory");
        return VK_ERROR_INCOMPATIBLE_DRIVER;
    }

    const bool currentIndexSupported =
        (hostMemoryInfo.index < 32) && (((1u << hostMemoryInfo.index) & memoryTypeBits) != 0) &&
        isHostVisible();
    if (!currentIndexSupported) {
        // Look for the first available supported host-visible memory index and update the full
        // HostMemoryInfo so that both index and propertyFlags (and thus caching) remain
        // synchronized.
        std::optional<EmulatedPhysicalDeviceMemoryProperties::HostMemoryInfo>
            fallbackHostMemoryInfoOpt;
        for (uint32_t i = 0; i <= 31; ++i) {
            if ((memoryTypeBits & (1u << i)) == 0) {
                continue;
            }
            auto candidateOpt = memoryProperties.getHostMemoryInfoFromHostMemoryTypeIndex(i);
            if (!candidateOpt) {
                continue;
            }
            if ((candidateOpt->memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) !=
                0) {
                fallbackHostMemoryInfoOpt = candidateOpt;
                break;
            }
        }
        if (!fallbackHostMemoryInfoOpt) {
            GFXSTREAM_ERROR(
                "Failed to get host memory info for fallback host memory type in "
                "VulkanAllocateHostMemory");
            return VK_ERROR_INCOMPATIBLE_DRIVER;
        }
        hostMemoryInfo = *fallbackHostMemoryInfoOpt;
        GFXSTREAM_DEBUG(
            "Detected memoryTypeIndex violation on requested host memory import. Switching to a "
            "supported memory index %d",
            hostMemoryInfo.index);
    }

    return VK_SUCCESS;
}

VkResult VkMemoryAllocationHelper::executeStrategy(
    const VkMemoryAllocationInputs& inputs,
    VkExternalMemoryProvider* provider,
    VkMemoryAllocationPlan* inOutPlan) const {
    if (!provider || !inOutPlan || !inputs.memoryProperties || !features) {
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    return std::visit(
        [&](const auto& strat) -> VkResult {
            using T = std::decay_t<decltype(strat)>;
            if constexpr (std::is_same_v<T, DriverAllocatedStrategy> ||
                          std::is_same_v<T, HostVisibleExternalBlobStrategy>) {
                return VK_SUCCESS;
            } else if constexpr (std::is_same_v<T, ImportColorBufferStrategy>) {
                return executeResourceImport(strat.colorBufferHandle, /*isColorBuffer=*/true,
                                             strat.mode, strat.supportsExternalMemoryImport,
                                             provider, &inOutPlan->externalMemoryNode);
            } else if constexpr (std::is_same_v<T, ImportBufferStrategy>) {
                return executeResourceImport(strat.bufferHandle, /*isColorBuffer=*/false,
                                             strat.mode, strat.supportsExternalMemoryImport,
                                             provider, &inOutPlan->externalMemoryNode);
            } else if constexpr (std::is_same_v<T, ImportGuestBlobStrategy>) {
                return executeImportGuestBlob(strat, provider, &inOutPlan->externalMemoryNode);
            } else if constexpr (std::is_same_v<T, HostVisibleSharedMemoryStrategy>) {
                return executeHostVisibleSharedMemory(strat, features->SystemBlob.enabled(),
                                                      *inputs.memoryProperties, provider,
                                                      inOutPlan);
            } else if constexpr (std::is_same_v<T, HostVisibleHeapMemoryStrategy>) {
                return executeHostVisibleHeapMemory(
                    strat, *inputs.memoryProperties, provider, inOutPlan);
            }
        },
        inOutPlan->strategy);
}

MemoryAllocationKind VkMemoryAllocationPlan::kind() const {
    return std::visit(
        [](const auto& strat) -> MemoryAllocationKind {
            using T = std::decay_t<decltype(strat)>;
            if constexpr (std::is_same_v<T, DriverAllocatedStrategy>) {
                return MemoryAllocationKind::kDriverAllocated;
            } else if constexpr (std::is_same_v<T, ImportColorBufferStrategy>) {
                return MemoryAllocationKind::kImportColorBuffer;
            } else if constexpr (std::is_same_v<T, ImportBufferStrategy>) {
                return MemoryAllocationKind::kImportBuffer;
            } else if constexpr (std::is_same_v<T, ImportGuestBlobStrategy>) {
                return MemoryAllocationKind::kImportGuestBlobHandle;
            } else if constexpr (std::is_same_v<T, HostVisibleSharedMemoryStrategy>) {
                return MemoryAllocationKind::kHostVisibleSharedMemory;
            } else if constexpr (std::is_same_v<T, HostVisibleExternalBlobStrategy>) {
                return MemoryAllocationKind::kHostVisibleExternalBlob;
            } else if constexpr (std::is_same_v<T, HostVisibleHeapMemoryStrategy>) {
                return MemoryAllocationKind::kHostVisibleHeapMemory;
            }
        },
        strategy);
}

bool VkMemoryAllocationPlan::isHostVisible() const {
    return (hostMemoryInfo.memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;
}

uint32_t VkMemoryAllocationPlan::computeCaching() const {
    const VkMemoryPropertyFlags flags = hostMemoryInfo.memoryType.propertyFlags;
    if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
        return MAP_CACHE_CACHED;
    }
    if (flags & VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD) {
        return MAP_CACHE_UNCACHED;
    }
    if (flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
        return MAP_CACHE_WC;
    }
    return MAP_CACHE_NONE;
}

bool VkMemoryAllocationPlan::hasChainedAllocateStructs() const {
    return allocFlagsInfo.has_value() || opaqueCaptureAddressInfo.has_value() ||
            dedicatedAllocInfo.has_value() ||
           !std::holds_alternative<std::monostate>(externalMemoryNode.pNextStruct);
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
