// Copyright (C) 2025 The Android Open Source Project
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

#include "VulkanBoxedHandles.h"

#include "VkDecoderGlobalState.h"
#include "VkDecoderInternalStructs.h"

namespace gfxstream {
namespace vk {
namespace {

struct ReadStreamRegistry {
    android::base::Lock mLock;

    std::vector<VulkanMemReadingStream*> freeStreams;

    ReadStreamRegistry() { freeStreams.reserve(100); };

    VulkanMemReadingStream* pop(const gfxstream::host::FeatureSet& features) {
        android::base::AutoLock lock(mLock);
        if (freeStreams.empty()) {
            return new VulkanMemReadingStream(nullptr, features);
        } else {
            VulkanMemReadingStream* res = freeStreams.back();
            freeStreams.pop_back();
            return res;
        }
    }

    void push(VulkanMemReadingStream* stream) {
        android::base::AutoLock lock(mLock);
        freeStreams.push_back(stream);
    }
};

static ReadStreamRegistry sReadStreamRegistry;

}  // namespace

void BoxedHandleManager::replayHandles(std::vector<BoxedHandle> handles) {
    mHandleReplayQueue.clear();
    for (BoxedHandle handle : handles) {
        mHandleReplayQueue.push_back(handle);
    }
    mHandleReplay = !mHandleReplayQueue.empty();
}

void BoxedHandleManager::clear() {
    std::lock_guard<std::mutex> lock(mMutex);
    mReverseMap.clear();
    mStore.clear();
}

uint64_t BoxedHandleManager::getHandlesCount() const {
    std::lock_guard<std::mutex> lock(mMutex);
    return mReverseMap.size();
}

BoxedHandle BoxedHandleManager::add(const BoxedHandleInfo& item, BoxedHandleTypeTag tag) {
    BoxedHandle handle;

    if (mHandleReplay) {
        handle = mHandleReplayQueue.front();
        mHandleReplayQueue.pop_front();
        mHandleReplay = !mHandleReplayQueue.empty();

        handle = (BoxedHandle)mStore.addFixed(handle, item, (size_t)tag);
    } else {
        handle = (BoxedHandle)mStore.add(item, (size_t)tag);
    }

    std::lock_guard<std::mutex> lock(mMutex);
    mReverseMap[(BoxedHandle)(item.underlying)] = handle;
    return handle;
}

void BoxedHandleManager::update(BoxedHandle handle, const BoxedHandleInfo& item,
                                BoxedHandleTypeTag tag) {
    auto storedItem = mStore.get(handle);
    UnboxedHandle oldHandle = (UnboxedHandle)storedItem->underlying;
    *storedItem = item;
    std::lock_guard<std::mutex> lock(mMutex);
    if (oldHandle) {
        mReverseMap.erase(oldHandle);
    }
    mReverseMap[(UnboxedHandle)(item.underlying)] = handle;
}

void BoxedHandleManager::remove(BoxedHandle h) {
    auto item = get(h);
    if (item) {
        std::lock_guard<std::mutex> lock(mMutex);
        mReverseMap.erase((UnboxedHandle)(item->underlying));
    }
    mStore.remove(h);
}

void BoxedHandleManager::removeDelayed(uint64_t h, VkDevice device,
                                       std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mDelayedRemoves[device].push_back({h, callback});
}

void BoxedHandleManager::processDelayedRemoves(VkDevice device) {
    std::vector<DelayedRemove> deviceDelayedRemoves;

    {
        std::lock_guard<std::mutex> lock(mMutex);

        auto it = mDelayedRemoves.find(device);
        if (it == mDelayedRemoves.end()) return;

        deviceDelayedRemoves = std::move(it->second);
        mDelayedRemoves.erase(it);
    }

    for (const auto& r : deviceDelayedRemoves) {
        auto h = r.handle;

        // VkDecoderGlobalState is not locked when callback is called.
        if (r.callback) {
            r.callback();
        }

        mStore.remove(h);
    }
}

BoxedHandleInfo* BoxedHandleManager::get(BoxedHandle handle) {
    return (BoxedHandleInfo*)mStore.get_const(handle);
}

BoxedHandle BoxedHandleManager::getBoxedFromUnboxed(UnboxedHandle unboxed) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mReverseMap.find(unboxed);
    if (it == mReverseMap.end()) {
        return 0;
    }

    return it->second;
}

BoxedHandleManager sBoxedHandleManager;

template <typename VkObjectT>
constexpr BoxedHandleTypeTag GetTag() {
    if constexpr (std::is_same_v<VkObjectT, VkAccelerationStructureKHR>) {
        return Tag_VkAccelerationStructureKHR;
    } else if constexpr (std::is_same_v<VkObjectT, VkAccelerationStructureNV>) {
        return Tag_VkAccelerationStructureNV;
    } else if constexpr (std::is_same_v<VkObjectT, VkBuffer>) {
        return Tag_VkBuffer;
    } else if constexpr (std::is_same_v<VkObjectT, VkBufferView>) {
        return Tag_VkBufferView;
    } else if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer>) {
        return Tag_VkCommandBuffer;
    } else if constexpr (std::is_same_v<VkObjectT, VkCommandPool>) {
        return Tag_VkCommandPool;
    } else if constexpr (std::is_same_v<VkObjectT, VkCuFunctionNVX>) {
        return Tag_VkCuFunctionNVX;
    } else if constexpr (std::is_same_v<VkObjectT, VkCuModuleNVX>) {
        return Tag_VkCuModuleNVX;
    } else if constexpr (std::is_same_v<VkObjectT, VkDebugReportCallbackEXT>) {
        return Tag_VkDebugReportCallbackEXT;
    } else if constexpr (std::is_same_v<VkObjectT, VkDebugUtilsMessengerEXT>) {
        return Tag_VkDebugUtilsMessengerEXT;
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorPool>) {
        return Tag_VkDescriptorPool;
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorSet>) {
        return Tag_VkDescriptorSet;
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorSetLayout>) {
        return Tag_VkDescriptorSetLayout;
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorUpdateTemplate>) {
        return Tag_VkDescriptorUpdateTemplate;
    } else if constexpr (std::is_same_v<VkObjectT, VkDevice>) {
        return Tag_VkDevice;
    } else if constexpr (std::is_same_v<VkObjectT, VkDeviceMemory>) {
        return Tag_VkDeviceMemory;
    } else if constexpr (std::is_same_v<VkObjectT, VkDisplayKHR>) {
        return Tag_VkDisplayKHR;
    } else if constexpr (std::is_same_v<VkObjectT, VkDisplayModeKHR>) {
        return Tag_VkDisplayModeKHR;
    } else if constexpr (std::is_same_v<VkObjectT, VkEvent>) {
        return Tag_VkEvent;
    } else if constexpr (std::is_same_v<VkObjectT, VkFence>) {
        return Tag_VkFence;
    } else if constexpr (std::is_same_v<VkObjectT, VkFramebuffer>) {
        return Tag_VkFramebuffer;
    } else if constexpr (std::is_same_v<VkObjectT, VkImage>) {
        return Tag_VkImage;
    } else if constexpr (std::is_same_v<VkObjectT, VkImageView>) {
        return Tag_VkImageView;
    } else if constexpr (std::is_same_v<VkObjectT, VkIndirectCommandsLayoutNV>) {
        return Tag_VkIndirectCommandsLayoutNV;
    } else if constexpr (std::is_same_v<VkObjectT, VkInstance>) {
        return Tag_VkInstance;
    } else if constexpr (std::is_same_v<VkObjectT, VkMicromapEXT>) {
        return Tag_VkMicromapEXT;
    } else if constexpr (std::is_same_v<VkObjectT, VkPhysicalDevice>) {
        return Tag_VkPhysicalDevice;
    } else if constexpr (std::is_same_v<VkObjectT, VkPipeline>) {
        return Tag_VkPipeline;
    } else if constexpr (std::is_same_v<VkObjectT, VkPipelineCache>) {
        return Tag_VkPipelineCache;
    } else if constexpr (std::is_same_v<VkObjectT, VkPipelineLayout>) {
        return Tag_VkPipelineLayout;
    } else if constexpr (std::is_same_v<VkObjectT, VkPrivateDataSlot>) {
        return Tag_VkPrivateDataSlot;
    } else if constexpr (std::is_same_v<VkObjectT, VkQueryPool>) {
        return Tag_VkQueryPool;
    } else if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        return Tag_VkQueue;
    } else if constexpr (std::is_same_v<VkObjectT, VkRenderPass>) {
        return Tag_VkRenderPass;
    } else if constexpr (std::is_same_v<VkObjectT, VkSampler>) {
        return Tag_VkSampler;
    } else if constexpr (std::is_same_v<VkObjectT, VkSamplerYcbcrConversion>) {
        return Tag_VkSamplerYcbcrConversion;
    } else if constexpr (std::is_same_v<VkObjectT, VkSemaphore>) {
        return Tag_VkSemaphore;
    } else if constexpr (std::is_same_v<VkObjectT, VkShaderModule>) {
        return Tag_VkShaderModule;
    } else if constexpr (std::is_same_v<VkObjectT, VkSurfaceKHR>) {
        return Tag_VkSurfaceKHR;
    } else if constexpr (std::is_same_v<VkObjectT, VkSwapchainKHR>) {
        return Tag_VkSwapchainKHR;
    } else if constexpr (std::is_same_v<VkObjectT, VkValidationCacheEXT>) {
        return Tag_VkValidationCacheEXT;
    } else {
        static_assert(sizeof(VkObjectT) == 0,
                      "Unhandled VkObjectT. Please update BoxedHandleTypeTag.");
    }
}

