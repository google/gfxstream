// Copyright 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "VkAndroidNativeBuffer.h"

#include <string.h>

#include <future>

#include "FrameBuffer.h"
#include "GrallocDefs.h"
#include "SyncThread.h"
#include "VkCommonOperations.h"
#include "VulkanDispatch.h"
#include "cereal/common/goldfish_vk_deepcopy.h"
#include "cereal/common/goldfish_vk_extension_structs.h"
#include "gfxstream/host/BackendCallbacks.h"
#include "gfxstream/host/Tracing.h"
#include "goldfish_vk_private_defs.h"
#include "host-common/GfxstreamFatalError.h"
#include "vulkan/vk_enum_string_helper.h"

namespace gfxstream {
namespace vk {

#define VK_ANB_ERR(fmt, ...) ERR(fmt, ##__VA_ARGS__);

#define ENABLE_VK_ANB_DEBUG 0

#if ENABLE_VK_ANB_DEBUG
#define VK_ANB_DEBUG(fmt, ...) \
    INFO("vk-anb-debug: " fmt, ##__VA_ARGS__);
#define VK_ANB_DEBUG_OBJ(obj, fmt, ...) \
    INFO("vk-anb-debug: %p " fmt, obj, ##__VA_ARGS__);
#else
#define VK_ANB_DEBUG(fmt, ...)
#define VK_ANB_DEBUG_OBJ(obj, fmt, ...)
#endif

using emugl::ABORT_REASON_OTHER;
using emugl::FatalError;

AndroidNativeBufferInfo::QsriWaitFencePool::QsriWaitFencePool(VulkanDispatch* vk, VkDevice device)
    : mVk(vk), mDevice(device) {}

VkFence AndroidNativeBufferInfo::QsriWaitFencePool::getFenceFromPool() {
    VK_ANB_DEBUG("enter");
    std::lock_guard<std::mutex> lock(mMutex);
    VkFence fence = VK_NULL_HANDLE;
    if (mAvailableFences.empty()) {
        VkFenceCreateInfo fenceCreateInfo = {
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            0,
            0,
        };
        mVk->vkCreateFence(mDevice, &fenceCreateInfo, nullptr, &fence);
        VK_ANB_DEBUG("no fences in pool, created %p", fence);
    } else {
        fence = mAvailableFences.back();
        mAvailableFences.pop_back();
        VkResult res = mVk->vkResetFences(mDevice, 1, &fence);
        if (res != VK_SUCCESS) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "Fail to reset Qsri VkFence: " << res << "(" << string_VkResult(res) << ").";
        }
        VK_ANB_DEBUG("existing fence in pool: %p. also reset the fence", fence);
    }
    mUsedFences.emplace(fence);
    VK_ANB_DEBUG("exit");
    return fence;
}

AndroidNativeBufferInfo::QsriWaitFencePool::~QsriWaitFencePool() {
    VK_ANB_DEBUG("enter");
    // Nothing in the fence pool is unsignaled
    if (!mUsedFences.empty()) {
        VK_ANB_ERR("%zu VkFences are still being used when destroying the Qsri fence pool.",
                   mUsedFences.size());
    }
    for (auto fence : mAvailableFences) {
        VK_ANB_DEBUG("destroy fence %p", fence);
        mVk->vkDestroyFence(mDevice, fence, nullptr);
    }
    VK_ANB_DEBUG("exit");
}

void AndroidNativeBufferInfo::QsriWaitFencePool::returnFence(VkFence fence) {
    std::lock_guard<std::mutex> lock(mMutex);
    if (!mUsedFences.erase(fence)) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "Return an unmanaged Qsri VkFence back to the pool.";
        return;
    }
    mAvailableFences.push_back(fence);
}

bool parseAndroidNativeBufferInfo(const VkImageCreateInfo* pCreateInfo,
                                  AndroidNativeBufferInfo* info_out) {
    // Look through the extension chain.
    const void* curr_pNext = pCreateInfo->pNext;
    if (!curr_pNext) return false;

    uint32_t structType = goldfish_vk_struct_type(curr_pNext);

    return structType == VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID;
}

/*static*/
std::unique_ptr<AndroidNativeBufferInfo> AndroidNativeBufferInfo::create(
    VkEmulation* emu,
    VulkanDispatch* vk, VkDevice device, android::base::BumpPool& allocator,
    const VkImageCreateInfo* pCreateInfo, const VkNativeBufferANDROID* nativeBufferANDROID,
    const VkAllocationCallbacks* pAllocator, const VkPhysicalDeviceMemoryProperties* memProps) {
    bool colorBufferExportedToGl = false;
    bool externalMemoryCompatible = false;

    std::unique_ptr<AndroidNativeBufferInfo> out(new AndroidNativeBufferInfo());

    out->mDeviceDispatch = vk;
    out->mDevice = device;
    out->mVkFormat = pCreateInfo->format;
    out->mExtent = pCreateInfo->extent;
    out->mUsage = pCreateInfo->usage;

    for (uint32_t i = 0; i < pCreateInfo->queueFamilyIndexCount; ++i) {
        out->mQueueFamilyIndices.push_back(pCreateInfo->pQueueFamilyIndices[i]);
    }

    out->mAhbFormat = nativeBufferANDROID->format;
    out->mStride = nativeBufferANDROID->stride;
    out->mColorBufferHandle = *static_cast<const uint32_t*>(nativeBufferANDROID->handle);

    if (!emu->getColorBufferShareInfo(out->mColorBufferHandle, &colorBufferExportedToGl,
                                      &externalMemoryCompatible)) {
        VK_ANB_ERR("Failed to query if ColorBuffer:%d exported to GL.", out->mColorBufferHandle);
        return nullptr;
    }

    if (externalMemoryCompatible) {
        emu->releaseColorBufferForGuestUse(out->mColorBufferHandle);
        out->mExternallyBacked = true;
    }

    out->mUseVulkanNativeImage =
        (emu && emu->isGuestVulkanOnly()) || colorBufferExportedToGl;

    VkDeviceSize bindOffset = 0;
    if (out->mExternallyBacked) {
        VkImageCreateInfo createImageCi;
        deepcopy_VkImageCreateInfo(&allocator, VK_STRUCTURE_TYPE_MAX_ENUM, pCreateInfo,
                                   &createImageCi);

        auto* nativeBufferAndroid = vk_find_struct<VkNativeBufferANDROID>(&createImageCi);
        if (!nativeBufferAndroid) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "VkNativeBufferANDROID is required to be included in the pNext chain of the "
                   "VkImageCreateInfo when importing a gralloc buffer.";
        }
        vk_struct_chain_remove(nativeBufferAndroid, &createImageCi);

        const uint32_t importedColorBufferHandle =
            *static_cast<const uint32_t*>(nativeBufferANDROID->handle);
        if (importedColorBufferHandle == 0) {
            VK_ANB_ERR(
                "Failed to prepare ANB image: attempted to import a non-existent ColorBuffer.");
            return nullptr;
        }
        const auto importedColorBufferInfoOpt = emu->getColorBufferInfo(importedColorBufferHandle);
        if (!importedColorBufferInfoOpt) {
            VK_ANB_ERR("Failed to prepare ANB image: ColorBuffer:%d info not found.",
                       importedColorBufferHandle);
            return nullptr;
        }
        const auto& importedColorBufferInfo = *importedColorBufferInfoOpt;
        if (pCreateInfo == nullptr) {
            VK_ANB_ERR("Failed to prepare ANB image: invalid pCreateInfo.");
            return nullptr;
        }
        if (pCreateInfo->extent.width > importedColorBufferInfo.width) {
            VK_ANB_ERR(
                "Failed to prepare ANB image: attempted to create a VkImage with width:%d by "
                "importing ColorBuffer:%d which only has width:%d",
                pCreateInfo->extent.width, importedColorBufferHandle,
                importedColorBufferInfo.width);
            return nullptr;
        }
        if (pCreateInfo->extent.height > importedColorBufferInfo.height) {
            VK_ANB_ERR(
                "Failed to prepare ANB image: attempted to create a VkImage with height:%d by "
                "importing ColorBuffer:%d which only has height:%d",
                pCreateInfo->extent.height, importedColorBufferHandle,
                importedColorBufferInfo.height);
            return nullptr;
        }

        // Update create flags to match the color buffer imported
        createImageCi.flags = importedColorBufferInfo.imageCreateInfoShallow.flags;

        const auto& importedColorBufferMemoryInfo = importedColorBufferInfo.memory;

        // VkBindImageMemorySwapchainInfoKHR may be included from the guest but
        // should not be passed to the host driver.
        auto* bindSwapchainInfo = vk_find_struct<VkBindImageMemorySwapchainInfoKHR>(&createImageCi);
        vk_struct_chain_remove(bindSwapchainInfo, &createImageCi);

        if (vk_find_struct<VkExternalMemoryImageCreateInfo>(&createImageCi)) {
            GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                << "Unhandled VkExternalMemoryImageCreateInfo in the pNext chain.";
        }

        // Create the image with extension structure about external backing.
        VkExternalMemoryImageCreateInfo extImageCi = {
            VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            0,
            static_cast<VkExternalMemoryHandleTypeFlags>(emu->getDefaultExternalMemoryHandleType()),
        };
#if defined(__APPLE__)
        if (emu->supportsMoltenVk()) {
            // Change handle type requested to metal handle
            extImageCi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
        }
#endif
        vk_insert_struct(createImageCi, extImageCi);

        VkResult createResult =
            vk->vkCreateImage(out->mDevice, &createImageCi, pAllocator, &out->mImage);
        if (createResult != VK_SUCCESS) {
            return nullptr;
        }

        vk->vkGetImageMemoryRequirements(out->mDevice, out->mImage, &out->mImageMemoryRequirements);

        if (out->mImageMemoryRequirements.size > importedColorBufferMemoryInfo.size) {
            VK_ANB_ERR(
                "Failed to prepare ANB image: attempted to import memory that is not large enough "
                "for the VkImage: image memory requirements size:%llu vs actual memory size:%llu, "
                "CB: %u, %s, %ux%u",
                out->mImageMemoryRequirements.size, importedColorBufferMemoryInfo.size,
                importedColorBufferHandle, string_VkFormat(createImageCi.format),
                createImageCi.extent.width, createImageCi.extent.height);

            return nullptr;
        }

        if (out->mImageMemoryRequirements.size < importedColorBufferMemoryInfo.size) {
            out->mImageMemoryRequirements.size = importedColorBufferMemoryInfo.size;
        }

        VkMemoryDedicatedAllocateInfo dedicatedInfo = {
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            nullptr,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
        };
        VkMemoryDedicatedAllocateInfo* dedicatedInfoPtr = nullptr;
        if (importedColorBufferMemoryInfo.dedicatedAllocation) {
            dedicatedInfo.image = out->mImage;
            dedicatedInfoPtr = &dedicatedInfo;
        }

        if (!emu->importExternalMemory(out->mDeviceDispatch, out->mDevice,
                                  &importedColorBufferMemoryInfo, dedicatedInfoPtr,
                                  &out->mImageMemory)) {
            VK_ANB_ERR("VK_ANDROID_native_buffer: Failed to import external memory%s",
                       importedColorBufferMemoryInfo.dedicatedAllocation ? " (dedicated)" : "");
            return nullptr;
        }

        bindOffset = importedColorBufferMemoryInfo.bindOffset;
    } else {
        // delete the info struct and pass to vkCreateImage, and also add
        // transfer src capability to allow us to copy to CPU.
        VkImageCreateInfo infoNoNative = *pCreateInfo;
        infoNoNative.pNext = nullptr;
        infoNoNative.usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

        VkResult createResult = vk->vkCreateImage(device, &infoNoNative, pAllocator, &out->mImage);
        if (createResult != VK_SUCCESS) {
            return nullptr;
        }

        vk->vkGetImageMemoryRequirements(device, out->mImage, &out->mImageMemoryRequirements);

        uint32_t imageMemoryTypeIndex = 0;
        bool imageMemoryTypeIndexFound = false;

        for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
            bool supported = out->mImageMemoryRequirements.memoryTypeBits & (1 << i);
            if (supported) {
                imageMemoryTypeIndex = i;
                imageMemoryTypeIndexFound = true;
                break;
            }
        }

