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

void BoxedHandleManager::replayHandles(const std::vector<uint64_t>& handles) {
    mHandleReplayQueue.clear();
    for (uint64_t handle : handles) {
        mHandleReplayQueue.push_back(BoxedHandle{handle});
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
    uint64_t rawHandle = 0;
    const UnboxedHandle underlying = item.underlying;
    item.tag = tag;

    if (mHandleReplay) {
        const BoxedHandle replayHandle = mHandleReplayQueue.front();
        mHandleReplayQueue.pop_front();
        mHandleReplay = !mHandleReplayQueue.empty();

        rawHandle = mStore.addFixed(toRaw(replayHandle), std::move(item), static_cast<size_t>(tag));
    } else {
        rawHandle = mStore.add(std::move(item), static_cast<size_t>(tag));
    }

    const BoxedHandle handle{rawHandle};

    // VK_NULL_HANDLE is never a valid reverse-lookup key (see eraseReverseMappingLocked), so
    // don't record one that could later be returned for getBoxedFromUnboxed(0).
    if (underlying != kNullUnboxedHandle) {
        std::lock_guard<std::mutex> lock(mMutex);
        mReverseMap[{underlying, tag}] = handle;
    }
    return handle;
}

void BoxedHandleManager::eraseReverseMappingLocked(UnboxedHandle unboxed, BoxedHandleTypeTag tag,
                                                   BoxedHandle boxed) {
    if (unboxed == kNullUnboxedHandle) {
        return;
    }
    // Only erase the reverse mapping if it still points at |boxed|. The host driver may reuse
    // an unboxed handle value as soon as the underlying object is destroyed, and a new boxed
    // handle may already have been registered for it by another thread before the old boxed
    // handle is removed (e.g. on_vkDestroyImage releases the global state lock before the decoder
    // calls delete_VkImage). Unconditionally erasing would drop the new object's reverse mapping
    // and break unboxed_to_boxed_* lookups (and with them snapshot dependency tracking).
    auto it = mReverseMap.find({unboxed, tag});
    if (it != mReverseMap.end() && it->second == boxed) {
        mReverseMap.erase(it);
    }
}

void BoxedHandleManager::update(BoxedHandle handle, BoxedHandleInfo item,
                                BoxedHandleTypeTag expectedTag) {
    const uint64_t rawHandle = toRaw(handle);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return;
    }
    const UnboxedHandle newHandle = item.underlying;
    // Declared before the lock so it is destroyed after mMutex is released: its members may run
    // arbitrary destructors.
    BoxedHandleInfo previous;
    // mMutex is held across the store swap and the reverse-map update so that update() and
    // remove() on the same boxed handle are atomic with respect to each other; otherwise a
    // remove() interleaved between the two steps could leave mReverseMap pointing at a dead
    // handle. mMutex is never taken while the store lock is held, so the nesting is safe.
    std::lock_guard<std::mutex> lock(mMutex);
    const bool live = mStore.withLocked(rawHandle, [&](BoxedHandleInfo* stored) {
        if (!stored || stored->tag != expectedTag) {
            return false;
        }
        item.tag = expectedTag;
        previous = std::move(*stored);
        *stored = std::move(item);
        return true;
    });
    if (!live) {
        return;
    }
    eraseReverseMappingLocked(previous.underlying, expectedTag, handle);
    if (newHandle != kNullUnboxedHandle) {
        mReverseMap[{newHandle, expectedTag}] = handle;
    }
}

void BoxedHandleManager::remove(BoxedHandle h, BoxedHandleTypeTag expectedTag) {
    const uint64_t rawHandle = toRaw(h);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return;
    }
    // As in update(): declared before the lock so its destructors run after mMutex is released.
    BoxedHandleInfo removed;
    // mMutex covers the store lookup, the reverse-map erase and the store removal so that no
    // concurrent update(h) can slip in between and leave a stale reverse mapping. The info is
    // moved out under the store lock so concurrent readers cannot observe a partially cleared
    // entry.
    std::lock_guard<std::mutex> lock(mMutex);
    const bool live = mStore.withLocked(rawHandle, [&](BoxedHandleInfo* stored) {
        if (!stored || stored->tag != expectedTag) {
            return false;
        }
        removed = std::move(*stored);
        *stored = {};
        return true;
    });
    if (!live) {
        return;
    }
    eraseReverseMappingLocked(removed.underlying, expectedTag, h);
    mStore.remove(rawHandle);
}

