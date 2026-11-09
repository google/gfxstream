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

#if !defined(_WIN32)
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <vulkan/vulkan_core.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>

#include "external_memory.h"
#include "gfxstream/Macros.h"
#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/features.h"
#include "gfxstream/memory/SharedMemory.h"
#include "gfxstream/virtio-gpu-gfxstream-renderer.h"
#include "vk_emulated_physical_device_memory.h"
#include "vk_utils.h"
#include "vulkan_gfxstream.h"
#include "vulkan_gfxstream_structure_type.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

using ::testing::_;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Field;
using ::testing::IsFalse;
using ::testing::IsNull;
using ::testing::IsTrue;
using ::testing::Ne;
using ::testing::NotNull;
using ::testing::Optional;
using ::testing::Test;
using ::testing::VariantWith;

struct FakeExternalMemoryProvider : public VkExternalMemoryProvider {
    std::unordered_map<uint32_t, ExternalHandleInfo> colorBufferHandles;
    std::unordered_map<uint32_t, void*> colorBufferHostPointers;
    std::unordered_map<uint32_t, ExternalHandleInfo> bufferHandles;
    std::unordered_map<uint32_t, void*> bufferHostPointers;
#if defined(__APPLE__)
    std::unordered_map<uint32_t, MTLResource_id> colorBufferMetalHandles;
    std::unordered_map<uint32_t, MTLResource_id> bufferMetalHandles;
#endif
#if defined(__QNX__)
    std::unordered_map<uint32_t, screen_buffer_t> colorBufferQnxHandles;
#endif
    std::optional<DescriptorType> nextBlobDescriptor;
    uint32_t removeBlobDescriptorCallCount = 0;
    std::optional<DescriptorType> nextUdmabufDescriptor;
    uint32_t memoryHostPointerTypeBits = 0;
    VkResult memoryHostPointerPropertiesResult = VK_SUCCESS;

    std::optional<ExternalHandleInfo> dupColorBufferExtMemoryHandle(
        uint32_t colorBufferHandle) override {
        auto it = colorBufferHandles.find(colorBufferHandle);
        return it != colorBufferHandles.end() ? std::make_optional(it->second) : std::nullopt;
    }

    void* getColorBufferHostPointer(uint32_t colorBufferHandle) override {
        auto it = colorBufferHostPointers.find(colorBufferHandle);
        return it != colorBufferHostPointers.end() ? it->second : nullptr;
    }

    std::optional<ExternalHandleInfo> dupBufferExtMemoryHandle(uint32_t bufferHandle) override {
        auto it = bufferHandles.find(bufferHandle);
        return it != bufferHandles.end() ? std::make_optional(it->second) : std::nullopt;
    }

    void* getBufferHostPointer(uint32_t bufferHandle) override {
        auto it = bufferHostPointers.find(bufferHandle);
        return it != bufferHostPointers.end() ? it->second : nullptr;
    }

#if defined(__APPLE__)
    MTLResource_id getColorBufferMetalMemoryHandle(uint32_t colorBufferHandle) override {
        auto it = colorBufferMetalHandles.find(colorBufferHandle);
        return it != colorBufferMetalHandles.end() ? it->second : nullptr;
    }
    MTLResource_id getBufferMetalMemoryHandle(uint32_t bufferHandle) override {
        auto it = bufferMetalHandles.find(bufferHandle);
        return it != bufferMetalHandles.end() ? it->second : nullptr;
    }
#endif
#if defined(__QNX__)
    screen_buffer_t getColorBufferScreenBufferQnxHandle(uint32_t colorBufferHandle) override {
        auto it = colorBufferQnxHandles.find(colorBufferHandle);
        return it != colorBufferQnxHandles.end() ? it->second : nullptr;
    }
#endif

    std::optional<DescriptorType> removeBlobDescriptor(uint32_t /*virtioGpuContextId*/,
                                                       uint64_t /*blobId*/) override {
        ++removeBlobDescriptorCallCount;
        return nextBlobDescriptor;
    }

    std::optional<DescriptorType> createUdmabufHandle(
        gfxstream::base::SharedMemory& /*memory*/) override {
        return nextUdmabufDescriptor;
    }

    VkResult getMemoryHostPointerProperties(void* /*hostPtr*/,
                                            VkMemoryHostPointerPropertiesEXT* outProps) override {
        if (outProps != nullptr) {
            *outProps = VkMemoryHostPointerPropertiesEXT{
                .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
                .pNext = nullptr,
                .memoryTypeBits = memoryHostPointerTypeBits,
            };
        }
        return memoryHostPointerPropertiesResult;
    }
};

class VkMemoryAllocationHelperTest : public Test {
   protected:
    static constexpr uint32_t kDefaultVirtioGpuContextId = 1;

    void SetUp() override {
        mHostMemoryProperties = {
            .memoryTypeCount = 4,
            .memoryTypes =
                {
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        .heapIndex = 0,
                    },
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                        .heapIndex = 1,
                    },
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                         VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                        .heapIndex = 1,
                    },
                    {
                        .propertyFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                         VK_MEMORY_PROPERTY_DEVICE_UNCACHED_BIT_AMD,
                        .heapIndex = 1,
                    },
                },
            .memoryHeapCount = 2,
            .memoryHeaps =
                {
                    {
                        .size = 1024 * 1024 * 1024,
                        .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
                    },
                    {
                        .size = 2ULL * 1024 * 1024 * 1024,
                        .flags = 0,
                    },
                },
        };
        mFeatures.GlDirectMem.setEnabled(true);
        RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    }

    void RebuildMemoryPropertiesHelper(uint32_t hostColorBufferMemoryTypeIndex) {
        mMemoryPropertiesHelper = std::make_unique<EmulatedPhysicalDeviceMemoryProperties>(
            mHostMemoryProperties, hostColorBufferMemoryTypeIndex, mFeatures);
        mHelper = {
            .features = &mFeatures,
            .externalMemoryMode = ExternalMemory::Mode::NotSupported,
            .defaultExternalMemoryHandleType = static_cast<VkExternalMemoryHandleTypeFlagBits>(0),
            .supportsExternalMemoryImport = false,
            .supportsExternalMemoryHostProperties = false,
            .minImportedHostPointerAlignment = 4096,
            .supportsDmaBuf = false,
            .hostPageSize = 4096,
            .guestPageSize = 4096,
        };
        mAllocInputs = {
            .pAllocateInfo = nullptr,
            .memoryProperties = mMemoryPropertiesHelper.get(),
            .virtioGpuContextId = kDefaultVirtioGpuContextId,
            .deviceHasDmaBufExt = false,
            .deviceUsesAstcCpuDecompression = false,
            .isSnapshotLoading = false,
        };
    }

    VkResult SelectStrategyFor(const VkMemoryAllocateInfo* pAllocateInfo,
                               VkMemoryAllocationPlan* outPlan) {
        mAllocInputs.pAllocateInfo = pAllocateInfo;
        return mHelper.selectStrategy(mAllocInputs, outPlan);
    }

    VkPhysicalDeviceMemoryProperties mHostMemoryProperties = {};
    gfxstream::host::FeatureSet mFeatures;
    std::unique_ptr<EmulatedPhysicalDeviceMemoryProperties> mMemoryPropertiesHelper;
    VkMemoryAllocationHelper mHelper;
    VkMemoryAllocationInputs mAllocInputs;
};

