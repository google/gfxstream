// Copyright 2024 The Android Open Source Project
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

#ifdef _WIN32
#include <malloc.h>
#endif

#include <stdlib.h>

#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>

#include "DebugUtilsHelper.h"
#include "DeviceOpTracker.h"
#include "Handle.h"
#include "VkEmulatedPhysicalDeviceMemory.h"
#include "VkEmulatedPhysicalDeviceQueue.h"
#include "render-utils/stream.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/memory/SharedMemory.h"
#include "gfxstream/synchronization/ConditionVariable.h"
#include "gfxstream/synchronization/Lock.h"
#include "common/goldfish_vk_deepcopy.h"
#include "vulkan/VkAndroidNativeBuffer.h"
#include "vulkan/VkFormatUtils.h"
#include "vulkan/emulated_textures/CompressedImageInfo.h"

namespace gfxstream {
namespace vk {

template <class TDispatch>
class ExternalFencePool {
   public:
    ExternalFencePool(TDispatch* dispatch, VkDevice device)
        : m_vk(dispatch), mDevice(device), mMaxSize(5) {}

    ~ExternalFencePool() {
        if (!mPool.empty()) {
            GFXSTREAM_FATAL(
                "External fence pool for VkDevice:%p destroyed but %zu fences still not destroyed.",
                mDevice, mPool.size());
        }
    }

    void add(VkFence fence) {
        gfxstream::base::AutoLock lock(mLock);
        mPool.push_back(fence);
        if (mPool.size() > mMaxSize) {
            GFXSTREAM_INFO("External fence pool for %p has increased to size %d", mDevice,
                           mPool.size());
            mMaxSize = mPool.size();
        }
    }

    VkFence pop(const VkFenceCreateInfo* pCreateInfo) {
        VkFence fence = VK_NULL_HANDLE;
        {
            gfxstream::base::AutoLock lock(mLock);
            auto it = std::find_if(mPool.begin(), mPool.end(), [this](const VkFence& fence) {
                VkResult status = m_vk->vkGetFenceStatus(mDevice, fence);
                if (status != VK_SUCCESS) {
                    if (status != VK_NOT_READY) {
                        VK_CHECK(status);
                    }

                    // Status is valid, but fence is not yet signaled
                    return false;
                }
                return true;
            });
            if (it == mPool.end()) {
                return VK_NULL_HANDLE;
            }

            fence = *it;
            mPool.erase(it);
        }

        if (!(pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)) {
            VK_CHECK(m_vk->vkResetFences(mDevice, 1, &fence));
        }

        return fence;
    }

    std::vector<VkFence> popAll() {
        gfxstream::base::AutoLock lock(mLock);
        std::vector<VkFence> popped = mPool;
        mPool.clear();
        return popped;
    }

   private:
    TDispatch* m_vk;
    VkDevice mDevice;
    gfxstream::base::Lock mLock;
    std::vector<VkFence> mPool;
    size_t mMaxSize;
};

class PrivateMemory {
public:
    PrivateMemory(size_t alignment, size_t size) {
#ifdef _WIN32
        mAddr = _aligned_malloc(size, alignment);
#else
        mAddr = aligned_alloc(alignment, size);
#endif
    }
    ~PrivateMemory() {
        if (mAddr) {
#ifdef _WIN32
            _aligned_free(mAddr);
#else
            free(mAddr);
#endif
            mAddr = nullptr;
        }
    }
    void* getAddr() {
        return mAddr;
    }
private:
    void* mAddr{nullptr};
};

// We always map the whole size on host.
// This makes it much easier to implement
// the memory map API.
struct MemoryInfo {
    // This indicates whether the VkDecoderGlobalState needs to clean up
    // and unmap the mapped memory; only the owner of the mapped memory
    // should call unmap.
    bool needUnmap = false;
    // When ptr is null, it means the VkDeviceMemory object
    // was not allocated with the HOST_VISIBLE property.
    void* ptr = nullptr;
    VkDeviceSize size;
    // GLDirectMem info
    bool directMapped = false;
    bool virtioGpuMapped = false;
    uint32_t caching = 0;
    uint64_t guestPhysAddr = 0;
    void* pageAlignedHva = nullptr;
    uint64_t sizeToPage = 0;
    uint64_t hostmemId = 0;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t memoryIndex = 0;
    // Set if the memory is backed by shared memory.
    std::optional<gfxstream::base::SharedMemory> sharedMemory;