void BoxedHandleManager::removeDelayed(BoxedHandle h, VkDevice device,
                                       std::function<void()> callback,
                                       BoxedHandleTypeTag expectedTag) {
    const uint64_t rawHandle = toRaw(h);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return;
    }
    std::lock_guard<std::mutex> lock(mMutex);
    const bool live =
        mStore.withSharedLocked(rawHandle, [expectedTag](const BoxedHandleInfo* stored) {
            return stored != nullptr && stored->tag == expectedTag;
        });
    if (!live) {
        return;
    }
    mDelayedRemoves[device].push_back({h, expectedTag, std::move(callback)});
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
        if (!getUnderlying(r.handle, r.expectedTag).has_value()) {
            continue;
        }
        // VkDecoderGlobalState is not locked when callback is called.
        if (r.callback) {
            r.callback();
        }

        remove(r.handle, r.expectedTag);
    }
}

std::optional<UnboxedHandle> BoxedHandleManager::getUnderlying(
    BoxedHandle handle, BoxedHandleTypeTag expectedTag) const {
    const uint64_t rawHandle = toRaw(handle);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return std::nullopt;
    }
    return mStore.withSharedLocked(
        rawHandle, [expectedTag](const BoxedHandleInfo* info) -> std::optional<UnboxedHandle> {
            if (info == nullptr || info->tag != expectedTag) {
                return std::nullopt;
            }
            return info->underlying;
        });
}

VulkanDispatch* BoxedHandleManager::getDispatch(BoxedHandle handle,
                                                BoxedHandleTypeTag expectedTag) const {
    const uint64_t rawHandle = toRaw(handle);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return nullptr;
    }
    return mStore.withSharedLocked(rawHandle,
                                   [expectedTag](const BoxedHandleInfo* info) -> VulkanDispatch* {
                                       if (info == nullptr || info->tag != expectedTag) {
                                           return nullptr;
                                       }
                                       return info->dispatch;
                                   });
}

std::shared_ptr<OrderMaintenanceInfo> BoxedHandleManager::getOrderMaintenanceInfo(
    BoxedHandle handle, BoxedHandleTypeTag expectedTag) const {
    const uint64_t rawHandle = toRaw(handle);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return nullptr;
    }
    return mStore.withSharedLocked(
        rawHandle,
        [expectedTag](const BoxedHandleInfo* info) -> std::shared_ptr<OrderMaintenanceInfo> {
            if (info == nullptr || info->tag != expectedTag) {
                return nullptr;
            }
            return info->ordMaintInfo;
        });
}

VulkanMemReadingStream* BoxedHandleManager::getReadStream(BoxedHandle handle,
                                                          BoxedHandleTypeTag expectedTag) const {
    const uint64_t rawHandle = toRaw(handle);
    if (expectedTag == Tag_Invalid ||
        Store::EM::getHandleType(rawHandle) != static_cast<size_t>(expectedTag)) {
        return nullptr;
    }
    return mStore.withSharedLocked(
        rawHandle, [expectedTag](const BoxedHandleInfo* info) -> VulkanMemReadingStream* {
            if (info == nullptr || info->tag != expectedTag) {
                return nullptr;
            }
            return info->readStream.get();
        });
}

VulkanMemReadingStream* BoxedHandleManager::getOrCreateReadStream(
    BoxedHandle handle, BoxedHandleTypeTag expectedTag,
    const gfxstream::host::FeatureSet& features) {
    if (VulkanMemReadingStream* existing = getReadStream(handle, expectedTag)) {
        return existing;
    }
    if (!getUnderlying(handle, expectedTag).has_value()) {
        return nullptr;
    }
    // Construct outside the store lock so |newStream|'s constructor/destructor never run while
    // holding the store lock.
    auto newStream = std::make_unique<VulkanMemReadingStream>(nullptr, features);
    const uint64_t rawHandle = toRaw(handle);
    return mStore.withLocked(rawHandle, [&](BoxedHandleInfo* info) -> VulkanMemReadingStream* {
        if (info == nullptr || info->tag != expectedTag) {
            return nullptr;
        }
        if (info->readStream == nullptr) {
            info->readStream = std::move(newStream);
        }
        return info->readStream.get();
    });
}

BoxedHandle BoxedHandleManager::getBoxedFromUnboxed(UnboxedHandle unboxed,
                                                    BoxedHandleTypeTag expectedTag) const {
    if (expectedTag == Tag_Invalid || unboxed == kNullUnboxedHandle) {
        return kNullBoxedHandle;
    }
    std::lock_guard<std::mutex> lock(mMutex);

    auto it = mReverseMap.find({unboxed, expectedTag});
    if (it == mReverseMap.end()) {
        return kNullBoxedHandle;
    }
    if (Store::EM::getHandleType(toRaw(it->second)) != static_cast<size_t>(expectedTag)) {
        return kNullBoxedHandle;
    }

    return it->second;
}

BoxedHandleManager sBoxedHandleManager;