TEST_F(VkMemoryAllocationHelperTest, DriverAllocatedDeviceLocalMemory) {
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 65536,
        .memoryTypeIndex = 0,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kDriverAllocated));
    EXPECT_THAT(plan.strategy, VariantWith<DriverAllocatedStrategy>(_));
    EXPECT_THAT(plan.allocationSize, Eq(65536u));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(0u));
    EXPECT_THAT(plan.isHostVisible(), IsFalse());
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsFalse());
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_NONE)));
    EXPECT_THAT(plan.hasChainedAllocateStructs(), IsFalse());
    EXPECT_THAT(plan.backing, VariantWith<std::monostate>(_));
    EXPECT_THAT(plan.preMappedHostPtr, IsNull());

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        EXPECT_THAT(hostAllocInfo.sType, Eq(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO));
        EXPECT_THAT(hostAllocInfo.pNext, IsNull());
        EXPECT_THAT(hostAllocInfo.allocationSize, Eq(65536u));
        EXPECT_THAT(hostAllocInfo.memoryTypeIndex, Eq(0u));
    });
}

TEST_F(VkMemoryAllocationHelperTest, NonDirectMappingRoundsUpUnalignedAllocationSizeToPageSize) {
    mFeatures.GlDirectMem.setEnabled(false);
    mHelper.hostPageSize = 4096;

    const VkMemoryAllocateInfo allocInfoUnaligned = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 100,
        .memoryTypeIndex = 0,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfoUnaligned, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.allocationSize, Eq(4096u));
}

TEST_F(VkMemoryAllocationHelperTest, NonDirectMappingRoundsUpPageSizedAllocationToNextPage) {
    mFeatures.GlDirectMem.setEnabled(false);
    mHelper.hostPageSize = 4096;

    const VkMemoryAllocateInfo allocInfoPageSized = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfoPageSized, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.allocationSize, Eq(8192u));
}

TEST_F(VkMemoryAllocationHelperTest,
       ImportColorBufferUsesHostMemoryTypeIndexEvenWhenGuestMemoryTypesShift) {
    // Enable VulkanUseDedicatedAhbMemoryType so a synthetic guest AHB memory type is added
    // and guest memory type indices diverge from host memory type indices.
    mFeatures.VulkanUseDedicatedAhbMemoryType.setEnabled(true);
    mFeatures.GuestVulkanOnly.setEnabled(false);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    VkMemoryDedicatedAllocateInfo dedicatedInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = reinterpret_cast<VkImage>(0x1111),
        .buffer = VK_NULL_HANDLE,
    };
    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = &dedicatedInfo,
        .colorBuffer = 42,
    };
    // The guest passes the synthetic guest AHB memoryTypeIndex (4), while the host ColorBuffer
    // was actually allocated with host memoryTypeIndex 2 (HOST_VISIBLE | HOST_CACHED).
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 4,
    };

    int fakeHostBackingByte = 7;
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 262144,
        .hostMemoryTypeIndex = 2,
        .dedicatedAllocation = false,
        .mappedPtr = &fakeHostBackingByte,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kImportColorBuffer));
    EXPECT_THAT(plan.strategy,
                VariantWith<ImportColorBufferStrategy>(AllOf(
                    Field(&ImportColorBufferStrategy::colorBufferHandle, Eq(42u)),
                    Field(&ImportColorBufferStrategy::mode,
                          Eq(ExternalMemory::Mode::HostAllocation)),
                    Field(&ImportColorBufferStrategy::invalidateColorBuffer, IsTrue()))));
    EXPECT_THAT(plan.allocationSize, Eq(262144u));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(2u));
    EXPECT_THAT(plan.isHostVisible(), IsTrue());
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));
    EXPECT_THAT(plan.boundColorBuffer, Optional(42u));
    EXPECT_THAT(plan.preMappedHostPtr, Eq(&fakeHostBackingByte));
    // Because the host ColorBuffer was not created with a dedicated allocation, the guest's
    // VkMemoryDedicatedAllocateInfo must be stripped.
    EXPECT_THAT(plan.dedicatedAllocInfo, Eq(std::nullopt));
}

TEST_F(VkMemoryAllocationHelperTest,
       ImportColorBufferMissingDuringSnapshotLoadFallsBackToDriverWithoutHostEmulation) {
    mFeatures.SystemBlob.setEnabled(true);
    mFeatures.GlDirectMem.setEnabled(false);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.hostPageSize = 4096;
    mAllocInputs.isSnapshotLoading = true;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 999,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 512,
        .memoryTypeIndex = 1,  // Host-visible type
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kDriverAllocated));
    EXPECT_THAT(plan.strategy, VariantWith<DriverAllocatedStrategy>(_));
    EXPECT_THAT(plan.allocationSize, Eq(4096u));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(1u));
    EXPECT_THAT(plan.boundColorBuffer, Eq(std::nullopt));
}

