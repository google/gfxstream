// Copyright (C) 2018 The Android Open Source Project
// Copyright (C) 2018 Google Inc.
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

#include <vulkan/vulkan.h>

#define GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(f) \
    f(VkInstance)                                     \
    f(VkPhysicalDevice)                               \
    f(VkDevice)                                       \
    f(VkQueue)                                        \
    f(VkCommandBuffer)

// Unboxing can be overridden for some handles, this would mean the actual
// unboxed handle to be used for the GPU can be different than the 'unboxed'
// handle used for tracking its properties (e.g. via mQueueInfo).
// Queues can be virtualized by using made up 'unboxed' handles for tracking,
// whichwill then needs translation when used for the driver operations.
#define GOLDFISH_VK_LIST_DISPATCHABLE_REGULAR_UNBOX_HANDLE_TYPES(f) \
    f(VkInstance)                                                   \
    f(VkPhysicalDevice)                                             \
    f(VkDevice)                                                     \
    f(VkCommandBuffer)

// VkQueues can be virtualized to provide multiple queues when only a single
// queue is supported. Custom unbox will ensure that the unboxed handle can
// be used by the GPU correctly.
#define GOLDFISH_VK_LIST_DISPATCHABLE_CUSTOM_UNBOX_HANDLE_TYPES(f) f(VkQueue)

#define GOLDFISH_VK_LIST_TRIVIAL_NON_DISPATCHABLE_HANDLE_TYPES(f) \
    f(VkBuffer)                                                   \
    f(VkBufferView)                                               \
    f(VkImage)                                                    \
    f(VkImageView)                                                \
    f(VkShaderModule)                                             \
    f(VkDescriptorPool)                                           \
    f(VkDescriptorSetLayout)                                      \
    f(VkDescriptorSet)                                            \
    f(VkSampler)                                                  \
    f(VkPipeline)                                                 \
    f(VkPipelineCache)                                            \
    f(VkPipelineLayout)                                           \
    f(VkRenderPass)                                               \
    f(VkFramebuffer)                                              \
    f(VkCommandPool)                                              \
    f(VkFence)                                                    \
    f(VkSemaphore)                                                \
    f(VkEvent)                                                    \
    f(VkQueryPool)                                                \
    f(VkSamplerYcbcrConversion)                                   \
    f(VkDescriptorUpdateTemplate)                                 \
    f(VkSurfaceKHR)                                               \
    f(VkSwapchainKHR)                                             \
    f(VkDisplayKHR)                                               \
    f(VkDisplayModeKHR)                                           \
    f(VkValidationCacheEXT)                                       \
    f(VkDebugReportCallbackEXT)                                   \
    f(VkDebugUtilsMessengerEXT)                                   \
    f(VkAccelerationStructureNV)                                  \
    f(VkIndirectCommandsLayoutNV)                                 \
    f(VkAccelerationStructureKHR)                                 \
    f(VkCuModuleNVX)                                              \
    f(VkCuFunctionNVX)                                            \
    f(VkPrivateDataSlot)                                          \
    f(VkMicromapEXT)

#define GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(f) \
    f(VkDeviceMemory)                                     \
    GOLDFISH_VK_LIST_TRIVIAL_NON_DISPATCHABLE_HANDLE_TYPES(f)

#define GOLDFISH_VK_LIST_HANDLE_TYPES(f)          \
    GOLDFISH_VK_LIST_DISPATCHABLE_HANDLE_TYPES(f) \
    GOLDFISH_VK_LIST_NON_DISPATCHABLE_HANDLE_TYPES(f)

// Need to sort by snapshot load dependency order
#define GOLDFISH_VK_LIST_HANDLE_TYPES_BY_STAGE(f) \
    f(VkInstance)                                 \
    f(VkPhysicalDevice)                           \
    f(VkDevice)                                   \
    f(VkQueue)                                    \
    f(VkDeviceMemory)                             \
    f(VkBuffer)                                   \
    f(VkImage)                                    \
    f(VkBufferView)                               \
    f(VkImageView)                                \
    f(VkShaderModule)                             \
    f(VkDescriptorSetLayout)                      \
    f(VkDescriptorPool)                           \
    f(VkDescriptorSet)                            \
    f(VkSampler)                                  \
    f(VkSamplerYcbcrConversion)                   \
    f(VkDescriptorUpdateTemplate)                 \
    f(VkRenderPass)                               \
    f(VkFramebuffer)                              \
    f(VkPipelineLayout)                           \
    f(VkPipelineCache)                            \
    f(VkPipeline)                                 \
    f(VkFence)                                    \
    f(VkSemaphore)                                \
    f(VkEvent)                                    \
    f(VkQueryPool)                                \
    f(VkSurfaceKHR)                               \
    f(VkSwapchainKHR)                             \
    f(VkDisplayKHR)                               \
    f(VkDisplayModeKHR)                           \
    f(VkValidationCacheEXT)                       \
    f(VkDebugReportCallbackEXT)                   \
    f(VkDebugUtilsMessengerEXT)                   \
    f(VkCommandPool)                              \
    f(VkCommandBuffer)                            \
    f(VkAccelerationStructureNV)                  \
    f(VkIndirectCommandsLayoutNV)                 \
    f(VkAccelerationStructureKHR)                 \
    f(VkCuModuleNVX)                              \
    f(VkCuFunctionNVX)                            \
    f(VkPrivateDataSlot)                          \
    f(VkMicromapEXT)