        if (!imageMemoryTypeIndexFound) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not obtain "
                "image memory type index");
            return nullptr;
        }

        out->mImageMemoryTypeIndex = imageMemoryTypeIndex;

        const VkMemoryAllocateInfo allocInfo = {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            0,
            out->mImageMemoryRequirements.size,
            out->mImageMemoryTypeIndex,
        };
        if (VK_SUCCESS != vk->vkAllocateMemory(device, &allocInfo, nullptr, &out->mImageMemory)) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not allocate "
                "image memory. requested size: %zu",
                (size_t)(out->mImageMemoryRequirements.size));
            return nullptr;
        }
    }

    if (VK_SUCCESS != vk->vkBindImageMemory(device, out->mImage, out->mImageMemory, bindOffset)) {
        VK_ANB_ERR(
            "VK_ANDROID_native_buffer: could not bind "
            "image memory.");
        return nullptr;
    }

    // Allocate a staging memory and set up the staging buffer.
    // TODO: Make this shared as well if we can get that to
    // work on Windows with NVIDIA.
    {
        VkBufferCreateInfo stagingBufferCreateInfo = {
            VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            0,
            0,
            out->mImageMemoryRequirements.size,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_SHARING_MODE_EXCLUSIVE,
            0,
            nullptr,
        };
        if (out->mQueueFamilyIndices.size() > 1) {
            stagingBufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
            stagingBufferCreateInfo.queueFamilyIndexCount =
                static_cast<uint32_t>(out->mQueueFamilyIndices.size());
            stagingBufferCreateInfo.pQueueFamilyIndices = out->mQueueFamilyIndices.data();
        }

        if (VK_SUCCESS !=
            vk->vkCreateBuffer(device, &stagingBufferCreateInfo, nullptr, &out->mStagingBuffer)) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not create "
                "staging buffer.");
            return nullptr;
        }

        VkMemoryRequirements stagingMemoryRequirements;
        vk->vkGetBufferMemoryRequirements(device, out->mStagingBuffer, &stagingMemoryRequirements);
        if (stagingMemoryRequirements.size < out->mImageMemoryRequirements.size) {
            VK_ANB_ERR("VK_ANDROID_native_buffer: unexpected staging buffer size");
            return nullptr;
        }

        uint32_t stagingMemoryTypeIndex = -1;
        bool stagingIndexRes =
            getStagingMemoryTypeIndex(vk, device, memProps, stagingMemoryRequirements, &stagingMemoryTypeIndex);
        if (!stagingIndexRes) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not obtain "
                "staging memory type index");
            return nullptr;
        }

        VkMemoryAllocateInfo allocInfo = {
            VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            0,
            stagingMemoryRequirements.size,
            stagingMemoryTypeIndex,
        };

        VkResult res =
            vk->vkAllocateMemory(device, &allocInfo, nullptr, &out->mStagingBufferMemory);
        if (VK_SUCCESS != res) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not allocate staging memory. "
                "res = %d. requested size: %zu",
                (int)res, (size_t)(stagingMemoryRequirements.size));
            return nullptr;
        }

        if (VK_SUCCESS !=
            vk->vkBindBufferMemory(device, out->mStagingBuffer, out->mStagingBufferMemory, 0)) {
            VK_ANB_ERR(
                "VK_ANDROID_native_buffer: could not bind "
                "staging buffer to staging memory.");
            return nullptr;
        }

        if (VK_SUCCESS != vk->vkMapMemory(device, out->mStagingBufferMemory, 0, VK_WHOLE_SIZE, 0,
                                          (void**)&out->mMappedStagingPtr)) {
            VK_ANB_ERR("VK_ANDROID_native_buffer: could not map staging buffer.");
            return nullptr;
        }
    }

    emu->getDebugUtilsHelper().addDebugLabel(out->mStagingBuffer, "ANB_StagingBuffer:%d",
                                             out->mColorBufferHandle);
    emu->getDebugUtilsHelper().addDebugLabel(out->mStagingBufferMemory, "ANB_StagingMemory:%d",
                                             out->mColorBufferHandle);

    out->mQsriWaitFencePool = std::make_unique<AndroidNativeBufferInfo::QsriWaitFencePool>(
        out->mDeviceDispatch, out->mDevice);
    out->mQsriTimeline = std::make_unique<VkQsriTimeline>();

    return out;
}

