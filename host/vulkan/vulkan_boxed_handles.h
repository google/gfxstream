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

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <type_traits>

#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/containers/HybridEntityManager.h"
#include "gfxstream/containers/Lookup.h"
#include "gfxstream/synchronization/ConditionVariable.h"
#include "gfxstream/synchronization/Lock.h"
#include "vulkan_dispatch.h"
#include "vulkan_handles.h"
#include "vulkan_stream.h"

namespace gfxstream {
namespace host {
namespace vk {

#define DEFINE_BOXED_HANDLE_TYPE_TAG(type) Tag_##type,

enum BoxedHandleTypeTag {
    Tag_Invalid = 0,

    GOLDFISH_VK_LIST_HANDLE_TYPES_BY_STAGE(DEFINE_BOXED_HANDLE_TYPE_TAG)

    // extra command for snapshot purpose
    Tag_VkBindMemory,
    Tag_VkMapMemory,
    Tag_VkCmdOp,
    Tag_VkUpdateDescriptorSets,
    // additional generic tag
    Tag_VkGeneric = 0xFF,
};

template <typename T>
struct BoxedHandleTraits {
    static constexpr bool kIsVulkanHandle = false;
    static constexpr bool kIsDispatchable = false;
};

#define DEFINE_DISPATCHABLE_BOXED_HANDLE_TRAITS(type)          \
    template <>                                                \
    struct BoxedHandleTraits<type> {                           \
        static constexpr bool kIsVulkanHandle = true;          \
        static constexpr bool kIsDispatchable = true;          \
        static constexpr BoxedHandleTypeTag kTag = Tag_##type; \
        static constexpr const char* kTypeName = #type;        \
    };

#define DEFINE_NON_DISPATCHABLE_BOXED_HANDLE_TRAITS(type)      \
    template <>                                                \
    struct BoxedHandleTraits<type> {                           \
        static constexpr bool kIsVulkanHandle = true;          \
        static constexpr bool kIsDispatchable = false;         \
        static constexpr BoxedHandleTypeTag kTag = Tag_##type; \
        static constexpr const char* kTypeName = #type;        \
    };

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(DEFINE_DISPATCHABLE_BOXED_HANDLE_TRAITS)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(DEFINE_NON_DISPATCHABLE_BOXED_HANDLE_TRAITS)

#undef DEFINE_DISPATCHABLE_BOXED_HANDLE_TRAITS
#undef DEFINE_NON_DISPATCHABLE_BOXED_HANDLE_TRAITS

enum class BoxedHandle : uint64_t {};
enum class UnboxedHandle : uint64_t {};

inline constexpr BoxedHandle kNullBoxedHandle{0};
inline constexpr UnboxedHandle kNullUnboxedHandle{0};

constexpr uint64_t toRaw(BoxedHandle handle) noexcept {
    return static_cast<uint64_t>(handle);
}

constexpr uint64_t toRaw(UnboxedHandle handle) noexcept {
    return static_cast<uint64_t>(handle);
}

template <typename VkT>
inline UnboxedHandle toUnboxedHandle(VkT handle) noexcept {
    static_assert(BoxedHandleTraits<VkT>::kIsVulkanHandle,
                  "toUnboxedHandle requires a Vulkan handle type");
    if constexpr (std::is_pointer_v<VkT>) {
        return UnboxedHandle{static_cast<uint64_t>(reinterpret_cast<uintptr_t>(handle))};
    } else {
        return UnboxedHandle{static_cast<uint64_t>(handle)};
    }
}

template <typename VkT>
inline VkT fromUnboxedHandle(UnboxedHandle handle) noexcept {
    static_assert(BoxedHandleTraits<VkT>::kIsVulkanHandle,
                  "fromUnboxedHandle requires a Vulkan handle type");
    const uint64_t raw = toRaw(handle);
    if constexpr (std::is_pointer_v<VkT>) {
        return reinterpret_cast<VkT>(static_cast<uintptr_t>(raw));
    } else {
        return static_cast<VkT>(raw);
    }
}

template <typename VkT>
class Boxed {
    static_assert(BoxedHandleTraits<VkT>::kIsVulkanHandle,
                  "Boxed<VkT> requires a Vulkan handle type");

   public:
    constexpr Boxed() noexcept = default;
    explicit constexpr Boxed(VkT handle) noexcept : mHandle(handle) {}

    static Boxed fromRaw(uint64_t raw) noexcept {
        if constexpr (std::is_pointer_v<VkT>) {
            return Boxed(reinterpret_cast<VkT>(static_cast<uintptr_t>(raw)));
        } else {
            return Boxed(static_cast<VkT>(raw));
        }
    }

    static Boxed fromRaw(const void* raw) noexcept {
        return fromRaw(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(raw)));
    }