TEST_F(VkMemoryAllocationHelperTest,
       ImportColorBufferMissingOutsideSnapshotLoadIsRejected) {
    mFeatures.SystemBlob.setEnabled(true);
    mFeatures.GlDirectMem.setEnabled(false);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.hostPageSize = 4096;
    mAllocInputs.isSnapshotLoading = false;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 999,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 512,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    // Outside snapshot loading, a missing ColorBuffer must fail rather than silently falling back.
    EXPECT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest,
       ImportBufferPopulatesBoundBufferAndPreservesDedicatedAlloc) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    VkMemoryDedicatedAllocateInfo dedicatedInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = VK_NULL_HANDLE,
        .buffer = reinterpret_cast<VkBuffer>(0x2222),
    };
    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = &dedicatedInfo,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };

    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 1,
        .dedicatedAllocation = true,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kImportBuffer));
    EXPECT_THAT(plan.strategy,
                VariantWith<ImportBufferStrategy>(
                    Field(&ImportBufferStrategy::bufferHandle, Eq(77u))));
    EXPECT_THAT(plan.allocationSize, Eq(8192u));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(1u));
    EXPECT_THAT(plan.boundBuffer, Optional(77u));
    EXPECT_THAT(plan.dedicatedAllocInfo, Ne(std::nullopt));
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_F(VkMemoryAllocationHelperTest, ImportGuestBlobHandleSelectsStrategy) {
    mAllocInputs.virtioGpuContextId = 5;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_GUEST,
        .blobFlags = STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE,
        .blobId = 888,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kImportGuestBlobHandle));
    EXPECT_THAT(plan.blobId, Optional(888u));
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsFalse());
    EXPECT_THAT(plan.strategy,
                VariantWith<ImportGuestBlobStrategy>(AllOf(
                    Field(&ImportGuestBlobStrategy::virtioGpuContextId, Eq(5u)),
                    Field(&ImportGuestBlobStrategy::blobId, Eq(888u)))));
}

TEST_F(VkMemoryAllocationHelperTest,
       ImportGuestBlobHandleRejectsWhenDeviceDmaBufExtMissing) {
    mAllocInputs.virtioGpuContextId = 5;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = false;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_GUEST,
        .blobFlags = STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE,
        .blobId = 888,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    // If the logical VkDevice does not have VK_EXT_external_memory_dma_buf enabled, strategy
    // selection fails in Phase 1 before any descriptor is removed from ExternalObjectManager.
    EXPECT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, HostVisibleSharedMemoryUdmabufStrategySelectionIsPure) {
    mFeatures.VulkanAllocateHostVisibleAsUdmabuf.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 2048,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleSharedMemory));
    EXPECT_THAT(plan.allocationSize, Eq(4096u));
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsTrue());
    // Phase 1 is pure: no SharedMemory object has been allocated yet.
    EXPECT_THAT(plan.backing, VariantWith<std::monostate>(_));
    EXPECT_THAT(plan.strategy,
                VariantWith<HostVisibleSharedMemoryStrategy>(AllOf(
                    Field(&HostVisibleSharedMemoryStrategy::alignment, Eq(4096u)),
                    Field(&HostVisibleSharedMemoryStrategy::size, Eq(4096u)),
                    Field(&HostVisibleSharedMemoryStrategy::allocateAsUdmabuf, IsTrue()))));
}
#endif

TEST_F(VkMemoryAllocationHelperTest,
       HostVisibleSharedMemorySystemBlobStrategyAlignsSizeWithoutSyscalls) {
    mFeatures.SystemBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;
    mHelper.minImportedHostPointerAlignment = 16384;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 1000,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleSharedMemory));
    EXPECT_THAT(plan.allocationSize, Eq(16384u));
    EXPECT_THAT(plan.backing, VariantWith<std::monostate>(_));
    EXPECT_THAT(plan.strategy,
                VariantWith<HostVisibleSharedMemoryStrategy>(AllOf(
                    Field(&HostVisibleSharedMemoryStrategy::alignment, Eq(16384u)),
                    Field(&HostVisibleSharedMemoryStrategy::size, Eq(16384u)),
                    Field(&HostVisibleSharedMemoryStrategy::allocateAsUdmabuf, IsFalse()))));
}

TEST_F(VkMemoryAllocationHelperTest, HostVisibleExternalBlobChainsExportAllocateInfo) {
    mFeatures.ExternalBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.defaultExternalMemoryHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 2,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleExternalBlob));
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsFalse());

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        const auto* exportInfo = vk_find_struct<VkExportMemoryAllocateInfo>(&hostAllocInfo);
        ASSERT_THAT(exportInfo, NotNull());
#ifdef __linux__
        EXPECT_THAT(exportInfo->handleTypes,
                    Eq(static_cast<VkExternalMemoryHandleTypeFlags>(
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)));
#else
        EXPECT_THAT(exportInfo->handleTypes,
                    Eq(static_cast<VkExternalMemoryHandleTypeFlags>(
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT)));
#endif
    });
}

TEST_F(VkMemoryAllocationHelperTest, HostVisibleExternalBlobDoesNotExportDmaBufInAndroidAhbMode) {
    mFeatures.ExternalBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.externalMemoryMode = ExternalMemory::Mode::AndroidAHB;
    mHelper.defaultExternalMemoryHandleType =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 2,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleExternalBlob));
    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        const auto* exportInfo = vk_find_struct<VkExportMemoryAllocateInfo>(&hostAllocInfo);
        ASSERT_THAT(exportInfo, NotNull());
        EXPECT_THAT(exportInfo->handleTypes,
                    Eq(static_cast<VkExternalMemoryHandleTypeFlags>(
                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID)));
    });
}

TEST_F(VkMemoryAllocationHelperTest,
       HostVisibleExternalBlobMapsWhenDeviceUsesAstcCpuDecompression) {
    mFeatures.ExternalBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.defaultExternalMemoryHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;
    mAllocInputs.deviceUsesAstcCpuDecompression = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 2,
    };

    VkMemoryAllocationPlan plan;
    // When ASTC CPU decompression is active on the device, ExternalBlob memory must be mapped.
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleExternalBlob));
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsTrue());
}