AndroidNativeBufferInfo::~AndroidNativeBufferInfo() {
    if (mDeviceDispatch == nullptr) {
        return;
    }

    if (mDevice == VK_NULL_HANDLE) {
        return;
    }

    for (auto& queueState : mQueueStates) {
        queueState.teardown(mDeviceDispatch, mDevice);
    }
    mQueueStates.clear();

    mAcquireQueueState.teardown(mDeviceDispatch, mDevice);

    if (mImage != VK_NULL_HANDLE) {
        mDeviceDispatch->vkDestroyImage(mDevice, mImage, nullptr);
    }
    if (mImageMemory != VK_NULL_HANDLE) {
        mDeviceDispatch->vkFreeMemory(mDevice, mImageMemory, nullptr);
    }

    if (mMappedStagingPtr != nullptr) {
        mDeviceDispatch->vkUnmapMemory(mDevice, mStagingBufferMemory);
    }
    if (mStagingBuffer != VK_NULL_HANDLE) {
        mDeviceDispatch->vkDestroyBuffer(mDevice, mStagingBuffer, nullptr);
    }
    if (mStagingBufferMemory != VK_NULL_HANDLE) {
        mDeviceDispatch->vkFreeMemory(mDevice, mStagingBufferMemory, nullptr);
    }
}