    static Boxed fromBoxedHandle(BoxedHandle handle) noexcept {
        return fromRaw(vk::toRaw(handle));
    }

    uint64_t toRaw() const noexcept {
        if constexpr (std::is_pointer_v<VkT>) {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(mHandle));
        } else {
            return static_cast<uint64_t>(mHandle);
        }
    }

    BoxedHandle toBoxedHandle() const noexcept { return BoxedHandle{toRaw()}; }

    constexpr VkT value() const noexcept { return mHandle; }

    explicit constexpr operator bool() const noexcept { return mHandle != VK_NULL_HANDLE; }

    friend constexpr bool operator==(Boxed a, Boxed b) noexcept { return a.mHandle == b.mHandle; }
    friend constexpr bool operator!=(Boxed a, Boxed b) noexcept { return a.mHandle != b.mHandle; }
    friend constexpr bool operator<(Boxed a, Boxed b) noexcept { return a.mHandle < b.mHandle; }

   private:
    VkT mHandle = VK_NULL_HANDLE;
};

// Boxed<VkT> must add no size or layout overhead for either handle kind:
// non-dispatchable (VkFence) and dispatchable (VkDevice, always a pointer).
static_assert(sizeof(Boxed<VkFence>) == sizeof(VkFence),
              "Boxed<VkFence> must have zero size overhead");
static_assert(std::is_standard_layout_v<Boxed<VkFence>>,
              "Boxed<VkFence> must be standard-layout");
static_assert(std::is_trivially_copyable_v<Boxed<VkFence>>,
              "Boxed<VkFence> must be trivially copyable");
static_assert(sizeof(Boxed<VkDevice>) == sizeof(VkDevice),
              "Boxed<VkDevice> must have zero size overhead");
static_assert(std::is_standard_layout_v<Boxed<VkDevice>>,
              "Boxed<VkDevice> must be standard-layout");
static_assert(std::is_trivially_copyable_v<Boxed<VkDevice>>,
              "Boxed<VkDevice> must be trivially copyable");

// Boxed handles are 64-bit EntityManager handles, and both handle kinds
// round-trip through the 64-bit BoxedHandle and UnboxedHandle strong types. For
// dispatchable handles this requires 64-bit pointers; every gfxstream host
// build is 64-bit.
static_assert(sizeof(Boxed<VkFence>) == sizeof(BoxedHandle),
              "Boxed<VkFence> must be the same size as BoxedHandle");
static_assert(sizeof(VkFence) == sizeof(UnboxedHandle),
              "VkFence must be the same size as UnboxedHandle");
static_assert(sizeof(Boxed<VkDevice>) == sizeof(BoxedHandle),
              "Boxed<VkDevice> must match the 64-bit BoxedHandle; gfxstream hosts are 64-bit");
static_assert(sizeof(VkDevice) == sizeof(UnboxedHandle),
              "VkDevice must match the 64-bit UnboxedHandle; gfxstream hosts are 64-bit");

struct OrderMaintenanceInfo {
    uint32_t sequenceNumber = 0;
    gfxstream::base::Lock lock;
    gfxstream::base::ConditionVariable cv;
};

class BoxedHandleInfo {
   public:
    UnboxedHandle underlying = kNullUnboxedHandle;
    BoxedHandleTypeTag tag = Tag_Invalid;
    VulkanDispatch* dispatch = nullptr;
    std::unique_ptr<VulkanDispatch> ownedDispatch;
    std::shared_ptr<OrderMaintenanceInfo> ordMaintInfo;
    std::unique_ptr<VulkanMemReadingStream> readStream;
};

class BoxedHandleManager {
   public:
    // The hybrid entity manager uses a reader-writer lock to allow concurrent
    // unbox lookups across render threads on a working set of 16000 handles
    // without contending on an exclusive mutex. Performance is degraded when
    // going over this number, as it will then fall back to a std::unordered_map.
    //
    // We use 16000 as the max number of live handles to track; we don't
    // expect the system to go over 16000 total live handles, outside some
    // dEQP object management tests.
    using Store = gfxstream::base::HybridEntityManager<16000, uint64_t, BoxedHandleInfo>;

    BoxedHandle add(BoxedHandleInfo item, BoxedHandleTypeTag tag);
    template <typename VkT>
    Boxed<VkT> add(BoxedHandleInfo item) {
        return Boxed<VkT>::fromBoxedHandle(add(std::move(item), BoxedHandleTraits<VkT>::kTag));
    }

