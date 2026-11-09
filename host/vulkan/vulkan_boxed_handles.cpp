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

#include "vulkan_boxed_handles.h"

#include "vk_decoder_global_state.h"
#include "vk_decoder_internal_structs.h"

namespace gfxstream {
namespace host {
namespace vk {

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

BoxedHandle BoxedHandleManager::add(BoxedHandleInfo item, BoxedHandleTypeTag tag) {
    BoxedHandle handle;
    UnboxedHandle underlying = item.underlying;

    if (mHandleReplay) {
        handle = mHandleReplayQueue.front();
        mHandleReplayQueue.pop_front();
        mHandleReplay = !mHandleReplayQueue.empty();

        handle = (BoxedHandle)mStore.addFixed(handle, std::move(item), (size_t)tag);
    } else {
        handle = (BoxedHandle)mStore.add(std::move(item), (size_t)tag);
    }

    // VK_NULL_HANDLE is never a valid reverse-lookup key (see eraseReverseMappingLocked), so
    // don't record one that could later be returned for getBoxedFromUnboxed(0).
    if (underlying) {
        std::lock_guard<std::mutex> lock(mMutex);
        mReverseMap[underlying] = handle;
    }
    return handle;
}

void BoxedHandleManager::eraseReverseMappingLocked(UnboxedHandle unboxed, BoxedHandle boxed) {
    if (!unboxed) {
        return;
    }
    // Only erase the reverse mapping if it still points at |boxed|. The host driver may reuse
    // an unboxed handle value as soon as the underlying object is destroyed, and a new boxed
    // handle may already have been registered for it by another thread before the old boxed
    // handle is removed (e.g. on_vkDestroyImage releases the global state lock before the decoder
    // calls delete_VkImage). Unconditionally erasing would drop the new object's reverse mapping
    // and break unboxed_to_boxed_* lookups (and with them snapshot dependency tracking).
    auto it = mReverseMap.find(unboxed);
    if (it != mReverseMap.end() && it->second == boxed) {
        mReverseMap.erase(it);
    }
}

void BoxedHandleManager::update(BoxedHandle handle, BoxedHandleInfo item) {
    const UnboxedHandle newHandle = (UnboxedHandle)item.underlying;
    // Declared before the lock so it is destroyed after mMutex is released: its members may run
    // arbitrary destructors.
    BoxedHandleInfo previous;
    // mMutex is held across the store swap and the reverse-map update so that update() and
    // remove() on the same boxed handle are atomic with respect to each other; otherwise a
    // remove() interleaved between the two steps could leave mReverseMap pointing at a dead
    // handle. mMutex is never taken while the store lock is held, so the nesting is safe.
    std::lock_guard<std::mutex> lock(mMutex);
    const bool live = mStore.withLocked(handle, [&](BoxedHandleInfo* stored) {
        if (!stored) {
            return false;
        }
        previous = std::move(*stored);
        *stored = std::move(item);
        return true;
    });
    if (!live) {
        return;
    }
    eraseReverseMappingLocked((UnboxedHandle)previous.underlying, handle);
    if (newHandle) {
        mReverseMap[newHandle] = handle;
    }
}

void BoxedHandleManager::remove(BoxedHandle h) {
    // As in update(): declared before the lock so its destructors run after mMutex is released.
    BoxedHandleInfo removed;
    // mMutex covers the store lookup, the reverse-map erase and the store removal so that no
    // concurrent update(h) can slip in between and leave a stale reverse mapping. The info is
    // moved out under the store lock, as get() hands out a raw pointer that a concurrent
    // remove(h) could otherwise free before it is dereferenced.
    std::lock_guard<std::mutex> lock(mMutex);
    const bool live = mStore.withLocked(h, [&](BoxedHandleInfo* stored) {
        if (!stored) {
            return false;
        }
        removed = std::move(*stored);
        *stored = {};
        return true;
    });
    if (!live) {
        return;
    }
    eraseReverseMappingLocked((UnboxedHandle)removed.underlying, h);
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

        remove(h);
    }
}

BoxedHandleInfo* BoxedHandleManager::get(BoxedHandle handle) {
    return (BoxedHandleInfo*)mStore.get_const(handle);
}

std::shared_ptr<OrderMaintenanceInfo> BoxedHandleManager::getOrderMaintenanceInfo(
    BoxedHandle handle) {
    return mStore.withLocked(
        handle, [](BoxedHandleInfo* info) -> std::shared_ptr<OrderMaintenanceInfo> {
            if (info == nullptr) {
                return nullptr;
            }
            return info->ordMaintInfo;
        });
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
Boxed<VkObjectT> new_boxed_VkType(VkObjectT underlying, bool dispatchable = false, VulkanDispatch* dispatch = nullptr) {
    BoxedHandleInfo info;
    info.underlying = (uint64_t)underlying;
    if (dispatchable) {
        if (dispatch != nullptr) {
            info.dispatch = dispatch;
        } else {
            info.ownedDispatch = std::make_unique<VulkanDispatch>();
            info.dispatch = info.ownedDispatch.get();
        }
        info.ordMaintInfo = std::make_shared<OrderMaintenanceInfo>();
    }
    return Boxed<VkObjectT>::fromRaw(
        sBoxedHandleManager.add(std::move(info), GetTag<VkObjectT>()));
}

template <typename VkObjectT>
void delete_VkType(Boxed<VkObjectT> boxed) {
    if (!boxed) {
        return;
    }

    sBoxedHandleManager.remove((uint64_t)(uintptr_t)boxed.value());
}

template <typename VkObjectT>
void delayed_delete_VkType(Boxed<VkObjectT> boxed, VkDevice device, std::function<void()> callback) {
    if (!boxed) {
        return;
    }

    sBoxedHandleManager.removeDelayed((uint64_t)boxed.value(), device, std::move(callback));
}

// Custom unbox_* functions or GOLDFISH_VK_LIST_DISPATCHABLE_CUSTOM_UNBOX_HANDLE_TYPES
// VkQueue objects can be virtual, meaning that multiple boxed queues can map into a single
// physical queue on the host GPU. Some conversion is needed for unboxing to physical.
static VkQueue unbox_VkQueueImpl(Boxed<VkQueue> boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get(boxed);
    if (!info) {
        return VK_NULL_HANDLE;
    }
    const uint64_t unboxedQueue64 = info->underlying;

    // Use VulkanVirtualQueue directly to avoid locking for hasVirtualGraphicsQueue call.
    if (VkDecoderGlobalState::get()->getFeatures().VulkanVirtualQueue.enabled()) {
        // Clear virtual bit and unbox into the actual physical queue handle
        return (VkQueue)(unboxedQueue64 & ~QueueInfo::kVirtualQueueBit);
    }

    return (VkQueue)(unboxedQueue64);
}

template <typename VkObjectT>
VkObjectT unbox_VkType(Boxed<VkObjectT> boxed) {
    if (!boxed) {
        return VK_NULL_HANDLE;
    }

    VkObjectT unboxed = VK_NULL_HANDLE;

    if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        // Queue handles require special unboxing due to virtual queue support
        unboxed = unbox_VkQueueImpl(boxed);
        if (unboxed == VK_NULL_HANDLE) {
            GFXSTREAM_ERROR("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed.value());
            if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
                VkDecoderGlobalState::get()->setSnapshotLoadError();
            }
        }
    } else {
        BoxedHandleInfo* info = sBoxedHandleManager.get(boxed);
        if (info == nullptr) {
            if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
                // During snapshot load, do not crash the emulator, just log the error
                // The caller will handle it and fail snapshot loading.
                GFXSTREAM_ERROR("Failed to unbox %s %p during snapshot load", GetTypeStr<VkObjectT>(), boxed.value());
                VkDecoderGlobalState::get()->setSnapshotLoadError();
            } else if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer> ||
                                 std::is_same_v<VkObjectT, VkDevice> ||
                                 std::is_same_v<VkObjectT, VkInstance> ||
                                 std::is_same_v<VkObjectT, VkFence> ||
                                 std::is_same_v<VkObjectT, VkPhysicalDevice>) {
                GFXSTREAM_ERROR("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed.value());
            } else {
                // This should not happen during runtime
                GFXSTREAM_FATAL("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed.value());
            }
            unboxed = VK_NULL_HANDLE;
        } else {
            unboxed = (VkObjectT)info->underlying;
        }
    }

    return unboxed;
}

template <typename VkObjectT>
VkObjectT try_unbox_VkType(Boxed<VkObjectT> boxed) {
    if (!boxed) {
        return VK_NULL_HANDLE;
    }

    VkObjectT unboxed = VK_NULL_HANDLE;

    if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        unboxed = unbox_VkQueueImpl(boxed);
    } else {
        BoxedHandleInfo* info = sBoxedHandleManager.get(boxed);
        if (info != nullptr) {
            unboxed = (VkObjectT)info->underlying;
        }
    }