template <typename VkObjectT>
Boxed<VkObjectT> new_boxed_VkType(VkObjectT underlying, bool dispatchable = false,
                                  VulkanDispatch* dispatch = nullptr) {
    BoxedHandleInfo info;
    info.underlying = toUnboxedHandle(underlying);
    if (dispatchable) {
        if (dispatch != nullptr) {
            info.dispatch = dispatch;
        } else {
            info.ownedDispatch = std::make_unique<VulkanDispatch>();
            info.dispatch = info.ownedDispatch.get();
        }
        info.ordMaintInfo = std::make_shared<OrderMaintenanceInfo>();
    }
    return sBoxedHandleManager.add<VkObjectT>(std::move(info));
}

template <typename VkObjectT>
void delete_VkType(Boxed<VkObjectT> boxed) {
    if (!boxed) {
        return;
    }

    sBoxedHandleManager.remove(boxed);
}

template <typename VkObjectT>
void delayed_delete_VkType(Boxed<VkObjectT> boxed, VkDevice device,
                           std::function<void()> callback) {
    if (!boxed) {
        return;
    }

    sBoxedHandleManager.removeDelayed(boxed, device, std::move(callback));
}

// Custom unbox_* functions or GOLDFISH_VK_LIST_DISPATCHABLE_CUSTOM_UNBOX_HANDLE_TYPES
// VkQueue objects can be virtual, meaning that multiple boxed queues can map into a single
// physical queue on the host GPU. Some conversion is needed for unboxing to physical.
static VkQueue unbox_VkQueueImpl(Boxed<VkQueue> boxed) {
    const std::optional<UnboxedHandle> underlying = sBoxedHandleManager.getUnderlying(boxed);
    if (!underlying.has_value()) {
        return VK_NULL_HANDLE;
    }
    const uint64_t unboxedQueue64 = toRaw(*underlying);

    // Use VulkanVirtualQueue directly to avoid locking for hasVirtualGraphicsQueue call.
    if (VkDecoderGlobalState::get()->getFeatures().VulkanVirtualQueue.enabled()) {
        // Clear virtual bit and unbox into the actual physical queue handle
        return fromUnboxedHandle<VkQueue>(
            UnboxedHandle{unboxedQueue64 & ~QueueInfo::kVirtualQueueBit});
    }

    return fromUnboxedHandle<VkQueue>(*underlying);
}

template <typename VkObjectT>
VkObjectT unbox_VkType(Boxed<VkObjectT> boxed) {
    if (!boxed) {
        return VK_NULL_HANDLE;
    }

    VkObjectT unboxed = VK_NULL_HANDLE;
    constexpr const char* kTypeName = BoxedHandleTraits<VkObjectT>::kTypeName;

    if constexpr (std::is_same_v<VkObjectT, VkQueue>) {
        // Queue handles require special unboxing due to virtual queue support
        unboxed = unbox_VkQueueImpl(boxed);
        if (unboxed == VK_NULL_HANDLE) {
            GFXSTREAM_ERROR("Failed to unbox %s %p", kTypeName, boxed.value());
            if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
                VkDecoderGlobalState::get()->setSnapshotLoadError();
            }
        }
    } else {
        const std::optional<UnboxedHandle> underlying = sBoxedHandleManager.getUnderlying(boxed);
        if (!underlying.has_value()) {
            if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
                // During snapshot load, do not crash the emulator, just log the error
                // The caller will handle it and fail snapshot loading.
                GFXSTREAM_ERROR("Failed to unbox %s %p during snapshot load", kTypeName,
                                boxed.value());
                VkDecoderGlobalState::get()->setSnapshotLoadError();
            } else if constexpr (std::is_same_v<VkObjectT, VkCommandBuffer> ||
                                 std::is_same_v<VkObjectT, VkDevice> ||
                                 std::is_same_v<VkObjectT, VkInstance> ||
                                 std::is_same_v<VkObjectT, VkFence> ||
                                 std::is_same_v<VkObjectT, VkPhysicalDevice>) {
                GFXSTREAM_ERROR("Failed to unbox %s %p", kTypeName, boxed.value());
            } else {
                // This should not happen during runtime
                GFXSTREAM_FATAL("Failed to unbox %s %p", kTypeName, boxed.value());
            }
            unboxed = VK_NULL_HANDLE;
        } else {
            unboxed = fromUnboxedHandle<VkObjectT>(*underlying);
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
        const std::optional<UnboxedHandle> underlying = sBoxedHandleManager.getUnderlying(boxed);
        if (underlying.has_value()) {
            unboxed = fromUnboxedHandle<VkObjectT>(*underlying);
        }
    }

    return unboxed;
}