TEST_F(VkMemoryAllocationHelperTest,
       HostVisibleHeapMemoryStrategyAndPureFallbackUpdatesIndexAndCachingFlagsInSync) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;
    mHelper.minImportedHostPointerAlignment = 4096;
    mHelper.guestPageSize = 16384;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_HOST3D,
        .blobFlags = STREAM_BLOB_FLAG_USE_MAPPABLE,
        .blobId = 101,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 2000,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleHeapMemory));
    // Because createBlobInfo is present and minImportedHostPointerAlignment (4096) < guestPageSize
    // (16384), allocationSize must be aligned to 16384.
    EXPECT_THAT(plan.allocationSize, Eq(16384u));
    EXPECT_THAT(plan.strategy,
                VariantWith<HostVisibleHeapMemoryStrategy>(AllOf(
                    Field(&HostVisibleHeapMemoryStrategy::alignment, Eq(16384u)),
                    Field(&HostVisibleHeapMemoryStrategy::size, Eq(16384u)))));

    // Purely test the host-pointer memoryTypeBits fallback without allocating any HeapMemory:
    // rejecting host memory type 1 (WC) and falling back to host memory type 2 (CACHED) must
    // update both `hostMemoryInfo.index` and `computeCaching()` in lockstep.
    ASSERT_THAT(plan.ApplyHostPointerMemoryTypeBits(/*memoryTypeBits=*/(1u << 2),
                                                    *mMemoryPropertiesHelper),
                Eq(VK_SUCCESS));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(2u));
    EXPECT_THAT(plan.hostMemoryInfo.memoryType.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT,
                Ne(0u));
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));
}

TEST_F(VkMemoryAllocationHelperTest,
       HostVisibleHeapMemoryIsSkippedWhenDedicatedOrFlagsStructIsChained) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;

    VkMemoryDedicatedAllocateInfo dedicatedInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = VK_NULL_HANDLE,
        .buffer = reinterpret_cast<VkBuffer>(0x3333),
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &dedicatedInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kDriverAllocated));
    EXPECT_THAT(plan.strategy, VariantWith<DriverAllocatedStrategy>(_));
    EXPECT_THAT(plan.backing, VariantWith<std::monostate>(_));
    EXPECT_THAT(plan.preMappedHostPtr, IsNull());
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteImportColorBufferHostAllocationRejectsMissingHostPointer) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 262144,
        .hostMemoryTypeIndex = 2,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };

    FakeExternalMemoryProvider backend;
    VkMemoryAllocationPlan cbPlan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &cbPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &cbPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteImportColorBufferHostAllocationChainsHostPointerInfo) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    int fakeCbHostBacking = 7;
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 262144,
        .hostMemoryTypeIndex = 2,
        .dedicatedAllocation = false,
        .mappedPtr = &fakeCbHostBacking,
    };

    FakeExternalMemoryProvider backend;
    backend.colorBufferHostPointers[42] = &fakeCbHostBacking;

    VkMemoryAllocationPlan cbPlan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &cbPlan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &cbPlan), Eq(VK_SUCCESS));

    cbPlan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        EXPECT_THAT(hostAllocInfo.allocationSize, Eq(262144u));
        EXPECT_THAT(hostAllocInfo.memoryTypeIndex, Eq(2u));
        const auto* importHostPtr =
            vk_find_struct<VkImportMemoryHostPointerInfoEXT>(&hostAllocInfo);
        ASSERT_THAT(importHostPtr, NotNull());
        EXPECT_THAT(importHostPtr->handleType,
                    Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT));
        EXPECT_THAT(importHostPtr->pHostPointer, Eq(&fakeCbHostBacking));
    });
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteImportBufferHostAllocationRejectsMissingHostPointer) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 1,
        .dedicatedAllocation = false,
    };

    FakeExternalMemoryProvider backend;
    VkMemoryAllocationPlan bufPlan;
    ASSERT_THAT(SelectStrategyFor(&bufAllocInfo, &bufPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &bufPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportBufferHostAllocationChainsHostPointerInfo) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::HostAllocation;

    int fakeBufferBacking = 123;
    VkMemoryDedicatedAllocateInfo dedicatedInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = VK_NULL_HANDLE,
        .buffer = reinterpret_cast<VkBuffer>(0x2222),
    };
    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = &dedicatedInfo,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 1,
        .dedicatedAllocation = true,
    };

    FakeExternalMemoryProvider backend;
    backend.bufferHostPointers[77] = &fakeBufferBacking;

    VkMemoryAllocationPlan bufPlan;
    ASSERT_THAT(SelectStrategyFor(&bufAllocInfo, &bufPlan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &bufPlan), Eq(VK_SUCCESS));

    bufPlan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        const auto* outDedicated = vk_find_struct<VkMemoryDedicatedAllocateInfo>(&hostAllocInfo);
        ASSERT_THAT(outDedicated, NotNull());
        EXPECT_THAT(outDedicated->buffer, Eq(reinterpret_cast<VkBuffer>(0x2222)));

        const auto* importHostPtr =
            vk_find_struct<VkImportMemoryHostPointerInfoEXT>(&hostAllocInfo);
        ASSERT_THAT(importHostPtr, NotNull());
        EXPECT_THAT(importHostPtr->pHostPointer, Eq(&fakeBufferBacking));
    });
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_F(VkMemoryAllocationHelperTest, ExecuteImportGuestBlobChainsDmaBufFd) {
    mAllocInputs.virtioGpuContextId = 5;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_GUEST,
        .blobFlags = STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE,
        .blobId = 888,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.nextBlobDescriptor = 45;

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(backend.removeBlobDescriptorCallCount, Eq(1u));
    EXPECT_THAT(plan.externalMemoryNode.ownedDescriptor.get(), Optional(45));

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        const auto* importFd = vk_find_struct<VkImportMemoryFdInfoKHR>(&hostAllocInfo);
        ASSERT_THAT(importFd, NotNull());
        EXPECT_THAT(importFd->handleType, Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT));
        EXPECT_THAT(importFd->fd, Eq(45));
    });

    // The driver takes ownership after a successful import.
    EXPECT_THAT(plan.externalMemoryNode.ownedDescriptor.release(), Optional(45));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportGuestBlobRejectsMissingDescriptor) {
    mAllocInputs.virtioGpuContextId = 5;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_GUEST,
        .blobFlags = STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE,
        .blobId = 888,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.nextBlobDescriptor = std::nullopt;

    VkMemoryAllocationPlan failPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &failPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &failPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportGuestBlobRejectsNegativeDescriptor) {
    mAllocInputs.virtioGpuContextId = 5;
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    VkCreateBlobGOOGLE createBlobInfo = {
        .sType = VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE,
        .pNext = nullptr,
        .blobMem = STREAM_BLOB_MEM_GUEST,
        .blobFlags = STREAM_BLOB_FLAG_CREATE_GUEST_HANDLE,
        .blobId = 888,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &createBlobInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.nextBlobDescriptor = -1;

    VkMemoryAllocationPlan invalidFdPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &invalidFdPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &invalidFdPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
    EXPECT_THAT(invalidFdPlan.externalMemoryNode.ownedDescriptor.get(), Eq(std::nullopt));
}
#endif

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteHostVisibleHeapMemoryAllocatesBackingAndChainsHostPointer) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;
    mHelper.minImportedHostPointerAlignment = 4096;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.memoryHostPointerTypeBits = (1u << 2);

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.backing, VariantWith<gfxstream::base::HeapMemory>(_));
    EXPECT_THAT(plan.preMappedHostPtr, NotNull());
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(2u));
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        EXPECT_THAT(hostAllocInfo.memoryTypeIndex, Eq(2u));
        const auto* importHostPtr =
            vk_find_struct<VkImportMemoryHostPointerInfoEXT>(&hostAllocInfo);
        ASSERT_THAT(importHostPtr, NotNull());
        EXPECT_THAT(importHostPtr->pHostPointer, Eq(plan.preMappedHostPtr));
    });
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteHostVisibleHeapMemoryPropagatesHostPointerPropertiesError) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;
    mHelper.minImportedHostPointerAlignment = 4096;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.memoryHostPointerTypeBits = (1u << 2);
    backend.memoryHostPointerPropertiesResult = VK_ERROR_INVALID_EXTERNAL_HANDLE;

    VkMemoryAllocationPlan failPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &failPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &failPlan),
                Eq(VK_ERROR_INVALID_EXTERNAL_HANDLE));
}