    std::shared_ptr<PrivateMemory> privateMemory;
    // virtio-gpu blobs
    uint64_t blobId = 0;

    // Buffer, provided via vkAllocateMemory().
    std::optional<HandleType> boundBuffer;
    // ColorBuffer, provided via vkAllocateMemory().
    std::optional<HandleType> boundColorBuffer;
};

struct InstanceInfo {
    std::vector<std::string> enabledExtensionNames;
    uint32_t apiVersion = VK_MAKE_VERSION(1, 0, 0);
    VkInstance boxed = nullptr;
    bool isAngle = false;
    std::string applicationName;
    std::string engineName;
    uint32_t contextId = 0;
};

struct PhysicalDeviceInfo {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties props;
    std::unique_ptr<EmulatedPhysicalDeviceMemoryProperties> memoryPropertiesHelper;
    std::unique_ptr<EmulatedPhysicalDeviceQueueProperties> queuePropertiesHelper;
    VkPhysicalDevice boxed = nullptr;
};

struct ExternalFenceInfo {
    VkExternalSemaphoreHandleTypeFlagBits supportedBinarySemaphoreHandleTypes;
    VkExternalFenceHandleTypeFlagBits supportedFenceHandleTypes;
};

struct DeviceInfo {
    std::unordered_map<uint32_t, std::vector<VkQueue>> queues;
    std::vector<std::string> enabledExtensionNames;
    bool emulateTextureEtc2 = false;
    bool emulateTextureAstc = false;
    bool useAstcCpuDecompression = false;

    ExternalFenceInfo externalFenceInfo;
    VkPhysicalDevice physicalDevice;
    VkDevice boxed = nullptr;
    DebugUtilsHelper debugUtilsHelper = DebugUtilsHelper::withUtilsDisabled();
    std::unique_ptr<ExternalFencePool<VulkanDispatch>> externalFencePool = nullptr;
    std::set<VkFormat> imageFormats = {};  // image formats used on this device
    std::unique_ptr<GpuDecompressionPipelineManager> decompPipelines = nullptr;
    DeviceOpTrackerPtr deviceOpTracker = nullptr;
    std::optional<uint32_t> virtioGpuContextId;

    // True if this is a compressed image that needs to be decompressed on the GPU (with our
    // compute shader)
    bool needGpuDecompression(const CompressedImageInfo& cmpInfo) {
        return ((cmpInfo.isEtc2() && emulateTextureEtc2) ||
                (cmpInfo.isAstc() && emulateTextureAstc && !useAstcCpuDecompression));
    }
    bool needEmulatedDecompression(const CompressedImageInfo& cmpInfo) {
        return ((cmpInfo.isEtc2() && emulateTextureEtc2) ||
                (cmpInfo.isAstc() && emulateTextureAstc));
    }
    bool needEmulatedDecompression(VkFormat format) {
        return (gfxstream::vk::isEtc2(format) && emulateTextureEtc2) ||
               (gfxstream::vk::isAstc(format) && emulateTextureAstc);
    }
};

struct PhysicalQueuePendingOps {
    // Wrapper structure to defer queue submission calls, e.g. VkSubmitInfo2
    // Pending operations will be checked and executed when the conditions are
    // met, e.g. the valid timeline semaphore point is signalled.
    // Normally, application should make safe submissions that'd avoid deadlock
    // conditions, but when the virtual queue is active, we have to manually block
    // the submissions until they can be executed safely, without blocking the
    // signalling submissions.

