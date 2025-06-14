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
#include <vulkan/vulkan_core.h>

#include "goldfish_vk_private_defs.h"
#include "vk_android_native_buffer_gfxstream.h"
#include "vulkan_gfxstream.h"
#include "vulkan_gfxstream_structure_type.h"

template <class T>
struct vk_get_vk_struct_id;

#define REGISTER_VK_STRUCT_ID(T, ID)              \
    template <>                                   \
    struct vk_get_vk_struct_id<T> {               \
        static constexpr VkStructureType id = ID; \
    };

REGISTER_VK_STRUCT_ID(VkInstanceCreateInfo, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkDebugReportCallbackCreateInfoEXT,
                      VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkDebugUtilsMessengerCreateInfoEXT,
                      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkBufferCreateInfo, VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkImageCreateInfo, VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkImageFormatProperties2, VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);
REGISTER_VK_STRUCT_ID(VkNativeBufferANDROID, VK_STRUCTURE_TYPE_NATIVE_BUFFER_ANDROID);
REGISTER_VK_STRUCT_ID(VkExternalMemoryBufferCreateInfo,
                      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkExternalMemoryImageCreateInfo,
                      VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkMemoryAllocateInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
REGISTER_VK_STRUCT_ID(VkMemoryDedicatedAllocateInfo,
                      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);
REGISTER_VK_STRUCT_ID(VkMemoryDedicatedRequirements,
                      VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
REGISTER_VK_STRUCT_ID(VkExportMemoryAllocateInfo, VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO);
REGISTER_VK_STRUCT_ID(VkMemoryRequirements2, VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2);
REGISTER_VK_STRUCT_ID(VkSemaphoreCreateInfo, VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkExportSemaphoreCreateInfoKHR,
                      VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO_KHR);
REGISTER_VK_STRUCT_ID(VkSamplerYcbcrConversionCreateInfo,
                      VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkImportColorBufferGOOGLE, VK_STRUCTURE_TYPE_IMPORT_COLOR_BUFFER_GOOGLE);
REGISTER_VK_STRUCT_ID(VkImageViewCreateInfo, VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkSamplerCreateInfo, VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkSamplerCustomBorderColorCreateInfoEXT,
                      VK_STRUCTURE_TYPE_SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkSamplerYcbcrConversionInfo,
                      VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO);
REGISTER_VK_STRUCT_ID(VkImportBufferGOOGLE, VK_STRUCTURE_TYPE_IMPORT_BUFFER_GOOGLE);
REGISTER_VK_STRUCT_ID(VkCreateBlobGOOGLE, VK_STRUCTURE_TYPE_CREATE_BLOB_GOOGLE);

#ifdef _WIN32
REGISTER_VK_STRUCT_ID(VkImportMemoryWin32HandleInfoKHR,
                      VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR);
#else
REGISTER_VK_STRUCT_ID(VkImportMemoryFdInfoKHR, VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR);
#endif

REGISTER_VK_STRUCT_ID(VkPhysicalDeviceImageFormatInfo2,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceExternalImageFormatInfo,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);
REGISTER_VK_STRUCT_ID(VkExternalImageFormatProperties,
                      VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceProperties2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceIDProperties,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceDriverProperties,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceDescriptorIndexingFeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT);
REGISTER_VK_STRUCT_ID(VkSemaphoreTypeCreateInfoKHR,
                      VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR)
REGISTER_VK_STRUCT_ID(VkTimelineSemaphoreSubmitInfoKHR,
                      VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR)
REGISTER_VK_STRUCT_ID(VkBindSparseInfo, VK_STRUCTURE_TYPE_BIND_SPARSE_INFO)
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceSamplerYcbcrConversionFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES)
REGISTER_VK_STRUCT_ID(VkDeviceCreateInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO)
REGISTER_VK_STRUCT_ID(VkFenceCreateInfo, VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkExportFenceCreateInfo, VK_STRUCTURE_TYPE_EXPORT_FENCE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkBindImageMemoryInfo, VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO);
REGISTER_VK_STRUCT_ID(VkBindImageMemorySwapchainInfoKHR,
                      VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);
REGISTER_VK_STRUCT_ID(VkMemoryAllocateFlagsInfo, VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO);
REGISTER_VK_STRUCT_ID(VkMemoryOpaqueCaptureAddressAllocateInfo,
                      VK_STRUCTURE_TYPE_MEMORY_OPAQUE_CAPTURE_ADDRESS_ALLOCATE_INFO);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT)
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceProtectedMemoryFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROTECTED_MEMORY_FEATURES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceExternalMemoryHostPropertiesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT);
REGISTER_VK_STRUCT_ID(VkPhysicalDevicePrivateDataFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceInlineUniformBlockFeatures,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_INLINE_UNIFORM_BLOCK_FEATURES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceRobustness2FeaturesEXT,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceTimelineSemaphoreFeaturesKHR,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES);
REGISTER_VK_STRUCT_ID(VkSubmitInfo,
                      VK_STRUCTURE_TYPE_SUBMIT_INFO);
REGISTER_VK_STRUCT_ID(VkSubmitInfo2,
                      VK_STRUCTURE_TYPE_SUBMIT_INFO_2);

#if defined(VK_USE_PLATFORM_SCREEN_QNX)
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_SCREEN_BUFFER_FEATURES_QNX);
REGISTER_VK_STRUCT_ID(VkImportScreenBufferInfoQNX, VK_STRUCTURE_TYPE_IMPORT_SCREEN_BUFFER_INFO_QNX);
#endif

REGISTER_VK_STRUCT_ID(VkImportMemoryHostPointerInfoEXT,
                      VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkDeviceDeviceMemoryReportCreateInfoEXT,
                      VK_STRUCTURE_TYPE_DEVICE_DEVICE_MEMORY_REPORT_CREATE_INFO_EXT);

#ifdef VK_USE_PLATFORM_METAL_EXT
REGISTER_VK_STRUCT_ID(VkImportMetalTextureInfoEXT, VK_STRUCTURE_TYPE_IMPORT_METAL_TEXTURE_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkExportMetalTextureInfoEXT, VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT);

REGISTER_VK_STRUCT_ID(VkImportMetalBufferInfoEXT, VK_STRUCTURE_TYPE_IMPORT_METAL_BUFFER_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkExportMetalBufferInfoEXT, VK_STRUCTURE_TYPE_EXPORT_METAL_BUFFER_INFO_EXT);

REGISTER_VK_STRUCT_ID(VkExportMetalObjectCreateInfoEXT,
                      VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT);

REGISTER_VK_STRUCT_ID(VkImportMemoryMetalHandleInfoEXT,
                      VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT);
REGISTER_VK_STRUCT_ID(VkMemoryMetalHandlePropertiesEXT,
                      VK_STRUCTURE_TYPE_MEMORY_METAL_HANDLE_PROPERTIES_EXT);
REGISTER_VK_STRUCT_ID(VkMemoryGetMetalHandleInfoEXT,
                      VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT);
#endif

REGISTER_VK_STRUCT_ID(VkPhysicalDeviceDiagnosticsConfigFeaturesNV,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV);
REGISTER_VK_STRUCT_ID(VkDeviceGroupDeviceCreateInfo,
                      VK_STRUCTURE_TYPE_DEVICE_GROUP_DEVICE_CREATE_INFO);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceVulkan11Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceVulkan12Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES);
REGISTER_VK_STRUCT_ID(VkPhysicalDeviceVulkan13Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES);

#undef REGISTER_VK_STRUCT_ID