#if !defined(_WIN32) && !defined(__ANDROID__)
TEST_F(VkMemoryAllocationHelperTest, ExecuteImportColorBufferOpaqueFdRejectsMissingHandle) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 65536,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };

    FakeExternalMemoryProvider backend;
    VkMemoryAllocationPlan missingPlan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &missingPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &missingPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportColorBufferOpaqueFdRejectsNegativeFd) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 65536,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };

    FakeExternalMemoryProvider backend;
    backend.colorBufferHandles[42] = ExternalHandleInfo{
        .handle = -1,
        .streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD,
    };

    VkMemoryAllocationPlan negativeFdPlan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &negativeFdPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &negativeFdPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
    EXPECT_THAT(negativeFdPlan.externalMemoryNode.ownedDescriptor.get(), Eq(std::nullopt));
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteImportColorBufferOpaqueFdChainsFdAndClosesOnFailure) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 1024,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 65536,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };

    FakeExternalMemoryProvider backend;
    int fdClosedOnFailure = dup(STDOUT_FILENO);
    ASSERT_THAT(fdClosedOnFailure, Ne(-1));
    backend.colorBufferHandles[42] = ExternalHandleInfo{
        .handle = fdClosedOnFailure,
        .streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD,
    };
    {
        VkMemoryAllocationPlan failedAllocPlan;
        ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &failedAllocPlan), Eq(VK_SUCCESS));
        ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &failedAllocPlan),
                    Eq(VK_SUCCESS));
        EXPECT_THAT(failedAllocPlan.externalMemoryNode.ownedDescriptor.get(),
                    Optional(fdClosedOnFailure));
        failedAllocPlan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
            const auto* importFd = vk_find_struct<VkImportMemoryFdInfoKHR>(&hostAllocInfo);
            ASSERT_THAT(importFd, NotNull());
            EXPECT_THAT(importFd->handleType, Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT));
            EXPECT_THAT(importFd->fd, Eq(fdClosedOnFailure));
        });
    }
    errno = 0;
    EXPECT_THAT(fcntl(fdClosedOnFailure, F_GETFD), Eq(-1));
    EXPECT_THAT(errno, Eq(EBADF));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportBufferOpaqueFdRejectsMissingHandle) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
    };

    FakeExternalMemoryProvider backend;
    VkMemoryAllocationPlan missingBufPlan;
    ASSERT_THAT(SelectStrategyFor(&bufAllocInfo, &missingBufPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &missingBufPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteImportBufferOpaqueFdRejectsNegativeFd) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
    };

    FakeExternalMemoryProvider backend;
    backend.bufferHandles[77] = ExternalHandleInfo{
        .handle = -1,
        .streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD,
    };

    VkMemoryAllocationPlan negativeBufFdPlan;
    ASSERT_THAT(SelectStrategyFor(&bufAllocInfo, &negativeBufFdPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &negativeBufFdPlan),
                Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
    EXPECT_THAT(negativeBufFdPlan.externalMemoryNode.ownedDescriptor.get(), Eq(std::nullopt));
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteImportBufferOpaqueFdChainsFdAndReleasesOwnershipOnSuccess) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::OpaqueFd;
    mHelper.supportsExternalMemoryImport = true;

    int fdReleasedOnSuccess = dup(STDOUT_FILENO);
    ASSERT_THAT(fdReleasedOnSuccess, Ne(-1));
    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 8192,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
    };

    FakeExternalMemoryProvider backend;
    backend.bufferHandles[77] = ExternalHandleInfo{
        .handle = fdReleasedOnSuccess,
        .streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD,
    };
    {
        VkMemoryAllocationPlan successBufPlan;
        ASSERT_THAT(SelectStrategyFor(&bufAllocInfo, &successBufPlan), Eq(VK_SUCCESS));
        ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &successBufPlan),
                    Eq(VK_SUCCESS));
        successBufPlan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
            const auto* importFd = vk_find_struct<VkImportMemoryFdInfoKHR>(&hostAllocInfo);
            ASSERT_THAT(importFd, NotNull());
            EXPECT_THAT(importFd->handleType, Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT));
            EXPECT_THAT(importFd->fd, Eq(fdReleasedOnSuccess));
        });
        // The driver takes ownership after a successful import.
        EXPECT_THAT(successBufPlan.externalMemoryNode.ownedDescriptor.release(),
                    Optional(fdReleasedOnSuccess));
    }
    EXPECT_THAT(fcntl(fdReleasedOnSuccess, F_GETFD), Ne(-1));
    close(fdReleasedOnSuccess);
}
#endif

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteHostVisibleSharedMemorySystemBlobAllocatesAndMapsSharedMemory) {
    mFeatures.SystemBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.memoryHostPointerTypeBits = (1u << 2);

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &plan), Eq(VK_SUCCESS));

    EXPECT_THAT(plan.backing, VariantWith<gfxstream::base::SharedMemory>(_));
    EXPECT_THAT(plan.preMappedHostPtr, NotNull());
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(2u));
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        EXPECT_THAT(hostAllocInfo.memoryTypeIndex, Eq(2u));
        const auto* importHostPtr =
            vk_find_struct<VkImportMemoryHostPointerInfoEXT>(&hostAllocInfo);
        ASSERT_THAT(importHostPtr, NotNull());
        EXPECT_THAT(importHostPtr->handleType,
                    Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT));
        EXPECT_THAT(importHostPtr->pHostPointer, Eq(plan.preMappedHostPtr));
    });
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteHostVisibleSharedMemorySystemBlobPropagatesHostPointerPropertiesError) {
    mFeatures.SystemBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.memoryHostPointerTypeBits = (1u << 2);
    backend.memoryHostPointerPropertiesResult = VK_ERROR_INVALID_EXTERNAL_HANDLE;

    VkMemoryAllocationPlan failPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &failPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &failPlan),
                Eq(VK_ERROR_INVALID_EXTERNAL_HANDLE));
}