    struct QueueSubmit2 {
        bool convertFrom(const VkSubmitInfo2& submit) {
            // TODO(b/379862480): Use deepcopy_VkSubmitInfo2 to support pNext values
            bool foundAnyPNext = false;
            if (submit.pNext) {
                foundAnyPNext = true;
                return false;
            }

            mWaitSemaphoreInfos.resize(submit.commandBufferInfoCount);
            for (uint32_t i = 0; i < submit.commandBufferInfoCount; i++) {
                if (submit.pWaitSemaphoreInfos[i].pNext) {
                    foundAnyPNext = true;
                }
                mWaitSemaphoreInfos[i] = submit.pWaitSemaphoreInfos[i];
            }

            mCommandBufferInfos.resize(submit.commandBufferInfoCount);
            for (uint32_t i = 0; i < submit.commandBufferInfoCount; i++) {
                if (submit.pCommandBufferInfos[i].pNext) {
                    foundAnyPNext = true;
                }
                mCommandBufferInfos[i] = submit.pCommandBufferInfos[i];
            }

            mSignalSemaphoreInfos.resize(submit.commandBufferInfoCount);
            for (uint32_t i = 0; i < submit.commandBufferInfoCount; i++) {
                if (submit.pSignalSemaphoreInfos[i].pNext) {
                    foundAnyPNext = true;
                }
                mSignalSemaphoreInfos[i] = submit.pSignalSemaphoreInfos[i];
            }

            if (foundAnyPNext) {
                return false;
            }

            mSubmitInfoCopy = submit;
            mSubmitInfoCopy.pWaitSemaphoreInfos = mWaitSemaphoreInfos.data();
            mSubmitInfoCopy.pCommandBufferInfos = mCommandBufferInfos.data();
            mSubmitInfoCopy.pSignalSemaphoreInfos = mSignalSemaphoreInfos.data();

            return true;
        }

        VkSubmitInfo2 mSubmitInfoCopy;

        std::vector<VkSemaphoreSubmitInfo> mWaitSemaphoreInfos;
        std::vector<VkCommandBufferSubmitInfo> mCommandBufferInfos;
        std::vector<VkSemaphoreSubmitInfo> mSignalSemaphoreInfos;
    };

    struct DeferredSubmitCall {
        std::vector<QueueSubmit2> mSubmits;
        VkFence mFence;
    };

    std::vector<DeferredSubmitCall> mSubmitCalls;

    VkResult queuePendingSubmission(uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence) {
        // TODO(b/379862480): VkSubmitInfo is not supported for deferred submissions, this
        // should not be called until we support VkTimelineSemaphoreSubmitInfo on pNext
        GFXSTREAM_ERROR("PhysicalQueuePendingOps: Cannot defer queue submissions with 'VkSubmitInfo'");
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    VkResult queuePendingSubmission(uint32_t submitCount, const VkSubmitInfo2* pSubmits,
                                VkFence fence) {
        PhysicalQueuePendingOps::DeferredSubmitCall deferredCall;
        for (uint32_t i = 0; i < submitCount; i++) {
            PhysicalQueuePendingOps::QueueSubmit2 deferredSubmit;
            if (!deferredSubmit.convertFrom(pSubmits[i])) {
                GFXSTREAM_ERROR("Unsupported submission type detected on virtual queue!");
                return VK_ERROR_OUT_OF_HOST_MEMORY;
            }
            deferredCall.mSubmits.push_back(deferredSubmit);
        }
        deferredCall.mFence = fence;
        mSubmitCalls.emplace_back(deferredCall);
        return VK_SUCCESS;
    }
};

struct QueueInfo {
    std::shared_ptr<std::mutex> queueMutex;
    std::shared_ptr<PhysicalQueuePendingOps> pendingOps;  // Only used if virtually shared
    VkDevice device;
    uint32_t queueFamilyIndex;
    VkQueue boxed = nullptr;
    bool usingSharedPhysicalQueue = false;

    // In order to create a virtual queue handle, we use an offset to the physical
    // queue handle value. This assumes the new generated virtual handle value will
    // be unique and won't be generated by the actual GPU. This is expected to be
    // true since most implementations will use a pointer for the handle value and
    // they will be at least 4-byte aligned. Using a small value allows us to check
    // if a given 'unboxed' queue handle value is virtual and convert into the actual
    // physical one easily, without locking for mQueueInfo.
    static const uint64_t kVirtualQueueBit = 0x1;
};

struct BufferInfo {
    VkDevice device;
    VkBufferUsageFlags usage;
    VkDeviceMemory memory = 0;
    VkDeviceSize memoryOffset = 0;
    VkDeviceSize size;
    std::shared_ptr<bool> alive{new bool(true)};
};

struct ImageInfo {
    VkDevice device;
    VkImage boxed = VK_NULL_HANDLE;
    VkImageCreateInfo imageCreateInfoShallow;
    std::unique_ptr<AndroidNativeBufferInfo> anbInfo;
    CompressedImageInfo cmpInfo;
    // ColorBuffer, provided via vkAllocateMemory().
    std::optional<HandleType> boundColorBuffer;
    // TODO: might need to use an array of layouts to represent each sub resource
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkDeviceMemory memory = VK_NULL_HANDLE;
};

struct ImageViewInfo {
    VkDevice device;
    bool needEmulatedAlpha = false;
    VkImageView boxed = VK_NULL_HANDLE;

