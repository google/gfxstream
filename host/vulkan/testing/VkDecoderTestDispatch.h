// copyright (c) 2022 the android open source project
//
// licensed under the apache license, version 2.0 (the "license");
// you may not use this file except in compliance with the license.
// you may obtain a copy of the license at
//
// http://www.apache.org/licenses/license-2.0
//
// unless required by applicable law or agreed to in writing, software
// distributed under the license is distributed on an "as is" basis,
// without warranties or conditions of any kind, either express or implied.
// see the license for the specific language governing permissions and
// limitations under the license.

#pragma once

#include "goldfish_vk_dispatch.h"
#include "vulkan/VkDecoderGlobalState.h"
#include "vulkan/vulkan.h"
#include "VulkanBoxedHandles.h"

namespace gfxstream {
namespace vk {
namespace testing {

// TODO(gregschlom): This class should be auto-generated
class VkDecoderTestDispatch {
   public:
    VkDecoderTestDispatch(VulkanDispatch* vk, gfxstream::base::BumpPool* bp,
                          VkDecoderContext* decoderContext)
        : mVk(vk), mDgs(VkDecoderGlobalState::get()), mBp(bp), mDecoderContext(decoderContext) {}

    // Vulkan API wrappers - please keep sorted alphabetically
    //
    // These methods call into VkDecoderGlobalState (or VulkanDispatch if the method isn't
    // implemented in VkDecoderGlobalState), while taking care of unboxing the parameters as
    // needed.

    VkResult vkAllocateCommandBuffers(VkDevice device,
                                      const VkCommandBufferAllocateInfo* pAllocateInfo,
                                      VkCommandBuffer* pCommandBuffers) {
        return mDgs->on_vkAllocateCommandBuffers(mBp, nullptr, device, pAllocateInfo,
                                                 pCommandBuffers);
    }

    VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                              const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
        return mDgs->on_vkAllocateMemory(mBp, nullptr, device, pAllocateInfo, pAllocator, pMemory);
    }

    VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                  const VkCommandBufferBeginInfo* pBeginInfo) {
        return mDgs->on_vkBeginCommandBuffer(mBp, nullptr, commandBuffer, pBeginInfo,
                                             *mDecoderContext);
    }

    VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                VkDeviceSize memoryOffset) {
        return mDgs->on_vkBindBufferMemory(mBp, nullptr, device, unbox_VkBuffer(buffer),
                                           unbox_VkDeviceMemory(memory), memoryOffset);
    }

    VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                               VkDeviceSize memoryOffset) {
        return mDgs->on_vkBindImageMemory(mBp, nullptr, device, unbox_VkImage(image),
                                          unbox_VkDeviceMemory(memory), memoryOffset);
    }

    void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                        VkImageLayout srcImageLayout, VkImage dstImage,
                        VkImageLayout dstImageLayout, uint32_t regionCount,
                        const VkImageBlit* pRegions, VkFilter filter) {
        return mVk->vkCmdBlitImage(unbox_VkCommandBuffer(commandBuffer), unbox_VkImage(srcImage),
                                   srcImageLayout, unbox_VkImage(dstImage), dstImageLayout,
                                   regionCount, pRegions, filter);
    }
    void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                VkImageLayout dstImageLayout, uint32_t regionCount,
                                const VkBufferImageCopy* pRegions) {
        mDgs->on_vkCmdCopyBufferToImage(mBp, nullptr, commandBuffer, unbox_VkBuffer(srcBuffer),
                                        unbox_VkImage(dstImage), dstImageLayout, regionCount,
                                        pRegions, *mDecoderContext);
    }

    void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                        VkImageLayout srcImageLayout, VkImage dstImage,
                        VkImageLayout dstImageLayout, uint32_t regionCount,
                        const VkImageCopy* pRegions) {
        mDgs->on_vkCmdCopyImage(mBp, nullptr, commandBuffer, unbox_VkImage(srcImage),
                                srcImageLayout, unbox_VkImage(dstImage), dstImageLayout,
                                regionCount, pRegions);
    }

    void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                              uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                              uint32_t bufferMemoryBarrierCount,
                              const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                              uint32_t imageMemoryBarrierCount,
                              const VkImageMemoryBarrier* pImageMemoryBarriers) {
        mDgs->on_vkCmdPipelineBarrier(mBp, nullptr, commandBuffer, srcStageMask, dstStageMask,
                                      dependencyFlags, memoryBarrierCount, pMemoryBarriers,
                                      bufferMemoryBarrierCount, pBufferMemoryBarriers,
                                      imageMemoryBarrierCount, pImageMemoryBarriers);
    }

    void vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                               const VkDependencyInfo* pDependencyInfo) {
        mDgs->on_vkCmdPipelineBarrier2(mBp, nullptr, commandBuffer, pDependencyInfo);
    }

    void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                                uint32_t regionCount, const VkBufferImageCopy* pRegions) {
        mDgs->on_vkCmdCopyImageToBuffer(mBp, nullptr, commandBuffer, unbox_VkImage(srcImage),
                                        srcImageLayout, unbox_VkBuffer(dstBuffer), regionCount,
                                        pRegions);
    }

    VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
        return mDgs->on_vkCreateBuffer(mBp, nullptr, device, pCreateInfo, pAllocator, pBuffer);
    }

    VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkCommandPool* pCommandPool) {
        return mDgs->on_vkCreateCommandPool(mBp, nullptr, device, pCreateInfo, pAllocator,
                                            pCommandPool);
    }

    VkResult vkCreateDebugUtilsMessengerEXT(VkInstance instance,
                                            const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDebugUtilsMessengerEXT* pDebugMessenger) {
        instance = unbox_VkInstance(instance);
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)mVk->vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
        return mDgs->on_vkCreateDevice(mBp, nullptr, physicalDevice, pCreateInfo, pAllocator,
                                       pDevice);
    }

    VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
                           const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
        mDgs->transformImpl_VkImageCreateInfo_tohost(pCreateInfo, 1);
        return mDgs->on_vkCreateImage(mBp, nullptr, device, pCreateInfo, pAllocator, pImage);
    }

    VkResult vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
        return mDgs->on_vkCreateImageView(mBp, nullptr, device, pCreateInfo, pAllocator, pView);
    }

    VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
        return mDgs->on_vkCreateInstance(mBp, nullptr, pCreateInfo, pAllocator, pInstance);
    }

    void vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                         const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyBuffer(mBp, nullptr, device, unbox_VkBuffer(buffer), pAllocator);
        delete_VkBuffer(buffer);
    }

    void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                              const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyCommandPool(mBp, nullptr, device, unbox_VkCommandPool(commandPool),
                                      pAllocator);
        delete_VkCommandPool(commandPool);
    }

    void vkDestroyDebugUtilsMessengerEXT(VkInstance instance,
                                         VkDebugUtilsMessengerEXT debugMessenger,
                                         const VkAllocationCallbacks* pAllocator) {
        instance = unbox_VkInstance(instance);
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)mVk->vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyDevice(mBp, nullptr, device, pAllocator);
    }

    void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyImage(mBp, nullptr, device, unbox_VkImage(image), pAllocator);
        delete_VkImage(image);
    }

    void vkDestroyImageView(VkDevice device, VkImageView imageView,
                            const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyImageView(mBp, nullptr, device, unbox_VkImageView(imageView), pAllocator);
        delete_VkImageView(imageView);
    }

    void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyInstance(mBp, nullptr, instance, pAllocator);
    }

    VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
        return mDgs->on_vkEndCommandBuffer(mBp, nullptr, commandBuffer, *mDecoderContext);
    }

    VkResult vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                VkLayerProperties* pProperties) {
        return mVk->vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* physicalDeviceCount,
                                        VkPhysicalDevice* physicalDevices) {
        return mDgs->on_vkEnumeratePhysicalDevices(mBp, nullptr, instance, physicalDeviceCount,
                                                   physicalDevices);
    }

    void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                              uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
        mDgs->on_vkFreeCommandBuffers(mBp, nullptr, device, unbox_VkCommandPool(commandPool),
                                      commandBufferCount, pCommandBuffers);
        // Calling delete_VkCommandBuffer is normally done in the decoder, so we have to do it here.
        for (int i = 0; i < commandBufferCount; ++i) {
            delete_VkCommandBuffer(unboxed_to_boxed_VkCommandBuffer(pCommandBuffers[i]));
        }
    }

    void vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                      const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkFreeMemory(mBp, nullptr, device, unbox_VkDeviceMemory(memory), pAllocator);
        delete_VkDeviceMemory(memory);
    }

    void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                                       VkMemoryRequirements* pMemoryRequirements) {
        mVk->vkGetBufferMemoryRequirements(unbox_VkDevice(device), unbox_VkBuffer(buffer),
                                           pMemoryRequirements);
    }

    void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
                          VkQueue* pQueue) {
        mDgs->on_vkGetDeviceQueue(mBp, nullptr, device, queueFamilyIndex, queueIndex, pQueue);
    }

    void vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                                      VkMemoryRequirements* pMemoryRequirements) {
        mDgs->on_vkGetImageMemoryRequirements(mBp, nullptr, device, unbox_VkImage(image),
                                              pMemoryRequirements);
    }

    void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                             VkFormatProperties* pFormatProperties) {
        mDgs->on_vkGetPhysicalDeviceFormatProperties(mBp, nullptr, physicalDevice, format,
                                                     pFormatProperties);
    }
    void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                             VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
        mDgs->on_vkGetPhysicalDeviceMemoryProperties(mBp, nullptr, physicalDevice,
                                                     pMemoryProperties);
    }

    VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                           VkFence fence) {
        return mDgs->on_vkQueueSubmit(mBp, nullptr, queue, submitCount, pSubmits, fence);
    }

    VkResult vkQueueWaitIdle(VkQueue queue) {
        return mDgs->on_vkQueueWaitIdle(mBp, nullptr, queue);
    }

    VkResult vkDeviceWaitIdle(VkDevice device) {
        return mVk->vkDeviceWaitIdle(unbox_VkDevice(device));
    }

    void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                                  uint32_t* pQueueFamilyPropertyCount,
                                                  VkQueueFamilyProperties* pQueueFamilyProperties) {
        mVk->vkGetPhysicalDeviceQueueFamilyProperties(unbox_VkPhysicalDevice(physicalDevice),
                                                      pQueueFamilyPropertyCount,
                                                      pQueueFamilyProperties);
    }

    VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                         VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
        return mDgs->on_vkMapMemory(mBp, nullptr, device, unbox_VkDeviceMemory(memory), offset,
                                    size, flags, ppData);
    }

    void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
        mDgs->on_vkUnmapMemory(mBp, nullptr, device, unbox_VkDeviceMemory(memory));
    }

   private:
    VulkanDispatch* mVk;
    VkDecoderGlobalState* mDgs;
    gfxstream::base::BumpPool* mBp;
    VkDecoderContext* mDecoderContext;
};

}  // namespace testing
}  // namespace vk
}  // namespace gfxstream