template <typename VkObjectT>
constexpr const char* GetTypeStr() {
    if constexpr (std::is_same_v<VkObjectT, VkAccelerationStructureKHR>) {
        return "VkAccelerationStructureKHR";
    } else if constexpr (std::is_same_v<VkObjectT, VkAccelerationStructureNV>) {
        return "VkAccelerationStructureNV";
    } else if constexpr (std::is_same_v<VkObjectT, VkBuffer>) {
        return "VkBuffer";
    } else if constexpr (std::is_same_v<VkObjectT, VkBufferView>) {
        return "VkBufferView";
    } else if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer>) {
        return "VkCommandBuffer";
    } else if constexpr (std::is_same_v<VkObjectT, VkCommandPool>) {
        return "VkCommandPool";
    } else if constexpr (std::is_same_v<VkObjectT, VkCuFunctionNVX>) {
        return "VkCuFunctionNVX";
    } else if constexpr (std::is_same_v<VkObjectT, VkCuModuleNVX>) {
        return "VkCuModuleNVX";
    } else if constexpr (std::is_same_v<VkObjectT, VkDebugReportCallbackEXT>) {
        return "VkDebugReportCallbackEXT";
    } else if constexpr (std::is_same_v<VkObjectT, VkDebugUtilsMessengerEXT>) {
        return "VkDebugUtilsMessengerEXT";
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorPool>) {
        return "VkDescriptorPool";
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorSet>) {
        return "VkDescriptorSet";
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorSetLayout>) {
        return "VkDescriptorSetLayout";
    } else if constexpr (std::is_same_v<VkObjectT, VkDescriptorUpdateTemplate>) {
        return "VkDescriptorUpdateTemplate";
    } else if constexpr (std::is_same_v<VkObjectT, VkDevice>) {
        return "VkDevice";
    } else if constexpr (std::is_same_v<VkObjectT, VkDeviceMemory>) {
        return "VkDeviceMemory";
    } else if constexpr (std::is_same_v<VkObjectT, VkDisplayKHR>) {
        return "VkDisplayKHR";
    } else if constexpr (std::is_same_v<VkObjectT, VkDisplayModeKHR>) {
        return "VkDisplayModeKHR";
    } else if constexpr (std::is_same_v<VkObjectT, VkEvent>) {
        return "VkEvent";
    } else if constexpr (std::is_same_v<VkObjectT, VkFence>) {
        return "VkFence";
    } else if constexpr (std::is_same_v<VkObjectT, VkFramebuffer>) {
        return "VkFramebuffer";
    } else if constexpr (std::is_same_v<VkObjectT, VkImage>) {
        return "VkImage";
    } else if constexpr (std::is_same_v<VkObjectT, VkImageView>) {
        return "VkImageView";
    } else if constexpr (std::is_same_v<VkObjectT, VkIndirectCommandsLayoutNV>) {
        return "VkIndirectCommandsLayoutNV";
    } else if constexpr (std::is_same_v<VkObjectT, VkInstance>) {
        return "VkInstance";
    } else if constexpr (std::is_same_v<VkObjectT, VkMicromapEXT>) {
        return "VkMicromapEXT";
    } else if constexpr (std::is_same_v<VkObjectT, VkPhysicalDevice>) {
        return "VkPhysicalDevice";
    } else if constexpr (std::is_same_v<VkObjectT, VkPipeline>) {
        return "VkPipeline";
    } else if constexpr (std::is_same_v<VkObjectT, VkPipelineCache>) {
        return "VkPipelineCache";
    } else if constexpr (std::is_same_v<VkObjectT, VkPipelineLayout>) {
        return "VkPipelineLayout";
    } else if constexpr (std::is_same_v<VkObjectT, VkPrivateDataSlot>) {
        return "VkPrivateDataSlot";
    } else if constexpr (std::is_same_v<VkObjectT, VkQueryPool>) {
        return "VkQueryPool";
    } else if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        return "VkQueue";
    } else if constexpr (std::is_same_v<VkObjectT, VkRenderPass>) {
        return "VkRenderPass";
    } else if constexpr (std::is_same_v<VkObjectT, VkSampler>) {
        return "VkSampler";
    } else if constexpr (std::is_same_v<VkObjectT, VkSamplerYcbcrConversion>) {
        return "VkSamplerYcbcrConversion";
    } else if constexpr (std::is_same_v<VkObjectT, VkSemaphore>) {
        return "VkSemaphore";
    } else if constexpr (std::is_same_v<VkObjectT, VkShaderModule>) {
        return "VkShaderModule";
    } else if constexpr (std::is_same_v<VkObjectT, VkSurfaceKHR>) {
        return "VkSurfaceKHR";
    } else if constexpr (std::is_same_v<VkObjectT, VkSwapchainKHR>) {
        return "VkSwapchainKHR";
    } else if constexpr (std::is_same_v<VkObjectT, VkValidationCacheEXT>) {
        return "VkValidationCacheEXT";
    } else {
        static_assert(sizeof(VkObjectT) == 0,
                      "Unhandled VkObjectT. Please update BoxedHandleTypeTag.");
    }
}