    // Color buffer, provided via vkAllocateMemory().
    std::optional<HandleType> boundColorBuffer;
    std::shared_ptr<bool> alive{new bool(true)};
};

struct SamplerInfo {
    VkDevice device;
    bool needEmulatedAlpha = false;
    VkSampler boxed = VK_NULL_HANDLE;
    VkSamplerCreateInfo createInfo = {};
    VkSampler emulatedborderSampler = VK_NULL_HANDLE;
    gfxstream::base::BumpPool pool = gfxstream::base::BumpPool(256);
    SamplerInfo() = default;
    SamplerInfo& operator=(const SamplerInfo& other) {
        deepcopy_VkSamplerCreateInfo(&pool, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                                     &other.createInfo, &createInfo);
        device = other.device;
        needEmulatedAlpha = other.needEmulatedAlpha;
        emulatedborderSampler = other.emulatedborderSampler;
        return *this;
    }
    SamplerInfo(const SamplerInfo& other) { *this = other; }
    SamplerInfo(SamplerInfo&& other) = delete;
    SamplerInfo& operator=(SamplerInfo&& other) = delete;
    std::shared_ptr<bool> alive{new bool(true)};
};

struct FenceInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkFence boxed = VK_NULL_HANDLE;
    VulkanDispatch* vk = nullptr;

    std::mutex mutex;
    std::condition_variable cv;

    enum class State {
        kNotWaitable,   // Newly created or reset
        kWaitable,      // A submission is made, or created as signaled
        kWaiting,       // Fence waitable status is acknowledged
    };
    State state = State::kNotWaitable;

    bool external = false;

    // If this fence was used in an additional host operation that must be waited
    // upon before destruction (e.g. as part of a vkAcquireImageANDROID() call),
    // the waitable that tracking that host operation.
    std::optional<DeviceOpWaitable> latestUse;
};

struct SemaphoreInfo {
    VkDevice device;
    VkSemaphore boxed = VK_NULL_HANDLE;
    int externalHandleId = 0;
    VK_EXT_SYNC_HANDLE externalHandle = VK_EXT_SYNC_HANDLE_INVALID;
    // If this fence was used in an additional host operation that must be waited
    // upon before destruction (e.g. as part of a vkAcquireImageANDROID() call),
    // the waitable that tracking that host operation.
    std::optional<DeviceOpWaitable> latestUse;

    uint64_t lastSignalValue = 0;  // Only valid when the virtual queue feature is enabled
    bool isTimelineSemaphore = false;
};

struct DescriptorSetLayoutInfo {
    VkDevice device = 0;
    VkDescriptorSetLayout boxed = 0;
    VkDescriptorSetLayoutCreateInfo createInfo;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

struct DescriptorPoolInfo {
    VkDevice device = 0;
    VkDescriptorPool boxed = 0;
    struct PoolState {
        VkDescriptorType type;
        uint32_t descriptorCount;
        uint32_t used;
    };

    VkDescriptorPoolCreateInfo createInfo;
    uint32_t maxSets;
    uint32_t usedSets;
    std::vector<PoolState> pools;

    std::unordered_map<VkDescriptorSet, VkDescriptorSet> allocedSetsToBoxed;
    std::vector<uint64_t> poolIds;
};

struct DescriptorSetInfo {
    enum DescriptorWriteType {
        Empty = 0,
        ImageInfo = 1,
        BufferInfo = 2,
        BufferView = 3,
        InlineUniformBlock = 4,
        AccelerationStructure = 5,
    };

    struct DescriptorWrite {
        VkDescriptorType descriptorType;
        DescriptorWriteType writeType = DescriptorWriteType::Empty;
        uint32_t dstArrayElement;  // Only used for inlineUniformBlock and accelerationStructure.

        union {
            VkDescriptorImageInfo imageInfo;
            VkDescriptorBufferInfo bufferInfo;
            VkBufferView bufferView;
            VkWriteDescriptorSetInlineUniformBlockEXT inlineUniformBlock;
            VkWriteDescriptorSetAccelerationStructureKHR accelerationStructure;
        };

        std::vector<uint8_t> inlineUniformBlockBuffer;
        // Weak pointer(s) to detect if all objects on dependency chain are alive.
        std::vector<std::weak_ptr<bool>> alives;
        std::optional<HandleType> boundColorBuffer;
    };