void getGralloc0Usage(VkFormat format, VkImageUsageFlags imageUsage, int* usage_out) {
    // Pick some default flexible values for gralloc usage for now.
    (void)format;
    (void)imageUsage;
    *usage_out = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
                 GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;
}

// Taken from Android GrallocUsageConversion.h
void getGralloc1Usage(VkFormat format, VkImageUsageFlags imageUsage,
                      VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
                      uint64_t* consumerUsage_out, uint64_t* producerUsage_out) {
    // Pick some default flexible values for gralloc usage for now.
    (void)format;
    (void)imageUsage;
    (void)swapchainImageUsage;

    constexpr int usage = GRALLOC_USAGE_SW_READ_OFTEN | GRALLOC_USAGE_SW_WRITE_OFTEN |
                          GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_TEXTURE;

    constexpr uint64_t PRODUCER_MASK =
        GRALLOC1_PRODUCER_USAGE_CPU_READ |
        /* GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN | */
        GRALLOC1_PRODUCER_USAGE_CPU_WRITE |
        /* GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN | */
        GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET | GRALLOC1_PRODUCER_USAGE_PROTECTED |
        GRALLOC1_PRODUCER_USAGE_CAMERA | GRALLOC1_PRODUCER_USAGE_VIDEO_DECODER |
        GRALLOC1_PRODUCER_USAGE_SENSOR_DIRECT_DATA;
    constexpr uint64_t CONSUMER_MASK =
        GRALLOC1_CONSUMER_USAGE_CPU_READ |
        /* GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN | */
        GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE | GRALLOC1_CONSUMER_USAGE_HWCOMPOSER |
        GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET | GRALLOC1_CONSUMER_USAGE_CURSOR |
        GRALLOC1_CONSUMER_USAGE_VIDEO_ENCODER | GRALLOC1_CONSUMER_USAGE_CAMERA |
        GRALLOC1_CONSUMER_USAGE_RENDERSCRIPT | GRALLOC1_CONSUMER_USAGE_GPU_DATA_BUFFER;

    *producerUsage_out = static_cast<uint64_t>(usage) & PRODUCER_MASK;
    *consumerUsage_out = static_cast<uint64_t>(usage) & CONSUMER_MASK;

    if ((static_cast<uint32_t>(usage) & GRALLOC_USAGE_SW_READ_OFTEN) ==
        GRALLOC_USAGE_SW_READ_OFTEN) {
        *producerUsage_out |= GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN;
        *consumerUsage_out |= GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN;
    }

    if ((static_cast<uint32_t>(usage) & GRALLOC_USAGE_SW_WRITE_OFTEN) ==
        GRALLOC_USAGE_SW_WRITE_OFTEN) {
        *producerUsage_out |= GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN;
    }
}

