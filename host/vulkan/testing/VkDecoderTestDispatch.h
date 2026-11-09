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

#include <vulkan/vulkan.h>

#include "goldfish_vk_dispatch.h"
#include "vulkan/vk_decoder_global_state.h"
#include "vulkan_boxed_handles.h"

namespace gfxstream {
namespace host {
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
        return mDgs->on_vkAllocateCommandBuffers(mBp, kInvalidSnapshotApiCallHandle, device,
                                                 pAllocateInfo, pCommandBuffers);
    }

    VkResult vkAllocateDescriptorSets(VkDevice device,
                                      const VkDescriptorSetAllocateInfo* pAllocateInfo,
                                      VkDescriptorSet* pDescriptorSets) {
        return mDgs->on_vkAllocateDescriptorSets(mBp, kInvalidSnapshotApiCallHandle, device,
                                                 pAllocateInfo, pDescriptorSets);
    }

    VkResult vkAllocateMemory(VkDevice device, const VkMemoryAllocateInfo* pAllocateInfo,
                              const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory) {
        return mDgs->on_vkAllocateMemory(mBp, kInvalidSnapshotApiCallHandle, device, pAllocateInfo,
                                         pAllocator, pMemory);
    }

    VkResult vkBeginCommandBuffer(VkCommandBuffer commandBuffer,
                                  const VkCommandBufferBeginInfo* pBeginInfo) {
        return mDgs->on_vkBeginCommandBuffer(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                             pBeginInfo, *mDecoderContext);
    }