TEST_F(VkMemoryAllocationHelperTest,
       ExecuteHostVisibleSharedMemoryRejectsWhenNeitherUdmabufNorSystemBlobEnabled) {
    mFeatures.SystemBlob.setEnabled(false);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);

    FakeExternalMemoryProvider backend;
    VkMemoryAllocationPlan invalidPlan;
    invalidPlan.strategy = HostVisibleSharedMemoryStrategy{
        .alignment = 4096,
        .size = 4096,
        .allocateAsUdmabuf = false,
    };
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &invalidPlan),
                Eq(VK_ERROR_INITIALIZATION_FAILED));
}

#if defined(__linux__) && !defined(__ANDROID__)
TEST_F(VkMemoryAllocationHelperTest, ExecuteHostVisibleSharedMemoryUdmabufRejectsMissingFd) {
    mFeatures.VulkanAllocateHostVisibleAsUdmabuf.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.nextUdmabufDescriptor = std::nullopt;

    VkMemoryAllocationPlan failPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &failPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &failPlan),
                Eq(VK_ERROR_OUT_OF_HOST_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteHostVisibleSharedMemoryUdmabufRejectsNegativeFd) {
    mFeatures.VulkanAllocateHostVisibleAsUdmabuf.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    backend.nextUdmabufDescriptor = -1;

    VkMemoryAllocationPlan negativeUdmabufFdPlan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &negativeUdmabufFdPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &negativeUdmabufFdPlan),
                Eq(VK_ERROR_OUT_OF_HOST_MEMORY));
    EXPECT_THAT(negativeUdmabufFdPlan.externalMemoryNode.ownedDescriptor.get(), Eq(std::nullopt));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteHostVisibleSharedMemoryUdmabufChainsDmaBufFd) {
    mFeatures.VulkanAllocateHostVisibleAsUdmabuf.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsDmaBuf = true;
    mAllocInputs.deviceHasDmaBufExt = true;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    FakeExternalMemoryProvider backend;
    int udmabufFd = dup(STDOUT_FILENO);
    ASSERT_THAT(udmabufFd, Ne(-1));
    backend.nextUdmabufDescriptor = udmabufFd;

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    ASSERT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.backing, VariantWith<gfxstream::base::SharedMemory>(_));
    EXPECT_THAT(plan.externalMemoryNode.ownedDescriptor.get(), Optional(udmabufFd));

    plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
        const auto* importFd = vk_find_struct<VkImportMemoryFdInfoKHR>(&hostAllocInfo);
        ASSERT_THAT(importFd, NotNull());
        EXPECT_THAT(importFd->handleType, Eq(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT));
        EXPECT_THAT(importFd->fd, Eq(udmabufFd));
    });

    // The driver takes ownership after a successful import.
    EXPECT_THAT(plan.externalMemoryNode.ownedDescriptor.release(), Optional(udmabufFd));
    close(udmabufFd);
}
#endif