void AndroidNativeBufferInfo::QueueState::setup(VulkanDispatch* vk, VkDevice device,
                                                VkQueue queueIn, uint32_t queueFamilyIndexIn,
                                                std::mutex* queueMutexIn) {
    queue = queueIn;
    queueFamilyIndex = queueFamilyIndexIn;
    queueMutex = queueMutexIn;

    VkCommandPoolCreateInfo poolCreateInfo = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        0,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        queueFamilyIndex,
    };

    vk->vkCreateCommandPool(device, &poolCreateInfo, nullptr, &pool);

    VkCommandBufferAllocateInfo cbAllocInfo = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, 0, pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1,
    };

    vk->vkAllocateCommandBuffers(device, &cbAllocInfo, &cb);

    vk->vkAllocateCommandBuffers(device, &cbAllocInfo, &cb2);

    VkFenceCreateInfo fenceCreateInfo = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        0,
        0,
    };

    vk->vkCreateFence(device, &fenceCreateInfo, nullptr, &fence);
}

void AndroidNativeBufferInfo::QueueState::teardown(VulkanDispatch* vk, VkDevice device) {
    if (latestUse) {
        latestUse->wait();
    }

    if (queue) {
        std::lock_guard<std::mutex> lock(*queueMutex);
        vk->vkQueueWaitIdle(queue);
    }
    if (cb) vk->vkFreeCommandBuffers(device, pool, 1, &cb);
    if (pool) vk->vkDestroyCommandPool(device, pool, nullptr);
    if (fence) vk->vkDestroyFence(device, fence, nullptr);

    queueMutex = nullptr;
    queue = VK_NULL_HANDLE;
    pool = VK_NULL_HANDLE;
    cb = VK_NULL_HANDLE;
    fence = VK_NULL_HANDLE;
    queueFamilyIndex = 0;
}