    VkResult vkBindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory,
                                VkDeviceSize memoryOffset) {
        return mDgs->on_vkBindBufferMemory(
            mBp, kInvalidSnapshotApiCallHandle, device, unbox_VkBuffer(Boxed<VkBuffer>(buffer)),
            unbox_VkDeviceMemory(Boxed<VkDeviceMemory>(memory)), memoryOffset);
    }

    VkResult vkBindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory,
                               VkDeviceSize memoryOffset) {
        return mDgs->on_vkBindImageMemory(
            mBp, kInvalidSnapshotApiCallHandle, device, unbox_VkImage(Boxed<VkImage>(image)),
            unbox_VkDeviceMemory(Boxed<VkDeviceMemory>(memory)), memoryOffset);
    }

    void vkCmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                                 VkPipelineBindPoint pipelineBindPoint, VkPipelineLayout layout,
                                 uint32_t firstSet, uint32_t descriptorSetCount,
                                 const VkDescriptorSet* pDescriptorSets,
                                 uint32_t dynamicOffsetCount, const uint32_t* pDynamicOffsets) {
        std::vector<VkDescriptorSet> unboxedSets(descriptorSetCount);
        for (uint32_t i = 0; i < descriptorSetCount; ++i) {
            unboxedSets[i] =
                pDescriptorSets[i]
                    ? unbox_VkDescriptorSet(Boxed<VkDescriptorSet>(pDescriptorSets[i]))
                    : VK_NULL_HANDLE;
        }
        mDgs->on_vkCmdBindDescriptorSets(
            mBp, kInvalidSnapshotApiCallHandle, commandBuffer, pipelineBindPoint,
            unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(layout)), firstSet, descriptorSetCount,
            unboxedSets.data(), dynamicOffsetCount, pDynamicOffsets);
    }

    void vkCmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint,
                           VkPipeline pipeline) {
        mDgs->on_vkCmdBindPipeline(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                   pipelineBindPoint, unbox_VkPipeline(Boxed<VkPipeline>(pipeline)));
    }

    void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                        VkImageLayout srcImageLayout, VkImage dstImage,
                        VkImageLayout dstImageLayout, uint32_t regionCount,
                        const VkImageBlit* pRegions, VkFilter filter) {
        return mVk->vkCmdBlitImage(unbox_VkCommandBuffer(Boxed<VkCommandBuffer>(commandBuffer)),
                                   unbox_VkImage(Boxed<VkImage>(srcImage)), srcImageLayout,
                                   unbox_VkImage(Boxed<VkImage>(dstImage)), dstImageLayout,
                                   regionCount, pRegions, filter);
    }
    void vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage,
                                VkImageLayout dstImageLayout, uint32_t regionCount,
                                const VkBufferImageCopy* pRegions) {
        mDgs->on_vkCmdCopyBufferToImage(
            mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
            unbox_VkBuffer(Boxed<VkBuffer>(srcBuffer)), unbox_VkImage(Boxed<VkImage>(dstImage)),
            dstImageLayout, regionCount, pRegions, *mDecoderContext);
    }

    void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage,
                        VkImageLayout srcImageLayout, VkImage dstImage,
                        VkImageLayout dstImageLayout, uint32_t regionCount,
                        const VkImageCopy* pRegions) {
        mDgs->on_vkCmdCopyImage(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                unbox_VkImage(Boxed<VkImage>(srcImage)), srcImageLayout,
                                unbox_VkImage(Boxed<VkImage>(dstImage)), dstImageLayout,
                                regionCount, pRegions);
    }

    void vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY,
                       uint32_t groupCountZ) {
        mVk->vkCmdDispatch(unbox_VkCommandBuffer(Boxed<VkCommandBuffer>(commandBuffer)),
                           groupCountX, groupCountY, groupCountZ);
    }

    void vkCmdPipelineBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags srcStageMask,
                              VkPipelineStageFlags dstStageMask, VkDependencyFlags dependencyFlags,
                              uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                              uint32_t bufferMemoryBarrierCount,
                              const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                              uint32_t imageMemoryBarrierCount,
                              const VkImageMemoryBarrier* pImageMemoryBarriers) {
        mDgs->on_vkCmdPipelineBarrier(
            mBp, kInvalidSnapshotApiCallHandle, commandBuffer, srcStageMask, dstStageMask,
            dependencyFlags, memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
            pBufferMemoryBarriers, imageMemoryBarrierCount, pImageMemoryBarriers);
    }

    void vkCmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                               const VkDependencyInfo* pDependencyInfo) {
        mDgs->on_vkCmdPipelineBarrier2(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                       pDependencyInfo);
    }

    void vkCmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout,
                            VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size,
                            const void* pValues) {
        mVk->vkCmdPushConstants(unbox_VkCommandBuffer(Boxed<VkCommandBuffer>(commandBuffer)),
                                unbox_VkPipelineLayout(Boxed<VkPipelineLayout>(layout)), stageFlags,
                                offset, size, pValues);
    }

    void vkCmdSetEvent(VkCommandBuffer commandBuffer, VkEvent event,
                       VkPipelineStageFlags stageMask) {
        mDgs->on_vkCmdSetEvent(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                               unbox_VkEvent(Boxed<VkEvent>(event)), stageMask);
    }

    void vkCmdWaitEvents(VkCommandBuffer commandBuffer, uint32_t eventCount, const VkEvent* pEvents,
                         VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                         uint32_t memoryBarrierCount, const VkMemoryBarrier* pMemoryBarriers,
                         uint32_t bufferMemoryBarrierCount,
                         const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                         uint32_t imageMemoryBarrierCount,
                         const VkImageMemoryBarrier* pImageMemoryBarriers) {
        std::vector<VkEvent> unboxedEvents(eventCount);
        for (uint32_t i = 0; i < eventCount; ++i) {
            unboxedEvents[i] =
                pEvents[i] ? unbox_VkEvent(Boxed<VkEvent>(pEvents[i])) : VK_NULL_HANDLE;
        }
        mDgs->on_vkCmdWaitEvents(mBp, kInvalidSnapshotApiCallHandle, commandBuffer, eventCount,
                                 unboxedEvents.data(), srcStageMask, dstStageMask,
                                 memoryBarrierCount, pMemoryBarriers, bufferMemoryBarrierCount,
                                 pBufferMemoryBarriers, imageMemoryBarrierCount,
                                 pImageMemoryBarriers);
    }

    void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage,
                                VkImageLayout srcImageLayout, VkBuffer dstBuffer,
                                uint32_t regionCount, const VkBufferImageCopy* pRegions) {
        mDgs->on_vkCmdCopyImageToBuffer(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                        unbox_VkImage(Boxed<VkImage>(srcImage)), srcImageLayout,
                                        unbox_VkBuffer(Boxed<VkBuffer>(dstBuffer)), regionCount,
                                        pRegions);
    }

    VkResult vkCreateBuffer(VkDevice device, const VkBufferCreateInfo* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer) {
        return mDgs->on_vkCreateBuffer(mBp, kInvalidSnapshotApiCallHandle, device, pCreateInfo,
                                       pAllocator, pBuffer);
    }

    VkResult vkCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkCommandPool* pCommandPool) {
        return mDgs->on_vkCreateCommandPool(mBp, kInvalidSnapshotApiCallHandle, device, pCreateInfo,
                                            pAllocator, pCommandPool);
    }

    VkResult vkCreateComputePipelines(VkDevice device, VkPipelineCache pipelineCache,
                                      uint32_t createInfoCount,
                                      const VkComputePipelineCreateInfo* pCreateInfos,
                                      const VkAllocationCallbacks* pAllocator,
                                      VkPipeline* pPipelines) {
        return mDgs->on_vkCreateComputePipelines(
            mBp, kInvalidSnapshotApiCallHandle, device,
            pipelineCache ? unbox_VkPipelineCache(Boxed<VkPipelineCache>(pipelineCache))
                          : VK_NULL_HANDLE,
            createInfoCount, pCreateInfos, pAllocator, pPipelines);
    }

    VkResult vkCreateDebugUtilsMessengerEXT(VkInstance instance,
                                            const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkDebugUtilsMessengerEXT* pDebugMessenger) {
        instance = unbox_VkInstance(Boxed<VkInstance>(instance));
        auto func = (PFN_vkCreateDebugUtilsMessengerEXT)mVk->vkGetInstanceProcAddr(
            instance, "vkCreateDebugUtilsMessengerEXT");
        if (func != nullptr) {
            return func(instance, pCreateInfo, pAllocator, pDebugMessenger);
        } else {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }

    VkResult vkCreateDescriptorPool(VkDevice device, const VkDescriptorPoolCreateInfo* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkDescriptorPool* pDescriptorPool) {
        return mDgs->on_vkCreateDescriptorPool(mBp, kInvalidSnapshotApiCallHandle, device,
                                               pCreateInfo, pAllocator, pDescriptorPool);
    }

    VkResult vkCreateDescriptorSetLayout(VkDevice device,
                                         const VkDescriptorSetLayoutCreateInfo* pCreateInfo,
                                         const VkAllocationCallbacks* pAllocator,
                                         VkDescriptorSetLayout* pSetLayout) {
        return mDgs->on_vkCreateDescriptorSetLayout(mBp, kInvalidSnapshotApiCallHandle, device,
                                                    pCreateInfo, pAllocator, pSetLayout);
    }

    VkResult vkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                            const VkAllocationCallbacks* pAllocator, VkDevice* pDevice) {
        return mDgs->on_vkCreateDevice(mBp, kInvalidSnapshotApiCallHandle, physicalDevice,
                                       pCreateInfo, pAllocator, pDevice);
    }

    VkResult vkCreateEvent(VkDevice device, const VkEventCreateInfo* pCreateInfo,
                           const VkAllocationCallbacks* pAllocator, VkEvent* pEvent) {
        return mDgs->on_vkCreateEvent(mBp, kInvalidSnapshotApiCallHandle, device, pCreateInfo,
                                      pAllocator, pEvent);
    }

    VkResult vkCreateImage(VkDevice device, const VkImageCreateInfo* pCreateInfo,
                           const VkAllocationCallbacks* pAllocator, VkImage* pImage) {
        mDgs->transformImpl_VkImageCreateInfo_tohost(pCreateInfo, 1);
        return mDgs->on_vkCreateImage(mBp, kInvalidSnapshotApiCallHandle, device, pCreateInfo,
                                      pAllocator, pImage);
    }

    VkResult vkCreateImageView(VkDevice device, const VkImageViewCreateInfo* pCreateInfo,
                               const VkAllocationCallbacks* pAllocator, VkImageView* pView) {
        return mDgs->on_vkCreateImageView(mBp, kInvalidSnapshotApiCallHandle, device, pCreateInfo,
                                          pAllocator, pView);
    }

    VkResult vkCreateInstance(const VkInstanceCreateInfo* pCreateInfo,
                              const VkAllocationCallbacks* pAllocator, VkInstance* pInstance) {
        return mDgs->on_vkCreateInstance(mBp, kInvalidSnapshotApiCallHandle, pCreateInfo,
                                         pAllocator, pInstance);
    }

    VkResult vkCreatePipelineLayout(VkDevice device, const VkPipelineLayoutCreateInfo* pCreateInfo,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkPipelineLayout* pPipelineLayout) {
        return mDgs->on_vkCreatePipelineLayout(mBp, kInvalidSnapshotApiCallHandle, device,
                                               pCreateInfo, pAllocator, pPipelineLayout);
    }

    VkResult vkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator,
                                  VkShaderModule* pShaderModule) {
        return mDgs->on_vkCreateShaderModule(mBp, kInvalidSnapshotApiCallHandle, device,
                                             pCreateInfo, pAllocator, pShaderModule);
    }

    void vkDestroyBuffer(VkDevice device, VkBuffer buffer,
                         const VkAllocationCallbacks* pAllocator) {
        Boxed<VkBuffer> boxedBuffer(buffer);
        mDgs->on_vkDestroyBuffer(mBp, kInvalidSnapshotApiCallHandle, device,
                                 unbox_VkBuffer(boxedBuffer), pAllocator);
        delete_VkBuffer(boxedBuffer);
    }

    void vkDestroyCommandPool(VkDevice device, VkCommandPool commandPool,
                              const VkAllocationCallbacks* pAllocator) {
        Boxed<VkCommandPool> boxedCommandPool(commandPool);
        mDgs->on_vkDestroyCommandPool(mBp, kInvalidSnapshotApiCallHandle, device,
                                      unbox_VkCommandPool(boxedCommandPool), pAllocator);
        delete_VkCommandPool(boxedCommandPool);
    }

    void vkDestroyDebugUtilsMessengerEXT(VkInstance instance,
                                         VkDebugUtilsMessengerEXT debugMessenger,
                                         const VkAllocationCallbacks* pAllocator) {
        instance = unbox_VkInstance(Boxed<VkInstance>(instance));
        auto func = (PFN_vkDestroyDebugUtilsMessengerEXT)mVk->vkGetInstanceProcAddr(
            instance, "vkDestroyDebugUtilsMessengerEXT");
        if (func != nullptr) {
            func(instance, debugMessenger, pAllocator);
        }
    }

    void vkDestroyDescriptorPool(VkDevice device, VkDescriptorPool descriptorPool,
                                 const VkAllocationCallbacks* pAllocator) {
        Boxed<VkDescriptorPool> boxedDescriptorPool(descriptorPool);
        mDgs->on_vkDestroyDescriptorPool(mBp, kInvalidSnapshotApiCallHandle, device,
                                         unbox_VkDescriptorPool(boxedDescriptorPool), pAllocator);
        delete_VkDescriptorPool(boxedDescriptorPool);
    }

    void vkDestroyDescriptorSetLayout(VkDevice device, VkDescriptorSetLayout descriptorSetLayout,
                                      const VkAllocationCallbacks* pAllocator) {
        Boxed<VkDescriptorSetLayout> boxedDescriptorSetLayout(descriptorSetLayout);
        mDgs->on_vkDestroyDescriptorSetLayout(mBp, kInvalidSnapshotApiCallHandle, device,
                                              unbox_VkDescriptorSetLayout(boxedDescriptorSetLayout),
                                              pAllocator);
        delete_VkDescriptorSetLayout(boxedDescriptorSetLayout);
    }

    void vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyDevice(mBp, kInvalidSnapshotApiCallHandle, device, pAllocator);
    }

    void vkDestroyEvent(VkDevice device, VkEvent event, const VkAllocationCallbacks* pAllocator) {
        Boxed<VkEvent> boxedEvent(event);
        mDgs->on_vkDestroyEvent(mBp, kInvalidSnapshotApiCallHandle, device,
                                unbox_VkEvent(boxedEvent), pAllocator);
        delete_VkEvent(boxedEvent);
    }

    void vkDestroyImage(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator) {
        Boxed<VkImage> boxedImage(image);
        mDgs->on_vkDestroyImage(mBp, kInvalidSnapshotApiCallHandle, device,
                                unbox_VkImage(boxedImage), pAllocator);
        delete_VkImage(boxedImage);
    }

    void vkDestroyImageView(VkDevice device, VkImageView imageView,
                            const VkAllocationCallbacks* pAllocator) {
        Boxed<VkImageView> boxedImageView(imageView);
        mDgs->on_vkDestroyImageView(mBp, kInvalidSnapshotApiCallHandle, device,
                                    unbox_VkImageView(boxedImageView), pAllocator);
        delete_VkImageView(boxedImageView);
    }

    void vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator) {
        mDgs->on_vkDestroyInstance(mBp, kInvalidSnapshotApiCallHandle, instance, pAllocator);
    }

    void vkDestroyPipeline(VkDevice device, VkPipeline pipeline,
                           const VkAllocationCallbacks* pAllocator) {
        Boxed<VkPipeline> boxedPipeline(pipeline);
        mDgs->on_vkDestroyPipeline(mBp, kInvalidSnapshotApiCallHandle, device,
                                   unbox_VkPipeline(boxedPipeline), pAllocator);
        delete_VkPipeline(boxedPipeline);
    }

    void vkDestroyPipelineLayout(VkDevice device, VkPipelineLayout pipelineLayout,
                                 const VkAllocationCallbacks* pAllocator) {
        Boxed<VkPipelineLayout> boxedPipelineLayout(pipelineLayout);
        mDgs->on_vkDestroyPipelineLayout(mBp, kInvalidSnapshotApiCallHandle, device,
                                         unbox_VkPipelineLayout(boxedPipelineLayout), pAllocator);
        delete_VkPipelineLayout(boxedPipelineLayout);
    }

    void vkDestroyShaderModule(VkDevice device, VkShaderModule shaderModule,
                               const VkAllocationCallbacks* pAllocator) {
        Boxed<VkShaderModule> boxedShaderModule(shaderModule);
        mDgs->on_vkDestroyShaderModule(mBp, kInvalidSnapshotApiCallHandle, device,
                                       unbox_VkShaderModule(boxedShaderModule), pAllocator);
        delete_VkShaderModule(boxedShaderModule);
    }

    VkResult vkEndCommandBuffer(VkCommandBuffer commandBuffer) {
        return mDgs->on_vkEndCommandBuffer(mBp, kInvalidSnapshotApiCallHandle, commandBuffer,
                                           *mDecoderContext);
    }

    VkResult vkEnumerateInstanceLayerProperties(uint32_t* pPropertyCount,
                                                VkLayerProperties* pProperties) {
        return mVk->vkEnumerateInstanceLayerProperties(pPropertyCount, pProperties);
    }

    VkResult vkEnumeratePhysicalDevices(VkInstance instance, uint32_t* physicalDeviceCount,
                                        VkPhysicalDevice* physicalDevices) {
        return mDgs->on_vkEnumeratePhysicalDevices(mBp, kInvalidSnapshotApiCallHandle, instance,
                                                   physicalDeviceCount, physicalDevices);
    }

    void vkFreeCommandBuffers(VkDevice device, VkCommandPool commandPool,
                              uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
        mDgs->on_vkFreeCommandBuffers(mBp, kInvalidSnapshotApiCallHandle, device,
                                      unbox_VkCommandPool(Boxed<VkCommandPool>(commandPool)),
                                      commandBufferCount, pCommandBuffers);
        // Calling delete_VkCommandBuffer is normally done in the decoder, so we have to do it here.
        for (uint32_t i = 0; i < commandBufferCount; ++i) {
            delete_VkCommandBuffer(unboxed_to_boxed_VkCommandBuffer(pCommandBuffers[i]));
        }
    }

    void vkFreeMemory(VkDevice device, VkDeviceMemory memory,
                      const VkAllocationCallbacks* pAllocator) {
        Boxed<VkDeviceMemory> boxedMemory(memory);
        mDgs->on_vkFreeMemory(mBp, kInvalidSnapshotApiCallHandle, device,
                              unbox_VkDeviceMemory(boxedMemory), pAllocator);
        delete_VkDeviceMemory(boxedMemory);
    }

    void vkGetBufferMemoryRequirements(VkDevice device, VkBuffer buffer,
                                       VkMemoryRequirements* pMemoryRequirements) {
        mVk->vkGetBufferMemoryRequirements(unbox_VkDevice(Boxed<VkDevice>(device)),
                                           unbox_VkBuffer(Boxed<VkBuffer>(buffer)),
                                           pMemoryRequirements);
    }

    void vkGetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex,
                          VkQueue* pQueue) {
        mDgs->on_vkGetDeviceQueue(mBp, kInvalidSnapshotApiCallHandle, device, queueFamilyIndex,
                                  queueIndex, pQueue);
    }

    void vkGetImageMemoryRequirements(VkDevice device, VkImage image,
                                      VkMemoryRequirements* pMemoryRequirements) {
        mDgs->on_vkGetImageMemoryRequirements(mBp, kInvalidSnapshotApiCallHandle, device,
                                              unbox_VkImage(Boxed<VkImage>(image)),
                                              pMemoryRequirements);
    }

    void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format,
                                             VkFormatProperties* pFormatProperties) {
        mDgs->on_vkGetPhysicalDeviceFormatProperties(mBp, kInvalidSnapshotApiCallHandle,
                                                     physicalDevice, format, pFormatProperties);
    }
    void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice,
                                             VkPhysicalDeviceMemoryProperties* pMemoryProperties) {
        mDgs->on_vkGetPhysicalDeviceMemoryProperties(mBp, kInvalidSnapshotApiCallHandle,
                                                     physicalDevice, pMemoryProperties);
    }

    VkResult vkQueueSubmit(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits,
                           VkFence fence) {
        return mDgs->on_vkQueueSubmit(mBp, kInvalidSnapshotApiCallHandle, queue, submitCount,
                                      pSubmits, fence);
    }

    VkResult vkQueueWaitIdle(VkQueue queue) {
        return mDgs->on_vkQueueWaitIdle(mBp, kInvalidSnapshotApiCallHandle, queue);
    }

    VkResult vkDeviceWaitIdle(VkDevice device) {
        return mVk->vkDeviceWaitIdle(unbox_VkDevice(Boxed<VkDevice>(device)));
    }

    void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice,
                                                  uint32_t* pQueueFamilyPropertyCount,
                                                  VkQueueFamilyProperties* pQueueFamilyProperties) {
        mVk->vkGetPhysicalDeviceQueueFamilyProperties(
            unbox_VkPhysicalDevice(Boxed<VkPhysicalDevice>(physicalDevice)),
            pQueueFamilyPropertyCount, pQueueFamilyProperties);
    }

    VkResult vkMapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset,
                         VkDeviceSize size, VkMemoryMapFlags flags, void** ppData) {
        return mDgs->on_vkMapMemory(mBp, kInvalidSnapshotApiCallHandle, device,
                                    unbox_VkDeviceMemory(Boxed<VkDeviceMemory>(memory)), offset,
                                    size, flags, ppData);
    }

    void vkUnmapMemory(VkDevice device, VkDeviceMemory memory) {
        mDgs->on_vkUnmapMemory(mBp, kInvalidSnapshotApiCallHandle, device,
                               unbox_VkDeviceMemory(Boxed<VkDeviceMemory>(memory)));
    }

    void vkUpdateDescriptorSets(VkDevice device, uint32_t descriptorWriteCount,
                                const VkWriteDescriptorSet* pDescriptorWrites,
                                uint32_t descriptorCopyCount,
                                const VkCopyDescriptorSet* pDescriptorCopies) {
        mDgs->on_vkUpdateDescriptorSets(mBp, kInvalidSnapshotApiCallHandle, device,
                                        descriptorWriteCount, pDescriptorWrites,
                                        descriptorCopyCount, pDescriptorCopies);
    }

   private:
    VulkanDispatch* mVk;
    VkDecoderGlobalState* mDgs;
    gfxstream::base::BumpPool* mBp;
    VkDecoderContext* mDecoderContext;
};

}  // namespace testing
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