template <typename VkObjectT>
Boxed<VkObjectT> unboxed_to_boxed_VkType(VkObjectT unboxed) {
    if (unboxed == VK_NULL_HANDLE) {
        return Boxed<VkObjectT>();
    }

    return sBoxedHandleManager.getBoxedFromUnboxed(unboxed);
}

template <typename VkObjectT>
void set_boxed_non_dispatchable_VkType(Boxed<VkObjectT> boxed, VkObjectT new_unboxed) {
    BoxedHandleInfo info;
    info.underlying = toUnboxedHandle(new_unboxed);
    sBoxedHandleManager.update(boxed, std::move(info));
}

template <typename VkObjectT>
std::shared_ptr<OrderMaintenanceInfo> get_order_maintenance_info_VkType(Boxed<VkObjectT> boxed) {
    return sBoxedHandleManager.getOrderMaintenanceInfo(boxed);
}

template <typename VkObjectT>
VulkanMemReadingStream* get_read_stream_VkType(Boxed<VkObjectT> boxed) {
    if (VulkanMemReadingStream* existing = sBoxedHandleManager.getReadStream(boxed)) {
        return existing;
    }
    if (!sBoxedHandleManager.isLive(boxed)) {
        return nullptr;
    }
    return sBoxedHandleManager.getOrCreateReadStream(boxed,
                                                     VkDecoderGlobalState::get()->getFeatures());
}

template <typename VkObjectT>
VulkanDispatch* get_dispatch_VkType(Boxed<VkObjectT> boxed) {
    VulkanDispatch* dispatch = sBoxedHandleManager.getDispatch(boxed);
    if (dispatch == nullptr) {
        GFXSTREAM_ERROR("Failed to unbox %s %p", BoxedHandleTraits<VkObjectT>::kTypeName,
                        boxed.value());
        if (VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading()) {
            VkDecoderGlobalState::get()->setSnapshotLoadError();
        }
        return nullptr;
    }
    return dispatch;
}

#define DEFINE_BOXED_DISPATCHABLE_HANDLE_API_IMPL(type)                                    \
    Boxed<type> new_boxed_##type(type unboxed, VulkanDispatch* dispatch) {                 \
        return new_boxed_VkType<type>(unboxed, /*dispatchable=*/true, dispatch);           \
    }                                                                                      \
    void delete_##type(Boxed<type> boxed) { delete_VkType(boxed); }                        \
    type unbox_##type(Boxed<type> boxed) { return unbox_VkType<type>(boxed); }             \
    type try_unbox_##type(Boxed<type> boxed) { return try_unbox_VkType<type>(boxed); }     \
    Boxed<type> unboxed_to_boxed_##type(type unboxed) {                                    \
        return unboxed_to_boxed_VkType<type>(unboxed);                                     \
    }                                                                                      \
    std::shared_ptr<OrderMaintenanceInfo> ordmaint_##type(Boxed<type> boxed) {             \
        return get_order_maintenance_info_VkType<type>(boxed);                             \
    }                                                                                      \
    VulkanMemReadingStream* readstream_##type(Boxed<type> boxed) {                         \
        return get_read_stream_VkType<type>(boxed);                                        \
    }                                                                                      \
    VulkanDispatch* dispatch_##type(Boxed<type> boxed) {                                   \
        return get_dispatch_VkType<type>(boxed);                                           \
    }

#define DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_IMPL(type)                                        \
    Boxed<type> new_boxed_non_dispatchable_##type(type unboxed) {                                  \
        return new_boxed_VkType<type>(unboxed, /*dispatchable=*/false);                            \
    }                                                                                              \
    void delete_##type(Boxed<type> boxed) { delete_VkType(boxed); }                                \
    void delayed_delete_##type(Boxed<type> boxed, VkDevice device,                                 \
                               std::function<void()> callback) {                                   \
        delayed_delete_VkType(boxed, device, std::move(callback));                                 \
    }                                                                                              \
    type unbox_##type(Boxed<type> boxed) { return unbox_VkType<type>(boxed); }                     \
    type try_unbox_##type(Boxed<type> boxed) { return try_unbox_VkType<type>(boxed); }             \
    Boxed<type> unboxed_to_boxed_non_dispatchable_##type(type unboxed) {                           \
        return unboxed_to_boxed_VkType<type>(unboxed);                                             \
    }                                                                                              \
    void set_boxed_non_dispatchable_##type(Boxed<type> boxed, type new_unboxed) {                  \
        set_boxed_non_dispatchable_VkType<type>(boxed, new_unboxed);                               \
    }

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(DEFINE_BOXED_DISPATCHABLE_HANDLE_API_IMPL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_IMPL)

#undef DEFINE_BOXED_DISPATCHABLE_HANDLE_API_IMPL
#undef DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_IMPL

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