VkResult AndroidNativeBufferInfo::on_vkAcquireImageANDROID(VkEmulation* emu,
                                                           VulkanDispatch* vk, VkDevice device,
                                                           VkQueue defaultQueue,
                                                           uint32_t defaultQueueFamilyIndex,
                                                           std::mutex* defaultQueueMutex,
                                                           VkSemaphore semaphore, VkFence fence) {
    const bool firstTimeSetup = !mEverSynced && !mEverAcquired;
    mEverAcquired = true;

    if (firstTimeSetup) {
        mLastUsedQueueFamilyIndex = defaultQueueFamilyIndex;
        VkSubmitInfo submitInfo = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            0,
            0,
            nullptr,
            nullptr,
            0,
            nullptr,
            (uint32_t)(semaphore == VK_NULL_HANDLE ? 0 : 1),
            semaphore == VK_NULL_HANDLE ? nullptr : &semaphore,
        };
        std::lock_guard<std::mutex> qlock(*defaultQueueMutex);
        VK_CHECK(vk->vkQueueSubmit(defaultQueue, 1, &submitInfo, fence));
        return VK_SUCCESS;
    }

    if (mLastUsedQueueFamilyIndex == INVALID_QUEUE_FAMILY_INDEX) {
        ERR("AndroidNativeBufferInfo missing last used queue.");
        return VK_ERROR_INITIALIZATION_FAILED;
    }

    // Setup queue state for this queue family index.
    auto queueFamilyIndex = mLastUsedQueueFamilyIndex;
    if (queueFamilyIndex >= mQueueStates.size()) {
        mQueueStates.resize(queueFamilyIndex + 1);
    }
    QueueState& queueState = mQueueStates[queueFamilyIndex];
    if (!queueState.queue) {
        queueState.setup(mDeviceDispatch, mDevice, defaultQueue, queueFamilyIndex,
                         defaultQueueMutex);
    }

    // If we used the Vulkan image without copying it back
    // to the CPU, reset the layout to PRESENT.
    if (mUseVulkanNativeImage) {
        VkCommandBufferBeginInfo beginInfo = {
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            0,
            VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            nullptr /* no inheritance info */,
        };

        vk->vkBeginCommandBuffer(queueState.cb2, &beginInfo);

        emu->getDebugUtilsHelper().cmdBeginDebugLabel(
            queueState.cb2, "vkAcquireImageANDROID(ColorBuffer:%d)", mColorBufferHandle);

        VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
            .dstQueueFamilyIndex = mLastUsedQueueFamilyIndex,
            .image = mImage,
            .subresourceRange =
                {
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    0,
                    1,
                    0,
                    1,
                },
        };
        vk->vkCmdPipelineBarrier(queueState.cb2, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &queueTransferBarrier);

        emu->getDebugUtilsHelper().cmdEndDebugLabel(queueState.cb2);

        vk->vkEndCommandBuffer(queueState.cb2);

        VkSubmitInfo submitInfo = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            0,
            0,
            nullptr,
            nullptr,
            1,
            &queueState.cb2,
            (uint32_t)(semaphore == VK_NULL_HANDLE ? 0 : 1),
            semaphore == VK_NULL_HANDLE ? nullptr : &semaphore,
        };

        std::lock_guard<std::mutex> queueLock(*queueState.queueMutex);
        // TODO(kaiyili): initiate ownership transfer from DisplayVk here
        VK_CHECK(vk->vkQueueSubmit(queueState.queue, 1, &submitInfo, fence));
    } else {
        const VkSubmitInfo submitInfo = {
            VK_STRUCTURE_TYPE_SUBMIT_INFO,
            0,
            0,
            nullptr,
            nullptr,
            0,
            nullptr,
            (uint32_t)(semaphore == VK_NULL_HANDLE ? 0 : 1),
            semaphore == VK_NULL_HANDLE ? nullptr : &semaphore,
        };
        std::lock_guard<std::mutex> queueLock(*queueState.queueMutex);
        VK_CHECK(vk->vkQueueSubmit(queueState.queue, 1, &submitInfo, fence));
    }

    return VK_SUCCESS;
}