template <typename VkObjectT>
VkObjectT new_boxed_VkType(VkObjectT underlying, bool dispatchable = false, VulkanDispatch* dispatch = nullptr, bool ownsDispatch = false) {
    BoxedHandleInfo info;
    info.underlying = (uint64_t)underlying;
    if (dispatchable) {
        if (dispatch != nullptr) {
            info.dispatch = dispatch;
        } else {
            info.dispatch = new VulkanDispatch();
        }
        info.ownDispatch = ownsDispatch;
        info.ordMaintInfo = new OrderMaintenanceInfo();
        info.readStream = nullptr;
    }
    return (VkObjectT)sBoxedHandleManager.add(info, GetTag<VkObjectT>());
}

template <typename VkObjectT>
void delete_VkType(VkObjectT boxed) {
    if (boxed == VK_NULL_HANDLE) {
        return;
    }

    BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
    if (info == nullptr) {
        return;
    }

    releaseOrderMaintInfo(info->ordMaintInfo);

    if (info->readStream) {
        sReadStreamRegistry.push(info->readStream);
        info->readStream = nullptr;
    }

    sBoxedHandleManager.remove((uint64_t)boxed);
}

template <typename VkObjectT>
void delayed_delete_VkType(VkObjectT boxed, VkDevice device, std::function<void()> callback) {
    if (boxed == VK_NULL_HANDLE) {
        return;
    }

    sBoxedHandleManager.removeDelayed((uint64_t)boxed, device, std::move(callback));
}

// Custom unbox_* functions or GOLDFISH_VK_LIST_DISPATCHABLE_CUSTOM_UNBOX_HANDLE_TYPES
// VkQueue objects can be virtual, meaning that multiple boxed queues can map into a single
// physical queue on the host GPU. Some conversion is needed for unboxing to physical.
VkQueue unbox_VkQueueImpl(VkQueue boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
    if (!info) {
        return VK_NULL_HANDLE;
    }
    const uint64_t unboxedQueue64 = info->underlying;

    // Use VulkanVirtualQueue directly to avoid locking for hasVirtualGraphicsQueue call.
    if (VkDecoderGlobalState::get()->getFeatures().VulkanVirtualQueue.enabled) {
        // Clear virtual bit and unbox into the actual physical queue handle
        return (VkQueue)(unboxedQueue64 & ~QueueInfo::kVirtualQueueBit);
    }

    return (VkQueue)(unboxedQueue64);
}

template <typename VkObjectT>
VkObjectT unbox_VkType(VkObjectT boxed) {
    if (boxed == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkObjectT unboxed = VK_NULL_HANDLE;

    if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        unboxed = unbox_VkQueueImpl(boxed);
    } else {
        BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
        if (info == nullptr) {
            if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer> ||
                          std::is_same_v<VkObjectT, VkDevice> ||
                          std::is_same_v<VkObjectT, VkInstance> ||
                          std::is_same_v<VkObjectT, VkPhysicalDevice> ||
                          std::is_same_v<VkObjectT, VkQueue>) {
                ERR("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed);
            } else if constexpr (std::is_same_v<VkObjectT, VkFence>) {
                // TODO: investigate.
            } else {
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
                        << "Failed to unbox "
                        << GetTypeStr<VkObjectT>()
                        << " "
                        << boxed
                        << ", not found.";
            }
            unboxed = VK_NULL_HANDLE;
        } else {
            unboxed = (VkObjectT)info->underlying;
        }
    }

    return unboxed;
}

template <typename VkObjectT>
VkObjectT try_unbox_VkType(VkObjectT boxed) {
    if (boxed == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    VkObjectT unboxed = VK_NULL_HANDLE;

    if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        unboxed = unbox_VkQueueImpl(boxed);
    } else {
        BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
        if (info != nullptr) {
            unboxed = (VkObjectT)info->underlying;
        }
    }

    if (unboxed == VK_NULL_HANDLE) {
        WARN("Failed to try unbox %s %p", GetTypeStr<VkObjectT>(), boxed);
    }

    return unboxed;
}

template <typename VkObjectT>
VkObjectT unboxed_to_boxed_non_dispatchable_VkType(VkObjectT unboxed) {
    if (unboxed == VK_NULL_HANDLE) {
        return VK_NULL_HANDLE;
    }

    return (VkObjectT)sBoxedHandleManager.getBoxedFromUnboxed((uint64_t)(uintptr_t)unboxed);
}

template <typename VkObjectT>
void set_boxed_non_dispatchable_VkType(VkObjectT boxed, VkObjectT new_unboxed) {
    BoxedHandleInfo info;
    info.underlying = (uint64_t)new_unboxed;
    sBoxedHandleManager.update((uint64_t)boxed, info, GetTag<VkObjectT>());
}

template <typename VkObjectT>
OrderMaintenanceInfo* get_order_maintenance_info_VkType(VkObjectT boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
    if (info == nullptr) {
        return nullptr;
    }

    if (info->ordMaintInfo == nullptr) {
        return nullptr;
    }

    acquireOrderMaintInfo(info->ordMaintInfo);

    return info->ordMaintInfo;
}

template <typename VkObjectT>
VulkanMemReadingStream* get_read_stream_VkType(VkObjectT boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
    if (info == nullptr) {
        return nullptr;
    }

    if (info->readStream == nullptr) {
        info->readStream = sReadStreamRegistry.pop(VkDecoderGlobalState::get()->getFeatures());
    }

    return info->readStream;
}