    VkDevice device;
    VkDescriptorPool pool;
    VkDescriptorSetLayout unboxedLayout = 0;
    std::vector<std::vector<DescriptorWrite>> allWrites;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

struct ShaderModuleInfo {
    VkDevice device;
};

struct PipelineCacheInfo {
    VkDevice device;
};

struct PipelineLayoutInfo {
    VkDevice device;
};

struct PipelineInfo {
    VkDevice device;
};

struct RenderPassInfo {
    VkDevice device;
};

struct FramebufferInfo {
    VkDevice device;
    std::vector<HandleType> attachedColorBuffers;
};

typedef std::function<void()> PreprocessFunc;
struct CommandBufferInfo {
    std::vector<PreprocessFunc> preprocessFuncs = {};
    std::vector<VkCommandBuffer> subCmds = {};
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer boxed = VK_NULL_HANDLE;
    DebugUtilsHelper debugUtilsHelper = DebugUtilsHelper::withUtilsDisabled();

    // Most recently bound compute pipeline and descriptor sets. We save it here so that we can
    // restore it after doing emulated texture decompression.
    VkPipeline computePipeline = VK_NULL_HANDLE;
    uint32_t firstSet = 0;
    VkPipelineLayout descriptorLayout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> currentDescriptorSets;
    std::unordered_set<VkDescriptorSet> allDescriptorSets;
    std::vector<uint32_t> dynamicOffsets;
    std::unordered_set<HandleType> acquiredColorBuffers;
    std::unordered_set<HandleType> releasedColorBuffers;
    std::unordered_map<HandleType, VkImageLayout> cbLayouts;
    std::unordered_map<VkImage, VkImageLayout> imageLayouts;

    void reset() {
        subCmds.clear();
        computePipeline = VK_NULL_HANDLE;
        firstSet = 0;
        descriptorLayout = VK_NULL_HANDLE;
        currentDescriptorSets.clear();
        allDescriptorSets.clear();
        dynamicOffsets.clear();
        acquiredColorBuffers.clear();
        releasedColorBuffers.clear();
        cbLayouts.clear();
        imageLayouts.clear();
    }
};

struct CommandPoolInfo {
    VkDevice device = VK_NULL_HANDLE;
    VkCommandPool boxed = VK_NULL_HANDLE;
    std::unordered_set<VkCommandBuffer> cmdBuffers = {};
};

struct InstanceObjects {
    std::unordered_map<VkInstance, InstanceInfo>::node_type instance;
    std::unordered_map<VkPhysicalDevice, PhysicalDeviceInfo> physicalDevices;
    struct DeviceObjects {
        std::unordered_map<VkDevice, DeviceInfo>::node_type device;

        std::unordered_map<VkBuffer, BufferInfo> buffers;
        std::unordered_map<VkCommandBuffer, CommandBufferInfo> commandBuffers;
        std::unordered_map<VkCommandPool, CommandPoolInfo> commandPools;
        std::unordered_map<VkDescriptorPool, DescriptorPoolInfo> descriptorPools;
        std::unordered_map<VkDescriptorSet, DescriptorSetInfo> descriptorSets;
        std::unordered_map<VkDescriptorSetLayout, DescriptorSetLayoutInfo> descriptorSetLayouts;
        std::unordered_map<VkDeviceMemory, MemoryInfo> memories;
        std::unordered_map<VkFence, FenceInfo> fences;
        std::unordered_map<VkFramebuffer, FramebufferInfo> framebuffers;
        std::unordered_map<VkImage, ImageInfo> images;
        std::unordered_map<VkImageView, ImageViewInfo> imageViews;
        std::unordered_map<VkPipeline, PipelineInfo> pipelines;
        std::unordered_map<VkPipelineCache, PipelineCacheInfo> pipelineCaches;
        std::unordered_map<VkPipelineLayout, PipelineLayoutInfo> pipelineLayouts;
        std::unordered_map<VkQueue, QueueInfo> queues;
        std::unordered_map<VkRenderPass, RenderPassInfo> renderPasses;
        std::unordered_map<VkSampler, SamplerInfo> samplers;
        std::unordered_map<VkSemaphore, SemaphoreInfo> semaphores;
        std::unordered_map<VkShaderModule, ShaderModuleInfo> shaderModules;
    };
    std::vector<DeviceObjects> devices;
};

}  // namespace vk
}  // namespace gfxstream