static constexpr uint64_t kTimeoutNs = 3ULL * 1000000000ULL;

VkResult AndroidNativeBufferInfo::on_vkQueueSignalReleaseImageANDROID(
    VkEmulation* emu, VulkanDispatch* vk, uint32_t queueFamilyIndex,
    VkQueue queue, std::mutex* queueMutex, uint32_t waitSemaphoreCount,
    const VkSemaphore* pWaitSemaphores, int* pNativeFenceFd) {
    const uint64_t traceId = gfxstream::host::GetUniqueTracingId();
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "vkQSRI syncImageToColorBuffer()",
                          GFXSTREAM_TRACE_FLOW(traceId));

    auto fb = FrameBuffer::getFB();
    fb->lock();

    // Implicitly synchronized
    *pNativeFenceFd = -1;

    mEverSynced = true;
    mLastUsedQueueFamilyIndex = queueFamilyIndex;

    // Setup queue state for this queue family index.
    if (queueFamilyIndex >= mQueueStates.size()) {
        mQueueStates.resize(queueFamilyIndex + 1);
    }

    auto& queueState = mQueueStates[queueFamilyIndex];

    if (!queueState.queue) {
        queueState.setup(vk, mDevice, queue, queueFamilyIndex, queueMutex);
    }

    // Record our synchronization commands.
    VkCommandBufferBeginInfo beginInfo = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        0,
        VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        nullptr /* no inheritance info */,
    };

    vk->vkBeginCommandBuffer(queueState.cb, &beginInfo);

    emu->getDebugUtilsHelper().cmdBeginDebugLabel(
        queueState.cb, "vkQueueSignalReleaseImageANDROID(ColorBuffer:%d)", mColorBufferHandle);

    // If using the Vulkan image directly (rather than copying it back to
    // the CPU), change its layout for that use.
    if (mUseVulkanNativeImage) {
        VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            .srcQueueFamilyIndex = queueFamilyIndex,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
            .image = mImage,
            .subresourceRange =
                {
                    VK_IMAGE_ASPECT_COLOR_BIT,
                    0,
                    1,
                    0,
                    1,
                },
        };
        vk->vkCmdPipelineBarrier(queueState.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &queueTransferBarrier);

    } else {
        // Not a GL texture. Read it back and put it back in present layout.

        // From the spec: If an application does not need the contents of a resource
        // to remain valid when transferring from one queue family to another, then
        // the ownership transfer should be skipped.
        // We definitely need to transition the image to
        // VK_TRANSFER_SRC_OPTIMAL and back.
        VkImageMemoryBarrier presentToTransferSrc = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            0,
            0,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImage,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
        };

        vk->vkCmdPipelineBarrier(queueState.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &presentToTransferSrc);

        VkBufferImageCopy region = {
            0 /* buffer offset */,
            mExtent.width,
            mExtent.height,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                0,
                1,
            },
            {0, 0, 0},
            mExtent,
        };

        vk->vkCmdCopyImageToBuffer(queueState.cb, mImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   mStagingBuffer, 1, &region);

        // Transfer back to present src.
        VkImageMemoryBarrier backToPresentSrc = {
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            0,
            VK_ACCESS_TRANSFER_READ_BIT,
            0,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_QUEUE_FAMILY_IGNORED,
            VK_QUEUE_FAMILY_IGNORED,
            mImage,
            {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0,
                1,
                0,
                1,
            },
        };

        vk->vkCmdPipelineBarrier(queueState.cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &backToPresentSrc);
    }

    emu->getDebugUtilsHelper().cmdEndDebugLabel(queueState.cb);

    vk->vkEndCommandBuffer(queueState.cb);

    std::vector<VkPipelineStageFlags> pipelineStageFlags;
    pipelineStageFlags.resize(waitSemaphoreCount, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);

    VkSubmitInfo submitInfo = {
        VK_STRUCTURE_TYPE_SUBMIT_INFO,
        0,
        waitSemaphoreCount,
        pWaitSemaphores,
        pipelineStageFlags.data(),
        1,
        &queueState.cb,
        0,
        nullptr,
    };

    // TODO(kaiyili): initiate ownership transfer to DisplayVk here.
    VkFence qsriFence = mQsriWaitFencePool->getFenceFromPool();
    std::lock_guard<std::mutex> qLock(*queueMutex);
    VK_CHECK(vk->vkQueueSubmit(queueState.queue, 1, &submitInfo, qsriFence));
    auto waitForQsriFenceTask = [this, vk, device = mDevice, qsriFence, traceId] {
        (void)traceId;
        GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "Wait for QSRI fence",
                              GFXSTREAM_TRACE_FLOW(traceId));

        VK_ANB_DEBUG_OBJ(this, "wait callback: enter");
        VK_ANB_DEBUG_OBJ(this, "wait callback: wait for fence %p...", qsriFence);
        VkResult res = vk->vkWaitForFences(device, 1, &qsriFence, VK_FALSE, kTimeoutNs);
        switch (res) {
            case VK_SUCCESS:
                break;
            case VK_TIMEOUT:
                VK_ANB_ERR("Timeout when waiting for the Qsri fence.");
                break;
            default:
                ERR("Failed to wait for QSRI fence: %s\n", string_VkResult(res));
                VK_CHECK(res);
        }
        VK_ANB_DEBUG_OBJ(this, "wait callback: wait for fence %p...(done)", qsriFence);
        mQsriWaitFencePool->returnFence(qsriFence);
    };
    fb->unlock();

    if (mUseVulkanNativeImage) {
        VK_ANB_DEBUG_OBJ(this, "using native image, so use sync thread to wait");
        // Queue wait to sync thread with completion callback
        // Pass anbInfo by value to get a ref
        auto waitable = emu->getCallbacks().scheduleAsyncWork(
            [waitForQsriFenceTask = std::move(waitForQsriFenceTask), this]() mutable {
                waitForQsriFenceTask();
                mQsriTimeline->signalNextPresentAndPoll();
            },
            "wait for the guest Qsri VkFence signaled");

        queueState.latestUse = std::move(waitable);
    } else {
        VK_ANB_DEBUG_OBJ(this, "not using native image, so wait right away");
        waitForQsriFenceTask();

        const VkMappedMemoryRange toInvalidate = {
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, 0, mStagingBufferMemory, 0, VK_WHOLE_SIZE,
        };
        vk->vkInvalidateMappedMemoryRanges(mDevice, 1, &toInvalidate);

        // Copy to from staging buffer to color buffer
        uint32_t bpp = 4; /* format always rgba8...not */
        switch (mVkFormat) {
            case VK_FORMAT_R5G6B5_UNORM_PACK16:
                bpp = 2;
                break;
            case VK_FORMAT_R8G8B8_UNORM:
                bpp = 3;
                break;
            default:
            case VK_FORMAT_R8G8B8A8_UNORM:
            case VK_FORMAT_B8G8R8A8_UNORM:
                bpp = 4;
                break;
        }
        const void* bytes = mMappedStagingPtr;
        const size_t bytesSize = bpp * mExtent.width * mExtent.height;
        emu->getCallbacks().flushColorBufferFromBytes(mColorBufferHandle, bytes, bytesSize);

        mQsriTimeline->signalNextPresentAndPoll();
    }

    return VK_SUCCESS;
}

AsyncResult AndroidNativeBufferInfo::registerQsriCallback(VkImage image,
                                                          VkQsriTimeline::Callback callback) {
    if (!mDeviceDispatch) {
        ERR("Attempted to register QSRI callback on VkImage:%p with uninitialized ANB info.",
            image);
        return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }

    // Could be null or mismatched image, check later
    if (image != mImage) {
        ERR("Attempted on register QSRI callback on VkImage:%p with wrong image %p.", image,
            mImage);
        return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }

    mQsriTimeline->registerCallbackForNextPresentAndPoll(std::move(callback));
    return AsyncResult::OK_AND_CALLBACK_SCHEDULED;
}

}  // namespace vk
}  // namespace gfxstream