    if (unboxed == VK_NULL_HANDLE) {
        GFXSTREAM_WARNING("Failed to try unbox %s %p", GetTypeStr<VkObjectT>(), boxed.value());
    }

    return unboxed;
}

template <typename VkObjectT>
Boxed<VkObjectT> unboxed_to_boxed_non_dispatchable_VkType(VkObjectT unboxed) {
    if (unboxed == VK_NULL_HANDLE) {
        return Boxed<VkObjectT>();
    }

    return Boxed<VkObjectT>::fromRaw(
        sBoxedHandleManager.getBoxedFromUnboxed((uint64_t)(uintptr_t)unboxed));
}

template <typename VkObjectT>
void set_boxed_non_dispatchable_VkType(Boxed<VkObjectT> boxed, VkObjectT new_unboxed) {
    BoxedHandleInfo info;
    info.underlying = (uint64_t)new_unboxed;
    sBoxedHandleManager.update((uint64_t)boxed.value(), std::move(info));
}

template <typename VkObjectT>
std::shared_ptr<OrderMaintenanceInfo> get_order_maintenance_info_VkType(Boxed<VkObjectT> boxed) {
    return sBoxedHandleManager.getOrderMaintenanceInfo((uint64_t)(uintptr_t)boxed.value());
}

template <typename VkObjectT>
VulkanMemReadingStream* get_read_stream_VkType(Boxed<VkObjectT> boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get(boxed);
    if (info == nullptr) {
        return nullptr;
    }

    if (info->readStream == nullptr) {
        info->readStream = std::make_unique<VulkanMemReadingStream>(
            nullptr, VkDecoderGlobalState::get()->getFeatures());
    }

    return info->readStream.get();
}

template <typename VkObjectT>
VulkanDispatch* get_dispatch_VkType(Boxed<VkObjectT> boxed) {
    BoxedHandleInfo* info = sBoxedHandleManager.get(boxed);
    if (info == nullptr) {
        GFXSTREAM_ERROR("Failed to unbox %s %p", GetTypeStr<VkObjectT>(), boxed.value());
        if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
            VkDecoderGlobalState::get()->setSnapshotLoadError();
        }
        return nullptr;
    }
    return info->dispatch;
}

///////////////////////////////////////////////////////////////////////////////
//////////////             DISPATCHABLE TYPES                    //////////////
///////////////////////////////////////////////////////////////////////////////

Boxed<VkInstance> new_boxed_VkInstance(VkInstance unboxed, VulkanDispatch* dispatch) {
    return new_boxed_VkType<VkInstance>(unboxed, /*dispatchable=*/true, dispatch);
}

void delete_VkInstance(Boxed<VkInstance> boxed) {
    delete_VkType(boxed);
}

VkInstance unbox_VkInstance(Boxed<VkInstance> boxed) {
    return unbox_VkType<VkInstance>(boxed);
}

VkInstance try_unbox_VkInstance(Boxed<VkInstance> boxed) {
    return try_unbox_VkType<VkInstance>(boxed);
}

Boxed<VkInstance> unboxed_to_boxed_VkInstance(VkInstance unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkInstance>(unboxed);
}

std::shared_ptr<OrderMaintenanceInfo> ordmaint_VkInstance(Boxed<VkInstance> boxed) {
    return get_order_maintenance_info_VkType<VkInstance>(boxed);
}

VulkanMemReadingStream* readstream_VkInstance(Boxed<VkInstance> boxed) {
    return get_read_stream_VkType<VkInstance>(boxed);
}

VulkanDispatch* dispatch_VkInstance(Boxed<VkInstance> boxed) {
    return get_dispatch_VkType<VkInstance>(boxed);
}

Boxed<VkPhysicalDevice> new_boxed_VkPhysicalDevice(VkPhysicalDevice unboxed, VulkanDispatch* dispatch) {
    return new_boxed_VkType<VkPhysicalDevice>(unboxed, /*dispatchable=*/true, dispatch);
}

void delete_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    delete_VkType(boxed);
}

VkPhysicalDevice unbox_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    return unbox_VkType<VkPhysicalDevice>(boxed);
}

VkPhysicalDevice try_unbox_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    return try_unbox_VkType<VkPhysicalDevice>(boxed);
}

Boxed<VkPhysicalDevice> unboxed_to_boxed_VkPhysicalDevice(VkPhysicalDevice unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPhysicalDevice>(unboxed);
}

std::shared_ptr<OrderMaintenanceInfo> ordmaint_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    return get_order_maintenance_info_VkType<VkPhysicalDevice>(boxed);
}

VulkanMemReadingStream* readstream_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    return get_read_stream_VkType<VkPhysicalDevice>(boxed);
}

VulkanDispatch* dispatch_VkPhysicalDevice(Boxed<VkPhysicalDevice> boxed) {
    return get_dispatch_VkType<VkPhysicalDevice>(boxed);
}

Boxed<VkDevice> new_boxed_VkDevice(VkDevice unboxed, VulkanDispatch* dispatch) {
    return new_boxed_VkType<VkDevice>(unboxed, /*dispatchable=*/true, dispatch);
}