TEST_F(VkMemoryAllocationHelperTest, ApplyHostPointerMemoryTypeBitsRejectsZeroMask) {
    VkMemoryAllocationPlan plan;
    plan.hostMemoryInfo =
        *mMemoryPropertiesHelper->getHostMemoryInfoFromHostMemoryTypeIndex(1);

    EXPECT_THAT(plan.ApplyHostPointerMemoryTypeBits(/*memoryTypeBits=*/0, *mMemoryPropertiesHelper),
                Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest, ApplyHostPointerMemoryTypeBitsRejectsOutOfRangeMask) {
    VkMemoryAllocationPlan plan;
    plan.hostMemoryInfo =
        *mMemoryPropertiesHelper->getHostMemoryInfoFromHostMemoryTypeIndex(1);

    EXPECT_THAT(
        plan.ApplyHostPointerMemoryTypeBits(/*memoryTypeBits=*/(1u << 30), *mMemoryPropertiesHelper),
        Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest,
       ApplyHostPointerMemoryTypeBitsRejectsNonHostVisibleMaskWhenInitialTypeIsHostVisible) {
    VkMemoryAllocationPlan plan;
    plan.hostMemoryInfo =
        *mMemoryPropertiesHelper->getHostMemoryInfoFromHostMemoryTypeIndex(1);
    EXPECT_THAT(
        plan.ApplyHostPointerMemoryTypeBits(/*memoryTypeBits=*/(1u << 0), *mMemoryPropertiesHelper),
        Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest,
       ApplyHostPointerMemoryTypeBitsRejectsNonHostVisibleMaskWhenInitialTypeIsNonHostVisible) {
    VkMemoryAllocationPlan nonHostVisiblePlan;
    nonHostVisiblePlan.hostMemoryInfo =
        *mMemoryPropertiesHelper->getHostMemoryInfoFromHostMemoryTypeIndex(0);
    EXPECT_THAT(nonHostVisiblePlan.ApplyHostPointerMemoryTypeBits(/*memoryTypeBits=*/(1u << 0),
                                                                  *mMemoryPropertiesHelper),
                Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest, ApplyHostPointerMemoryTypeBitsSelectsHostVisibleFallback) {
    // When both a non-HOST_VISIBLE bit (0) and a HOST_VISIBLE bit (2) are set in
    // memoryTypeBits (and requested index 1 is not set), ApplyHostPointerMemoryTypeBits
    // selects the HOST_VISIBLE memory type (2) so plan.isHostVisible() stays true.
    VkMemoryAllocationPlan plan;
    plan.hostMemoryInfo =
        *mMemoryPropertiesHelper->getHostMemoryInfoFromHostMemoryTypeIndex(1);

    ASSERT_THAT(plan.ApplyHostPointerMemoryTypeBits(
                    /*memoryTypeBits=*/(1u << 0) | (1u << 2), *mMemoryPropertiesHelper),
                Eq(VK_SUCCESS));
    EXPECT_THAT(plan.hostMemoryInfo.index, Eq(2u));
    EXPECT_THAT(plan.isHostVisible(), IsTrue());
    EXPECT_THAT(plan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_CACHED)));
}

TEST_F(VkMemoryAllocationHelperTest,
       ChainedAllocateFlagsAndOpaqueCaptureAddressStructsArePreservedAcrossRepeatedCalls) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;

    VkMemoryOpaqueCaptureAddressAllocateInfo captureAddressInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO,
        .pNext = nullptr,
        .opaqueCaptureAddress = 0xDEADBEEFCAFEBABEULL,
    };
    VkMemoryAllocateFlagsInfo flagsInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .pNext = &captureAddressInfo,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT,
        .deviceMask = 0,
    };
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &flagsInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.hasChainedAllocateStructs(), IsTrue());
    // Chained allocate flags/capture address structs skip VulkanAllocateHostMemory.
    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kDriverAllocated));

    for (int i = 0; i < 2; ++i) {
        plan.WithVkMemoryAllocateInfo([&](const VkMemoryAllocateInfo& hostAllocInfo) {
            const auto* outFlags = vk_find_struct<VkMemoryAllocateFlagsInfo>(&hostAllocInfo);
            ASSERT_THAT(outFlags, NotNull());
            EXPECT_THAT(outFlags->flags, Eq(VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT));

            const auto* outCapture =
                vk_find_struct<VkMemoryOpaqueCaptureAddressAllocateInfo>(&hostAllocInfo);
            ASSERT_THAT(outCapture, NotNull());
            EXPECT_THAT(outCapture->opaqueCaptureAddress, Eq(0xDEADBEEFCAFEBABEULL));
        });
    }
}

TEST_F(VkMemoryAllocationHelperTest, ComputeCachingWriteCombinedMode) {
    const VkMemoryAllocateInfo wcAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };
    VkMemoryAllocationPlan wcPlan;
    ASSERT_THAT(SelectStrategyFor(&wcAllocInfo, &wcPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(wcPlan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_WC)));
}

TEST_F(VkMemoryAllocationHelperTest, ComputeCachingUncachedMode) {
    const VkMemoryAllocateInfo uncachedAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 3,
    };
    VkMemoryAllocationPlan uncachedPlan;
    ASSERT_THAT(SelectStrategyFor(&uncachedAllocInfo, &uncachedPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(uncachedPlan.computeCaching(), Eq(static_cast<uint32_t>(MAP_CACHE_UNCACHED)));
}

TEST_F(VkMemoryAllocationHelperTest, VirtioGpuNextEnablesDirectMappingWithoutGlDirectMem) {
    mFeatures.GlDirectMem.setEnabled(false);
    mFeatures.VirtioGpuNext.setEnabled(true);
    EXPECT_THAT(mHelper.usingDirectMapping(), IsTrue());

    const VkMemoryAllocateInfo unalignedAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 100,
        .memoryTypeIndex = 0,
    };
    VkMemoryAllocationPlan directMapPlan;
    ASSERT_THAT(SelectStrategyFor(&unalignedAllocInfo, &directMapPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(directMapPlan.allocationSize, Eq(100u));
}

TEST_F(VkMemoryAllocationHelperTest, GuestVulkanOnlyDisablesColorBufferGlInvalidation) {
    mFeatures.GuestVulkanOnly.setEnabled(true);
    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 12,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 4096,
        .hostMemoryTypeIndex = 0,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };

    VkMemoryAllocationPlan cbPlan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &cbPlan), Eq(VK_SUCCESS));
    EXPECT_THAT(cbPlan.strategy,
                VariantWith<ImportColorBufferStrategy>(
                    Field(&ImportColorBufferStrategy::invalidateColorBuffer, IsFalse())));
}

TEST_F(VkMemoryAllocationHelperTest, SelectStrategyRejectsNullAllocateInfo) {
    VkMemoryAllocationPlan plan;
    EXPECT_THAT(SelectStrategyFor(nullptr, &plan), Eq(VK_ERROR_INITIALIZATION_FAILED));
}

TEST_F(VkMemoryAllocationHelperTest, SelectStrategyRejectsNullOutPlan) {
    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };
    EXPECT_THAT(SelectStrategyFor(&allocInfo, nullptr), Eq(VK_ERROR_INITIALIZATION_FAILED));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteStrategyRejectsNullProvider) {
    VkMemoryAllocationPlan plan;
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, nullptr, &plan),
                Eq(VK_ERROR_INITIALIZATION_FAILED));
}