    template <typename VkT>
    void update(Boxed<VkT> handle, BoxedHandleInfo item) {
        update(handle.toBoxedHandle(), std::move(item), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    void remove(Boxed<VkT> h) {
        remove(h.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    void removeDelayed(Boxed<VkT> h, VkDevice device, std::function<void()> callback) {
        removeDelayed(h.toBoxedHandle(), device, std::move(callback), BoxedHandleTraits<VkT>::kTag);
    }

    // Do not call directly! Instead use `processDelayedRemovesForDevice()` which has
    // thread safety annotations for `VkDecoderGlobalState::Impl`.
    void processDelayedRemoves(VkDevice device);

    // Returns the stored underlying handle if |handle| is live with a matching
    // type tag (which may be kNullUnboxedHandle for an unallocated slot), or
    // std::nullopt otherwise. Validated and copied under the store lock.
    template <typename VkT>
    std::optional<UnboxedHandle> getUnderlying(Boxed<VkT> handle) const {
        return getUnderlying(handle.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    bool isLive(Boxed<VkT> handle) const {
        return getUnderlying(handle).has_value();
    }

    template <typename VkT>
    VulkanDispatch* getDispatch(Boxed<VkT> handle) const {
        return getDispatch(handle.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    Boxed<VkT> getBoxedFromUnboxed(VkT unboxed) const {
        return Boxed<VkT>::fromBoxedHandle(
            getBoxedFromUnboxed(toUnboxedHandle(unboxed), BoxedHandleTraits<VkT>::kTag));
    }

    // Returns nullptr if |handle| is not live or has a mismatched tag. The
    // lookup and the copy are done under the store lock, so a concurrent
    // remove() cannot release the OrderMaintenanceInfo in between.
    template <typename VkT>
    std::shared_ptr<OrderMaintenanceInfo> getOrderMaintenanceInfo(Boxed<VkT> handle) const {
        return getOrderMaintenanceInfo(handle.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    VulkanMemReadingStream* getReadStream(Boxed<VkT> handle) const {
        return getReadStream(handle.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag);
    }

    template <typename VkT>
    VulkanMemReadingStream* getOrCreateReadStream(Boxed<VkT> handle,
                                                  const gfxstream::host::FeatureSet& features) {
        return getOrCreateReadStream(handle.toBoxedHandle(), BoxedHandleTraits<VkT>::kTag,
                                     features);
    }

    void replayHandles(const std::vector<uint64_t>& handles);

    void clear();

    uint64_t getHandlesCount() const;

   private:
    void update(BoxedHandle handle, BoxedHandleInfo item, BoxedHandleTypeTag expectedTag);
    void remove(BoxedHandle h, BoxedHandleTypeTag expectedTag);
    void removeDelayed(BoxedHandle h, VkDevice device, std::function<void()> callback,
                       BoxedHandleTypeTag expectedTag);
    std::optional<UnboxedHandle> getUnderlying(BoxedHandle handle,
                                               BoxedHandleTypeTag expectedTag) const;
    VulkanDispatch* getDispatch(BoxedHandle handle, BoxedHandleTypeTag expectedTag) const;
    BoxedHandle getBoxedFromUnboxed(UnboxedHandle unboxed, BoxedHandleTypeTag expectedTag) const;
    std::shared_ptr<OrderMaintenanceInfo> getOrderMaintenanceInfo(
        BoxedHandle handle, BoxedHandleTypeTag expectedTag) const;
    VulkanMemReadingStream* getReadStream(BoxedHandle handle, BoxedHandleTypeTag expectedTag) const;
    VulkanMemReadingStream* getOrCreateReadStream(BoxedHandle handle,
                                                  BoxedHandleTypeTag expectedTag,
                                                  const gfxstream::host::FeatureSet& features);

    // Erases the (|unboxed|, |tag|) -> |boxed| reverse mapping, but only if it is still owned by
    // |boxed|.
    void eraseReverseMappingLocked(UnboxedHandle unboxed, BoxedHandleTypeTag tag, BoxedHandle boxed)
        REQUIRES(mMutex);

    struct ReverseMapKey {
        UnboxedHandle unboxed;
        BoxedHandleTypeTag tag;
        bool operator==(const ReverseMapKey& other) const noexcept {
            return unboxed == other.unboxed && tag == other.tag;
        }
    };
    struct ReverseMapKeyHash {
        size_t operator()(const ReverseMapKey& key) const noexcept {
            const uint64_t raw = toRaw(key.unboxed);
            return std::hash<uint64_t>{}(raw) ^
                   (std::hash<uint32_t>{}(static_cast<uint32_t>(key.tag)) + 0x9e3779b9u +
                    (raw << 6) + (raw >> 2));
        }
    };

    Store mStore;

    mutable std::mutex mMutex;
    std::unordered_map<ReverseMapKey, BoxedHandle, ReverseMapKeyHash> mReverseMap
        GUARDED_BY(mMutex);

    struct DelayedRemove {
        BoxedHandle handle;
        BoxedHandleTypeTag expectedTag;
        std::function<void()> callback;
    };
    std::unordered_map<VkDevice, std::vector<DelayedRemove>> mDelayedRemoves GUARDED_BY(mMutex);

    // If true, `add()` will use and consume the handles in `mHandleReplayQueue`.
    // This is useful for snapshot loading when a explicit set of handles should
    // be used when replaying commands.
    bool mHandleReplay = false;
    std::deque<BoxedHandle> mHandleReplayQueue;
};

extern BoxedHandleManager sBoxedHandleManager;

#define DEFINE_BOXED_DISPATCHABLE_HANDLE_API_DECL(type)                                       \
    Boxed<type> new_boxed_##type(type underlying, VulkanDispatch* dispatch);                  \
    void delete_##type(Boxed<type> boxed);                                                    \
    type unbox_##type(Boxed<type> boxed);                                                     \
    type try_unbox_##type(Boxed<type> boxed);                                                 \
    Boxed<type> unboxed_to_boxed_##type(type unboxed);                                        \
    VulkanDispatch* dispatch_##type(Boxed<type> boxed);                                       \
    std::shared_ptr<OrderMaintenanceInfo> ordmaint_##type(Boxed<type> boxed);                 \
    VulkanMemReadingStream* readstream_##type(Boxed<type> boxed);                             \
    inline Boxed<type> new_boxed(type underlying, VulkanDispatch* dispatch) {                 \
        return new_boxed_##type(underlying, dispatch);                                        \
    }                                                                                         \
    inline void delete_boxed(Boxed<type> boxed) { delete_##type(boxed); }                     \
    inline type unbox(Boxed<type> boxed) { return unbox_##type(boxed); }                      \
    inline type try_unbox(Boxed<type> boxed) { return try_unbox_##type(boxed); }              \
    inline Boxed<type> unboxed_to_boxed(type unboxed) {                                       \
        return unboxed_to_boxed_##type(unboxed);                                              \
    }                                                                                         \
    inline VulkanDispatch* dispatch(Boxed<type> boxed) { return dispatch_##type(boxed); }     \
    inline std::shared_ptr<OrderMaintenanceInfo> ordmaint(Boxed<type> boxed) {                \
        return ordmaint_##type(boxed);                                                        \
    }                                                                                         \
    inline VulkanMemReadingStream* readstream(Boxed<type> boxed) {                            \
        return readstream_##type(boxed);                                                      \
    }

#define DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_DECL(type)                                         \
    Boxed<type> new_boxed_non_dispatchable_##type(type underlying);                                 \
    void delete_##type(Boxed<type> boxed);                                                          \
    void delayed_delete_##type(Boxed<type> boxed, VkDevice device, std::function<void()> callback); \
    type unbox_##type(Boxed<type> boxed);                                                           \
    type try_unbox_##type(Boxed<type> boxed);                                                       \
    Boxed<type> unboxed_to_boxed_non_dispatchable_##type(type unboxed);                             \
    void set_boxed_non_dispatchable_##type(Boxed<type> boxed, type underlying);                     \
    inline Boxed<type> new_boxed(type underlying) {                                                 \
        return new_boxed_non_dispatchable_##type(underlying);                                       \
    }                                                                                               \
    inline void delete_boxed(Boxed<type> boxed) { delete_##type(boxed); }                           \
    inline void delayed_delete_boxed(Boxed<type> boxed, VkDevice device,                            \
                                     std::function<void()> callback) {                              \
        delayed_delete_##type(boxed, device, std::move(callback));                                  \
    }                                                                                               \
    inline type unbox(Boxed<type> boxed) { return unbox_##type(boxed); }                            \
    inline type try_unbox(Boxed<type> boxed) { return try_unbox_##type(boxed); }                    \
    inline Boxed<type> unboxed_to_boxed(type unboxed) {                                             \
        return unboxed_to_boxed_non_dispatchable_##type(unboxed);                                   \
    }                                                                                               \
    inline void set_boxed(Boxed<type> boxed, type underlying) {                                     \
        set_boxed_non_dispatchable_##type(boxed, underlying);                                       \
    }

GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(DEFINE_BOXED_DISPATCHABLE_HANDLE_API_DECL)
GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_DECL)

#undef DEFINE_BOXED_DISPATCHABLE_HANDLE_API_DECL
#undef DEFINE_BOXED_NON_DISPATCHABLE_HANDLE_API_DECL
#undef DEFINE_BOXED_HANDLE_TYPE_TAG

}  // namespace vk
}  // namespace host
}  // namespace gfxstream

namespace std {
template <typename VkT>
struct hash<gfxstream::host::vk::Boxed<VkT>> {
    size_t operator()(gfxstream::host::vk::Boxed<VkT> boxed) const noexcept {
        return std::hash<VkT>{}(boxed.value());
    }
};
}  // namespace std