void delete_VkDevice(Boxed<VkDevice> boxed) {
    delete_VkType(boxed);
}

VkDevice unbox_VkDevice(Boxed<VkDevice> boxed) {
    return unbox_VkType<VkDevice>(boxed);
}

VkDevice try_unbox_VkDevice(Boxed<VkDevice> boxed) {
    return try_unbox_VkType<VkDevice>(boxed);
}

Boxed<VkDevice> unboxed_to_boxed_VkDevice(VkDevice unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDevice>(unboxed);
}

std::shared_ptr<OrderMaintenanceInfo> ordmaint_VkDevice(Boxed<VkDevice> boxed) {
    return get_order_maintenance_info_VkType<VkDevice>(boxed);
}

VulkanMemReadingStream* readstream_VkDevice(Boxed<VkDevice> boxed) {
    return get_read_stream_VkType<VkDevice>(boxed);
}

VulkanDispatch* dispatch_VkDevice(Boxed<VkDevice> boxed) {
    return get_dispatch_VkType<VkDevice>(boxed);
}

Boxed<VkCommandBuffer> new_boxed_VkCommandBuffer(VkCommandBuffer unboxed, VulkanDispatch* dispatch) {
    return new_boxed_VkType<VkCommandBuffer>(unboxed, /*dispatchable=*/true, dispatch);
}

void delete_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    delete_VkType(boxed);
}

VkCommandBuffer unbox_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    return unbox_VkType<VkCommandBuffer>(boxed);
}

VkCommandBuffer try_unbox_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    return try_unbox_VkType<VkCommandBuffer>(boxed);
}

Boxed<VkCommandBuffer> unboxed_to_boxed_VkCommandBuffer(VkCommandBuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCommandBuffer>(unboxed);
}

std::shared_ptr<OrderMaintenanceInfo> ordmaint_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    return get_order_maintenance_info_VkType<VkCommandBuffer>(boxed);
}

VulkanMemReadingStream* readstream_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    return get_read_stream_VkType<VkCommandBuffer>(boxed);
}

VulkanDispatch* dispatch_VkCommandBuffer(Boxed<VkCommandBuffer> boxed) {
    return get_dispatch_VkType<VkCommandBuffer>(boxed);
}

Boxed<VkQueue> new_boxed_VkQueue(VkQueue unboxed, VulkanDispatch* dispatch) {
    return new_boxed_VkType<VkQueue>(unboxed, /*dispatchable=*/true, dispatch);
}

void delete_VkQueue(Boxed<VkQueue> boxed) {
    delete_VkType(boxed);
}

VkQueue unbox_VkQueue(Boxed<VkQueue> boxed) {
    return unbox_VkType<VkQueue>(boxed);
}

VkQueue try_unbox_VkQueue(Boxed<VkQueue> boxed) {
    return try_unbox_VkType<VkQueue>(boxed);
}

Boxed<VkQueue> unboxed_to_boxed_VkQueue(VkQueue unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkQueue>(unboxed);
}

std::shared_ptr<OrderMaintenanceInfo> ordmaint_VkQueue(Boxed<VkQueue> boxed) {
    return get_order_maintenance_info_VkType<VkQueue>(boxed);
}

VulkanMemReadingStream* readstream_VkQueue(Boxed<VkQueue> boxed) {
    return get_read_stream_VkType<VkQueue>(boxed);
}

VulkanDispatch* dispatch_VkQueue(Boxed<VkQueue> boxed) {
    return get_dispatch_VkType<VkQueue>(boxed);
}

///////////////////////////////////////////////////////////////////////////////
//////////////             NON DISPATCHABLE TYPES                //////////////
///////////////////////////////////////////////////////////////////////////////

Boxed<VkAccelerationStructureKHR> new_boxed_non_dispatchable_VkAccelerationStructureKHR(VkAccelerationStructureKHR unboxed) {
    return new_boxed_VkType<VkAccelerationStructureKHR>(unboxed);
}