TEST_F(VkMemoryAllocationHelperTest, ExecuteStrategyRejectsNullPlan) {
    FakeExternalMemoryProvider backend;
    EXPECT_THAT(mHelper.executeStrategy(mAllocInputs, &backend, nullptr),
                Eq(VK_ERROR_INITIALIZATION_FAILED));
}

TEST_F(VkMemoryAllocationHelperTest, MinImportedHostPointerAlignmentZeroIsNormalized) {
    // minImportedHostPointerAlignment == 0 is normalized in selectStrategy so allocationSize
    // is not zeroed out by ALIGN(size, 0).
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;
    mHelper.minImportedHostPointerAlignment = 0;

    const VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 100,
        .memoryTypeIndex = 1,
    };
    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&allocInfo, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kHostVisibleHeapMemory));
    EXPECT_THAT(plan.allocationSize, Eq(ALIGN(100u, sizeof(void*))));
}

TEST_F(VkMemoryAllocationHelperTest,
       AllocationSizeOverflowRejectedInNonDirectMappingPageRounding) {
    mFeatures.GlDirectMem.setEnabled(false);
    mFeatures.VirtioGpuNext.setEnabled(false);

    const VkMemoryAllocateInfo overflowAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = std::numeric_limits<VkDeviceSize>::max(),
        .memoryTypeIndex = 1,
    };
    VkMemoryAllocationPlan plan;
    EXPECT_THAT(SelectStrategyFor(&overflowAllocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, AllocationSizeOverflowRejectedInSharedMemoryAlignment) {
    mFeatures.GlDirectMem.setEnabled(true);
    mFeatures.SystemBlob.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);

    const VkMemoryAllocateInfo overflowAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = std::numeric_limits<VkDeviceSize>::max(),
        .memoryTypeIndex = 1,
    };
    VkMemoryAllocationPlan plan;
    EXPECT_THAT(SelectStrategyFor(&overflowAllocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, AllocationSizeOverflowRejectedInHeapMemoryAlignment) {
    mFeatures.GlDirectMem.setEnabled(true);
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = true;

    const VkMemoryAllocateInfo overflowAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = std::numeric_limits<VkDeviceSize>::max(),
        .memoryTypeIndex = 1,
    };
    VkMemoryAllocationPlan plan;
    EXPECT_THAT(SelectStrategyFor(&overflowAllocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest, SelectStrategyRejectsOutOfRangeGuestMemoryTypeIndex) {
    VkMemoryAllocationPlan plan;
    const VkMemoryAllocateInfo invalidGuestIndexAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 99,
    };
    EXPECT_THAT(SelectStrategyFor(&invalidGuestIndexAllocInfo, &plan),
                Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest,
       SelectStrategyRejectsOutOfRangeImportedColorBufferHostMemoryTypeIndex) {
    VkMemoryAllocationPlan plan;
    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 4096,
        .hostMemoryTypeIndex = 99,
        .dedicatedAllocation = false,
        .mappedPtr = nullptr,
    };
    EXPECT_THAT(SelectStrategyFor(&cbAllocInfo, &plan), Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest,
       SelectStrategyRejectsOutOfRangeImportedBufferHostMemoryTypeIndex) {
    VkMemoryAllocationPlan plan;
    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo = ImportedBufferAllocationInfo{
        .allocationSize = 4096,
        .hostMemoryTypeIndex = 99,
        .dedicatedAllocation = false,
    };
    EXPECT_THAT(SelectStrategyFor(&bufAllocInfo, &plan), Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest, SelectStrategyRejectsMissingImportedBufferInfo) {
    VkMemoryAllocationPlan plan;
    VkImportBufferGOOGLE importBufferInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE,
        .pNext = nullptr,
        .buffer = 77,
    };
    const VkMemoryAllocateInfo bufAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importBufferInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 0,
    };
    mAllocInputs.importedBufferInfo.reset();
    EXPECT_THAT(SelectStrategyFor(&bufAllocInfo, &plan), Eq(VK_ERROR_OUT_OF_DEVICE_MEMORY));
}

TEST_F(VkMemoryAllocationHelperTest,
       SelectStrategyRejectsHostVisibleHeapMemoryWithoutExternalMemoryHostProperties) {
    mFeatures.VulkanAllocateHostMemory.setEnabled(true);
    RebuildMemoryPropertiesHelper(/*hostColorBufferMemoryTypeIndex=*/0);
    mHelper.supportsExternalMemoryHostProperties = false;

    VkMemoryAllocationPlan plan;
    const VkMemoryAllocateInfo hostVisibleAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };
    EXPECT_THAT(SelectStrategyFor(&hostVisibleAllocInfo, &plan), Eq(VK_ERROR_INCOMPATIBLE_DRIVER));
}

TEST_F(VkMemoryAllocationHelperTest, ImportColorBufferAndroidAhbSkipsHostMapAfterAlloc) {
    mHelper.externalMemoryMode = ExternalMemory::Mode::AndroidAHB;
    mHelper.supportsExternalMemoryImport = true;

    VkImportColorBufferGOOGLE importCbInfo = {
        .sType = VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE,
        .pNext = nullptr,
        .colorBuffer = 42,
    };
    const VkMemoryAllocateInfo cbAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &importCbInfo,
        .allocationSize = 4096,
        .memoryTypeIndex = 1,
    };
    mAllocInputs.importedColorBufferInfo = ImportedColorBufferAllocationInfo{
        .allocationSize = 65536,
        .hostMemoryTypeIndex = 2,  // Host-visible type
        .dedicatedAllocation = true,
        .mappedPtr = nullptr,
    };

    VkMemoryAllocationPlan plan;
    ASSERT_THAT(SelectStrategyFor(&cbAllocInfo, &plan), Eq(VK_SUCCESS));
    EXPECT_THAT(plan.kind(), Eq(MemoryAllocationKind::kImportColorBuffer));
    EXPECT_THAT(plan.isHostVisible(), IsTrue());
    EXPECT_THAT(plan.shouldMapAfterAlloc, IsFalse());
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
