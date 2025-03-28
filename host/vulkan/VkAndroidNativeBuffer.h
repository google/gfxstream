// Copyright 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <vector>

#include "VkCommonOperations.h"
#include "VkQsriTimeline.h"
#include "aemu/base/AsyncResult.h"
#include "aemu/base/BumpPool.h"
#include "aemu/base/ThreadAnnotations.h"
#include "aemu/base/synchronization/ConditionVariable.h"
#include "aemu/base/synchronization/Lock.h"
#include "gfxstream/host/BackendCallbacks.h"
#include "goldfish_vk_private_defs.h"

namespace gfxstream {
namespace vk {

struct VulkanDispatch;

// This class provides methods to create and query information about Android
// native buffers in the context of creating Android swapchain images that have
// Android native buffer backing.

class AndroidNativeBufferInfo {
   public:
    static std::unique_ptr<AndroidNativeBufferInfo> create(
        VkEmulation* emu, VulkanDispatch* vk, VkDevice device, android::base::BumpPool& allocator,
        const VkImageCreateInfo* pCreateInfo, const VkNativeBufferANDROID* nativeBufferANDROID,
        const VkAllocationCallbacks* pAllocator, const VkPhysicalDeviceMemoryProperties* memProps);

    AndroidNativeBufferInfo(const AndroidNativeBufferInfo&) = delete;
    AndroidNativeBufferInfo& operator=(const AndroidNativeBufferInfo&) = delete;

    AndroidNativeBufferInfo(AndroidNativeBufferInfo&&) = delete;
    AndroidNativeBufferInfo& operator=(AndroidNativeBufferInfo&&) = delete;

    ~AndroidNativeBufferInfo();

    VkImage getImage() const { return mImage; }

    bool isExternallyBacked() const { return mExternallyBacked; }

    bool isUsingNativeImage() const { return mUseVulkanNativeImage; }

    uint32_t getColorBufferHandle() const { return mColorBufferHandle; }

    VkResult on_vkAcquireImageANDROID(VkEmulation* emu, VulkanDispatch* vk, VkDevice device, VkQueue defaultQueue,
                                      uint32_t defaultQueueFamilyIndex,
                                      std::mutex* defaultQueueMutex, VkSemaphore semaphore,
                                      VkFence fence);

    VkResult on_vkQueueSignalReleaseImageANDROID(VkEmulation* emu,
                                                 VulkanDispatch* vk, uint32_t queueFamilyIndex,
                                                 VkQueue queue, std::mutex* queueMutex,
                                                 uint32_t waitSemaphoreCount,
                                                 const VkSemaphore* pWaitSemaphores,
                                                 int* pNativeFenceFd);

    AsyncResult registerQsriCallback(VkImage image, VkQsriTimeline::Callback callback);

   private:
    AndroidNativeBufferInfo() = default;

    VulkanDispatch* mDeviceDispatch = nullptr;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkFormat mVkFormat = VK_FORMAT_UNDEFINED;
    VkExtent3D mExtent = {};
    VkImageUsageFlags mUsage = 0;
    std::vector<uint32_t> mQueueFamilyIndices;

    int mAhbFormat = 0;
    int mStride = 0;
    uint32_t mColorBufferHandle = 0;
    bool mExternallyBacked = false;
    bool mUseVulkanNativeImage = false;

    // We will be using separate allocations for image versus staging memory,
    // because not all host Vulkan drivers will support directly rendering to
    // host visible memory in a layout that glTexSubImage2D can consume.

    // If we are using external memory, these memories are imported
    // to the current instance.
    VkDeviceMemory mImageMemory = VK_NULL_HANDLE;
    uint32_t mImageMemoryTypeIndex = -1;

    VkDeviceMemory mStagingBufferMemory = VK_NULL_HANDLE;
    VkBuffer mStagingBuffer = VK_NULL_HANDLE;
    uint8_t* mMappedStagingPtr = nullptr;

    // To be populated later as we go.
    VkImage mImage = VK_NULL_HANDLE;
    VkMemoryRequirements mImageMemoryRequirements = {};

    // The queue over which we send the buffer/image copy commands depends on
    // the queue over which vkQueueSignalReleaseImageANDROID happens.
    // It is assumed that the VkImage object has been created by Android swapchain layer
    // with all the relevant queue family indices for sharing set properly.
    struct QueueState {
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cb = VK_NULL_HANDLE;
        VkCommandBuffer cb2 = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        std::mutex* queueMutex = nullptr;
        uint32_t queueFamilyIndex = 0;
        std::optional<CancelableFuture> latestUse;
        void setup(VulkanDispatch* vk, VkDevice device, VkQueue queue, uint32_t queueFamilyIndex,
                   std::mutex* queueMutex);
        void teardown(VulkanDispatch* vk, VkDevice device);
    };
    // We keep one QueueState for each queue family index used by the guest
    // in vkQueuePresentKHR.
    std::vector<QueueState> mQueueStates;

    // Did we ever sync the Vulkan image with a ColorBuffer?
    // If so, set everSynced along with the queue family index
    // used to do that.
    // If the swapchain image was created with exclusive sharing
    // mode (reflected in this struct's |sharingMode| field),
    // this part doesn't really matter.
    bool mEverSynced = false;
    static constexpr uint32_t INVALID_QUEUE_FAMILY_INDEX = std::numeric_limits<uint32_t>::max();
    uint32_t mLastUsedQueueFamilyIndex = INVALID_QUEUE_FAMILY_INDEX;

    // On first acquire, we might use a different queue family
    // to initially set the semaphore/fence to be signaled.
    // Track that here.
    bool mEverAcquired = false;
    QueueState mAcquireQueueState;

    // State that is of interest when interacting with sync fds and SyncThread.
    // Protected by this lock and condition variable.
    class QsriWaitFencePool {
       public:
        QsriWaitFencePool(VulkanDispatch*, VkDevice);
        ~QsriWaitFencePool();
        VkFence getFenceFromPool();
        void returnFence(VkFence fence);

       private:
        std::mutex mMutex;

        VulkanDispatch* mVk;
        VkDevice mDevice;

        // A pool of vkFences for waiting (optimization so we don't keep recreating them every
        // time).
        std::vector<VkFence> mAvailableFences GUARDED_BY(mMutex);
        std::unordered_set<VkFence> mUsedFences GUARDED_BY(mMutex);
    };

    std::unique_ptr<QsriWaitFencePool> mQsriWaitFencePool;
    std::unique_ptr<VkQsriTimeline> mQsriTimeline;
};

void getGralloc0Usage(VkFormat format, VkImageUsageFlags imageUsage, int* usage_out);
void getGralloc1Usage(VkFormat format, VkImageUsageFlags imageUsage,
                      VkSwapchainImageUsageFlagsANDROID swapchainImageUsage,
                      uint64_t* consumerUsage_out, uint64_t* producerUsage_out);

}  // namespace vk
}  // namespace gfxstream
