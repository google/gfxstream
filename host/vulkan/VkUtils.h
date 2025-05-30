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

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <vector>

#include "VkDecoderContext.h"
#include "VulkanDispatch.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/common/logging.h"
#include "vk_fn_info.h"
#include "vk_struct_id.h"

namespace gfxstream {
namespace vk {

struct vk_struct_chain_iterator {
    VkBaseOutStructure* value;
};

template <class T, class H>
T* vk_find_struct(H* head) {
    (void)vk_get_vk_struct_id<H>::id;

    constexpr const VkStructureType desired = vk_get_vk_struct_id<T>::id;

    VkBaseOutStructure* vkstruct = reinterpret_cast<VkBaseOutStructure*>(head);
    while (vkstruct != nullptr) {
        if (vkstruct->sType == desired) {
            return reinterpret_cast<T*>(vkstruct);
        }
        vkstruct = vkstruct->pNext;
    }
    return nullptr;
}

template <class T, class H>
const T* vk_find_struct(const H* head) {
    (void)vk_get_vk_struct_id<H>::id;

    constexpr const VkStructureType desired = vk_get_vk_struct_id<T>::id;

    const VkBaseInStructure* vkstruct = reinterpret_cast<const VkBaseInStructure*>(head);
    while (vkstruct != nullptr) {
        if (vkstruct->sType == desired) {
            return reinterpret_cast<const T*>(vkstruct);
        }
        vkstruct = vkstruct->pNext;
    }
    return nullptr;
}

template <class T>
T vk_make_orphan_copy(const T& vk_struct) {
    T copy = vk_struct;
    copy.pNext = NULL;
    return copy;
}

template <class T>
vk_struct_chain_iterator vk_make_chain_iterator(T* vk_struct) {
    (void)vk_get_vk_struct_id<T>::id;
    vk_struct_chain_iterator result = {reinterpret_cast<VkBaseOutStructure*>(vk_struct)};
    return result;
}

template <class T>
void vk_append_struct(vk_struct_chain_iterator* i, T* vk_struct) {
    (void)vk_get_vk_struct_id<T>::id;

    VkBaseOutStructure* p = i->value;
    if (p->pNext) {
        ::abort();
    }

    p->pNext = reinterpret_cast<VkBaseOutStructure*>(vk_struct);
    vk_struct->pNext = NULL;

    *i = vk_make_chain_iterator(vk_struct);
}

// The caller should guarantee that all the pNext structs in the chain starting at nextChain is not
// a const object to avoid unexpected undefined behavior.
template <class T, class U, typename = std::enable_if_t<!std::is_const_v<T> && !std::is_const_v<U>>>
void vk_insert_struct(T& pos, U& nextChain) {
    VkBaseOutStructure* nextChainTail = reinterpret_cast<VkBaseOutStructure*>(&nextChain);
    for (; nextChainTail->pNext; nextChainTail = nextChainTail->pNext) {}

    nextChainTail->pNext = reinterpret_cast<VkBaseOutStructure*>(const_cast<void*>(pos.pNext));
    pos.pNext = &nextChain;
}

template <class S, class T>
void vk_struct_chain_remove(S* unwanted, T* vk_struct) {
    if (!unwanted) return;

    VkBaseOutStructure* current = reinterpret_cast<VkBaseOutStructure*>(vk_struct);
    while (current != nullptr) {
        if (current->pNext == (void*)unwanted) {
            current->pNext = reinterpret_cast<const VkBaseOutStructure*>(unwanted)->pNext;
        }
        current = current->pNext;
    }
}

template <class TypeToFilter, class H>
void vk_struct_chain_filter(H* head) {
    (void)vk_get_vk_struct_id<H>::id;

    auto* curr = reinterpret_cast<VkBaseOutStructure*>(head);
    while (curr != nullptr) {
        if (curr->pNext != nullptr && curr->pNext->sType == vk_get_vk_struct_id<TypeToFilter>::id) {
            curr->pNext = curr->pNext->pNext;
        }
        curr = curr->pNext;
    }
}

#define VK_CHECK(x)                                                                             \
    do {                                                                                        \
        VkResult err = x;                                                                       \
        if (err != VK_SUCCESS) {                                                                \
            if (err == VK_ERROR_DEVICE_LOST) {                                                  \
                vk_util::getVkCheckCallbacks().callIfExists(                                    \
                    &vk_util::VkCheckCallbacks::onVkErrorDeviceLost);                           \
            }                                                                                   \
            if (err == VK_ERROR_OUT_OF_HOST_MEMORY || err == VK_ERROR_OUT_OF_DEVICE_MEMORY ||   \
                err == VK_ERROR_OUT_OF_POOL_MEMORY) {                                           \
                vk_util::getVkCheckCallbacks().callIfExists(                                    \
                    &vk_util::VkCheckCallbacks::onVkErrorOutOfMemory, err, __func__, __LINE__); \
            }                                                                                   \
            const std::string errString = string_VkResult(err);                                 \
            GFXSTREAM_FATAL("VK_CHECK(" #x ") failed with %s", errString.c_str());              \
        }                                                                                       \
    } while (0)

#define VK_CHECK_MEMALLOC(x, allocateInfo)                                                       \
    do {                                                                                         \
        VkResult err = x;                                                                        \
        if (err != VK_SUCCESS) {                                                                 \
            if (err == VK_ERROR_OUT_OF_HOST_MEMORY || err == VK_ERROR_OUT_OF_DEVICE_MEMORY) {    \
                vk_util::getVkCheckCallbacks().callIfExists(                                     \
                    &vk_util::VkCheckCallbacks::onVkErrorOutOfMemoryOnAllocation, err, __func__, \
                    __LINE__, allocateInfo.allocationSize);                                      \
            }                                                                                    \
            const std::string errString = string_VkResult(err);                                  \
            GFXSTREAM_FATAL("VK_CHECK_MEMALLOC(" #x ") failed with %s", errString.c_str());      \
        }                                                                                        \
    } while (0)

typedef void* MTLTextureRef;
typedef void* MTLBufferRef;

namespace vk_util {

inline VkResult waitForVkQueueIdleWithRetry(const VulkanDispatch& vk, VkQueue queue) {
    constexpr uint32_t retryLimit = 5;
    constexpr auto kWaitInterval = std::chrono::milliseconds(4);
    VkResult res = vk.vkQueueWaitIdle(queue);
    for (uint32_t retryTimes = 1; retryTimes < retryLimit && res == VK_TIMEOUT; retryTimes++) {
        GFXSTREAM_INFO(
            "VK_TIMEOUT returned from vkQueueWaitIdle with %" PRIu32 " attempt. Wait for %" PRIu32
            "ms before another attempt.",
            retryTimes,
            static_cast<uint32_t>(kWaitInterval.count()));
        std::this_thread::sleep_for(kWaitInterval);
        res = vk.vkQueueWaitIdle(queue);
    }
    return res;
}

typedef struct {
    std::function<void()> onVkErrorDeviceLost;
    std::function<void(VkResult, const char*, int)> onVkErrorOutOfMemory;
    std::function<void(VkResult, const char*, int, uint64_t)> onVkErrorOutOfMemoryOnAllocation;
} VkCheckCallbacks;

template <class T>
class CallbacksWrapper {
   public:
    CallbacksWrapper(std::unique_ptr<T> callbacks) : mCallbacks(std::move(callbacks)) {}
    // function should be a member function pointer to T.
    template <class U, class... Args>
    void callIfExists(U function, Args&&... args) const {
        if (mCallbacks && (*mCallbacks.*function)) {
            (*mCallbacks.*function)(std::forward<Args>(args)...);
        }
    }

    T* get() const { return mCallbacks.get(); }

   private:
    std::unique_ptr<T> mCallbacks;
};

std::optional<uint32_t> findMemoryType(const VulkanDispatch* ivk, VkPhysicalDevice physicalDevice,
                                       uint32_t typeFilter, VkMemoryPropertyFlags properties);

void setVkCheckCallbacks(std::unique_ptr<VkCheckCallbacks>);
const CallbacksWrapper<VkCheckCallbacks>& getVkCheckCallbacks();

class CrtpBase {};

// Utility class to make chaining inheritance of multiple CRTP classes more
// readable by allowing one to replace
//
//    class MyClass
//        : public vk_util::Crtp1<MyClass,
//                                vk_util::Crtp2<MyClass,
//                                               vk_util::Crtp3<MyClass>>> {};
//
// with
//
//    class MyClass :
//        : public vk_util::MultiCrtp<MyClass,
//                                    vk_util::Crtp1,
//                                    vk_util::Crtp2,
//                                    vk_util::Ctrp3> {};
namespace vk_util_internal {

// For the template "recursion", this is the base case where the list is empty
// and which just inherits from the last type.
template <typename T,  //
          typename U,  //
          template <typename, typename> class... CrtpClasses>
class MultiCrtpChainHelper : public U {};

// For the template "recursion", this is the case where the list is not empty
// and which uses the "current" CRTP class as the "U" type and passes the
// resulting type to the next step in the template "recursion".
template <typename T,                                //
          typename U,                                //
          template <typename, typename> class Crtp,  //
          template <typename, typename> class... Crtps>
class MultiCrtpChainHelper<T, U, Crtp, Crtps...>
    : public MultiCrtpChainHelper<T, Crtp<T, U>, Crtps...> {};

}  // namespace vk_util_internal

template <typename T,  //
          template <typename, typename> class... CrtpClasses>
class MultiCrtp : public vk_util_internal::MultiCrtpChainHelper<T, CrtpBase, CrtpClasses...> {};

template <class T, class U = CrtpBase>
class FindMemoryType : public U {
   protected:
    std::optional<uint32_t> findMemoryType(uint32_t typeFilter,
                                           VkMemoryPropertyFlags properties) const {
        const T& self = static_cast<const T&>(*this);
        return vk_util::findMemoryType(&self.m_vk, self.m_vkPhysicalDevice, typeFilter, properties);
    }
};

template <class T, class U = CrtpBase>
class RunSingleTimeCommand : public U {
   protected:
    void runSingleTimeCommands(VkQueue queue, std::shared_ptr<gfxstream::base::Lock> queueLock,
                               std::function<void(const VkCommandBuffer& commandBuffer)> f) const {
        const T& self = static_cast<const T&>(*this);
        VkCommandBuffer cmdBuff;
        VkCommandBufferAllocateInfo cmdBuffAllocInfo = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = self.m_vkCommandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1};
        VK_CHECK(self.m_vk.vkAllocateCommandBuffers(self.m_vkDevice, &cmdBuffAllocInfo, &cmdBuff));
        VkCommandBufferBeginInfo beginInfo = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                              .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
        VK_CHECK(self.m_vk.vkBeginCommandBuffer(cmdBuff, &beginInfo));
        f(cmdBuff);
        VK_CHECK(self.m_vk.vkEndCommandBuffer(cmdBuff));
        VkSubmitInfo submitInfo = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers = &cmdBuff};
        {
            std::unique_ptr<gfxstream::base::AutoLock> lock = nullptr;
            if (queueLock) {
                lock = std::make_unique<gfxstream::base::AutoLock>(*queueLock);
            }
            VK_CHECK(self.m_vk.vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
            VK_CHECK(self.m_vk.vkQueueWaitIdle(queue));
        }
        self.m_vk.vkFreeCommandBuffers(self.m_vkDevice, self.m_vkCommandPool, 1, &cmdBuff);
    }
};
template <class T, class U = CrtpBase>
class RecordImageLayoutTransformCommands : public U {
   protected:
    void recordImageLayoutTransformCommands(VkCommandBuffer cmdBuff, VkImage image,
                                            VkImageLayout oldLayout,
                                            VkImageLayout newLayout) const {
        const T& self = static_cast<const T&>(*this);
        VkImageMemoryBarrier imageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};
        self.m_vk.vkCmdPipelineBarrier(cmdBuff, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                                       nullptr, 1, &imageBarrier);
    }
};

template <class T>
typename vk_fn_info::GetVkFnInfo<T>::type getVkInstanceProcAddrWithFallback(
    const std::vector<std::function<std::remove_pointer_t<PFN_vkGetInstanceProcAddr>>>&
        vkGetInstanceProcAddrs,
    VkInstance instance) {
    for (const auto& vkGetInstanceProcAddr : vkGetInstanceProcAddrs) {
        if (!vkGetInstanceProcAddr) {
            continue;
        }
        PFN_vkVoidFunction resWithCurrentVkGetInstanceProcAddr = std::apply(
            [&vkGetInstanceProcAddr, instance](auto&&... names) -> PFN_vkVoidFunction {
                for (const char* name : {names...}) {
                    if (PFN_vkVoidFunction resWithCurrentName =
                            vkGetInstanceProcAddr(instance, name)) {
                        return resWithCurrentName;
                    }
                }
                return nullptr;
            },
            vk_fn_info::GetVkFnInfo<T>::names);
        if (resWithCurrentVkGetInstanceProcAddr) {
            return reinterpret_cast<typename vk_fn_info::GetVkFnInfo<T>::type>(
                resWithCurrentVkGetInstanceProcAddr);
        }
    }
    return nullptr;
}

static inline bool vk_descriptor_type_has_image_view(VkDescriptorType type) {
    switch (type) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            return true;
        default:
            return false;
    }
}

}  // namespace vk_util
}  // namespace vk
}  // namespace gfxstream