template <typename VkObjectT>
VulkanDispatch* get_dispatch_VkType(VkObjectT boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get((uint64_t)(uintptr_t)boxed);
    if (info == nullptr) {
        ERR("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed);
        return nullptr;
    }
    return info->dispatch;
}

///////////////////////////////////////////////////////////////////////////////
//////////////             DISPATCHABLE TYPES                    //////////////
///////////////////////////////////////////////////////////////////////////////

VkInstance new_boxed_VkInstance(VkInstance unboxed, VulkanDispatch* dispatch, bool ownsDispatch) {
    return new_boxed_VkType<VkInstance>(unboxed, /*dispatchable=*/true, dispatch, ownsDispatch);
}

void delete_VkInstance(VkInstance boxed) {
    delete_VkType(boxed);
}

VkInstance unbox_VkInstance(VkInstance boxed) {
    return unbox_VkType<VkInstance>(boxed);
}

VkInstance try_unbox_VkInstance(VkInstance boxed) {
    return try_unbox_VkType<VkInstance>(boxed);
}

VkInstance unboxed_to_boxed_VkInstance(VkInstance unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkInstance>(unboxed);
}

OrderMaintenanceInfo* ordmaint_VkInstance(VkInstance boxed) {
    return get_order_maintenance_info_VkType<VkInstance>(boxed);
}

VulkanMemReadingStream* readstream_VkInstance(VkInstance boxed) {
    return get_read_stream_VkType<VkInstance>(boxed);
}

VulkanDispatch* dispatch_VkInstance(VkInstance boxed) {
    return get_dispatch_VkType<VkInstance>(boxed);
}

VkPhysicalDevice new_boxed_VkPhysicalDevice(VkPhysicalDevice unboxed, VulkanDispatch* dispatch, bool ownsDispatch) {
    return new_boxed_VkType<VkPhysicalDevice>(unboxed, /*dispatchable=*/true, dispatch, ownsDispatch);
}

void delete_VkPhysicalDevice(VkPhysicalDevice boxed) {
    delete_VkType(boxed);
}

VkPhysicalDevice unbox_VkPhysicalDevice(VkPhysicalDevice boxed) {
    return unbox_VkType<VkPhysicalDevice>(boxed);
}

VkPhysicalDevice try_unbox_VkPhysicalDevice(VkPhysicalDevice boxed) {
    return try_unbox_VkType<VkPhysicalDevice>(boxed);
}

VkPhysicalDevice unboxed_to_boxed_VkPhysicalDevice(VkPhysicalDevice unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPhysicalDevice>(unboxed);
}

OrderMaintenanceInfo* ordmaint_VkPhysicalDevice(VkPhysicalDevice boxed) {
    return get_order_maintenance_info_VkType<VkPhysicalDevice>(boxed);
}

VulkanMemReadingStream* readstream_VkPhysicalDevice(VkPhysicalDevice boxed) {
    return get_read_stream_VkType<VkPhysicalDevice>(boxed);
}

VulkanDispatch* dispatch_VkPhysicalDevice(VkPhysicalDevice boxed) {
    return get_dispatch_VkType<VkPhysicalDevice>(boxed);
}

VkDevice new_boxed_VkDevice(VkDevice unboxed, VulkanDispatch* dispatch, bool ownsDispatch) {
    return new_boxed_VkType<VkDevice>(unboxed, /*dispatchable=*/true, dispatch, ownsDispatch);
}

void delete_VkDevice(VkDevice boxed) {
    delete_VkType(boxed);
}

VkDevice unbox_VkDevice(VkDevice boxed) {
    return unbox_VkType<VkDevice>(boxed);
}

VkDevice try_unbox_VkDevice(VkDevice boxed) {
    return try_unbox_VkType<VkDevice>(boxed);
}

VkDevice unboxed_to_boxed_VkDevice(VkDevice unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDevice>(unboxed);
}

OrderMaintenanceInfo* ordmaint_VkDevice(VkDevice boxed) {
    return get_order_maintenance_info_VkType<VkDevice>(boxed);
}

VulkanMemReadingStream* readstream_VkDevice(VkDevice boxed) {
    return get_read_stream_VkType<VkDevice>(boxed);
}

VulkanDispatch* dispatch_VkDevice(VkDevice boxed) {
    return get_dispatch_VkType<VkDevice>(boxed);
}

VkCommandBuffer new_boxed_VkCommandBuffer(VkCommandBuffer unboxed, VulkanDispatch* dispatch, bool ownsDispatch) {
    return new_boxed_VkType<VkCommandBuffer>(unboxed, /*dispatchable=*/true, dispatch, ownsDispatch);
}

void delete_VkCommandBuffer(VkCommandBuffer boxed) {
    delete_VkType(boxed);
}

VkCommandBuffer unbox_VkCommandBuffer(VkCommandBuffer boxed) {
    return unbox_VkType<VkCommandBuffer>(boxed);
}

VkCommandBuffer try_unbox_VkCommandBuffer(VkCommandBuffer boxed) {
    return try_unbox_VkType<VkCommandBuffer>(boxed);
}

VkCommandBuffer unboxed_to_boxed_VkCommandBuffer(VkCommandBuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCommandBuffer>(unboxed);
}

OrderMaintenanceInfo* ordmaint_VkCommandBuffer(VkCommandBuffer boxed) {
    return get_order_maintenance_info_VkType<VkCommandBuffer>(boxed);
}

VulkanMemReadingStream* readstream_VkCommandBuffer(VkCommandBuffer boxed) {
    return get_read_stream_VkType<VkCommandBuffer>(boxed);
}

VulkanDispatch* dispatch_VkCommandBuffer(VkCommandBuffer boxed) {
    return get_dispatch_VkType<VkCommandBuffer>(boxed);
}

VkQueue new_boxed_VkQueue(VkQueue unboxed, VulkanDispatch* dispatch, bool ownsDispatch) {
    return new_boxed_VkType<VkQueue>(unboxed, /*dispatchable=*/true, dispatch, ownsDispatch);
}

void delete_VkQueue(VkQueue boxed) {
    delete_VkType(boxed);
}

VkQueue unbox_VkQueue(VkQueue boxed) {
    return unbox_VkType<VkQueue>(boxed);
}

VkQueue try_unbox_VkQueue(VkQueue boxed) {
    return try_unbox_VkType<VkQueue>(boxed);
}

VkQueue unboxed_to_boxed_VkQueue(VkQueue unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkQueue>(unboxed);
}

OrderMaintenanceInfo* ordmaint_VkQueue(VkQueue boxed) {
    return get_order_maintenance_info_VkType<VkQueue>(boxed);
}

VulkanMemReadingStream* readstream_VkQueue(VkQueue boxed) {
    return get_read_stream_VkType<VkQueue>(boxed);
}

VulkanDispatch* dispatch_VkQueue(VkQueue boxed) {
    return get_dispatch_VkType<VkQueue>(boxed);
}

///////////////////////////////////////////////////////////////////////////////
//////////////             NON DISPATCHABLE TYPES                //////////////
///////////////////////////////////////////////////////////////////////////////

VkAccelerationStructureKHR new_boxed_non_dispatchable_VkAccelerationStructureKHR(VkAccelerationStructureKHR unboxed) {
    return new_boxed_VkType<VkAccelerationStructureKHR>(unboxed);
}

void delete_VkAccelerationStructureKHR(VkAccelerationStructureKHR boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkAccelerationStructureKHR(VkAccelerationStructureKHR boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkAccelerationStructureKHR unbox_VkAccelerationStructureKHR(VkAccelerationStructureKHR boxed) {
    return unbox_VkType<VkAccelerationStructureKHR>(boxed);
}

VkAccelerationStructureKHR try_unbox_VkAccelerationStructureKHR(VkAccelerationStructureKHR boxed) {
    return try_unbox_VkType<VkAccelerationStructureKHR>(boxed);
}

VkAccelerationStructureKHR unboxed_to_boxed_non_dispatchable_VkAccelerationStructureKHR(VkAccelerationStructureKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkAccelerationStructureKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkAccelerationStructureKHR(VkAccelerationStructureKHR boxed, VkAccelerationStructureKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkAccelerationStructureKHR>(boxed, new_unboxed);
}

VkAccelerationStructureNV new_boxed_non_dispatchable_VkAccelerationStructureNV(VkAccelerationStructureNV unboxed) {
    return new_boxed_VkType<VkAccelerationStructureNV>(unboxed);
}

void delete_VkAccelerationStructureNV(VkAccelerationStructureNV boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkAccelerationStructureNV(VkAccelerationStructureNV boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkAccelerationStructureNV unbox_VkAccelerationStructureNV(VkAccelerationStructureNV boxed) {
    return unbox_VkType<VkAccelerationStructureNV>(boxed);
}

VkAccelerationStructureNV try_unbox_VkAccelerationStructureNV(VkAccelerationStructureNV boxed) {
    return try_unbox_VkType<VkAccelerationStructureNV>(boxed);
}

VkAccelerationStructureNV unboxed_to_boxed_non_dispatchable_VkAccelerationStructureNV(VkAccelerationStructureNV unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkAccelerationStructureNV>(unboxed);
}

void set_boxed_non_dispatchable_VkAccelerationStructureNV(VkAccelerationStructureNV boxed, VkAccelerationStructureNV new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkAccelerationStructureNV>(boxed, new_unboxed);
}

VkBuffer new_boxed_non_dispatchable_VkBuffer(VkBuffer unboxed) {
    return new_boxed_VkType<VkBuffer>(unboxed);
}

void delete_VkBuffer(VkBuffer boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkBuffer(VkBuffer boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkBuffer unbox_VkBuffer(VkBuffer boxed) {
    return unbox_VkType<VkBuffer>(boxed);
}

VkBuffer try_unbox_VkBuffer(VkBuffer boxed) {
    return try_unbox_VkType<VkBuffer>(boxed);
}

VkBuffer unboxed_to_boxed_non_dispatchable_VkBuffer(VkBuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkBuffer>(unboxed);
}

void set_boxed_non_dispatchable_VkBuffer(VkBuffer boxed, VkBuffer new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkBuffer>(boxed, new_unboxed);
}

VkBufferView new_boxed_non_dispatchable_VkBufferView(VkBufferView unboxed) {
    return new_boxed_VkType<VkBufferView>(unboxed);
}

void delete_VkBufferView(VkBufferView boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkBufferView(VkBufferView boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkBufferView unbox_VkBufferView(VkBufferView boxed) {
    return unbox_VkType<VkBufferView>(boxed);
}

VkBufferView try_unbox_VkBufferView(VkBufferView boxed) {
    return try_unbox_VkType<VkBufferView>(boxed);
}

VkBufferView unboxed_to_boxed_non_dispatchable_VkBufferView(VkBufferView unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkBufferView>(unboxed);
}

void set_boxed_non_dispatchable_VkBufferView(VkBufferView boxed, VkBufferView new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkBufferView>(boxed, new_unboxed);
}

VkCommandPool new_boxed_non_dispatchable_VkCommandPool(VkCommandPool unboxed) {
    return new_boxed_VkType<VkCommandPool>(unboxed);
}

void delete_VkCommandPool(VkCommandPool boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCommandPool(VkCommandPool boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCommandPool unbox_VkCommandPool(VkCommandPool boxed) {
    return unbox_VkType<VkCommandPool>(boxed);
}

VkCommandPool try_unbox_VkCommandPool(VkCommandPool boxed) {
    return try_unbox_VkType<VkCommandPool>(boxed);
}

VkCommandPool unboxed_to_boxed_non_dispatchable_VkCommandPool(VkCommandPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCommandPool>(unboxed);
}

void set_boxed_non_dispatchable_VkCommandPool(VkCommandPool boxed, VkCommandPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCommandPool>(boxed, new_unboxed);
}

VkCuFunctionNVX new_boxed_non_dispatchable_VkCuFunctionNVX(VkCuFunctionNVX unboxed) {
    return new_boxed_VkType<VkCuFunctionNVX>(unboxed);
}

void delete_VkCuFunctionNVX(VkCuFunctionNVX boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCuFunctionNVX(VkCuFunctionNVX boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCuFunctionNVX unbox_VkCuFunctionNVX(VkCuFunctionNVX boxed) {
    return unbox_VkType<VkCuFunctionNVX>(boxed);
}

VkCuFunctionNVX try_unbox_VkCuFunctionNVX(VkCuFunctionNVX boxed) {
    return try_unbox_VkType<VkCuFunctionNVX>(boxed);
}

VkCuFunctionNVX unboxed_to_boxed_non_dispatchable_VkCuFunctionNVX(VkCuFunctionNVX unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCuFunctionNVX>(unboxed);
}

void set_boxed_non_dispatchable_VkCuFunctionNVX(VkCuFunctionNVX boxed, VkCuFunctionNVX new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCuFunctionNVX>(boxed, new_unboxed);
}

VkCuModuleNVX new_boxed_non_dispatchable_VkCuModuleNVX(VkCuModuleNVX unboxed) {
    return new_boxed_VkType<VkCuModuleNVX>(unboxed);
}

void delete_VkCuModuleNVX(VkCuModuleNVX boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCuModuleNVX(VkCuModuleNVX boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCuModuleNVX unbox_VkCuModuleNVX(VkCuModuleNVX boxed) {
    return unbox_VkType<VkCuModuleNVX>(boxed);
}

VkCuModuleNVX try_unbox_VkCuModuleNVX(VkCuModuleNVX boxed) {
    return try_unbox_VkType<VkCuModuleNVX>(boxed);
}

VkCuModuleNVX unboxed_to_boxed_non_dispatchable_VkCuModuleNVX(VkCuModuleNVX unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCuModuleNVX>(unboxed);
}

void set_boxed_non_dispatchable_VkCuModuleNVX(VkCuModuleNVX boxed, VkCuModuleNVX new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCuModuleNVX>(boxed, new_unboxed);
}

VkDebugReportCallbackEXT new_boxed_non_dispatchable_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT unboxed) {
    return new_boxed_VkType<VkDebugReportCallbackEXT>(unboxed);
}

void delete_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDebugReportCallbackEXT unbox_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT boxed) {
    return unbox_VkType<VkDebugReportCallbackEXT>(boxed);
}

VkDebugReportCallbackEXT try_unbox_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT boxed) {
    return try_unbox_VkType<VkDebugReportCallbackEXT>(boxed);
}

VkDebugReportCallbackEXT unboxed_to_boxed_non_dispatchable_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDebugReportCallbackEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT boxed, VkDebugReportCallbackEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDebugReportCallbackEXT>(boxed, new_unboxed);
}

VkDebugUtilsMessengerEXT new_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT unboxed) {
    return new_boxed_VkType<VkDebugUtilsMessengerEXT>(unboxed);
}

void delete_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDebugUtilsMessengerEXT unbox_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT boxed) {
    return unbox_VkType<VkDebugUtilsMessengerEXT>(boxed);
}

VkDebugUtilsMessengerEXT try_unbox_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT boxed) {
    return try_unbox_VkType<VkDebugUtilsMessengerEXT>(boxed);
}

VkDebugUtilsMessengerEXT unboxed_to_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDebugUtilsMessengerEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT boxed, VkDebugUtilsMessengerEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDebugUtilsMessengerEXT>(boxed, new_unboxed);
}

VkDescriptorPool new_boxed_non_dispatchable_VkDescriptorPool(VkDescriptorPool unboxed) {
    return new_boxed_VkType<VkDescriptorPool>(unboxed);
}

void delete_VkDescriptorPool(VkDescriptorPool boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorPool(VkDescriptorPool boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorPool unbox_VkDescriptorPool(VkDescriptorPool boxed) {
    return unbox_VkType<VkDescriptorPool>(boxed);
}

VkDescriptorPool try_unbox_VkDescriptorPool(VkDescriptorPool boxed) {
    return try_unbox_VkType<VkDescriptorPool>(boxed);
}

VkDescriptorPool unboxed_to_boxed_non_dispatchable_VkDescriptorPool(VkDescriptorPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorPool>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorPool(VkDescriptorPool boxed, VkDescriptorPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorPool>(boxed, new_unboxed);
}

VkDescriptorSet new_boxed_non_dispatchable_VkDescriptorSet(VkDescriptorSet unboxed) {
    return new_boxed_VkType<VkDescriptorSet>(unboxed);
}

void delete_VkDescriptorSet(VkDescriptorSet boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorSet(VkDescriptorSet boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorSet unbox_VkDescriptorSet(VkDescriptorSet boxed) {
    return unbox_VkType<VkDescriptorSet>(boxed);
}

VkDescriptorSet try_unbox_VkDescriptorSet(VkDescriptorSet boxed) {
    return try_unbox_VkType<VkDescriptorSet>(boxed);
}

VkDescriptorSet unboxed_to_boxed_non_dispatchable_VkDescriptorSet(VkDescriptorSet unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorSet>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorSet(VkDescriptorSet boxed, VkDescriptorSet new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorSet>(boxed, new_unboxed);
}

VkDescriptorSetLayout new_boxed_non_dispatchable_VkDescriptorSetLayout(VkDescriptorSetLayout unboxed) {
    return new_boxed_VkType<VkDescriptorSetLayout>(unboxed);
}

void delete_VkDescriptorSetLayout(VkDescriptorSetLayout boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorSetLayout(VkDescriptorSetLayout boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorSetLayout unbox_VkDescriptorSetLayout(VkDescriptorSetLayout boxed) {
    return unbox_VkType<VkDescriptorSetLayout>(boxed);
}

VkDescriptorSetLayout try_unbox_VkDescriptorSetLayout(VkDescriptorSetLayout boxed) {
    return try_unbox_VkType<VkDescriptorSetLayout>(boxed);
}

VkDescriptorSetLayout unboxed_to_boxed_non_dispatchable_VkDescriptorSetLayout(VkDescriptorSetLayout unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorSetLayout>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorSetLayout(VkDescriptorSetLayout boxed, VkDescriptorSetLayout new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorSetLayout>(boxed, new_unboxed);
}

VkDescriptorUpdateTemplate new_boxed_non_dispatchable_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate unboxed) {
    return new_boxed_VkType<VkDescriptorUpdateTemplate>(unboxed);
}

void delete_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorUpdateTemplate unbox_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate boxed) {
    return unbox_VkType<VkDescriptorUpdateTemplate>(boxed);
}

VkDescriptorUpdateTemplate try_unbox_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate boxed) {
    return try_unbox_VkType<VkDescriptorUpdateTemplate>(boxed);
}

VkDescriptorUpdateTemplate unboxed_to_boxed_non_dispatchable_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorUpdateTemplate>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate boxed, VkDescriptorUpdateTemplate new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorUpdateTemplate>(boxed, new_unboxed);
}

VkDeviceMemory new_boxed_non_dispatchable_VkDeviceMemory(VkDeviceMemory unboxed) {
    return new_boxed_VkType<VkDeviceMemory>(unboxed);
}

void delete_VkDeviceMemory(VkDeviceMemory boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDeviceMemory(VkDeviceMemory boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDeviceMemory unbox_VkDeviceMemory(VkDeviceMemory boxed) {
    return unbox_VkType<VkDeviceMemory>(boxed);
}

VkDeviceMemory try_unbox_VkDeviceMemory(VkDeviceMemory boxed) {
    return try_unbox_VkType<VkDeviceMemory>(boxed);
}

VkDeviceMemory unboxed_to_boxed_non_dispatchable_VkDeviceMemory(VkDeviceMemory unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDeviceMemory>(unboxed);
}

void set_boxed_non_dispatchable_VkDeviceMemory(VkDeviceMemory boxed, VkDeviceMemory new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDeviceMemory>(boxed, new_unboxed);
}

VkDisplayKHR new_boxed_non_dispatchable_VkDisplayKHR(VkDisplayKHR unboxed) {
    return new_boxed_VkType<VkDisplayKHR>(unboxed);
}

void delete_VkDisplayKHR(VkDisplayKHR boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDisplayKHR(VkDisplayKHR boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDisplayKHR unbox_VkDisplayKHR(VkDisplayKHR boxed) {
    return unbox_VkType<VkDisplayKHR>(boxed);
}

VkDisplayKHR try_unbox_VkDisplayKHR(VkDisplayKHR boxed) {
    return try_unbox_VkType<VkDisplayKHR>(boxed);
}

VkDisplayKHR unboxed_to_boxed_non_dispatchable_VkDisplayKHR(VkDisplayKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDisplayKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkDisplayKHR(VkDisplayKHR boxed, VkDisplayKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDisplayKHR>(boxed, new_unboxed);
}

VkDisplayModeKHR new_boxed_non_dispatchable_VkDisplayModeKHR(VkDisplayModeKHR unboxed) {
    return new_boxed_VkType<VkDisplayModeKHR>(unboxed);
}

void delete_VkDisplayModeKHR(VkDisplayModeKHR boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDisplayModeKHR(VkDisplayModeKHR boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDisplayModeKHR unbox_VkDisplayModeKHR(VkDisplayModeKHR boxed) {
    return unbox_VkType<VkDisplayModeKHR>(boxed);
}

VkDisplayModeKHR try_unbox_VkDisplayModeKHR(VkDisplayModeKHR boxed) {
    return try_unbox_VkType<VkDisplayModeKHR>(boxed);
}

VkDisplayModeKHR unboxed_to_boxed_non_dispatchable_VkDisplayModeKHR(VkDisplayModeKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDisplayModeKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkDisplayModeKHR(VkDisplayModeKHR boxed, VkDisplayModeKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDisplayModeKHR>(boxed, new_unboxed);
}

VkEvent new_boxed_non_dispatchable_VkEvent(VkEvent unboxed) {
    return new_boxed_VkType<VkEvent>(unboxed);
}

void delete_VkEvent(VkEvent boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkEvent(VkEvent boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkEvent unbox_VkEvent(VkEvent boxed) {
    return unbox_VkType<VkEvent>(boxed);
}

VkEvent try_unbox_VkEvent(VkEvent boxed) {
    return try_unbox_VkType<VkEvent>(boxed);
}

VkEvent unboxed_to_boxed_non_dispatchable_VkEvent(VkEvent unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkEvent>(unboxed);
}

void set_boxed_non_dispatchable_VkEvent(VkEvent boxed, VkEvent new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkEvent>(boxed, new_unboxed);
}

VkFence new_boxed_non_dispatchable_VkFence(VkFence unboxed) {
    return new_boxed_VkType<VkFence>(unboxed);
}

void delete_VkFence(VkFence boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkFence(VkFence boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkFence unbox_VkFence(VkFence boxed) {
    return unbox_VkType<VkFence>(boxed);
}

VkFence try_unbox_VkFence(VkFence boxed) {
    return try_unbox_VkType<VkFence>(boxed);
}

VkFence unboxed_to_boxed_non_dispatchable_VkFence(VkFence unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkFence>(unboxed);
}

void set_boxed_non_dispatchable_VkFence(VkFence boxed, VkFence new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkFence>(boxed, new_unboxed);
}

VkFramebuffer new_boxed_non_dispatchable_VkFramebuffer(VkFramebuffer unboxed) {
    return new_boxed_VkType<VkFramebuffer>(unboxed);
}

void delete_VkFramebuffer(VkFramebuffer boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkFramebuffer(VkFramebuffer boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkFramebuffer unbox_VkFramebuffer(VkFramebuffer boxed) {
    return unbox_VkType<VkFramebuffer>(boxed);
}

VkFramebuffer try_unbox_VkFramebuffer(VkFramebuffer boxed) {
    return try_unbox_VkType<VkFramebuffer>(boxed);
}

VkFramebuffer unboxed_to_boxed_non_dispatchable_VkFramebuffer(VkFramebuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkFramebuffer>(unboxed);
}

void set_boxed_non_dispatchable_VkFramebuffer(VkFramebuffer boxed, VkFramebuffer new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkFramebuffer>(boxed, new_unboxed);
}

VkImage new_boxed_non_dispatchable_VkImage(VkImage unboxed) {
    return new_boxed_VkType<VkImage>(unboxed);
}

void delete_VkImage(VkImage boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkImage(VkImage boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkImage unbox_VkImage(VkImage boxed) {
    return unbox_VkType<VkImage>(boxed);
}

VkImage try_unbox_VkImage(VkImage boxed) {
    return try_unbox_VkType<VkImage>(boxed);
}

VkImage unboxed_to_boxed_non_dispatchable_VkImage(VkImage unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkImage>(unboxed);
}

void set_boxed_non_dispatchable_VkImage(VkImage boxed, VkImage new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkImage>(boxed, new_unboxed);
}

VkImageView new_boxed_non_dispatchable_VkImageView(VkImageView unboxed) {
    return new_boxed_VkType<VkImageView>(unboxed);
}

void delete_VkImageView(VkImageView boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkImageView(VkImageView boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkImageView unbox_VkImageView(VkImageView boxed) {
    return unbox_VkType<VkImageView>(boxed);
}

VkImageView try_unbox_VkImageView(VkImageView boxed) {
    return try_unbox_VkType<VkImageView>(boxed);
}

VkImageView unboxed_to_boxed_non_dispatchable_VkImageView(VkImageView unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkImageView>(unboxed);
}

void set_boxed_non_dispatchable_VkImageView(VkImageView boxed, VkImageView new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkImageView>(boxed, new_unboxed);
}

VkIndirectCommandsLayoutNV new_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV unboxed) {
    return new_boxed_VkType<VkIndirectCommandsLayoutNV>(unboxed);
}

void delete_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkIndirectCommandsLayoutNV unbox_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV boxed) {
    return unbox_VkType<VkIndirectCommandsLayoutNV>(boxed);
}

VkIndirectCommandsLayoutNV try_unbox_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV boxed) {
    return try_unbox_VkType<VkIndirectCommandsLayoutNV>(boxed);
}

VkIndirectCommandsLayoutNV unboxed_to_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkIndirectCommandsLayoutNV>(unboxed);
}

void set_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV boxed, VkIndirectCommandsLayoutNV new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkIndirectCommandsLayoutNV>(boxed, new_unboxed);
}

VkMicromapEXT new_boxed_non_dispatchable_VkMicromapEXT(VkMicromapEXT unboxed) {
    return new_boxed_VkType<VkMicromapEXT>(unboxed);
}

void delete_VkMicromapEXT(VkMicromapEXT boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkMicromapEXT(VkMicromapEXT boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkMicromapEXT unbox_VkMicromapEXT(VkMicromapEXT boxed) {
    return unbox_VkType<VkMicromapEXT>(boxed);
}

VkMicromapEXT try_unbox_VkMicromapEXT(VkMicromapEXT boxed) {
    return try_unbox_VkType<VkMicromapEXT>(boxed);
}

VkMicromapEXT unboxed_to_boxed_non_dispatchable_VkMicromapEXT(VkMicromapEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkMicromapEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkMicromapEXT(VkMicromapEXT boxed, VkMicromapEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkMicromapEXT>(boxed, new_unboxed);
}

VkPipeline new_boxed_non_dispatchable_VkPipeline(VkPipeline unboxed) {
    return new_boxed_VkType<VkPipeline>(unboxed);
}

void delete_VkPipeline(VkPipeline boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipeline(VkPipeline boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipeline unbox_VkPipeline(VkPipeline boxed) {
    return unbox_VkType<VkPipeline>(boxed);
}

VkPipeline try_unbox_VkPipeline(VkPipeline boxed) {
    return try_unbox_VkType<VkPipeline>(boxed);
}

VkPipeline unboxed_to_boxed_non_dispatchable_VkPipeline(VkPipeline unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipeline>(unboxed);
}

void set_boxed_non_dispatchable_VkPipeline(VkPipeline boxed, VkPipeline new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipeline>(boxed, new_unboxed);
}

VkPipelineCache new_boxed_non_dispatchable_VkPipelineCache(VkPipelineCache unboxed) {
    return new_boxed_VkType<VkPipelineCache>(unboxed);
}

void delete_VkPipelineCache(VkPipelineCache boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipelineCache(VkPipelineCache boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipelineCache unbox_VkPipelineCache(VkPipelineCache boxed) {
    return unbox_VkType<VkPipelineCache>(boxed);
}

VkPipelineCache try_unbox_VkPipelineCache(VkPipelineCache boxed) {
    return try_unbox_VkType<VkPipelineCache>(boxed);
}

VkPipelineCache unboxed_to_boxed_non_dispatchable_VkPipelineCache(VkPipelineCache unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipelineCache>(unboxed);
}

void set_boxed_non_dispatchable_VkPipelineCache(VkPipelineCache boxed, VkPipelineCache new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipelineCache>(boxed, new_unboxed);
}

VkPipelineLayout new_boxed_non_dispatchable_VkPipelineLayout(VkPipelineLayout unboxed) {
    return new_boxed_VkType<VkPipelineLayout>(unboxed);
}

void delete_VkPipelineLayout(VkPipelineLayout boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipelineLayout(VkPipelineLayout boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipelineLayout unbox_VkPipelineLayout(VkPipelineLayout boxed) {
    return unbox_VkType<VkPipelineLayout>(boxed);
}

VkPipelineLayout try_unbox_VkPipelineLayout(VkPipelineLayout boxed) {
    return try_unbox_VkType<VkPipelineLayout>(boxed);
}

VkPipelineLayout unboxed_to_boxed_non_dispatchable_VkPipelineLayout(VkPipelineLayout unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipelineLayout>(unboxed);
}

void set_boxed_non_dispatchable_VkPipelineLayout(VkPipelineLayout boxed, VkPipelineLayout new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipelineLayout>(boxed, new_unboxed);
}

VkPrivateDataSlot new_boxed_non_dispatchable_VkPrivateDataSlot(VkPrivateDataSlot unboxed) {
    return new_boxed_VkType<VkPrivateDataSlot>(unboxed);
}

void delete_VkPrivateDataSlot(VkPrivateDataSlot boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPrivateDataSlot(VkPrivateDataSlot boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPrivateDataSlot unbox_VkPrivateDataSlot(VkPrivateDataSlot boxed) {
    return unbox_VkType<VkPrivateDataSlot>(boxed);
}

VkPrivateDataSlot try_unbox_VkPrivateDataSlot(VkPrivateDataSlot boxed) {
    return try_unbox_VkType<VkPrivateDataSlot>(boxed);
}

VkPrivateDataSlot unboxed_to_boxed_non_dispatchable_VkPrivateDataSlot(VkPrivateDataSlot unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPrivateDataSlot>(unboxed);
}

void set_boxed_non_dispatchable_VkPrivateDataSlot(VkPrivateDataSlot boxed, VkPrivateDataSlot new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPrivateDataSlot>(boxed, new_unboxed);
}

VkQueryPool new_boxed_non_dispatchable_VkQueryPool(VkQueryPool unboxed) {
    return new_boxed_VkType<VkQueryPool>(unboxed);
}

void delete_VkQueryPool(VkQueryPool boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkQueryPool(VkQueryPool boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkQueryPool unbox_VkQueryPool(VkQueryPool boxed) {
    return unbox_VkType<VkQueryPool>(boxed);
}

VkQueryPool try_unbox_VkQueryPool(VkQueryPool boxed) {
    return try_unbox_VkType<VkQueryPool>(boxed);
}

VkQueryPool unboxed_to_boxed_non_dispatchable_VkQueryPool(VkQueryPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkQueryPool>(unboxed);
}

void set_boxed_non_dispatchable_VkQueryPool(VkQueryPool boxed, VkQueryPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkQueryPool>(boxed, new_unboxed);
}

VkRenderPass new_boxed_non_dispatchable_VkRenderPass(VkRenderPass unboxed) {
    return new_boxed_VkType<VkRenderPass>(unboxed);
}

void delete_VkRenderPass(VkRenderPass boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkRenderPass(VkRenderPass boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkRenderPass unbox_VkRenderPass(VkRenderPass boxed) {
    return unbox_VkType<VkRenderPass>(boxed);
}

VkRenderPass try_unbox_VkRenderPass(VkRenderPass boxed) {
    return try_unbox_VkType<VkRenderPass>(boxed);
}

VkRenderPass unboxed_to_boxed_non_dispatchable_VkRenderPass(VkRenderPass unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkRenderPass>(unboxed);
}

void set_boxed_non_dispatchable_VkRenderPass(VkRenderPass boxed, VkRenderPass new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkRenderPass>(boxed, new_unboxed);
}

VkSampler new_boxed_non_dispatchable_VkSampler(VkSampler unboxed) {
    return new_boxed_VkType<VkSampler>(unboxed);
}

void delete_VkSampler(VkSampler boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSampler(VkSampler boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSampler unbox_VkSampler(VkSampler boxed) {
    return unbox_VkType<VkSampler>(boxed);
}

VkSampler try_unbox_VkSampler(VkSampler boxed) {
    return try_unbox_VkType<VkSampler>(boxed);
}

VkSampler unboxed_to_boxed_non_dispatchable_VkSampler(VkSampler unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSampler>(unboxed);
}

void set_boxed_non_dispatchable_VkSampler(VkSampler boxed, VkSampler new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSampler>(boxed, new_unboxed);
}

VkSamplerYcbcrConversion new_boxed_non_dispatchable_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion unboxed) {
    return new_boxed_VkType<VkSamplerYcbcrConversion>(unboxed);
}

void delete_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSamplerYcbcrConversion unbox_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion boxed) {
    return unbox_VkType<VkSamplerYcbcrConversion>(boxed);
}

VkSamplerYcbcrConversion try_unbox_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion boxed) {
    return try_unbox_VkType<VkSamplerYcbcrConversion>(boxed);
}

VkSamplerYcbcrConversion unboxed_to_boxed_non_dispatchable_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSamplerYcbcrConversion>(unboxed);
}

void set_boxed_non_dispatchable_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion boxed, VkSamplerYcbcrConversion new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSamplerYcbcrConversion>(boxed, new_unboxed);
}

VkSemaphore new_boxed_non_dispatchable_VkSemaphore(VkSemaphore unboxed) {
    return new_boxed_VkType<VkSemaphore>(unboxed);
}

void delete_VkSemaphore(VkSemaphore boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSemaphore(VkSemaphore boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSemaphore unbox_VkSemaphore(VkSemaphore boxed) {
    return unbox_VkType<VkSemaphore>(boxed);
}

VkSemaphore try_unbox_VkSemaphore(VkSemaphore boxed) {
    return try_unbox_VkType<VkSemaphore>(boxed);
}

VkSemaphore unboxed_to_boxed_non_dispatchable_VkSemaphore(VkSemaphore unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSemaphore>(unboxed);
}

void set_boxed_non_dispatchable_VkSemaphore(VkSemaphore boxed, VkSemaphore new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSemaphore>(boxed, new_unboxed);
}

VkShaderModule new_boxed_non_dispatchable_VkShaderModule(VkShaderModule unboxed) {
    return new_boxed_VkType<VkShaderModule>(unboxed);
}

void delete_VkShaderModule(VkShaderModule boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkShaderModule(VkShaderModule boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkShaderModule unbox_VkShaderModule(VkShaderModule boxed) {
    return unbox_VkType<VkShaderModule>(boxed);
}

VkShaderModule try_unbox_VkShaderModule(VkShaderModule boxed) {
    return try_unbox_VkType<VkShaderModule>(boxed);
}

VkShaderModule unboxed_to_boxed_non_dispatchable_VkShaderModule(VkShaderModule unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkShaderModule>(unboxed);
}

void set_boxed_non_dispatchable_VkShaderModule(VkShaderModule boxed, VkShaderModule new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkShaderModule>(boxed, new_unboxed);
}

VkSurfaceKHR new_boxed_non_dispatchable_VkSurfaceKHR(VkSurfaceKHR unboxed) {
    return new_boxed_VkType<VkSurfaceKHR>(unboxed);
}

void delete_VkSurfaceKHR(VkSurfaceKHR boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSurfaceKHR(VkSurfaceKHR boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSurfaceKHR unbox_VkSurfaceKHR(VkSurfaceKHR boxed) {
    return unbox_VkType<VkSurfaceKHR>(boxed);
}

VkSurfaceKHR try_unbox_VkSurfaceKHR(VkSurfaceKHR boxed) {
    return try_unbox_VkType<VkSurfaceKHR>(boxed);
}

VkSurfaceKHR unboxed_to_boxed_non_dispatchable_VkSurfaceKHR(VkSurfaceKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSurfaceKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkSurfaceKHR(VkSurfaceKHR boxed, VkSurfaceKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSurfaceKHR>(boxed, new_unboxed);
}

VkSwapchainKHR new_boxed_non_dispatchable_VkSwapchainKHR(VkSwapchainKHR unboxed) {
    return new_boxed_VkType<VkSwapchainKHR>(unboxed);
}

void delete_VkSwapchainKHR(VkSwapchainKHR boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSwapchainKHR(VkSwapchainKHR boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSwapchainKHR unbox_VkSwapchainKHR(VkSwapchainKHR boxed) {
    return unbox_VkType<VkSwapchainKHR>(boxed);
}

VkSwapchainKHR try_unbox_VkSwapchainKHR(VkSwapchainKHR boxed) {
    return try_unbox_VkType<VkSwapchainKHR>(boxed);
}

VkSwapchainKHR unboxed_to_boxed_non_dispatchable_VkSwapchainKHR(VkSwapchainKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSwapchainKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkSwapchainKHR(VkSwapchainKHR boxed, VkSwapchainKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSwapchainKHR>(boxed, new_unboxed);
}

VkValidationCacheEXT new_boxed_non_dispatchable_VkValidationCacheEXT(VkValidationCacheEXT unboxed) {
    return new_boxed_VkType<VkValidationCacheEXT>(unboxed);
}

void delete_VkValidationCacheEXT(VkValidationCacheEXT boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkValidationCacheEXT(VkValidationCacheEXT boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkValidationCacheEXT unbox_VkValidationCacheEXT(VkValidationCacheEXT boxed) {
    return unbox_VkType<VkValidationCacheEXT>(boxed);
}

VkValidationCacheEXT try_unbox_VkValidationCacheEXT(VkValidationCacheEXT boxed) {
    return try_unbox_VkType<VkValidationCacheEXT>(boxed);
}

VkValidationCacheEXT unboxed_to_boxed_non_dispatchable_VkValidationCacheEXT(VkValidationCacheEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkValidationCacheEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkValidationCacheEXT(VkValidationCacheEXT boxed, VkValidationCacheEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkValidationCacheEXT>(boxed, new_unboxed);
}

}  // namespace vk
}  // namespace gfxstream