void delete_VkAccelerationStructureKHR(Boxed<VkAccelerationStructureKHR> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkAccelerationStructureKHR(Boxed<VkAccelerationStructureKHR> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkAccelerationStructureKHR unbox_VkAccelerationStructureKHR(Boxed<VkAccelerationStructureKHR> boxed) {
    return unbox_VkType<VkAccelerationStructureKHR>(boxed);
}

VkAccelerationStructureKHR try_unbox_VkAccelerationStructureKHR(Boxed<VkAccelerationStructureKHR> boxed) {
    return try_unbox_VkType<VkAccelerationStructureKHR>(boxed);
}

Boxed<VkAccelerationStructureKHR> unboxed_to_boxed_non_dispatchable_VkAccelerationStructureKHR(VkAccelerationStructureKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkAccelerationStructureKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkAccelerationStructureKHR(Boxed<VkAccelerationStructureKHR> boxed, VkAccelerationStructureKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkAccelerationStructureKHR>(boxed, new_unboxed);
}

Boxed<VkAccelerationStructureNV> new_boxed_non_dispatchable_VkAccelerationStructureNV(VkAccelerationStructureNV unboxed) {
    return new_boxed_VkType<VkAccelerationStructureNV>(unboxed);
}

void delete_VkAccelerationStructureNV(Boxed<VkAccelerationStructureNV> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkAccelerationStructureNV(Boxed<VkAccelerationStructureNV> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkAccelerationStructureNV unbox_VkAccelerationStructureNV(Boxed<VkAccelerationStructureNV> boxed) {
    return unbox_VkType<VkAccelerationStructureNV>(boxed);
}

VkAccelerationStructureNV try_unbox_VkAccelerationStructureNV(Boxed<VkAccelerationStructureNV> boxed) {
    return try_unbox_VkType<VkAccelerationStructureNV>(boxed);
}

Boxed<VkAccelerationStructureNV> unboxed_to_boxed_non_dispatchable_VkAccelerationStructureNV(VkAccelerationStructureNV unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkAccelerationStructureNV>(unboxed);
}

void set_boxed_non_dispatchable_VkAccelerationStructureNV(Boxed<VkAccelerationStructureNV> boxed, VkAccelerationStructureNV new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkAccelerationStructureNV>(boxed, new_unboxed);
}

Boxed<VkBuffer> new_boxed_non_dispatchable_VkBuffer(VkBuffer unboxed) {
    return new_boxed_VkType<VkBuffer>(unboxed);
}

void delete_VkBuffer(Boxed<VkBuffer> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkBuffer(Boxed<VkBuffer> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkBuffer unbox_VkBuffer(Boxed<VkBuffer> boxed) {
    return unbox_VkType<VkBuffer>(boxed);
}

VkBuffer try_unbox_VkBuffer(Boxed<VkBuffer> boxed) {
    return try_unbox_VkType<VkBuffer>(boxed);
}

Boxed<VkBuffer> unboxed_to_boxed_non_dispatchable_VkBuffer(VkBuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkBuffer>(unboxed);
}

void set_boxed_non_dispatchable_VkBuffer(Boxed<VkBuffer> boxed, VkBuffer new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkBuffer>(boxed, new_unboxed);
}

Boxed<VkBufferView> new_boxed_non_dispatchable_VkBufferView(VkBufferView unboxed) {
    return new_boxed_VkType<VkBufferView>(unboxed);
}

void delete_VkBufferView(Boxed<VkBufferView> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkBufferView(Boxed<VkBufferView> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkBufferView unbox_VkBufferView(Boxed<VkBufferView> boxed) {
    return unbox_VkType<VkBufferView>(boxed);
}

VkBufferView try_unbox_VkBufferView(Boxed<VkBufferView> boxed) {
    return try_unbox_VkType<VkBufferView>(boxed);
}

Boxed<VkBufferView> unboxed_to_boxed_non_dispatchable_VkBufferView(VkBufferView unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkBufferView>(unboxed);
}

void set_boxed_non_dispatchable_VkBufferView(Boxed<VkBufferView> boxed, VkBufferView new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkBufferView>(boxed, new_unboxed);
}

Boxed<VkCommandPool> new_boxed_non_dispatchable_VkCommandPool(VkCommandPool unboxed) {
    return new_boxed_VkType<VkCommandPool>(unboxed);
}

void delete_VkCommandPool(Boxed<VkCommandPool> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCommandPool(Boxed<VkCommandPool> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCommandPool unbox_VkCommandPool(Boxed<VkCommandPool> boxed) {
    return unbox_VkType<VkCommandPool>(boxed);
}

VkCommandPool try_unbox_VkCommandPool(Boxed<VkCommandPool> boxed) {
    return try_unbox_VkType<VkCommandPool>(boxed);
}

Boxed<VkCommandPool> unboxed_to_boxed_non_dispatchable_VkCommandPool(VkCommandPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCommandPool>(unboxed);
}

void set_boxed_non_dispatchable_VkCommandPool(Boxed<VkCommandPool> boxed, VkCommandPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCommandPool>(boxed, new_unboxed);
}

Boxed<VkCuFunctionNVX> new_boxed_non_dispatchable_VkCuFunctionNVX(VkCuFunctionNVX unboxed) {
    return new_boxed_VkType<VkCuFunctionNVX>(unboxed);
}

void delete_VkCuFunctionNVX(Boxed<VkCuFunctionNVX> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCuFunctionNVX(Boxed<VkCuFunctionNVX> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCuFunctionNVX unbox_VkCuFunctionNVX(Boxed<VkCuFunctionNVX> boxed) {
    return unbox_VkType<VkCuFunctionNVX>(boxed);
}

VkCuFunctionNVX try_unbox_VkCuFunctionNVX(Boxed<VkCuFunctionNVX> boxed) {
    return try_unbox_VkType<VkCuFunctionNVX>(boxed);
}

Boxed<VkCuFunctionNVX> unboxed_to_boxed_non_dispatchable_VkCuFunctionNVX(VkCuFunctionNVX unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCuFunctionNVX>(unboxed);
}

void set_boxed_non_dispatchable_VkCuFunctionNVX(Boxed<VkCuFunctionNVX> boxed, VkCuFunctionNVX new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCuFunctionNVX>(boxed, new_unboxed);
}

Boxed<VkCuModuleNVX> new_boxed_non_dispatchable_VkCuModuleNVX(VkCuModuleNVX unboxed) {
    return new_boxed_VkType<VkCuModuleNVX>(unboxed);
}

void delete_VkCuModuleNVX(Boxed<VkCuModuleNVX> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkCuModuleNVX(Boxed<VkCuModuleNVX> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkCuModuleNVX unbox_VkCuModuleNVX(Boxed<VkCuModuleNVX> boxed) {
    return unbox_VkType<VkCuModuleNVX>(boxed);
}

VkCuModuleNVX try_unbox_VkCuModuleNVX(Boxed<VkCuModuleNVX> boxed) {
    return try_unbox_VkType<VkCuModuleNVX>(boxed);
}

Boxed<VkCuModuleNVX> unboxed_to_boxed_non_dispatchable_VkCuModuleNVX(VkCuModuleNVX unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkCuModuleNVX>(unboxed);
}

void set_boxed_non_dispatchable_VkCuModuleNVX(Boxed<VkCuModuleNVX> boxed, VkCuModuleNVX new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkCuModuleNVX>(boxed, new_unboxed);
}

Boxed<VkDebugReportCallbackEXT> new_boxed_non_dispatchable_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT unboxed) {
    return new_boxed_VkType<VkDebugReportCallbackEXT>(unboxed);
}

void delete_VkDebugReportCallbackEXT(Boxed<VkDebugReportCallbackEXT> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDebugReportCallbackEXT(Boxed<VkDebugReportCallbackEXT> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDebugReportCallbackEXT unbox_VkDebugReportCallbackEXT(Boxed<VkDebugReportCallbackEXT> boxed) {
    return unbox_VkType<VkDebugReportCallbackEXT>(boxed);
}

VkDebugReportCallbackEXT try_unbox_VkDebugReportCallbackEXT(Boxed<VkDebugReportCallbackEXT> boxed) {
    return try_unbox_VkType<VkDebugReportCallbackEXT>(boxed);
}

Boxed<VkDebugReportCallbackEXT> unboxed_to_boxed_non_dispatchable_VkDebugReportCallbackEXT(VkDebugReportCallbackEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDebugReportCallbackEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkDebugReportCallbackEXT(Boxed<VkDebugReportCallbackEXT> boxed, VkDebugReportCallbackEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDebugReportCallbackEXT>(boxed, new_unboxed);
}

Boxed<VkDebugUtilsMessengerEXT> new_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT unboxed) {
    return new_boxed_VkType<VkDebugUtilsMessengerEXT>(unboxed);
}

void delete_VkDebugUtilsMessengerEXT(Boxed<VkDebugUtilsMessengerEXT> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDebugUtilsMessengerEXT(Boxed<VkDebugUtilsMessengerEXT> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDebugUtilsMessengerEXT unbox_VkDebugUtilsMessengerEXT(Boxed<VkDebugUtilsMessengerEXT> boxed) {
    return unbox_VkType<VkDebugUtilsMessengerEXT>(boxed);
}

VkDebugUtilsMessengerEXT try_unbox_VkDebugUtilsMessengerEXT(Boxed<VkDebugUtilsMessengerEXT> boxed) {
    return try_unbox_VkType<VkDebugUtilsMessengerEXT>(boxed);
}

Boxed<VkDebugUtilsMessengerEXT> unboxed_to_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(VkDebugUtilsMessengerEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDebugUtilsMessengerEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkDebugUtilsMessengerEXT(Boxed<VkDebugUtilsMessengerEXT> boxed, VkDebugUtilsMessengerEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDebugUtilsMessengerEXT>(boxed, new_unboxed);
}

Boxed<VkDescriptorPool> new_boxed_non_dispatchable_VkDescriptorPool(VkDescriptorPool unboxed) {
    return new_boxed_VkType<VkDescriptorPool>(unboxed);
}

void delete_VkDescriptorPool(Boxed<VkDescriptorPool> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorPool(Boxed<VkDescriptorPool> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorPool unbox_VkDescriptorPool(Boxed<VkDescriptorPool> boxed) {
    return unbox_VkType<VkDescriptorPool>(boxed);
}

VkDescriptorPool try_unbox_VkDescriptorPool(Boxed<VkDescriptorPool> boxed) {
    return try_unbox_VkType<VkDescriptorPool>(boxed);
}

Boxed<VkDescriptorPool> unboxed_to_boxed_non_dispatchable_VkDescriptorPool(VkDescriptorPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorPool>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorPool(Boxed<VkDescriptorPool> boxed, VkDescriptorPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorPool>(boxed, new_unboxed);
}

Boxed<VkDescriptorSet> new_boxed_non_dispatchable_VkDescriptorSet(VkDescriptorSet unboxed) {
    return new_boxed_VkType<VkDescriptorSet>(unboxed);
}

void delete_VkDescriptorSet(Boxed<VkDescriptorSet> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorSet(Boxed<VkDescriptorSet> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorSet unbox_VkDescriptorSet(Boxed<VkDescriptorSet> boxed) {
    return unbox_VkType<VkDescriptorSet>(boxed);
}

VkDescriptorSet try_unbox_VkDescriptorSet(Boxed<VkDescriptorSet> boxed) {
    return try_unbox_VkType<VkDescriptorSet>(boxed);
}

Boxed<VkDescriptorSet> unboxed_to_boxed_non_dispatchable_VkDescriptorSet(VkDescriptorSet unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorSet>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorSet(Boxed<VkDescriptorSet> boxed, VkDescriptorSet new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorSet>(boxed, new_unboxed);
}

Boxed<VkDescriptorSetLayout> new_boxed_non_dispatchable_VkDescriptorSetLayout(VkDescriptorSetLayout unboxed) {
    return new_boxed_VkType<VkDescriptorSetLayout>(unboxed);
}

void delete_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorSetLayout unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout> boxed) {
    return unbox_VkType<VkDescriptorSetLayout>(boxed);
}

VkDescriptorSetLayout try_unbox_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout> boxed) {
    return try_unbox_VkType<VkDescriptorSetLayout>(boxed);
}

Boxed<VkDescriptorSetLayout> unboxed_to_boxed_non_dispatchable_VkDescriptorSetLayout(VkDescriptorSetLayout unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorSetLayout>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorSetLayout(Boxed<VkDescriptorSetLayout> boxed, VkDescriptorSetLayout new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorSetLayout>(boxed, new_unboxed);
}

Boxed<VkDescriptorUpdateTemplate> new_boxed_non_dispatchable_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate unboxed) {
    return new_boxed_VkType<VkDescriptorUpdateTemplate>(unboxed);
}

void delete_VkDescriptorUpdateTemplate(Boxed<VkDescriptorUpdateTemplate> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDescriptorUpdateTemplate(Boxed<VkDescriptorUpdateTemplate> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDescriptorUpdateTemplate unbox_VkDescriptorUpdateTemplate(Boxed<VkDescriptorUpdateTemplate> boxed) {
    return unbox_VkType<VkDescriptorUpdateTemplate>(boxed);
}

VkDescriptorUpdateTemplate try_unbox_VkDescriptorUpdateTemplate(Boxed<VkDescriptorUpdateTemplate> boxed) {
    return try_unbox_VkType<VkDescriptorUpdateTemplate>(boxed);
}

Boxed<VkDescriptorUpdateTemplate> unboxed_to_boxed_non_dispatchable_VkDescriptorUpdateTemplate(VkDescriptorUpdateTemplate unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDescriptorUpdateTemplate>(unboxed);
}

void set_boxed_non_dispatchable_VkDescriptorUpdateTemplate(Boxed<VkDescriptorUpdateTemplate> boxed, VkDescriptorUpdateTemplate new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDescriptorUpdateTemplate>(boxed, new_unboxed);
}

Boxed<VkDeviceMemory> new_boxed_non_dispatchable_VkDeviceMemory(VkDeviceMemory unboxed) {
    return new_boxed_VkType<VkDeviceMemory>(unboxed);
}

void delete_VkDeviceMemory(Boxed<VkDeviceMemory> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDeviceMemory(Boxed<VkDeviceMemory> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDeviceMemory unbox_VkDeviceMemory(Boxed<VkDeviceMemory> boxed) {
    return unbox_VkType<VkDeviceMemory>(boxed);
}

VkDeviceMemory try_unbox_VkDeviceMemory(Boxed<VkDeviceMemory> boxed) {
    return try_unbox_VkType<VkDeviceMemory>(boxed);
}

Boxed<VkDeviceMemory> unboxed_to_boxed_non_dispatchable_VkDeviceMemory(VkDeviceMemory unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDeviceMemory>(unboxed);
}

void set_boxed_non_dispatchable_VkDeviceMemory(Boxed<VkDeviceMemory> boxed, VkDeviceMemory new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDeviceMemory>(boxed, new_unboxed);
}

Boxed<VkDisplayKHR> new_boxed_non_dispatchable_VkDisplayKHR(VkDisplayKHR unboxed) {
    return new_boxed_VkType<VkDisplayKHR>(unboxed);
}

void delete_VkDisplayKHR(Boxed<VkDisplayKHR> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDisplayKHR(Boxed<VkDisplayKHR> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDisplayKHR unbox_VkDisplayKHR(Boxed<VkDisplayKHR> boxed) {
    return unbox_VkType<VkDisplayKHR>(boxed);
}

VkDisplayKHR try_unbox_VkDisplayKHR(Boxed<VkDisplayKHR> boxed) {
    return try_unbox_VkType<VkDisplayKHR>(boxed);
}

Boxed<VkDisplayKHR> unboxed_to_boxed_non_dispatchable_VkDisplayKHR(VkDisplayKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDisplayKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkDisplayKHR(Boxed<VkDisplayKHR> boxed, VkDisplayKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDisplayKHR>(boxed, new_unboxed);
}

Boxed<VkDisplayModeKHR> new_boxed_non_dispatchable_VkDisplayModeKHR(VkDisplayModeKHR unboxed) {
    return new_boxed_VkType<VkDisplayModeKHR>(unboxed);
}

void delete_VkDisplayModeKHR(Boxed<VkDisplayModeKHR> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkDisplayModeKHR(Boxed<VkDisplayModeKHR> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkDisplayModeKHR unbox_VkDisplayModeKHR(Boxed<VkDisplayModeKHR> boxed) {
    return unbox_VkType<VkDisplayModeKHR>(boxed);
}

VkDisplayModeKHR try_unbox_VkDisplayModeKHR(Boxed<VkDisplayModeKHR> boxed) {
    return try_unbox_VkType<VkDisplayModeKHR>(boxed);
}

Boxed<VkDisplayModeKHR> unboxed_to_boxed_non_dispatchable_VkDisplayModeKHR(VkDisplayModeKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkDisplayModeKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkDisplayModeKHR(Boxed<VkDisplayModeKHR> boxed, VkDisplayModeKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkDisplayModeKHR>(boxed, new_unboxed);
}

Boxed<VkEvent> new_boxed_non_dispatchable_VkEvent(VkEvent unboxed) {
    return new_boxed_VkType<VkEvent>(unboxed);
}

void delete_VkEvent(Boxed<VkEvent> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkEvent(Boxed<VkEvent> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkEvent unbox_VkEvent(Boxed<VkEvent> boxed) {
    return unbox_VkType<VkEvent>(boxed);
}

VkEvent try_unbox_VkEvent(Boxed<VkEvent> boxed) {
    return try_unbox_VkType<VkEvent>(boxed);
}

Boxed<VkEvent> unboxed_to_boxed_non_dispatchable_VkEvent(VkEvent unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkEvent>(unboxed);
}

void set_boxed_non_dispatchable_VkEvent(Boxed<VkEvent> boxed, VkEvent new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkEvent>(boxed, new_unboxed);
}

Boxed<VkFence> new_boxed_non_dispatchable_VkFence(VkFence unboxed) {
    return new_boxed_VkType<VkFence>(unboxed);
}

void delete_VkFence(Boxed<VkFence> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkFence(Boxed<VkFence> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkFence unbox_VkFence(Boxed<VkFence> boxed) {
    return unbox_VkType<VkFence>(boxed);
}

VkFence try_unbox_VkFence(Boxed<VkFence> boxed) {
    return try_unbox_VkType<VkFence>(boxed);
}

Boxed<VkFence> unboxed_to_boxed_non_dispatchable_VkFence(VkFence unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkFence>(unboxed);
}

void set_boxed_non_dispatchable_VkFence(Boxed<VkFence> boxed, VkFence new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkFence>(boxed, new_unboxed);
}

Boxed<VkFramebuffer> new_boxed_non_dispatchable_VkFramebuffer(VkFramebuffer unboxed) {
    return new_boxed_VkType<VkFramebuffer>(unboxed);
}

void delete_VkFramebuffer(Boxed<VkFramebuffer> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkFramebuffer(Boxed<VkFramebuffer> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkFramebuffer unbox_VkFramebuffer(Boxed<VkFramebuffer> boxed) {
    return unbox_VkType<VkFramebuffer>(boxed);
}

VkFramebuffer try_unbox_VkFramebuffer(Boxed<VkFramebuffer> boxed) {
    return try_unbox_VkType<VkFramebuffer>(boxed);
}

Boxed<VkFramebuffer> unboxed_to_boxed_non_dispatchable_VkFramebuffer(VkFramebuffer unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkFramebuffer>(unboxed);
}

void set_boxed_non_dispatchable_VkFramebuffer(Boxed<VkFramebuffer> boxed, VkFramebuffer new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkFramebuffer>(boxed, new_unboxed);
}

Boxed<VkImage> new_boxed_non_dispatchable_VkImage(VkImage unboxed) {
    return new_boxed_VkType<VkImage>(unboxed);
}

void delete_VkImage(Boxed<VkImage> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkImage(Boxed<VkImage> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkImage unbox_VkImage(Boxed<VkImage> boxed) {
    return unbox_VkType<VkImage>(boxed);
}

VkImage try_unbox_VkImage(Boxed<VkImage> boxed) {
    return try_unbox_VkType<VkImage>(boxed);
}

Boxed<VkImage> unboxed_to_boxed_non_dispatchable_VkImage(VkImage unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkImage>(unboxed);
}

void set_boxed_non_dispatchable_VkImage(Boxed<VkImage> boxed, VkImage new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkImage>(boxed, new_unboxed);
}

Boxed<VkImageView> new_boxed_non_dispatchable_VkImageView(VkImageView unboxed) {
    return new_boxed_VkType<VkImageView>(unboxed);
}

void delete_VkImageView(Boxed<VkImageView> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkImageView(Boxed<VkImageView> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkImageView unbox_VkImageView(Boxed<VkImageView> boxed) {
    return unbox_VkType<VkImageView>(boxed);
}

VkImageView try_unbox_VkImageView(Boxed<VkImageView> boxed) {
    return try_unbox_VkType<VkImageView>(boxed);
}

Boxed<VkImageView> unboxed_to_boxed_non_dispatchable_VkImageView(VkImageView unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkImageView>(unboxed);
}

void set_boxed_non_dispatchable_VkImageView(Boxed<VkImageView> boxed, VkImageView new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkImageView>(boxed, new_unboxed);
}

Boxed<VkIndirectCommandsLayoutNV> new_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV unboxed) {
    return new_boxed_VkType<VkIndirectCommandsLayoutNV>(unboxed);
}

void delete_VkIndirectCommandsLayoutNV(Boxed<VkIndirectCommandsLayoutNV> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkIndirectCommandsLayoutNV(Boxed<VkIndirectCommandsLayoutNV> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkIndirectCommandsLayoutNV unbox_VkIndirectCommandsLayoutNV(Boxed<VkIndirectCommandsLayoutNV> boxed) {
    return unbox_VkType<VkIndirectCommandsLayoutNV>(boxed);
}

VkIndirectCommandsLayoutNV try_unbox_VkIndirectCommandsLayoutNV(Boxed<VkIndirectCommandsLayoutNV> boxed) {
    return try_unbox_VkType<VkIndirectCommandsLayoutNV>(boxed);
}

Boxed<VkIndirectCommandsLayoutNV> unboxed_to_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(VkIndirectCommandsLayoutNV unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkIndirectCommandsLayoutNV>(unboxed);
}

void set_boxed_non_dispatchable_VkIndirectCommandsLayoutNV(Boxed<VkIndirectCommandsLayoutNV> boxed, VkIndirectCommandsLayoutNV new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkIndirectCommandsLayoutNV>(boxed, new_unboxed);
}

Boxed<VkMicromapEXT> new_boxed_non_dispatchable_VkMicromapEXT(VkMicromapEXT unboxed) {
    return new_boxed_VkType<VkMicromapEXT>(unboxed);
}

void delete_VkMicromapEXT(Boxed<VkMicromapEXT> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkMicromapEXT(Boxed<VkMicromapEXT> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkMicromapEXT unbox_VkMicromapEXT(Boxed<VkMicromapEXT> boxed) {
    return unbox_VkType<VkMicromapEXT>(boxed);
}

VkMicromapEXT try_unbox_VkMicromapEXT(Boxed<VkMicromapEXT> boxed) {
    return try_unbox_VkType<VkMicromapEXT>(boxed);
}

Boxed<VkMicromapEXT> unboxed_to_boxed_non_dispatchable_VkMicromapEXT(VkMicromapEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkMicromapEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkMicromapEXT(Boxed<VkMicromapEXT> boxed, VkMicromapEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkMicromapEXT>(boxed, new_unboxed);
}

Boxed<VkPipeline> new_boxed_non_dispatchable_VkPipeline(VkPipeline unboxed) {
    return new_boxed_VkType<VkPipeline>(unboxed);
}

void delete_VkPipeline(Boxed<VkPipeline> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipeline(Boxed<VkPipeline> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipeline unbox_VkPipeline(Boxed<VkPipeline> boxed) {
    return unbox_VkType<VkPipeline>(boxed);
}

VkPipeline try_unbox_VkPipeline(Boxed<VkPipeline> boxed) {
    return try_unbox_VkType<VkPipeline>(boxed);
}

Boxed<VkPipeline> unboxed_to_boxed_non_dispatchable_VkPipeline(VkPipeline unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipeline>(unboxed);
}

void set_boxed_non_dispatchable_VkPipeline(Boxed<VkPipeline> boxed, VkPipeline new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipeline>(boxed, new_unboxed);
}

Boxed<VkPipelineCache> new_boxed_non_dispatchable_VkPipelineCache(VkPipelineCache unboxed) {
    return new_boxed_VkType<VkPipelineCache>(unboxed);
}

void delete_VkPipelineCache(Boxed<VkPipelineCache> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipelineCache(Boxed<VkPipelineCache> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipelineCache unbox_VkPipelineCache(Boxed<VkPipelineCache> boxed) {
    return unbox_VkType<VkPipelineCache>(boxed);
}

VkPipelineCache try_unbox_VkPipelineCache(Boxed<VkPipelineCache> boxed) {
    return try_unbox_VkType<VkPipelineCache>(boxed);
}

Boxed<VkPipelineCache> unboxed_to_boxed_non_dispatchable_VkPipelineCache(VkPipelineCache unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipelineCache>(unboxed);
}

void set_boxed_non_dispatchable_VkPipelineCache(Boxed<VkPipelineCache> boxed, VkPipelineCache new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipelineCache>(boxed, new_unboxed);
}

Boxed<VkPipelineLayout> new_boxed_non_dispatchable_VkPipelineLayout(VkPipelineLayout unboxed) {
    return new_boxed_VkType<VkPipelineLayout>(unboxed);
}

void delete_VkPipelineLayout(Boxed<VkPipelineLayout> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPipelineLayout(Boxed<VkPipelineLayout> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPipelineLayout unbox_VkPipelineLayout(Boxed<VkPipelineLayout> boxed) {
    return unbox_VkType<VkPipelineLayout>(boxed);
}

VkPipelineLayout try_unbox_VkPipelineLayout(Boxed<VkPipelineLayout> boxed) {
    return try_unbox_VkType<VkPipelineLayout>(boxed);
}

Boxed<VkPipelineLayout> unboxed_to_boxed_non_dispatchable_VkPipelineLayout(VkPipelineLayout unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPipelineLayout>(unboxed);
}

void set_boxed_non_dispatchable_VkPipelineLayout(Boxed<VkPipelineLayout> boxed, VkPipelineLayout new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPipelineLayout>(boxed, new_unboxed);
}

Boxed<VkPrivateDataSlot> new_boxed_non_dispatchable_VkPrivateDataSlot(VkPrivateDataSlot unboxed) {
    return new_boxed_VkType<VkPrivateDataSlot>(unboxed);
}

void delete_VkPrivateDataSlot(Boxed<VkPrivateDataSlot> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkPrivateDataSlot(Boxed<VkPrivateDataSlot> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkPrivateDataSlot unbox_VkPrivateDataSlot(Boxed<VkPrivateDataSlot> boxed) {
    return unbox_VkType<VkPrivateDataSlot>(boxed);
}

VkPrivateDataSlot try_unbox_VkPrivateDataSlot(Boxed<VkPrivateDataSlot> boxed) {
    return try_unbox_VkType<VkPrivateDataSlot>(boxed);
}

Boxed<VkPrivateDataSlot> unboxed_to_boxed_non_dispatchable_VkPrivateDataSlot(VkPrivateDataSlot unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkPrivateDataSlot>(unboxed);
}

void set_boxed_non_dispatchable_VkPrivateDataSlot(Boxed<VkPrivateDataSlot> boxed, VkPrivateDataSlot new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkPrivateDataSlot>(boxed, new_unboxed);
}

Boxed<VkQueryPool> new_boxed_non_dispatchable_VkQueryPool(VkQueryPool unboxed) {
    return new_boxed_VkType<VkQueryPool>(unboxed);
}

void delete_VkQueryPool(Boxed<VkQueryPool> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkQueryPool(Boxed<VkQueryPool> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkQueryPool unbox_VkQueryPool(Boxed<VkQueryPool> boxed) {
    return unbox_VkType<VkQueryPool>(boxed);
}

VkQueryPool try_unbox_VkQueryPool(Boxed<VkQueryPool> boxed) {
    return try_unbox_VkType<VkQueryPool>(boxed);
}

Boxed<VkQueryPool> unboxed_to_boxed_non_dispatchable_VkQueryPool(VkQueryPool unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkQueryPool>(unboxed);
}

void set_boxed_non_dispatchable_VkQueryPool(Boxed<VkQueryPool> boxed, VkQueryPool new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkQueryPool>(boxed, new_unboxed);
}

Boxed<VkRenderPass> new_boxed_non_dispatchable_VkRenderPass(VkRenderPass unboxed) {
    return new_boxed_VkType<VkRenderPass>(unboxed);
}

void delete_VkRenderPass(Boxed<VkRenderPass> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkRenderPass(Boxed<VkRenderPass> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkRenderPass unbox_VkRenderPass(Boxed<VkRenderPass> boxed) {
    return unbox_VkType<VkRenderPass>(boxed);
}

VkRenderPass try_unbox_VkRenderPass(Boxed<VkRenderPass> boxed) {
    return try_unbox_VkType<VkRenderPass>(boxed);
}

Boxed<VkRenderPass> unboxed_to_boxed_non_dispatchable_VkRenderPass(VkRenderPass unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkRenderPass>(unboxed);
}

void set_boxed_non_dispatchable_VkRenderPass(Boxed<VkRenderPass> boxed, VkRenderPass new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkRenderPass>(boxed, new_unboxed);
}

Boxed<VkSampler> new_boxed_non_dispatchable_VkSampler(VkSampler unboxed) {
    return new_boxed_VkType<VkSampler>(unboxed);
}

void delete_VkSampler(Boxed<VkSampler> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSampler(Boxed<VkSampler> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSampler unbox_VkSampler(Boxed<VkSampler> boxed) {
    return unbox_VkType<VkSampler>(boxed);
}

VkSampler try_unbox_VkSampler(Boxed<VkSampler> boxed) {
    return try_unbox_VkType<VkSampler>(boxed);
}

Boxed<VkSampler> unboxed_to_boxed_non_dispatchable_VkSampler(VkSampler unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSampler>(unboxed);
}

void set_boxed_non_dispatchable_VkSampler(Boxed<VkSampler> boxed, VkSampler new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSampler>(boxed, new_unboxed);
}

Boxed<VkSamplerYcbcrConversion> new_boxed_non_dispatchable_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion unboxed) {
    return new_boxed_VkType<VkSamplerYcbcrConversion>(unboxed);
}

void delete_VkSamplerYcbcrConversion(Boxed<VkSamplerYcbcrConversion> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSamplerYcbcrConversion(Boxed<VkSamplerYcbcrConversion> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSamplerYcbcrConversion unbox_VkSamplerYcbcrConversion(Boxed<VkSamplerYcbcrConversion> boxed) {
    return unbox_VkType<VkSamplerYcbcrConversion>(boxed);
}

VkSamplerYcbcrConversion try_unbox_VkSamplerYcbcrConversion(Boxed<VkSamplerYcbcrConversion> boxed) {
    return try_unbox_VkType<VkSamplerYcbcrConversion>(boxed);
}

Boxed<VkSamplerYcbcrConversion> unboxed_to_boxed_non_dispatchable_VkSamplerYcbcrConversion(VkSamplerYcbcrConversion unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSamplerYcbcrConversion>(unboxed);
}

void set_boxed_non_dispatchable_VkSamplerYcbcrConversion(Boxed<VkSamplerYcbcrConversion> boxed, VkSamplerYcbcrConversion new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSamplerYcbcrConversion>(boxed, new_unboxed);
}

Boxed<VkSemaphore> new_boxed_non_dispatchable_VkSemaphore(VkSemaphore unboxed) {
    return new_boxed_VkType<VkSemaphore>(unboxed);
}

void delete_VkSemaphore(Boxed<VkSemaphore> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSemaphore(Boxed<VkSemaphore> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSemaphore unbox_VkSemaphore(Boxed<VkSemaphore> boxed) {
    return unbox_VkType<VkSemaphore>(boxed);
}

VkSemaphore try_unbox_VkSemaphore(Boxed<VkSemaphore> boxed) {
    return try_unbox_VkType<VkSemaphore>(boxed);
}

Boxed<VkSemaphore> unboxed_to_boxed_non_dispatchable_VkSemaphore(VkSemaphore unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSemaphore>(unboxed);
}

void set_boxed_non_dispatchable_VkSemaphore(Boxed<VkSemaphore> boxed, VkSemaphore new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSemaphore>(boxed, new_unboxed);
}

Boxed<VkShaderModule> new_boxed_non_dispatchable_VkShaderModule(VkShaderModule unboxed) {
    return new_boxed_VkType<VkShaderModule>(unboxed);
}

void delete_VkShaderModule(Boxed<VkShaderModule> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkShaderModule(Boxed<VkShaderModule> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkShaderModule unbox_VkShaderModule(Boxed<VkShaderModule> boxed) {
    return unbox_VkType<VkShaderModule>(boxed);
}

VkShaderModule try_unbox_VkShaderModule(Boxed<VkShaderModule> boxed) {
    return try_unbox_VkType<VkShaderModule>(boxed);
}

Boxed<VkShaderModule> unboxed_to_boxed_non_dispatchable_VkShaderModule(VkShaderModule unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkShaderModule>(unboxed);
}

void set_boxed_non_dispatchable_VkShaderModule(Boxed<VkShaderModule> boxed, VkShaderModule new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkShaderModule>(boxed, new_unboxed);
}

Boxed<VkSurfaceKHR> new_boxed_non_dispatchable_VkSurfaceKHR(VkSurfaceKHR unboxed) {
    return new_boxed_VkType<VkSurfaceKHR>(unboxed);
}

void delete_VkSurfaceKHR(Boxed<VkSurfaceKHR> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSurfaceKHR(Boxed<VkSurfaceKHR> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSurfaceKHR unbox_VkSurfaceKHR(Boxed<VkSurfaceKHR> boxed) {
    return unbox_VkType<VkSurfaceKHR>(boxed);
}

VkSurfaceKHR try_unbox_VkSurfaceKHR(Boxed<VkSurfaceKHR> boxed) {
    return try_unbox_VkType<VkSurfaceKHR>(boxed);
}

Boxed<VkSurfaceKHR> unboxed_to_boxed_non_dispatchable_VkSurfaceKHR(VkSurfaceKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSurfaceKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkSurfaceKHR(Boxed<VkSurfaceKHR> boxed, VkSurfaceKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSurfaceKHR>(boxed, new_unboxed);
}

Boxed<VkSwapchainKHR> new_boxed_non_dispatchable_VkSwapchainKHR(VkSwapchainKHR unboxed) {
    return new_boxed_VkType<VkSwapchainKHR>(unboxed);
}

void delete_VkSwapchainKHR(Boxed<VkSwapchainKHR> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkSwapchainKHR(Boxed<VkSwapchainKHR> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkSwapchainKHR unbox_VkSwapchainKHR(Boxed<VkSwapchainKHR> boxed) {
    return unbox_VkType<VkSwapchainKHR>(boxed);
}

VkSwapchainKHR try_unbox_VkSwapchainKHR(Boxed<VkSwapchainKHR> boxed) {
    return try_unbox_VkType<VkSwapchainKHR>(boxed);
}

Boxed<VkSwapchainKHR> unboxed_to_boxed_non_dispatchable_VkSwapchainKHR(VkSwapchainKHR unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkSwapchainKHR>(unboxed);
}

void set_boxed_non_dispatchable_VkSwapchainKHR(Boxed<VkSwapchainKHR> boxed, VkSwapchainKHR new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkSwapchainKHR>(boxed, new_unboxed);
}

Boxed<VkValidationCacheEXT> new_boxed_non_dispatchable_VkValidationCacheEXT(VkValidationCacheEXT unboxed) {
    return new_boxed_VkType<VkValidationCacheEXT>(unboxed);
}

void delete_VkValidationCacheEXT(Boxed<VkValidationCacheEXT> boxed) {
    delete_VkType(boxed);
}

void delayed_delete_VkValidationCacheEXT(Boxed<VkValidationCacheEXT> boxed, VkDevice device, std::function<void()> callback) {
    delayed_delete_VkType(boxed, device, std::move(callback));
}

VkValidationCacheEXT unbox_VkValidationCacheEXT(Boxed<VkValidationCacheEXT> boxed) {
    return unbox_VkType<VkValidationCacheEXT>(boxed);
}

VkValidationCacheEXT try_unbox_VkValidationCacheEXT(Boxed<VkValidationCacheEXT> boxed) {
    return try_unbox_VkType<VkValidationCacheEXT>(boxed);
}

Boxed<VkValidationCacheEXT> unboxed_to_boxed_non_dispatchable_VkValidationCacheEXT(VkValidationCacheEXT unboxed) {
    return unboxed_to_boxed_non_dispatchable_VkType<VkValidationCacheEXT>(unboxed);
}

void set_boxed_non_dispatchable_VkValidationCacheEXT(Boxed<VkValidationCacheEXT> boxed, VkValidationCacheEXT new_unboxed) {
    set_boxed_non_dispatchable_VkType<VkValidationCacheEXT>(boxed, new_unboxed);
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
