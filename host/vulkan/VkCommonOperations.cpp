// Copyright 2018 The Android Open Source Project
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
#include "VkCommonOperations.h"

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vk_enum_string_helper.h>

#include <iomanip>
#include <ostream>
#include <sstream>
#include <unordered_set>

#include "VkDecoderGlobalState.h"
#include "VkEmulatedPhysicalDeviceMemory.h"
#include "VkFormatUtils.h"
#include "VulkanDispatch.h"
#include "gfxstream/Optional.h"
#include "gfxstream/Tracing.h"
#include "gfxstream/containers/Lookup.h"
#include "gfxstream/containers/StaticMap.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/system/System.h"
#include "common/goldfish_vk_dispatch.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/vm_operations.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#ifdef __APPLE__
#include <CoreFoundation/CoreFoundation.h>
#include <vulkan/vulkan_beta.h>  // for MoltenVK portability extensions
#endif

namespace gfxstream {
namespace vk {
namespace {

using gfxstream::base::AutoLock;
using gfxstream::base::kNullopt;
using gfxstream::base::Optional;
using gfxstream::base::StaticLock;
using gfxstream::base::StaticMap;

constexpr size_t kPageBits = 12;
constexpr size_t kPageSize = 1u << kPageBits;

static std::optional<std::string> sMemoryLogPath = std::nullopt;

const char* string_AstcEmulationMode(AstcEmulationMode mode) {
    switch (mode) {
        case AstcEmulationMode::Disabled:
            return "Disabled";
        case AstcEmulationMode::Cpu:
            return "Cpu";
        case AstcEmulationMode::Gpu:
            return "Gpu";
    }
    return "Unknown";
}

}  // namespace

std::optional<GenericDescriptorInfo> VkEmulation::exportMemoryHandle(VkDevice device,
                                                                     VkDeviceMemory memory) {
    GenericDescriptorInfo ret;

#if defined(__ANDROID__)
    // On Android, we currently don't run virtio-gpu in a separate process. Therefore,
    // we don't have a need to export memory handles from the gpu process to the main
    // process. Let's not implement this for now.
    return std::nullopt;
#elif defined(__unix__)
    VkMemoryGetFdInfoKHR memoryGetFdInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
        .pNext = nullptr,
        .memory = memory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
    };
    ret.streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD;

#if defined(__linux__)
    if (supportsDmaBuf()) {
        memoryGetFdInfo.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        ret.streamHandleType = STREAM_HANDLE_TYPE_MEM_DMABUF;
    }
#endif

    int fd = -1;
    if (mDeviceInfo.getMemoryHandleFunc(mDevice, &memoryGetFdInfo, &fd) != VK_SUCCESS) {
        return std::nullopt;
    };

    ret.descriptor = ManagedDescriptor(fd);

#elif defined(_WIN32)
    VkMemoryGetWin32HandleInfoKHR memoryGetHandleInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        .pNext = nullptr,
        .memory = memory,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
    };
    ret.streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_WIN32;

    HANDLE handle;
    if (mDeviceInfo.getMemoryHandleFunc(mDevice, &memoryGetHandleInfo, &handle) != VK_SUCCESS) {
        return std::nullopt;
    }

    ret.descriptor = ManagedDescriptor(handle);
#else
    GFXSTREAM_ERROR("Unsupported external memory handle type.");
    return std::nullopt;
#endif

    return std::move(ret);
}

static std::optional<ExternalHandleInfo> dupExternalMemory(std::optional<ExternalHandleInfo> handleInfo) {
    if (!handleInfo) {
        GFXSTREAM_ERROR(
            "dupExternalMemory: No external memory handle info provided to duplicate the external "
            "memory");
        return std::nullopt;
    }
#if defined(_WIN32)
    auto myProcessHandle = GetCurrentProcess();
    HANDLE res;
    DuplicateHandle(myProcessHandle,
                    static_cast<HANDLE>(
                        reinterpret_cast<void*>(handleInfo->handle)),  // source process and handle
                    myProcessHandle, &res,  // target process and pointer to handle
                    0 /* desired access (ignored) */, true /* inherit */,
                    DUPLICATE_SAME_ACCESS /* same access option */);
    return ExternalHandleInfo{
        .handle = reinterpret_cast<ExternalHandleType>(res),
        .streamHandleType = handleInfo->streamHandleType,
    };
#elif defined(__QNX__)
    if (STREAM_HANDLE_TYPE_PLATFORM_SCREEN_BUFFER_QNX == handleInfo->streamHandleType) {
        // No dup required for the screen_buffer handle
        return ExternalHandleInfo{
            .handle = handleInfo->handle,
            .streamHandleType = handleInfo->streamHandleType,
        };
    }
    // TODO(aruby@blackberry.com): Support dup-ing for OPAQUE_FD or DMABUF types on QNX
    return std::nullopt;
#else
    // TODO(aruby@blackberry.com): Check handleType?
    return ExternalHandleInfo{
        .handle = handleInfo->dupFd(),
        .streamHandleType = handleInfo->streamHandleType,
    };
#endif
}

bool getStagingMemoryTypeIndex(VulkanDispatch* vk, VkDevice device,
                               const VkPhysicalDeviceMemoryProperties* memProps,
                               const VkMemoryRequirements& memReqs,
                               uint32_t* typeIndex) {
    // To be a staging buffer, we need to allow CPU read/write access.
    // Thus, we need the memory type index both to be host visible
    // and to be supported in the memory requirements of the buffer.
    bool foundSuitableStagingMemoryType = false;
    uint32_t stagingMemoryTypeIndex = 0;

    for (uint32_t i = 0; i < memProps->memoryTypeCount; ++i) {
        const auto& typeInfo = memProps->memoryTypes[i];
        bool hostVisible = typeInfo.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        bool hostCached = typeInfo.propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
        bool allowedInBuffer = (1 << i) & memReqs.memoryTypeBits;
        if (hostVisible && hostCached && allowedInBuffer) {
            foundSuitableStagingMemoryType = true;
            stagingMemoryTypeIndex = i;
            break;
        }
    }

    // If the previous loop failed, try to accept a type that is not HOST_CACHED.
    if (!foundSuitableStagingMemoryType) {
        for (uint32_t i = 0; i < memProps->memoryTypeCount; ++i) {
            const auto& typeInfo = memProps->memoryTypes[i];
            bool hostVisible = typeInfo.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            bool allowedInBuffer = (1 << i) & memReqs.memoryTypeBits;
            if (hostVisible && allowedInBuffer) {
                GFXSTREAM_ERROR("Warning: using non-cached HOST_VISIBLE type for staging memory");
                foundSuitableStagingMemoryType = true;
                stagingMemoryTypeIndex = i;
                break;
            }
        }
    }

    if (!foundSuitableStagingMemoryType) {
        std::stringstream ss;
        ss << "Could not find suitable memory type index "
           << "for staging buffer. Memory type bits: " << std::hex << memReqs.memoryTypeBits << "\n"
           << "Available host visible memory type indices:"
           << "\n";
        for (uint32_t i = 0; i < VK_MAX_MEMORY_TYPES; ++i) {
            if (memProps->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
                ss << "Host visible memory type index: %u" << i << "\n";
            }
            if (memProps->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
                ss << "Host cached memory type index: %u" << i << "\n";
            }
        }

        GFXSTREAM_ERROR("Error: %s", ss.str().c_str());

        return false;
    }

    *typeIndex = stagingMemoryTypeIndex;

    return true;
}

bool VkEmulation::StagingBuffer::create(VulkanDispatch* vk, VkDevice device,
                                            const VkPhysicalDeviceMemoryProperties* memProps,
                                            const DebugUtilsHelper& debugUtilsHelper,
                                            const VkDeviceSize size) {
    VkBufferCreateInfo bufCi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        0,
        0,
        size,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        nullptr,
    };

    VkResult bufCreateRes = vk->vkCreateBuffer(device, &bufCi, nullptr, &mBuffer);
    if (bufCreateRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create staging buffer. Error: %s [%d].",
                        string_VkResult(bufCreateRes), bufCreateRes);
        return false;
    }

    VkMemoryRequirements memReqs;
    vk->vkGetBufferMemoryRequirements(device, mBuffer, &memReqs);

    mAllocationSize = memReqs.size;

    uint32_t typeIndex = 0;
    if (!getStagingMemoryTypeIndex(vk, device, memProps, memReqs, &typeIndex)) {
        GFXSTREAM_ERROR("Failed to determine staging memory type index.");
        return false;
    }

    GFXSTREAM_VERBOSE("%s: selected memory type index = %d, propertyFlags = %d, heapIndex = %d",
                      __func__, typeIndex, memProps->memoryTypes[typeIndex].propertyFlags,
                      memProps->memoryTypes[typeIndex].heapIndex);

    if (!((1 << typeIndex) & memReqs.memoryTypeBits)) {
        GFXSTREAM_ERROR(
            "Failed: Inconsistent determination of memory type index for staging buffer");
        return false;
    }

    const VkMemoryType& memType = memProps->memoryTypes[typeIndex];
    if ((memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0) {
        GFXSTREAM_ERROR("Failed: Could not select host visible memory for staging buffer");
        return false;
    }

    // Non-host coherent memory would require manual flush/invalidate
    mIsHostCoherent = (memType.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = mAllocationSize,
        .memoryTypeIndex = typeIndex,
    };

    VkResult allocRes = vk->vkAllocateMemory(device, &allocInfo, nullptr, &mMemory);
    if (allocRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("%s: failed in vkAllocateMemory: %s [%d]", __func__,
                        string_VkResult(allocRes), allocRes);
        return false;
    }

    VkResult mapRes = vk->vkMapMemory(device, mMemory, 0, VK_WHOLE_SIZE, 0, &mMappedPtr);
    if (mapRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("%s: failed in vkMapMemory: %s", __func__, string_VkResult(mapRes));
        return false;
    }

    VkResult stagingBufferBindRes = vk->vkBindBufferMemory(device, mBuffer, mMemory, 0);

    if (stagingBufferBindRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to bind memory for staging buffer. Error %s.",
                        string_VkResult(stagingBufferBindRes));
        return false;
    }

    debugUtilsHelper.addDebugLabel(mMemory, "AEMU_StagingBufferMemory");
    debugUtilsHelper.addDebugLabel(mBuffer, "AEMU_StagingBuffer");

    return true;
}

void VkEmulation::StagingBuffer::destroy(VulkanDispatch* vk, VkDevice device) {
    vk->vkUnmapMemory(device, mMemory);
    vk->vkDestroyBuffer(device, mBuffer, nullptr);
    vk->vkFreeMemory(device, mMemory, nullptr);

    mMemory = VK_NULL_HANDLE;
    mBuffer = VK_NULL_HANDLE;
}

VkExternalMemoryHandleTypeFlagBits VkEmulation::getDefaultExternalMemoryHandleType() {
#if defined(_WIN32)
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
#else

#if defined(__APPLE__)
    if (mInstanceSupportsMoltenVK) {
        return VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
    }
#endif

#if defined(__ANDROID__)
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
#endif

#if defined(__QNX__)
    // TODO(aruby@blackberry.com): Use (DMABUF|OPAQUE_FD) on QNX, when screen_buffer not supported?
    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_SCREEN_BUFFER_BIT_QNX;
#endif

    return VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
#endif
}

static bool extensionsSupported(const std::vector<VkExtensionProperties>& currentProps,
                                const std::vector<const char*>& wantedExtNames) {
    std::vector<bool> foundExts(wantedExtNames.size(), false);

    for (uint32_t i = 0; i < currentProps.size(); ++i) {
        for (size_t j = 0; j < wantedExtNames.size(); ++j) {
            if (!strcmp(wantedExtNames[j], currentProps[i].extensionName)) {
                foundExts[j] = true;
            }
        }
    }

    for (size_t i = 0; i < wantedExtNames.size(); ++i) {
        bool found = foundExts[i];
        if (!found) {
            GFXSTREAM_DEBUG("%s not found, bailing.", wantedExtNames[i]);
            return false;
        }
    }

    return true;
}

// Return true if format requires sampler YCBCR conversion for VK_IMAGE_ASPECT_COLOR_BIT image
// views. Table found in spec
static bool formatRequiresYcbcrConversion(VkFormat format) {
    switch (format) {
        case VK_FORMAT_G8B8G8R8_422_UNORM:
        case VK_FORMAT_B8G8R8G8_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        case VK_FORMAT_R10X6G10X6B10X6A10X6_UNORM_4PACK16:
        case VK_FORMAT_G10X6B10X6G10X6R10X6_422_UNORM_4PACK16:
        case VK_FORMAT_B10X6G10X6R10X6G10X6_422_UNORM_4PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_R12X4G12X4B12X4A12X4_UNORM_4PACK16:
        case VK_FORMAT_G12X4B12X4G12X4R12X4_422_UNORM_4PACK16:
        case VK_FORMAT_B12X4G12X4R12X4G12X4_422_UNORM_4PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16B16G16R16_422_UNORM:
        case VK_FORMAT_B16G16R16G16_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
        case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
        case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
        case VK_FORMAT_G8_B8R8_2PLANE_444_UNORM:
        case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_444_UNORM_3PACK16:
        case VK_FORMAT_G16_B16R16_2PLANE_444_UNORM:
            return true;
        default:
            return false;
    }
}

bool VkEmulation::populateImageFormatExternalMemorySupportInfo(VulkanDispatch* vk,
                                                               VkPhysicalDevice physdev,
                                                               ImageSupportInfo* info) {
    // Currently there is nothing special we need to do about
    // VkFormatProperties2, so just use the normal version
    // and put it in the format2 struct.
    VkFormatProperties outFormatProps;
    vk->vkGetPhysicalDeviceFormatProperties(physdev, info->format, &outFormatProps);

    info->formatProps2 = {
        VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
        0,
        outFormatProps,
    };

    if (!mInstanceSupportsExternalMemoryCapabilities) {
        info->supportsExternalMemory = false;
        info->requiresDedicatedAllocation = false;

        VkImageFormatProperties outImageFormatProps;
        VkResult res = vk->vkGetPhysicalDeviceImageFormatProperties(
            physdev, info->format, info->type, info->tiling, info->usageFlags, info->createFlags,
            &outImageFormatProps);

        if (res != VK_SUCCESS) {
            if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
                info->supported = false;
                return true;
            } else {
                GFXSTREAM_ERROR(
                    "vkGetPhysicalDeviceImageFormatProperties query "
                    "failed with %s"
                    "for format 0x%x type 0x%x usage 0x%x flags 0x%x",
                    string_VkResult(res), info->format, info->type, info->usageFlags,
                    info->createFlags);
                return false;
            }
        }

        info->supported = true;

        info->imageFormatProps2 = {
            VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
            0,
            outImageFormatProps,
        };

        GFXSTREAM_DEBUG("Supported (not externally): %s %s %s %s", string_VkFormat(info->format),
                        string_VkImageType(info->type), string_VkImageTiling(info->tiling),
                        string_VkImageUsageFlagBits((VkImageUsageFlagBits)info->usageFlags));

        return true;
    }

    VkPhysicalDeviceExternalImageFormatInfo extInfo = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
        0,
        getDefaultExternalMemoryHandleType(),
    };

    VkPhysicalDeviceImageFormatInfo2 formatInfo2 = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        &extInfo,
        info->format,
        info->type,
        info->tiling,
        info->usageFlags,
        info->createFlags,
    };

    VkExternalImageFormatProperties outExternalProps = {
        VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
        0,
        {
            (VkExternalMemoryFeatureFlags)0,
            (VkExternalMemoryHandleTypeFlags)0,
            (VkExternalMemoryHandleTypeFlags)0,
        },
    };

    VkImageFormatProperties2 outProps2 = {VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
                                          &outExternalProps,
                                          {
                                              {0, 0, 0},
                                              0,
                                              0,
                                              1,
                                              0,
                                          }};

    VkResult res = mGetImageFormatProperties2Func(physdev, &formatInfo2, &outProps2);

    if (res != VK_SUCCESS) {
        if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
            GFXSTREAM_DEBUG("Not Supported: %s %s %s %s", string_VkFormat(info->format),
                            string_VkImageType(info->type), string_VkImageTiling(info->tiling),
                            string_VkImageUsageFlagBits((VkImageUsageFlagBits)info->usageFlags));

            info->supported = false;
            return true;
        } else {
            GFXSTREAM_ERROR(
                "vkGetPhysicalDeviceImageFormatProperties2KHR query "
                "failed with %s "
                "for format 0x%x type 0x%x usage 0x%x flags 0x%x",
                string_VkResult(res), info->format, info->type, info->usageFlags,
                info->createFlags);
            return false;
        }
    }

    info->supported = true;

    VkExternalMemoryFeatureFlags featureFlags =
        outExternalProps.externalMemoryProperties.externalMemoryFeatures;

    VkExternalMemoryHandleTypeFlags exportImportedFlags =
        outExternalProps.externalMemoryProperties.exportFromImportedHandleTypes;

    // Don't really care about export form imported handle types yet
    (void)exportImportedFlags;

    VkExternalMemoryHandleTypeFlags compatibleHandleTypes =
        outExternalProps.externalMemoryProperties.compatibleHandleTypes;

    VkExternalMemoryHandleTypeFlags handleTypeNeeded = getDefaultExternalMemoryHandleType();

    info->supportsExternalMemory = (handleTypeNeeded & compatibleHandleTypes) &&
                                   (VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT & featureFlags) &&
                                   (VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT & featureFlags);

    info->requiresDedicatedAllocation =
        (VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT & featureFlags);

    info->imageFormatProps2 = outProps2;
    info->extFormatProps = outExternalProps;
    info->imageFormatProps2.pNext = &info->extFormatProps;

    GFXSTREAM_DEBUG("Supported: %s %s %s %s, supportsExternalMemory? %d, requiresDedicated? %d",
                    string_VkFormat(info->format), string_VkImageType(info->type),
                    string_VkImageTiling(info->tiling),
                    string_VkImageUsageFlagBits((VkImageUsageFlagBits)info->usageFlags),
                    info->supportsExternalMemory, info->requiresDedicatedAllocation);

    return true;
}

// Vulkan driverVersions are bit-shift packs of their dotted versions
// For example, nvidia driverversion 1934229504 unpacks to 461.40
// note: while this is equivalent to VkPhysicalDeviceDriverProperties.driverInfo on NVIDIA,
// on intel that value is simply "Intel driver".
static std::string decodeDriverVersion(uint32_t vendorId, uint32_t driverVersion) {
    std::stringstream result;
    switch (vendorId) {
        case 0x10DE: {
            // Nvidia. E.g. driverVersion = 1934229504(0x734a0000) maps to 461.40
            uint32_t major = driverVersion >> 22;
            uint32_t minor = (driverVersion >> 14) & 0xff;
            uint32_t build = (driverVersion >> 6) & 0xff;
            uint32_t revision = driverVersion & 0x3f;
            result << major << '.' << minor << '.' << build << '.' << revision;
            break;
        }
        case 0x8086: {
            // Intel. E.g. driverVersion = 1647866(0x1924fa) maps to 100.9466 (27.20.100.9466)
            uint32_t high = driverVersion >> 14;
            uint32_t low = driverVersion & 0x3fff;
            result << high << '.' << low;
            break;
        }
        case 0x002:  // amd
        default: {
            uint32_t major = VK_VERSION_MAJOR(driverVersion);
            uint32_t minor = VK_VERSION_MINOR(driverVersion);
            uint32_t patch = VK_VERSION_PATCH(driverVersion);
            result << major << "." << minor << "." << patch;
            break;
        }
    }
    return result.str();
}

/*static*/ std::vector<VkEmulation::ImageSupportInfo> VkEmulation::getBasicImageSupportList() {
    struct ImageFeatureCombo {
        VkFormat format;
        VkImageCreateFlags createFlags = 0;
    };
    // Set the mutable flag for RGB UNORM formats so that the created image can also be sampled in
    // the sRGB Colorspace. See
    // https://chromium-review.googlesource.com/c/chromiumos/platform/minigbm/+/3827672/comments/77db9cb3_60663a6a
    // for details.
    std::vector<ImageFeatureCombo> combos = {
        // Cover all the gralloc formats
        {VK_FORMAT_R8G8B8A8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R8G8B8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},

        {VK_FORMAT_R5G6B5_UNORM_PACK16},
        {VK_FORMAT_A1R5G5B5_UNORM_PACK16},

        {VK_FORMAT_R16G16B16A16_SFLOAT},
        {VK_FORMAT_R16G16B16_SFLOAT},

        {VK_FORMAT_B8G8R8A8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},

        {VK_FORMAT_B4G4R4A4_UNORM_PACK16,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R4G4B4A4_UNORM_PACK16,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},

        {VK_FORMAT_R8_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},
        {VK_FORMAT_R16_UNORM,
         VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT},

        {VK_FORMAT_A2R10G10B10_UINT_PACK32},
        {VK_FORMAT_A2R10G10B10_UNORM_PACK32},
        {VK_FORMAT_A2B10G10R10_UNORM_PACK32},

        // Compressed texture formats
        {VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK},
        {VK_FORMAT_ASTC_4x4_UNORM_BLOCK},

        // YUV formats used in Android
        {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM},
        {VK_FORMAT_G8_B8R8_2PLANE_422_UNORM},
        {VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM},
        {VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM},
        {VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16},
    };

    std::vector<VkImageType> types = {
        VK_IMAGE_TYPE_2D,
    };

    std::vector<VkImageTiling> tilings = {
        VK_IMAGE_TILING_LINEAR,
        VK_IMAGE_TILING_OPTIMAL,
    };

    std::vector<VkImageUsageFlags> usageFlags = {
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_SAMPLED_BIT,          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    std::vector<VkEmulation::ImageSupportInfo> res;

    // Currently: 17 format + create flags combo, 2 tilings, 5 usage flags -> 170 cases to check.
    for (auto combo : combos) {
        for (auto t : types) {
            for (auto ti : tilings) {
                for (auto u : usageFlags) {
                    VkEmulation::ImageSupportInfo info;
                    info.format = combo.format;
                    info.type = t;
                    info.tiling = ti;
                    info.usageFlags = u;
                    info.createFlags = combo.createFlags;
                    res.push_back(info);
                }
            }
        }
    }

    // Add depth attachment cases
    std::vector<ImageFeatureCombo> depthCombos = {
        // Depth formats
        {VK_FORMAT_D16_UNORM},
        {VK_FORMAT_X8_D24_UNORM_PACK32},
        {VK_FORMAT_D24_UNORM_S8_UINT},
        {VK_FORMAT_D32_SFLOAT},
        {VK_FORMAT_D32_SFLOAT_S8_UINT},
    };

    std::vector<VkImageUsageFlags> depthUsageFlags = {
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT,
        VK_IMAGE_USAGE_SAMPLED_BIT,          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };

    for (auto combo : depthCombos) {
        for (auto t : types) {
            for (auto u : depthUsageFlags) {
                ImageSupportInfo info;
                info.format = combo.format;
                info.type = t;
                info.tiling = VK_IMAGE_TILING_OPTIMAL;
                info.usageFlags = u;
                info.createFlags = combo.createFlags;
                res.push_back(info);
            }
        }
    }

    return res;
}

// Checks if the user enforced a specific GPU, it can be done via index or name.
// Otherwise try to find the best device with discrete GPU and high vulkan API level.
// Scoring of the devices is done by some implicit choices based on known driver
// quality, stability and performance issues of current GPUs.
// Only one Vulkan device is selected; this makes things simple for now, but we
// could consider utilizing multiple devices in use cases that make sense.
int VkEmulation::getSelectedGpuIndex(
    const std::vector<VkEmulation::DeviceSupportInfo>& deviceInfos) {
    const int physicalDeviceCount = deviceInfos.size();
    if (physicalDeviceCount == 1) {
        return 0;
    }

    if (!mInstanceSupportsGetPhysicalDeviceProperties2) {
        // If we don't support physical device ID properties, pick the first physical device
        GFXSTREAM_WARNING("Instance doesn't support '%s', picking the first physical device",
                          VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        return 0;
    }

    const char* EnvVarSelectGpu = "ANDROID_EMU_VK_SELECT_GPU";
    std::string enforcedGpuStr = gfxstream::base::getEnvironmentVariable(EnvVarSelectGpu);
    int enforceGpuIndex = -1;
    if (enforcedGpuStr.size()) {
        GFXSTREAM_INFO("%s is set to %s", EnvVarSelectGpu, enforcedGpuStr.c_str());

        if (enforcedGpuStr[0] == '0') {
            enforceGpuIndex = 0;
        } else {
            enforceGpuIndex = (atoi(enforcedGpuStr.c_str()));
            if (enforceGpuIndex == 0) {
                // Could not convert to an integer, try searching with device name
                // Do the comparison case insensitive as vendor names don't have consistency
                enforceGpuIndex = -1;
                std::transform(enforcedGpuStr.begin(), enforcedGpuStr.end(), enforcedGpuStr.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                for (int i = 0; i < physicalDeviceCount; ++i) {
                    std::string deviceName = std::string(deviceInfos[i].physdevProps.deviceName);
                    std::transform(deviceName.begin(), deviceName.end(), deviceName.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    GFXSTREAM_INFO("Physical device [%d] = %s", i, deviceName.c_str());

                    if (deviceName.find(enforcedGpuStr) != std::string::npos) {
                        enforceGpuIndex = i;
                    }
                }
            }
        }

        if (enforceGpuIndex != -1 && enforceGpuIndex >= 0 && enforceGpuIndex < (int)deviceInfos.size()) {
            GFXSTREAM_INFO("Selecting GPU (%s) at index %d.",
                           deviceInfos[enforceGpuIndex].physdevProps.deviceName, enforceGpuIndex);
        } else {
            GFXSTREAM_WARNING("Could not select the GPU with ANDROID_EMU_VK_GPU_SELECT.");
            enforceGpuIndex = -1;
        }
    }

    if (enforceGpuIndex != -1) {
        return enforceGpuIndex;
    }

    // If there are multiple devices, and none of them are enforced to use,
    // score each device and select the best
    int selectedGpuIndex = 0;
    auto getDeviceScore = [](const VkEmulation::DeviceSupportInfo& deviceInfo) {
        uint32_t deviceScore = 0;
        if (!deviceInfo.hasGraphicsQueueFamily) {
            // Not supporting graphics, cannot be used.
            return deviceScore;
        }

        // Matches the ordering in VkPhysicalDeviceType
        const uint32_t deviceTypeScoreTable[] = {
            100,   // VK_PHYSICAL_DEVICE_TYPE_OTHER = 0,
            1000,  // VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU = 1,
            2000,  // VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU = 2,
            500,   // VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU = 3,
            600,   // VK_PHYSICAL_DEVICE_TYPE_CPU = 4,
        };

        // Prefer discrete GPUs, then integrated and then others..
        const int deviceType = deviceInfo.physdevProps.deviceType;
        deviceScore += deviceTypeScoreTable[deviceType];

        // Prefer higher level of Vulkan API support, restrict version numbers to
        // common limits to ensure an always increasing scoring change
        const uint32_t major = VK_API_VERSION_MAJOR(deviceInfo.physdevProps.apiVersion);
        const uint32_t minor = VK_API_VERSION_MINOR(deviceInfo.physdevProps.apiVersion);
        const uint32_t patch = VK_API_VERSION_PATCH(deviceInfo.physdevProps.apiVersion);
        deviceScore += major * 5000 + std::min(minor, 10u) * 500 + std::min(patch, 400u);

        return deviceScore;
    };

    uint32_t maxScore = 0;
    for (int i = 0; i < physicalDeviceCount; ++i) {
        const uint32_t score = getDeviceScore(deviceInfos[i]);
        GFXSTREAM_DEBUG("Device selection score for '%s' = %d",
                        deviceInfos[i].physdevProps.deviceName, score);
        if (score > maxScore) {
            selectedGpuIndex = i;
            maxScore = score;
        }
    }

    return selectedGpuIndex;
}

/*static*/
std::unique_ptr<VkEmulation> VkEmulation::create(VulkanDispatch* gvk,
                                                 gfxstream::host::BackendCallbacks callbacks,
                                                 const gfxstream::host::FeatureSet& features) {
    if (!vkDispatchValid(gvk)) {
        GFXSTREAM_ERROR("Dispatch is invalid.");
        return nullptr;
    }

    std::unique_ptr<VkEmulation> emulation(new VkEmulation());

    std::lock_guard<std::mutex> lock(emulation->mMutex);

    emulation->mCallbacks = callbacks;
    emulation->mFeatures = features;
    emulation->mGvk = gvk;

    std::vector<const char*> getPhysicalDeviceProperties2InstanceExtNames = {
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    };
    std::vector<const char*> externalMemoryInstanceExtNames = {
        VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
    };

    std::vector<const char*> externalSemaphoreInstanceExtNames = {
        VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME,
    };

    std::vector<const char*> externalFenceInstanceExtNames = {
        VK_KHR_EXTERNAL_FENCE_CAPABILITIES_EXTENSION_NAME,
    };

    std::vector<const char*> surfaceInstanceExtNames = {
        VK_KHR_SURFACE_EXTENSION_NAME,
    };

    std::vector<const char*> externalMemoryDeviceExtNames = {
        VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
        VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME,
        VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
#ifdef _WIN32
        VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
#elif defined(__ANDROID__)
        VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
#elif defined(__QNX__)
        VK_QNX_EXTERNAL_MEMORY_SCREEN_BUFFER_EXTENSION_NAME,
        VK_EXT_QUEUE_FAMILY_FOREIGN_EXTENSION_NAME,
#elif defined(__APPLE__)
        // VK_EXT_metal_objects will be added if host MoltenVK is enabled,
        // otherwise VK_KHR_external_memory_fd will be used
#else
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
#endif
    };

#if defined(__APPLE__)
    std::vector<const char*> moltenVkInstanceExtNames = {
        VK_MVK_MACOS_SURFACE_EXTENSION_NAME,
        VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME,
    };
    std::vector<const char*> moltenVkDeviceExtNames = {
        VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME,
        VK_EXT_METAL_OBJECTS_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME,
    };
#endif

    std::vector<VkExtensionProperties>& instanceExts = emulation->mInstanceExtensions;
    uint32_t instanceExtCount = 0;
    gvk->vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, nullptr);
    instanceExts.resize(instanceExtCount);
    gvk->vkEnumerateInstanceExtensionProperties(nullptr, &instanceExtCount, instanceExts.data());

    bool getPhysicalDeviceProperties2Supported =
        extensionsSupported(instanceExts, getPhysicalDeviceProperties2InstanceExtNames);
    bool externalMemoryCapabilitiesSupported = getPhysicalDeviceProperties2Supported &&
        extensionsSupported(instanceExts, externalMemoryInstanceExtNames);
    bool externalSemaphoreCapabilitiesSupported = getPhysicalDeviceProperties2Supported &&
        extensionsSupported(instanceExts, externalSemaphoreInstanceExtNames);
    bool externalFenceCapabilitiesSupported = getPhysicalDeviceProperties2Supported &&
        extensionsSupported(instanceExts, externalFenceInstanceExtNames);
    bool surfaceSupported = extensionsSupported(instanceExts, surfaceInstanceExtNames);
#if defined(__APPLE__)
    const std::string vulkanIcd = gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD");
    const bool moltenVKEnabled = (vulkanIcd == "moltenvk");
    const bool moltenVKSupported = extensionsSupported(instanceExts, moltenVkInstanceExtNames);
    if (moltenVKEnabled && !moltenVKSupported) {
        // This might happen if the user manually changes moltenvk ICD library
        GFXSTREAM_FATAL("MoltenVK requested, but the required extensions are not supported.");
    }
    const bool useMoltenVK = moltenVKEnabled && moltenVKSupported;
#endif

    VkInstanceCreateInfo instCi = {
        VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0, nullptr, 0, nullptr, 0, nullptr,
    };

    std::unordered_set<const char*> selectedInstanceExtensionNames;

    const bool debugUtilsSupported =
        extensionsSupported(instanceExts, {VK_EXT_DEBUG_UTILS_EXTENSION_NAME});
    const bool debugUtilsRequested = emulation->mFeatures.VulkanDebugUtils.enabled;
    const bool debugUtilsAvailableAndRequested = debugUtilsSupported && debugUtilsRequested;
    if (debugUtilsAvailableAndRequested) {
        selectedInstanceExtensionNames.emplace(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    } else if (debugUtilsRequested) {
        GFXSTREAM_WARNING("VulkanDebugUtils requested, but '%' extension is not supported.",
                          VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    if (getPhysicalDeviceProperties2Supported) {
        for (auto extension : getPhysicalDeviceProperties2InstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

    if (externalSemaphoreCapabilitiesSupported) {
        for (auto extension : externalMemoryInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

    if (externalFenceCapabilitiesSupported) {
        for (auto extension : externalSemaphoreInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

    if (externalMemoryCapabilitiesSupported) {
        for (auto extension : externalFenceInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

    if (surfaceSupported) {
        for (auto extension : surfaceInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

    if (emulation->mFeatures.VulkanNativeSwapchain.enabled) {
        for (auto extension : SwapChainStateVk::getRequiredInstanceExtensions()) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }

#if defined(__APPLE__)
    if (useMoltenVK) {
        GFXSTREAM_INFO("MoltenVK is supported, enabling Vulkan portability.");
        instCi.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        for (auto extension : moltenVkInstanceExtNames) {
            selectedInstanceExtensionNames.emplace(extension);
        }
    }
#endif

    std::vector<const char*> selectedInstanceExtensionNames_(selectedInstanceExtensionNames.begin(),
                                                             selectedInstanceExtensionNames.end());
    instCi.enabledExtensionCount = static_cast<uint32_t>(selectedInstanceExtensionNames_.size());
    instCi.ppEnabledExtensionNames = selectedInstanceExtensionNames_.data();

    VkApplicationInfo appInfo = {
        VK_STRUCTURE_TYPE_APPLICATION_INFO, 0, "AEMU", 1, "AEMU", 1, VK_MAKE_VERSION(1, 0, 0),
    };

    instCi.pApplicationInfo = &appInfo;

    // Can we know instance version early?
    if (gvk->vkEnumerateInstanceVersion) {
        GFXSTREAM_DEBUG("global loader has vkEnumerateInstanceVersion.");
        uint32_t instanceVersion;
        VkResult res = gvk->vkEnumerateInstanceVersion(&instanceVersion);
        if (VK_SUCCESS == res) {
            if (instanceVersion >= VK_MAKE_VERSION(1, 1, 0)) {
                GFXSTREAM_DEBUG("global loader has vkEnumerateInstanceVersion returning >= 1.1.");
                appInfo.apiVersion = VK_MAKE_VERSION(1, 1, 0);
            }
        }
    }

    GFXSTREAM_DEBUG("Creating instance, asking for version %d.%d.%d ...",
                    VK_VERSION_MAJOR(appInfo.apiVersion), VK_VERSION_MINOR(appInfo.apiVersion),
                    VK_VERSION_PATCH(appInfo.apiVersion));

    VkResult res = gvk->vkCreateInstance(&instCi, nullptr, &emulation->mInstance);
    if (res != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create Vulkan instance. Error %s.", string_VkResult(res));
        return nullptr;
    }

    // Create instance level dispatch.
    emulation->mIvk = new VulkanDispatch();
    init_vulkan_dispatch_from_instance(gvk, emulation->mInstance, emulation->mIvk);

    auto ivk = emulation->mIvk;
    if (!vulkan_dispatch_check_instance_VK_VERSION_1_0(ivk)) {
        GFXSTREAM_ERROR("Warning: Vulkan 1.0 APIs missing from instance");
    }

    if (ivk->vkEnumerateInstanceVersion) {
        uint32_t instanceVersion;
        VkResult enumInstanceRes = ivk->vkEnumerateInstanceVersion(&instanceVersion);
        if ((VK_SUCCESS == enumInstanceRes) && instanceVersion >= VK_MAKE_VERSION(1, 1, 0)) {
            if (!vulkan_dispatch_check_instance_VK_VERSION_1_1(ivk)) {
                GFXSTREAM_ERROR("Warning: Vulkan 1.1 APIs missing from instance (1st try)");
            }
        }

        if (appInfo.apiVersion < VK_MAKE_VERSION(1, 1, 0) &&
            instanceVersion >= VK_MAKE_VERSION(1, 1, 0)) {
            GFXSTREAM_DEBUG("Found out that we can create a higher version instance.");
            appInfo.apiVersion = VK_MAKE_VERSION(1, 1, 0);

            gvk->vkDestroyInstance(emulation->mInstance, nullptr);

            res = gvk->vkCreateInstance(&instCi, nullptr, &emulation->mInstance);
            if (res != VK_SUCCESS) {
                GFXSTREAM_ERROR("Failed to create Vulkan 1.1 instance. Error %s.", string_VkResult(res));
                return nullptr;
            }

            init_vulkan_dispatch_from_instance(gvk, emulation->mInstance, emulation->mIvk);

            GFXSTREAM_DEBUG("Created Vulkan 1.1 instance on second try.");

            if (!vulkan_dispatch_check_instance_VK_VERSION_1_1(ivk)) {
                GFXSTREAM_ERROR("Warning: Vulkan 1.1 APIs missing from instance (2nd try)");
            }
        }
    }

    emulation->mVulkanInstanceVersion = appInfo.apiVersion;

    // https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VkPhysicalDeviceIDProperties.html
    // Provided by VK_VERSION_1_1, or VK_KHR_external_fence_capabilities, VK_KHR_external_memory_capabilities,
    // VK_KHR_external_semaphore_capabilities
    emulation->mInstanceSupportsPhysicalDeviceIDProperties = externalFenceCapabilitiesSupported ||
                                                             externalMemoryCapabilitiesSupported ||
                                                             externalSemaphoreCapabilitiesSupported;

    emulation->mInstanceSupportsGetPhysicalDeviceProperties2 =
        getPhysicalDeviceProperties2Supported;
    emulation->mInstanceSupportsExternalMemoryCapabilities = externalMemoryCapabilitiesSupported;
    emulation->mInstanceSupportsExternalSemaphoreCapabilities =
        externalSemaphoreCapabilitiesSupported;
    emulation->mInstanceSupportsExternalFenceCapabilities = externalFenceCapabilitiesSupported;
    emulation->mInstanceSupportsSurface = surfaceSupported;
#if defined(__APPLE__)
    emulation->mInstanceSupportsMoltenVK = useMoltenVK;
#endif

    if (emulation->mInstanceSupportsGetPhysicalDeviceProperties2) {
        emulation->mGetImageFormatProperties2Func = vk_util::getVkInstanceProcAddrWithFallback<
            vk_util::vk_fn_info::GetPhysicalDeviceImageFormatProperties2>(
            {ivk->vkGetInstanceProcAddr, gvk->vkGetInstanceProcAddr}, emulation->mInstance);
        emulation->mGetPhysicalDeviceProperties2Func = vk_util::getVkInstanceProcAddrWithFallback<
            vk_util::vk_fn_info::GetPhysicalDeviceProperties2>(
            {ivk->vkGetInstanceProcAddr, gvk->vkGetInstanceProcAddr}, emulation->mInstance);
        emulation->mGetPhysicalDeviceFeatures2Func = vk_util::getVkInstanceProcAddrWithFallback<
            vk_util::vk_fn_info::GetPhysicalDeviceFeatures2>(
            {ivk->vkGetInstanceProcAddr, gvk->vkGetInstanceProcAddr}, emulation->mInstance);

        if (!emulation->mGetPhysicalDeviceProperties2Func) {
            GFXSTREAM_ERROR(
                "Warning: device claims to support ID properties "
                "but vkGetPhysicalDeviceProperties2 could not be found");
        }
    }

#if defined(__APPLE__)
    if (emulation->mInstanceSupportsMoltenVK) {
        // Enable some specific extensions on MacOS when moltenVK is used.
        externalMemoryDeviceExtNames.push_back(VK_EXT_METAL_OBJECTS_EXTENSION_NAME);
        externalMemoryDeviceExtNames.push_back(VK_EXT_EXTERNAL_MEMORY_METAL_EXTENSION_NAME);
    } else {
        // When MoltenVK is not used(e.g. SwiftShader), use memory fd extension for external memory.
        externalMemoryDeviceExtNames.push_back(VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME);
    }
#endif

    uint32_t physicalDeviceCount = 0;
    ivk->vkEnumeratePhysicalDevices(emulation->mInstance, &physicalDeviceCount, nullptr);
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    ivk->vkEnumeratePhysicalDevices(emulation->mInstance, &physicalDeviceCount,
                                    physicalDevices.data());

    GFXSTREAM_DEBUG("Found %d Vulkan physical devices.", physicalDeviceCount);

    if (physicalDeviceCount == 0) {
        GFXSTREAM_FATAL("No physical devices available.");
    }

    std::vector<DeviceSupportInfo> deviceInfos(physicalDeviceCount);

    for (uint32_t i = 0; i < physicalDeviceCount; ++i) {
        ivk->vkGetPhysicalDeviceProperties(physicalDevices[i], &deviceInfos[i].physdevProps);

        GFXSTREAM_DEBUG("Considering Vulkan physical device %d : %s", i,
                        deviceInfos[i].physdevProps.deviceName);

        // It's easier to figure out the staging buffer along with
        // external memories if we have the memory properties on hand.
        ivk->vkGetPhysicalDeviceMemoryProperties(physicalDevices[i], &deviceInfos[i].memProps);

        uint32_t deviceExtensionCount = 0;
        ivk->vkEnumerateDeviceExtensionProperties(physicalDevices[i], nullptr,
                                                  &deviceExtensionCount, nullptr);
        std::vector<VkExtensionProperties>& deviceExts = deviceInfos[i].extensions;
        deviceExts.resize(deviceExtensionCount);
        ivk->vkEnumerateDeviceExtensionProperties(physicalDevices[i], nullptr,
                                                  &deviceExtensionCount, deviceExts.data());

        deviceInfos[i].supportsExternalMemoryImport = false;
        deviceInfos[i].supportsExternalMemoryExport = false;
        deviceInfos[i].glInteropSupported = 0;  // set later

#if defined(__APPLE__)
        if (useMoltenVK && !extensionsSupported(deviceExts, moltenVkDeviceExtNames)) {
            GFXSTREAM_ERROR("MoltenVK enabled but necessary device extensions are not supported.");
            return nullptr;
        }
#endif

        if (emulation->mInstanceSupportsExternalMemoryCapabilities) {
            deviceInfos[i].supportsExternalMemoryExport =
                deviceInfos[i].supportsExternalMemoryImport =
                    extensionsSupported(deviceExts, externalMemoryDeviceExtNames);
#if defined(__QNX__)
            // External memory export not supported on QNX
            deviceInfos[i].supportsExternalMemoryExport = false;
#endif
        }

        if (emulation->mInstanceSupportsGetPhysicalDeviceProperties2) {
            deviceInfos[i].supportsDriverProperties =
                extensionsSupported(deviceExts, {VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME}) ||
                (deviceInfos[i].physdevProps.apiVersion >= VK_API_VERSION_1_2);
            deviceInfos[i].supportsExternalMemoryHostProps =
                extensionsSupported(deviceExts, {VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME});

            VkPhysicalDeviceProperties2 deviceProps = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2_KHR,
            };
            auto devicePropsChain = vk_make_chain_iterator(&deviceProps);

            VkPhysicalDeviceIDProperties idProps = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES_KHR,
            };
            if (emulation->mInstanceSupportsPhysicalDeviceIDProperties) {
                vk_append_struct(&devicePropsChain, &idProps);
            }

            VkPhysicalDeviceDriverPropertiesKHR driverProps = {
                VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR,
            };
            if (deviceInfos[i].supportsDriverProperties) {
                vk_append_struct(&devicePropsChain, &driverProps);
            }

            VkPhysicalDeviceExternalMemoryHostPropertiesEXT externalMemoryHostProps = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
            };
            if(deviceInfos[i].supportsExternalMemoryHostProps) {
                vk_append_struct(&devicePropsChain, &externalMemoryHostProps);
            }
            emulation->mGetPhysicalDeviceProperties2Func(physicalDevices[i], &deviceProps);
            deviceInfos[i].idProps = vk_make_orphan_copy(idProps);
            deviceInfos[i].externalMemoryHostProps = vk_make_orphan_copy(externalMemoryHostProps);

            std::stringstream driverVendorBuilder;
            driverVendorBuilder << "Vendor " << std::hex << std::setfill('0') << std::showbase
                                << deviceInfos[i].physdevProps.vendorID;

            std::string decodedDriverVersion = decodeDriverVersion(
                deviceInfos[i].physdevProps.vendorID, deviceInfos[i].physdevProps.driverVersion);

            std::stringstream driverVersionBuilder;
            driverVersionBuilder << "Driver Version " << std::hex << std::setfill('0')
                                 << std::showbase << deviceInfos[i].physdevProps.driverVersion
                                 << " Decoded As " << decodedDriverVersion;

            std::string driverVendor = driverVendorBuilder.str();
            std::string driverVersion = driverVersionBuilder.str();
            if (deviceInfos[i].supportsDriverProperties && driverProps.driverID) {
                driverVendor = std::string{driverProps.driverName} + " (" + driverVendor + ")";
                driverVersion = std::string{driverProps.driverInfo} + " (" +
                                string_VkDriverId(driverProps.driverID) + " " + driverVersion + ")";
            }

            deviceInfos[i].driverVendor = driverVendor;
            deviceInfos[i].driverVersion = driverVersion;
        }

// TODO(aruby@qnx.com): Remove once dmabuf extension support has been flushed out on QNX
#if !defined(__QNX__)
        bool dmaBufBlockList = (deviceInfos[i].driverVendor == "NVIDIA (Vendor 0x10de)");
#ifdef CONFIG_AEMU
        // TODO(b/400999642): dma_buf support should be checked with image format support
        dmaBufBlockList |= (deviceInfos[i].driverVendor == "radv (Vendor 0x1002)");
#endif
        deviceInfos[i].supportsDmaBuf =
            extensionsSupported(deviceExts, {VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME}) &&
            !dmaBufBlockList;
#endif

        deviceInfos[i].hasSamplerYcbcrConversionExtension =
            extensionsSupported(deviceExts, {VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME});

        deviceInfos[i].hasNvidiaDeviceDiagnosticCheckpointsExtension =
            extensionsSupported(deviceExts, {VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME});

        if (emulation->mGetPhysicalDeviceFeatures2Func) {
            VkPhysicalDeviceFeatures2 features2 = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            };
            auto features2Chain = vk_make_chain_iterator(&features2);

            VkPhysicalDeviceSamplerYcbcrConversionFeatures samplerYcbcrConversionFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
            };
            vk_append_struct(&features2Chain, &samplerYcbcrConversionFeatures);

#if defined(__QNX__)
            VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX extMemScreenBufferFeatures = {
                .sType =
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_SCREEN_BUFFER_FEATURES_QNX,
            };
            vk_append_struct(&features2Chain, &extMemScreenBufferFeatures);
#endif

            VkPhysicalDeviceDiagnosticsConfigFeaturesNV deviceDiagnosticsConfigFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV,
                .diagnosticsConfig = VK_FALSE,
            };
            if (deviceInfos[i].hasNvidiaDeviceDiagnosticCheckpointsExtension) {
                vk_append_struct(&features2Chain, &deviceDiagnosticsConfigFeatures);
            }

            VkPhysicalDevicePrivateDataFeatures privateDataFeatures = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRIVATE_DATA_FEATURES,
                .privateData = VK_FALSE};
            if (extensionsSupported(deviceExts, {VK_EXT_PRIVATE_DATA_EXTENSION_NAME})) {
                vk_append_struct(&features2Chain, &privateDataFeatures);
            }

            VkPhysicalDeviceRobustness2FeaturesEXT robustness2Features = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
            const bool robustnessRequested = emulation->mFeatures.VulkanRobustness.enabled;
            const bool robustnessSupported =
                extensionsSupported(deviceExts, {VK_EXT_ROBUSTNESS_2_EXTENSION_NAME});
            if (robustnessRequested && robustnessSupported) {
                vk_append_struct(&features2Chain, &robustness2Features);
            }

            emulation->mGetPhysicalDeviceFeatures2Func(physicalDevices[i], &features2);

            deviceInfos[i].supportsSamplerYcbcrConversion =
                samplerYcbcrConversionFeatures.samplerYcbcrConversion == VK_TRUE;

            deviceInfos[i].supportsNvidiaDeviceDiagnosticCheckpoints =
                deviceDiagnosticsConfigFeatures.diagnosticsConfig == VK_TRUE;

            deviceInfos[i].supportsPrivateData = (privateDataFeatures.privateData == VK_TRUE);

            // Enable robustness only when requested
            if (robustnessRequested && robustnessSupported) {
                deviceInfos[i].robustness2Features = vk_make_orphan_copy(robustness2Features);
            } else if (robustnessRequested) {
                GFXSTREAM_WARNING(
                    "VulkanRobustness was requested but the "
                    "VK_EXT_robustness2 extension is not supported.");
            }

#if defined(__QNX__)
            deviceInfos[i].supportsExternalMemoryImport =
                extMemScreenBufferFeatures.screenBufferImport == VK_TRUE;
        } else {
            deviceInfos[i].supportsExternalMemoryImport = false;
#endif
        }

        uint32_t queueFamilyCount = 0;
        ivk->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount,
                                                      nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilyProps(queueFamilyCount);
        ivk->vkGetPhysicalDeviceQueueFamilyProperties(physicalDevices[i], &queueFamilyCount,
                                                      queueFamilyProps.data());

        for (uint32_t j = 0; j < queueFamilyCount; ++j) {
            auto count = queueFamilyProps[j].queueCount;
            auto flags = queueFamilyProps[j].queueFlags;

            bool hasGraphicsQueueFamily = (count > 0 && (flags & VK_QUEUE_GRAPHICS_BIT));
            bool hasComputeQueueFamily = (count > 0 && (flags & VK_QUEUE_COMPUTE_BIT));

            deviceInfos[i].hasGraphicsQueueFamily =
                deviceInfos[i].hasGraphicsQueueFamily || hasGraphicsQueueFamily;

            deviceInfos[i].hasComputeQueueFamily =
                deviceInfos[i].hasComputeQueueFamily || hasComputeQueueFamily;

            if (hasGraphicsQueueFamily) {
                deviceInfos[i].graphicsQueueFamilyIndices.push_back(j);
                GFXSTREAM_DEBUG("Graphics queue family index: %d", j);
            }

            if (hasComputeQueueFamily) {
                deviceInfos[i].computeQueueFamilyIndices.push_back(j);
                GFXSTREAM_DEBUG("Compute queue family index: %d", j);
            }
        }
    }

    // When there are multiple physical devices, find the best one or enable selecting
    // the one enforced by environment variable setting.
    int selectedGpuIndex = emulation->getSelectedGpuIndex(deviceInfos);

    emulation->mPhysicalDevice = physicalDevices[selectedGpuIndex];
    emulation->mPhysicalDeviceIndex = selectedGpuIndex;
    emulation->mDeviceInfo = deviceInfos[selectedGpuIndex];
    // Postcondition: emulation has valid device support info

    // Collect image support info of the selected device
    emulation->mImageSupportInfo = getBasicImageSupportList();
    for (size_t i = 0; i < emulation->mImageSupportInfo.size(); ++i) {
        emulation->populateImageFormatExternalMemorySupportInfo(ivk, emulation->mPhysicalDevice,
                                                                &emulation->mImageSupportInfo[i]);
    }

    if (!emulation->mDeviceInfo.hasGraphicsQueueFamily) {
        GFXSTREAM_ERROR("No Vulkan devices with graphics queues found.");
        return nullptr;
    }

    auto deviceVersion = emulation->mDeviceInfo.physdevProps.apiVersion;
    GFXSTREAM_INFO("Selecting Vulkan device: %s, Version: %d.%d.%d",
                   emulation->mDeviceInfo.physdevProps.deviceName, VK_VERSION_MAJOR(deviceVersion),
                   VK_VERSION_MINOR(deviceVersion), VK_VERSION_PATCH(deviceVersion));

    GFXSTREAM_DEBUG(
        "deviceInfo: \n"
        "hasGraphicsQueueFamily = %d\n"
        "hasComputeQueueFamily = %d\n"
        "supportsExternalMemoryImport = %d\n"
        "supportsExternalMemoryExport = %d\n"
        "supportsDriverProperties = %d\n"
        "hasSamplerYcbcrConversionExtension = %d\n"
        "supportsSamplerYcbcrConversion = %d\n"
        "glInteropSupported = %d",
        emulation->mDeviceInfo.hasGraphicsQueueFamily, emulation->mDeviceInfo.hasComputeQueueFamily,
        emulation->mDeviceInfo.supportsExternalMemoryImport,
        emulation->mDeviceInfo.supportsExternalMemoryExport,
        emulation->mDeviceInfo.supportsDriverProperties,
        emulation->mDeviceInfo.hasSamplerYcbcrConversionExtension,
        emulation->mDeviceInfo.supportsSamplerYcbcrConversion,
        emulation->mDeviceInfo.glInteropSupported);

    float priority = 1.0f;
    VkDeviceQueueCreateInfo dqCi = {
        VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,           0, 0,
        emulation->mDeviceInfo.graphicsQueueFamilyIndices[0], 1, &priority,
    };

    std::unordered_set<const char*> selectedDeviceExtensionNames_;

    if (emulation->mDeviceInfo.supportsExternalMemoryImport ||
        emulation->mDeviceInfo.supportsExternalMemoryExport) {
        for (auto extension : externalMemoryDeviceExtNames) {
            selectedDeviceExtensionNames_.emplace(extension);
        }
    }

#if defined(__linux__)
    if (emulation->mDeviceInfo.supportsDmaBuf) {
        selectedDeviceExtensionNames_.emplace(VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME);
    }
#endif

    // We need to always enable swapchain extensions to be able to use this device
    // to do VK_IMAGE_LAYOUT_PRESENT_SRC_KHR transition operations done
    // in releaseColorBufferForGuestUse for the apps using Vulkan swapchain
    selectedDeviceExtensionNames_.emplace(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

    if (emulation->mFeatures.VulkanNativeSwapchain.enabled) {
        for (auto extension : SwapChainStateVk::getRequiredDeviceExtensions()) {
            selectedDeviceExtensionNames_.emplace(extension);
        }
    }

    if (emulation->mDeviceInfo.hasSamplerYcbcrConversionExtension) {
        selectedDeviceExtensionNames_.emplace(VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
    }

#if defined(__APPLE__)
    if (useMoltenVK) {
        for (auto extension : moltenVkDeviceExtNames) {
            selectedDeviceExtensionNames_.emplace(extension);
        }
    }
#endif

    if (emulation->mDeviceInfo.robustness2Features) {
        selectedDeviceExtensionNames_.emplace(VK_EXT_ROBUSTNESS_2_EXTENSION_NAME);
    }

    std::vector<const char*> selectedDeviceExtensionNames(selectedDeviceExtensionNames_.begin(),
                                                          selectedDeviceExtensionNames_.end());

    VkDeviceCreateInfo dCi = {};
    dCi.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dCi.queueCreateInfoCount = 1;
    dCi.pQueueCreateInfos = &dqCi;
    dCi.enabledExtensionCount = static_cast<uint32_t>(selectedDeviceExtensionNames.size());
    dCi.ppEnabledExtensionNames = selectedDeviceExtensionNames.data();

    // Setting up VkDeviceCreateInfo::pNext
    auto deviceCiChain = vk_make_chain_iterator(&dCi);

    VkPhysicalDeviceFeatures2 physicalDeviceFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
    };
    vk_append_struct(&deviceCiChain, &physicalDeviceFeatures);

    std::unique_ptr<VkPhysicalDeviceSamplerYcbcrConversionFeatures> samplerYcbcrConversionFeatures =
        nullptr;
    if (emulation->mDeviceInfo.supportsSamplerYcbcrConversion) {
        samplerYcbcrConversionFeatures =
            std::make_unique<VkPhysicalDeviceSamplerYcbcrConversionFeatures>(
                VkPhysicalDeviceSamplerYcbcrConversionFeatures{
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES,
                    .samplerYcbcrConversion = VK_TRUE,
                });
        vk_append_struct(&deviceCiChain, samplerYcbcrConversionFeatures.get());
    }

#if defined(__QNX__)
    std::unique_ptr<VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX>
        extMemScreenBufferFeaturesQNX = nullptr;
    if (emulation->mDeviceInfo.supportsExternalMemoryImport) {
        extMemScreenBufferFeaturesQNX = std::make_unique<
            VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX>(
            VkPhysicalDeviceExternalMemoryScreenBufferFeaturesQNX{
                .sType =
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_SCREEN_BUFFER_FEATURES_QNX,
                .screenBufferImport = VK_TRUE,
            });
        vk_append_struct(&deviceCiChain, extMemScreenBufferFeaturesQNX.get());
    }
#endif

    const bool commandBufferCheckpointsSupported =
        emulation->mDeviceInfo.supportsNvidiaDeviceDiagnosticCheckpoints;
    const bool commandBufferCheckpointsRequested =
        emulation->mFeatures.VulkanCommandBufferCheckpoints.enabled;
    const bool commandBufferCheckpointsSupportedAndRequested =
        commandBufferCheckpointsSupported && commandBufferCheckpointsRequested;
    VkPhysicalDeviceDiagnosticsConfigFeaturesNV deviceDiagnosticsConfigFeatures = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DIAGNOSTICS_CONFIG_FEATURES_NV,
        .diagnosticsConfig = VK_TRUE,
    };
    if (commandBufferCheckpointsSupportedAndRequested) {
        GFXSTREAM_INFO(
            "Enabling command buffer checkpoints with VK_NV_device_diagnostic_checkpoints.");
        vk_append_struct(&deviceCiChain, &deviceDiagnosticsConfigFeatures);
    } else if (commandBufferCheckpointsRequested) {
        GFXSTREAM_WARNING(
            "VulkanCommandBufferCheckpoints was requested but the "
            "VK_NV_device_diagnostic_checkpoints extension is not supported.");
    }

    VkPhysicalDeviceRobustness2FeaturesEXT r2features = {};
    if (emulation->mDeviceInfo.robustness2Features) {
        r2features = *emulation->mDeviceInfo.robustness2Features;
        GFXSTREAM_INFO("Enabling VK_EXT_robustness2 (%d %d %d).", r2features.robustBufferAccess2,
                       r2features.robustImageAccess2, r2features.nullDescriptor);
        vk_append_struct(&deviceCiChain, &r2features);

        // vkCreateDevice() - VUID-04000: If robustBufferAccess2 is enabled then robustBufferAccess
        // must be enabled.
        if (r2features.robustBufferAccess2) {
            physicalDeviceFeatures.features.robustBufferAccess = VK_TRUE;
        }
    }

    ivk->vkCreateDevice(emulation->mPhysicalDevice, &dCi, nullptr, &emulation->mDevice);

    if (res != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create Vulkan device. Error %s.", string_VkResult(res));
        return nullptr;
    }

    // device created; populate dispatch table
    emulation->mDvk = new VulkanDispatch();
    init_vulkan_dispatch_from_device(ivk, emulation->mDevice, emulation->mDvk);

    auto dvk = emulation->mDvk;

    // Check if the dispatch table has everything 1.1 related
    if (!vulkan_dispatch_check_device_VK_VERSION_1_0(dvk)) {
        GFXSTREAM_ERROR("Warning: Vulkan 1.0 APIs missing from device.");
    }
    if (deviceVersion >= VK_MAKE_VERSION(1, 1, 0)) {
        if (!vulkan_dispatch_check_device_VK_VERSION_1_1(dvk)) {
            GFXSTREAM_ERROR("Warning: Vulkan 1.1 APIs missing from device");
        }
    }

    if (emulation->mDeviceInfo.supportsExternalMemoryImport) {
        emulation->mDeviceInfo.getImageMemoryRequirements2Func =
            reinterpret_cast<PFN_vkGetImageMemoryRequirements2KHR>(
                dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetImageMemoryRequirements2KHR"));
        if (!emulation->mDeviceInfo.getImageMemoryRequirements2Func) {
            GFXSTREAM_ERROR("Cannot find vkGetImageMemoryRequirements2KHR.");
            return nullptr;
        }
        emulation->mDeviceInfo.getBufferMemoryRequirements2Func =
            reinterpret_cast<PFN_vkGetBufferMemoryRequirements2KHR>(
                dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetBufferMemoryRequirements2KHR"));
        if (!emulation->mDeviceInfo.getBufferMemoryRequirements2Func) {
            GFXSTREAM_ERROR("Cannot find vkGetBufferMemoryRequirements2KHR");
            return nullptr;
        }
    }
    if (emulation->mDeviceInfo.supportsExternalMemoryExport) {
#ifdef _WIN32
        // Use vkGetMemoryWin32HandleKHR
        emulation->mDeviceInfo.getMemoryHandleFunc =
            reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
                dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetMemoryWin32HandleKHR"));
        if (!emulation->mDeviceInfo.getMemoryHandleFunc) {
            GFXSTREAM_ERROR("Cannot find vkGetMemoryWin32HandleKHR");
            return nullptr;
        }
#elif defined(__ANDROID__)
        // Use vkGetMemoryAndroidHardwareBufferANDROID
        emulation->mDeviceInfo.getMemoryHandleFunc =
            reinterpret_cast<PFN_vkGetMemoryAndroidHardwareBufferANDROID>(
                dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetMemoryAndroidHardwareBufferANDROID"));
        if (!emulation->mDeviceInfo.getMemoryHandleFunc) {
            GFXSTREAM_ERROR("Cannot find vkGetMemoryAndroidHardwareBufferANDROID");
            return nullptr;
        }
#else
        if (emulation->mInstanceSupportsMoltenVK) {
            // We'll use vkGetMemoryMetalHandleEXT, no need to save into getMemoryHandleFunc
            emulation->mDeviceInfo.getMemoryHandleFunc = nullptr;
            if (!dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetMemoryMetalHandleEXT")) {
                GFXSTREAM_ERROR("Cannot find vkGetMemoryMetalHandleEXT");
                return nullptr;
            }
        } else {
            // Use vkGetMemoryFdKHR
            emulation->mDeviceInfo.getMemoryHandleFunc = reinterpret_cast<PFN_vkGetMemoryFdKHR>(
                dvk->vkGetDeviceProcAddr(emulation->mDevice, "vkGetMemoryFdKHR"));
            if (!emulation->mDeviceInfo.getMemoryHandleFunc) {
                GFXSTREAM_ERROR("Cannot find vkGetMemoryFdKHR");
                return nullptr;
            }
        }
#endif
    }

    GFXSTREAM_DEBUG("Vulkan logical device created and extension functions obtained.");

    emulation->mQueueLock = std::make_shared<gfxstream::base::Lock>();
    {
        gfxstream::base::AutoLock queueLock(*emulation->mQueueLock);
        dvk->vkGetDeviceQueue(emulation->mDevice,
                              emulation->mDeviceInfo.graphicsQueueFamilyIndices[0], 0,
                              &emulation->mQueue);
    }

    emulation->mQueueFamilyIndex = emulation->mDeviceInfo.graphicsQueueFamilyIndices[0];

    GFXSTREAM_DEBUG("Vulkan device queue obtained.");

    VkCommandPoolCreateInfo poolCi = {
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        0,
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        emulation->mQueueFamilyIndex,
    };

    VkResult poolCreateRes =
        dvk->vkCreateCommandPool(emulation->mDevice, &poolCi, nullptr, &emulation->mCommandPool);

    if (poolCreateRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create command pool. Error: %s.", string_VkResult(poolCreateRes));
        return nullptr;
    }

    VkCommandBufferAllocateInfo cbAi = {
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        0,
        emulation->mCommandPool,
        VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        1,
    };

    VkResult cbAllocRes =
        dvk->vkAllocateCommandBuffers(emulation->mDevice, &cbAi, &emulation->mCommandBuffer);

    if (cbAllocRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to allocate command buffer. Error: %s.", string_VkResult(cbAllocRes));
        return nullptr;
    }

    VkFenceCreateInfo fenceCi = {
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        0,
        0,
    };

    VkResult fenceCreateRes =
        dvk->vkCreateFence(emulation->mDevice, &fenceCi, nullptr, &emulation->mCommandBufferFence);

    if (fenceCreateRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create fence for command buffer. Error: %s.",
                        string_VkResult(fenceCreateRes));
        return nullptr;
    }

    if (debugUtilsAvailableAndRequested) {
        emulation->mDebugUtilsAvailableAndRequested = true;
        emulation->mDebugUtilsHelper =
            DebugUtilsHelper::withUtilsEnabled(emulation->mDevice, emulation->mIvk);

        emulation->mDebugUtilsHelper.addDebugLabel(emulation->mInstance, "AEMU_Instance");
        emulation->mDebugUtilsHelper.addDebugLabel(emulation->mDevice, "AEMU_Device");
        emulation->mDebugUtilsHelper.addDebugLabel(emulation->mCommandBuffer, "AEMU_CommandBuffer");
    }

    if (commandBufferCheckpointsSupportedAndRequested) {
        emulation->mCommandBufferCheckpointsSupportedAndRequested = true;
        emulation->mDeviceLostHelper.enableWithNvidiaDeviceDiagnosticCheckpoints();
    }

    // Create a staging buffer for color buffer copy/update operations
    if (!emulation->mStaging.create(dvk, emulation->mDevice,
                                    &emulation->mDeviceInfo.memProps,
                                    emulation->mDebugUtilsHelper,
                                    kDefaultStagingBufferSize)) {
        GFXSTREAM_FATAL("Failed: Could not allocate staging buffer for Vulkan emulation");
    }

    GFXSTREAM_VERBOSE("Vulkan global emulation state successfully initialized.");

    emulation->mTransferQueueCommandBufferPool.resize(0);

    return emulation;
}

void VkEmulation::initFeatures(Features features) {
    std::lock_guard<std::mutex> lock(mMutex);
    GFXSTREAM_INFO("Initializing VkEmulation features:");
    GFXSTREAM_INFO("    glInteropSupported: %s", features.glInteropSupported ? "true" : "false");
    GFXSTREAM_INFO("    useDeferredCommands: %s", features.deferredCommands ? "true" : "false");
    GFXSTREAM_INFO("    createResourceWithRequirements: %s",
                   features.createResourceWithRequirements ? "true" : "false");
    GFXSTREAM_INFO("    useVulkanComposition: %s",
                   features.useVulkanComposition ? "true" : "false");
    GFXSTREAM_INFO("    useVulkanNativeSwapchain: %s",
                   features.useVulkanNativeSwapchain ? "true" : "false");
    GFXSTREAM_INFO("    enable guestRenderDoc: %s", features.guestRenderDoc ? "true" : "false");
    GFXSTREAM_INFO("    ASTC LDR emulation mode: %s",
                   string_AstcEmulationMode(features.astcLdrEmulationMode));
    GFXSTREAM_INFO("    enable ETC2 emulation: %s",
                   features.enableEtc2Emulation ? "true" : "false");
    GFXSTREAM_INFO("    enable Ycbcr emulation: %s",
                   features.enableYcbcrEmulation ? "true" : "false");
    GFXSTREAM_INFO("    guestVulkanOnly: %s", features.guestVulkanOnly ? "true" : "false");
    GFXSTREAM_INFO("    useDedicatedAllocations: %s",
                   features.useDedicatedAllocations ? "true" : "false");
    mDeviceInfo.glInteropSupported = features.glInteropSupported;
    mUseDeferredCommands = features.deferredCommands;
    mUseCreateResourcesWithRequirements = features.createResourceWithRequirements;
    mGuestRenderDoc = std::move(features.guestRenderDoc);
    mAstcLdrEmulationMode = features.astcLdrEmulationMode;
    mEnableEtc2Emulation = features.enableEtc2Emulation;
    mEnableYcbcrEmulation = features.enableYcbcrEmulation;
    mGuestVulkanOnly = features.guestVulkanOnly;
    mUseDedicatedAllocations = features.useDedicatedAllocations;

    if (features.useVulkanComposition) {
        if (mCompositorVk) {
            GFXSTREAM_ERROR("Reset VkEmulation::compositorVk.");
        }
        mCompositorVk = CompositorVk::create(*mIvk, mDevice, mPhysicalDevice, mQueue, mQueueLock,
                                             mQueueFamilyIndex, 3, mDebugUtilsHelper);
    }

    if (features.useVulkanNativeSwapchain) {
        if (mDisplayVk) {
            GFXSTREAM_ERROR("Reset VkEmulation::displayVk.");
        }
        mDisplayVk = std::make_unique<DisplayVk>(*mIvk, mPhysicalDevice, mQueueFamilyIndex,
                                                 mQueueFamilyIndex, mDevice, mQueue, mQueueLock,
                                                 mQueue, mQueueLock);
    }

    auto representativeInfo = findRepresentativeColorBufferMemoryTypeIndexLocked();
    if (!representativeInfo) {
        GFXSTREAM_FATAL("Failed to find memory type for ColorBuffers.");
    }
    mRepresentativeColorBufferMemoryTypeInfo = *representativeInfo;
    GFXSTREAM_DEBUG(
        "Representative ColorBuffer memory type using host memory type index %d "
        "and guest memory type index :%d",
        mRepresentativeColorBufferMemoryTypeInfo.hostMemoryTypeIndex,
        mRepresentativeColorBufferMemoryTypeInfo.guestMemoryTypeIndex);
}

VkEmulation::~VkEmulation() {
    std::lock_guard<std::mutex> lock(mMutex);

    mCompositorVk.reset();
    mDisplayVk.reset();

    mStaging.destroy(mDvk, mDevice);

    mDvk->vkDestroyFence(mDevice, mCommandBufferFence, nullptr);
    mDvk->vkFreeCommandBuffers(mDevice, mCommandPool, 1, &mCommandBuffer);
    mDvk->vkDestroyCommandPool(mDevice, mCommandPool, nullptr);

    mIvk->vkDestroyDevice(mDevice, nullptr);

    mGvk->vkDestroyInstance(mInstance, nullptr);
}

bool VkEmulation::isYcbcrEmulationEnabled() const { return mEnableYcbcrEmulation; }

bool VkEmulation::isEtc2EmulationEnabled() const { return mEnableEtc2Emulation; }

bool VkEmulation::deferredCommandsEnabled() const { return mUseDeferredCommands; }

bool VkEmulation::createResourcesWithRequirementsEnabled() const {
    return mUseCreateResourcesWithRequirements;
}

bool VkEmulation::supportsGetPhysicalDeviceProperties2() const {
    return mInstanceSupportsGetPhysicalDeviceProperties2;
}

bool VkEmulation::supportsExternalMemoryCapabilities() const {
    return mInstanceSupportsExternalMemoryCapabilities;
}

bool VkEmulation::supportsExternalSemaphoreCapabilities() const {
    return mInstanceSupportsExternalSemaphoreCapabilities;
}

bool VkEmulation::supportsExternalFenceCapabilities() const {
    return mInstanceSupportsExternalFenceCapabilities;
}

bool VkEmulation::supportsSurfaces() const { return mInstanceSupportsSurface; }

bool VkEmulation::supportsMoltenVk() const { return mInstanceSupportsMoltenVK; }

bool VkEmulation::supportsPhysicalDeviceIDProperties() const {
    return mInstanceSupportsPhysicalDeviceIDProperties;
}

bool VkEmulation::supportsPrivateData() const { return mDeviceInfo.supportsPrivateData; }

bool VkEmulation::supportsExternalMemoryImport() const {
    return mDeviceInfo.supportsExternalMemoryImport;
}

bool VkEmulation::supportsDmaBuf() const { return mDeviceInfo.supportsDmaBuf; }

bool VkEmulation::supportsExternalMemoryHostProperties() const {
    return mDeviceInfo.supportsExternalMemoryHostProps;
}

std::optional<VkPhysicalDeviceRobustness2FeaturesEXT> VkEmulation::getRobustness2Features() const {
    return mDeviceInfo.robustness2Features;
}

VkPhysicalDeviceExternalMemoryHostPropertiesEXT VkEmulation::externalMemoryHostProperties() const {
    return mDeviceInfo.externalMemoryHostProps;
}

bool VkEmulation::isGuestVulkanOnly() const { return mGuestVulkanOnly; }

bool VkEmulation::commandBufferCheckpointsEnabled() const {
    return mCommandBufferCheckpointsSupportedAndRequested;
}

bool VkEmulation::supportsSamplerYcbcrConversion() const {
    return mDeviceInfo.supportsSamplerYcbcrConversion;
}

bool VkEmulation::debugUtilsEnabled() const { return mDebugUtilsAvailableAndRequested; }

DebugUtilsHelper& VkEmulation::getDebugUtilsHelper() { return mDebugUtilsHelper; }

DeviceLostHelper& VkEmulation::getDeviceLostHelper() { return mDeviceLostHelper; }

const gfxstream::host::FeatureSet& VkEmulation::getFeatures() const { return mFeatures; }

const gfxstream::host::BackendCallbacks& VkEmulation::getCallbacks() const { return mCallbacks; }

AstcEmulationMode VkEmulation::getAstcLdrEmulationMode() const { return mAstcLdrEmulationMode; }

gfxstream::host::RenderDocWithMultipleVkInstances* VkEmulation::getRenderDoc() {
    return mGuestRenderDoc.get();
}

Compositor* VkEmulation::getCompositor() { return mCompositorVk.get(); }

DisplayVk* VkEmulation::getDisplay() { return mDisplayVk.get(); }

VkInstance VkEmulation::getInstance() { return mInstance; }

std::optional<std::array<uint8_t, VK_UUID_SIZE>> VkEmulation::getDeviceUuid() {
    if (!supportsPhysicalDeviceIDProperties()) {
        return std::nullopt;
    }

    std::array<uint8_t, VK_UUID_SIZE> uuid;
    std::memcpy(uuid.data(), mDeviceInfo.idProps.deviceUUID, VK_UUID_SIZE);
    return uuid;
}

std::optional<std::array<uint8_t, VK_UUID_SIZE>> VkEmulation::getDriverUuid() {
    if (!supportsPhysicalDeviceIDProperties()) {
        return std::nullopt;
    }

    std::array<uint8_t, VK_UUID_SIZE> uuid;
    std::memcpy(uuid.data(), mDeviceInfo.idProps.driverUUID, VK_UUID_SIZE);
    return uuid;
}

std::string VkEmulation::getGpuVendor() const { return mDeviceInfo.driverVendor; }

std::string VkEmulation::getGpuName() const { return mDeviceInfo.physdevProps.deviceName; }

std::string VkEmulation::getGpuVersionString() const {
    std::stringstream builder;
    builder << "Vulkan "                                            //
            << VK_API_VERSION_MAJOR(mVulkanInstanceVersion) << "."  //
            << VK_API_VERSION_MINOR(mVulkanInstanceVersion) << "."  //
            << VK_API_VERSION_PATCH(mVulkanInstanceVersion) << " "  //
            << getGpuVendor() << " "                                //
            << getGpuName();
    return builder.str();
}

std::string VkEmulation::getInstanceExtensionsString() const {
    std::stringstream builder;
    for (const auto& instanceExtension : mInstanceExtensions) {
        if (builder.tellp() != 0) {
            builder << " ";
        }
        builder << instanceExtension.extensionName;
    }
    return builder.str();
}

std::string VkEmulation::getDeviceExtensionsString() const {
    std::stringstream builder;
    for (const auto& deviceExtension : mDeviceInfo.extensions) {
        if (builder.tellp() != 0) {
            builder << " ";
        }
        builder << deviceExtension.extensionName;
    }
    return builder.str();
}

const VkPhysicalDeviceProperties VkEmulation::getPhysicalDeviceProperties() const {
    return mDeviceInfo.physdevProps;
}

VkEmulation::RepresentativeColorBufferMemoryTypeInfo
VkEmulation::getRepresentativeColorBufferMemoryTypeInfo() const {
    return mRepresentativeColorBufferMemoryTypeInfo;
}

void VkEmulation::onVkDeviceLost() { VkDecoderGlobalState::get()->on_DeviceLost(); }

std::unique_ptr<gfxstream::DisplaySurface> VkEmulation::createDisplaySurface(
    FBNativeWindowType window, uint32_t width, uint32_t height) {
    auto surfaceVk = DisplaySurfaceVk::create(*mIvk, mInstance, window);
    if (!surfaceVk) {
        GFXSTREAM_ERROR("Failed to create DisplaySurfaceVk.");
        return nullptr;
    }

    return std::make_unique<gfxstream::DisplaySurface>(width, height, std::move(surfaceVk));
}

#ifdef __APPLE__
MTLResource_id VkEmulation::getMtlResourceFromVkDeviceMemory(VulkanDispatch* vk,
                                                             VkDeviceMemory memory) {
    if (memory == VK_NULL_HANDLE) {
        GFXSTREAM_WARNING("Requested metal resource handle for null memory!");
        return nullptr;
    }

    VkMemoryGetMetalHandleInfoEXT getMetalHandleInfo = {
        VK_STRUCTURE_TYPE_MEMORY_GET_METAL_HANDLE_INFO_EXT,
        nullptr,
        memory,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT
    };

    MTLResource_id outputHandle = nullptr;
    vk->vkGetMemoryMetalHandleEXT(mDevice, &getMetalHandleInfo, &outputHandle);
    if (outputHandle == nullptr) {
        GFXSTREAM_ERROR("vkGetMemoryMetalHandleEXT returned null");
    }
    return outputHandle;
}
#endif

// Precondition: sVkEmulation has valid device support info
bool VkEmulation::allocExternalMemory(VulkanDispatch* vk, VkEmulation::ExternalMemoryInfo* info,
                                      Optional<uint64_t> deviceAlignment,
                                      Optional<VkBuffer> bufferForDedicatedAllocation,
                                      Optional<VkImage> imageForDedicatedAllocation) {
    VkExportMemoryAllocateInfo exportAi = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .handleTypes =
            static_cast<VkExternalMemoryHandleTypeFlags>(getDefaultExternalMemoryHandleType()),
    };

    VkMemoryDedicatedAllocateInfo dedicatedAllocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext = nullptr,
        .image = VK_NULL_HANDLE,
        .buffer = VK_NULL_HANDLE,
    };

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = info->size,
        .memoryTypeIndex = info->typeIndex,
    };

    auto allocInfoChain = vk_make_chain_iterator(&allocInfo);

    if (mDeviceInfo.supportsExternalMemoryExport) {
#ifdef __APPLE__
        if (mInstanceSupportsMoltenVK) {
            // Change handle type for metal resources
            exportAi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
        }
#endif
        if (mDeviceInfo.supportsDmaBuf) {
            exportAi.handleTypes |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        }

        vk_append_struct(&allocInfoChain, &exportAi);
    }

    if (bufferForDedicatedAllocation.hasValue() || imageForDedicatedAllocation.hasValue()) {
        info->dedicatedAllocation = true;
        if (bufferForDedicatedAllocation.hasValue()) {
            dedicatedAllocInfo.buffer = *bufferForDedicatedAllocation;
        }
        if (imageForDedicatedAllocation.hasValue()) {
            dedicatedAllocInfo.image = *imageForDedicatedAllocation;
        }
        vk_append_struct(&allocInfoChain, &dedicatedAllocInfo);
    }

    bool memoryAllocated = false;
    std::vector<VkDeviceMemory> allocationAttempts;
    constexpr size_t kMaxAllocationAttempts = 20u;

    while (!memoryAllocated) {
        VkResult allocRes = vk->vkAllocateMemory(mDevice, &allocInfo, nullptr, &info->memory);

        if (allocRes != VK_SUCCESS) {
            GFXSTREAM_DEBUG("allocExternalMemory: failed in vkAllocateMemory: %s",
                            string_VkResult(allocRes));
            break;
        }

        if (mDeviceInfo.memProps.memoryTypes[info->typeIndex].propertyFlags &
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
            VkResult mapRes =
                vk->vkMapMemory(mDevice, info->memory, 0, info->size, 0, &info->mappedPtr);
            if (mapRes != VK_SUCCESS) {
                GFXSTREAM_DEBUG("allocExternalMemory: failed in vkMapMemory: %s",
                                string_VkResult(mapRes));
                break;
            }
        }

        uint64_t mappedPtrPageOffset = reinterpret_cast<uint64_t>(info->mappedPtr) % kPageSize;

        if (  // don't care about alignment (e.g. device-local memory)
            !deviceAlignment.hasValue() ||
            // If device has an alignment requirement larger than current
            // host pointer alignment (i.e. the lowest 1 bit of mappedPtr),
            // the only possible way to make mappedPtr valid is to ensure
            // that it is already aligned to page.
            mappedPtrPageOffset == 0u ||
            // If device has an alignment requirement smaller or equals to
            // current host pointer alignment, clients can set a offset
            // |kPageSize - mappedPtrPageOffset| in vkBindImageMemory to
            // make it aligned to page and compatible with device
            // requirements.
            (kPageSize - mappedPtrPageOffset) % deviceAlignment.value() == 0) {
            // allocation success.
            memoryAllocated = true;
        } else {
            allocationAttempts.push_back(info->memory);

            GFXSTREAM_DEBUG("allocExternalMemory: attempt #%zu failed; deviceAlignment: %" PRIu64
                            ", mappedPtrPageOffset: %" PRIu64,
                            __func__, allocationAttempts.size(), deviceAlignment.valueOr(0),
                            mappedPtrPageOffset);

            if (allocationAttempts.size() >= kMaxAllocationAttempts) {
                GFXSTREAM_DEBUG(
                    "allocExternalMemory: unable to allocate memory with CPU mapped ptr aligned to "
                    "page");
                break;
            }
        }
    }

    // clean up previous failed attempts
    for (const auto& mem : allocationAttempts) {
        vk->vkFreeMemory(mDevice, mem, nullptr /* allocator */);
    }
    if (!memoryAllocated) {
        return false;
    }

    if (!mDeviceInfo.supportsExternalMemoryExport) {
        return true;
    }

    uint32_t streamHandleType = 0;
    VkResult exportRes = VK_SUCCESS;
    bool validHandle = false;
#ifdef _WIN32
    VkMemoryGetWin32HandleInfoKHR getWin32HandleInfo = {
        VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
        0,
        info->memory,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
    };

    HANDLE exportHandle = NULL;
    exportRes = mDeviceInfo.getMemoryHandleFunc(mDevice, &getWin32HandleInfo, &exportHandle);
    validHandle = (VK_SUCCESS == exportRes) && (NULL != exportHandle);
    info->handleInfo = ExternalHandleInfo{
        .handle = reinterpret_cast<ExternalHandleType>(exportHandle),
        .streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_WIN32,
    };
#elif defined(__ANDROID__)
    VkMemoryGetAndroidHardwareBufferInfoANDROID getAhbInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_GET_ANDROID_HARDWARE_BUFFER_INFO_ANDROID,
        .pNext = nullptr,
        .memory = info->memory,
    };
    AHardwareBuffer* exportHandle = static_cast<AHardwareBuffer*>(reinterpret_cast<void*>(info->handleInfo->handle));
    exportRes = mDeviceInfo.getMemoryHandleFunc(
        mDevice, &getAhbInfo, &exportHandle);
    validHandle = (VK_SUCCESS == exportRes) && (NULL != exportHandle);
    info->handleInfo = ExternalHandleInfo{
        .handle = reinterpret_cast<ExternalHandleType>(exportHandle),
    };
#else

    bool opaqueFd = true;
#if defined(__APPLE__)
    if (mInstanceSupportsMoltenVK) {
        opaqueFd = false;
        info->externalMetalHandle = getMtlResourceFromVkDeviceMemory(vk, info->memory);
        validHandle = (nullptr != info->externalMetalHandle);
        if (validHandle) {
            CFRetain(info->externalMetalHandle);
            exportRes = VK_SUCCESS;
        } else {
            exportRes = VK_ERROR_INVALID_EXTERNAL_HANDLE;
        }
    }
#endif

    if (opaqueFd) {
        streamHandleType = STREAM_HANDLE_TYPE_MEM_OPAQUE_FD;
        VkExternalMemoryHandleTypeFlagBits vkHandleType =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        if (mDeviceInfo.supportsDmaBuf) {
            vkHandleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
            streamHandleType = STREAM_HANDLE_TYPE_MEM_DMABUF;
        }

        VkMemoryGetFdInfoKHR getFdInfo = {
            VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
            0,
            info->memory,
            vkHandleType,
        };
        int exportFd = -1;
        exportRes = mDeviceInfo.getMemoryHandleFunc(mDevice, &getFdInfo, &exportFd);
        validHandle = (VK_SUCCESS == exportRes) && (-1 != exportFd);
        info->handleInfo = ExternalHandleInfo{
            .handle = exportFd,
            .streamHandleType = streamHandleType,
        };
    }
#endif

    if (exportRes != VK_SUCCESS || !validHandle) {
        GFXSTREAM_WARNING("%s: Failed to get external memory, result: %s", __func__,
                          string_VkResult(exportRes));
        return false;
    }

    return true;
}

void VkEmulation::freeExternalMemoryLocked(VulkanDispatch* vk,
                                           VkEmulation::ExternalMemoryInfo* info) {
    if (!info->memory) return;

    if (mDeviceInfo.memProps.memoryTypes[info->typeIndex].propertyFlags &
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
        if (mOccupiedGpas.find(info->gpa) != mOccupiedGpas.end()) {
            mOccupiedGpas.erase(info->gpa);
            get_gfxstream_vm_operations().unmap_user_memory_async(info->gpa, info->sizeToPage);
            info->gpa = 0u;
        }

        if (info->mappedPtr != nullptr) {
            vk->vkUnmapMemory(mDevice, info->memory);
            info->mappedPtr = nullptr;
            info->pageAlignedHva = nullptr;
        }
    }

    vk->vkFreeMemory(mDevice, info->memory, nullptr);

    info->memory = VK_NULL_HANDLE;

    if (info->handleInfo) {
#ifdef _WIN32
        CloseHandle(static_cast<HANDLE>(reinterpret_cast<void*>(info->handleInfo->handle)));
#else
        switch (info->handleInfo->streamHandleType) {
            case STREAM_HANDLE_TYPE_MEM_OPAQUE_FD:
            case STREAM_HANDLE_TYPE_MEM_DMABUF:
                close(info->handleInfo->handle);
                break;
            case STREAM_HANDLE_TYPE_PLATFORM_SCREEN_BUFFER_QNX:
            default:
                break;
        }
#endif
        info->handleInfo = std::nullopt;
    }

#if defined(__APPLE__)
    if (info->externalMetalHandle) {
        CFRelease(info->externalMetalHandle);
    }
#endif
}

bool VkEmulation::importExternalMemory(VulkanDispatch* vk, VkDevice targetDevice,
                                       const VkEmulation::ExternalMemoryInfo* info,
                                       VkMemoryDedicatedAllocateInfo* dedicatedAllocInfoPtr,
                                       VkDeviceMemory* out) {
    const void* importInfoPtr = nullptr;
    auto handleInfo = info->handleInfo;
#ifdef _WIN32
    if (!handleInfo) {
        GFXSTREAM_ERROR(
            "importExternalMemory: external handle info is not available, cannot retrieve win32 "
            "handle.");
        return false;
    }
    VkImportMemoryWin32HandleInfoKHR importInfo = {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        dedicatedAllocInfoPtr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        static_cast<HANDLE>(reinterpret_cast<void*>(handleInfo->handle)),
        0,
    };
    importInfoPtr = &importInfo;
#elif defined(__QNX__)
    if (!handleInfo) {
        GFXSTREAM_ERROR(
            "importExternalMemory: external handle info is not available, cannot retrieve "
            "screen_buffer_t handle.");
        return false;
    }
    VkImportScreenBufferInfoQNX importInfo = {
        VK_STRUCTURE_TYPE_IMPORT_SCREEN_BUFFER_INFO_QNX,
        dedicatedAllocInfoPtr,
        static_cast<screen_buffer_t>(reinterpret_cast<void*>(handleInfo->handle)),
    };
    importInfoPtr = &importInfo;
#elif defined(__APPLE__)
    VkImportMemoryMetalHandleInfoEXT importInfoMetalInfo = {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_METAL_HANDLE_INFO_EXT, dedicatedAllocInfoPtr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT, nullptr};
    if (mInstanceSupportsMoltenVK) {
        importInfoMetalInfo.handle = info->externalMetalHandle;
        importInfoPtr = &importInfoMetalInfo;
    }
#endif

    VkImportMemoryFdInfoKHR importInfoFd = {
        VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        dedicatedAllocInfoPtr,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT,
        -1,
    };
    if (!importInfoPtr) {
        if (!handleInfo) {
            GFXSTREAM_ERROR(
                "importExternalMemory: external handle info is not available, cannot retrieve "
                "information required to duplicate the external handle.");
            return false;
        }
        auto dupHandle = dupExternalMemory(handleInfo);
        if (!dupHandle) {
            GFXSTREAM_ERROR(
                "importExternalMemory: Failed to duplicate handleInfo.handle: 0x%x, "
                "streamHandleType: %d",
                handleInfo->handle, handleInfo->streamHandleType);
            return false;
        }
        importInfoFd.fd = dupHandle->handle;
        importInfoPtr = &importInfoFd;
    }

    VkMemoryAllocateInfo allocInfo = {
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        importInfoPtr,
        info->size,
        info->typeIndex,
    };

    VkResult res = vk->vkAllocateMemory(targetDevice, &allocInfo, nullptr, out);

    if (res != VK_SUCCESS) {
        GFXSTREAM_ERROR("importExternalMemory: Failed with %s", string_VkResult(res));
        return false;
    }

    return true;
}

// From ANGLE "src/common/angleutils.h"
#define GL_BGR10_A2_ANGLEX 0x6AF9

static VkFormat glFormat2VkFormat(GLint internalFormat) {
    switch (internalFormat) {
        case GL_R8:
        case GL_LUMINANCE:
            return VK_FORMAT_R8_UNORM;
        case GL_RGB:
        case GL_RGB8:
            // b/281550953
            // RGB8 is not supported on many vulkan drivers.
            // Try RGBA8 instead.
            // Note: updateColorBufferFromBytesLocked() performs channel conversion for this case.
            return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_RGB565:
            return VK_FORMAT_R5G6B5_UNORM_PACK16;
        case GL_RGB16F:
            return VK_FORMAT_R16G16B16_SFLOAT;
        case GL_RGBA:
        case GL_RGBA8:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case GL_RGB5_A1_OES:
            return VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        case GL_RGBA4_OES: {
            // TODO: add R4G4B4A4 support to lavapipe, and check support programmatically
            const bool lavapipe =
                (gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD").compare("lavapipe") ==
                 0);
            if (lavapipe) {
                // RGBA4 is not supported on lavapipe, use more widely available BGRA4 instead.
                // Note: updateColorBufferFromBytesLocked() performs channel conversion for this
                // case.
                return VK_FORMAT_B4G4R4A4_UNORM_PACK16;
            }
            return VK_FORMAT_R4G4B4A4_UNORM_PACK16;
        }
        case GL_RGB10_A2:
        case GL_UNSIGNED_INT_10_10_10_2_OES:
            return VK_FORMAT_A2R10G10B10_UNORM_PACK32;
        case GL_BGR10_A2_ANGLEX:
            return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case GL_RGBA16F:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case GL_BGRA_EXT:
        case GL_BGRA8_EXT:
            return VK_FORMAT_B8G8R8A8_UNORM;
        case GL_R16_EXT:
            return VK_FORMAT_R16_UNORM;
        case GL_RG8_EXT:
            return VK_FORMAT_R8G8_UNORM;
        case GL_DEPTH_COMPONENT16:
            return VK_FORMAT_D16_UNORM;
        case GL_DEPTH_COMPONENT24:
            return VK_FORMAT_X8_D24_UNORM_PACK32;
        case GL_DEPTH24_STENCIL8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case GL_DEPTH_COMPONENT32F:
            return VK_FORMAT_D32_SFLOAT;
        case GL_DEPTH32F_STENCIL8:
            return VK_FORMAT_D32_SFLOAT_S8_UINT;
        default:
            GFXSTREAM_ERROR("Unhandled format %d, falling back to VK_FORMAT_R8G8B8A8_UNORM",
                            internalFormat);
            return VK_FORMAT_R8G8B8A8_UNORM;
    }
};

bool VkEmulation::isFormatVulkanCompatible(GLenum internalFormat) {
    VkFormat vkFormat = glFormat2VkFormat(internalFormat);

    for (const auto& supportInfo : mImageSupportInfo) {
        if (supportInfo.format == vkFormat && supportInfo.supported) {
            return true;
        }
    }

    return false;
}

bool VkEmulation::getColorBufferShareInfo(uint32_t colorBufferHandle, bool* glExported,
                                          bool* externalMemoryCompatible) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto info = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!info) {
        return false;
    }

    *glExported = info->glExported;
    *externalMemoryCompatible = info->externalMemoryCompatible;
    return true;
}

bool VkEmulation::getColorBufferAllocationInfoLocked(uint32_t colorBufferHandle,
                                                     VkDeviceSize* outSize,
                                                     uint32_t* outMemoryTypeIndex,
                                                     bool* outMemoryIsDedicatedAlloc,
                                                     void** outMappedPtr) {
    auto info = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!info) {
        return false;
    }

    if (outSize) {
        *outSize = info->memory.size;
    }

    if (outMemoryTypeIndex) {
        *outMemoryTypeIndex = info->memory.typeIndex;
    }

    if (outMemoryIsDedicatedAlloc) {
        *outMemoryIsDedicatedAlloc = info->memory.dedicatedAllocation;
    }

    if (outMappedPtr) {
        *outMappedPtr = info->memory.mappedPtr;
    }

    return true;
}

bool VkEmulation::getColorBufferAllocationInfo(uint32_t colorBufferHandle, VkDeviceSize* outSize,
                                               uint32_t* outMemoryTypeIndex,
                                               bool* outMemoryIsDedicatedAlloc,
                                               void** outMappedPtr) {
    std::lock_guard<std::mutex> lock(mMutex);
    return getColorBufferAllocationInfoLocked(colorBufferHandle, outSize, outMemoryTypeIndex,
                                              outMemoryIsDedicatedAlloc, outMappedPtr);
}

// This function will return the first memory type that exactly matches the
// requested properties, if there is any. Otherwise it'll return the last
// index that supports all the requested memory property flags.
// Eg. this avoids returning a host coherent memory type when only device local
// memory flag is requested, which may be slow or not support some other features,
// such as association with optimal-tiling images on some implementations.
uint32_t VkEmulation::getValidMemoryTypeIndex(uint32_t requiredMemoryTypeBits,
                                              VkMemoryPropertyFlags memoryProperty) {
    uint32_t secondBest = ~0;
    bool found = false;
    for (int32_t i = 0; i <= 31; i++) {
        if ((requiredMemoryTypeBits & (1u << i)) == 0) {
            // Not a suitable memory index
            continue;
        }

        const VkMemoryPropertyFlags memPropertyFlags =
            mDeviceInfo.memProps.memoryTypes[i].propertyFlags;

        // Exact match, return immediately
        if (memPropertyFlags == memoryProperty) {
            return i;
        }

        // Valid memory index, but keep  looking for an exact match
        // TODO: this should compare against memoryProperty, but some existing tests
        // are depending on this behavior.
        const bool propertyValid = !memoryProperty || ((memPropertyFlags & memoryProperty) != 0);
        if (propertyValid) {
            secondBest = i;
            found = true;
        }
    }

    if (!found) {
        const std::string memoryPropertyString = string_VkMemoryPropertyFlags(memoryProperty);
        GFXSTREAM_FATAL("Could not find a valid memory index with memoryProperty:%s "
                        ", and requiredMemoryTypeBits:%" PRIu32,
                        memoryPropertyString.c_str(), requiredMemoryTypeBits);
    }
    return secondBest;
}

// pNext, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, and initialLayout won't be
// filled.
std::unique_ptr<VkImageCreateInfo> VkEmulation::generateColorBufferVkImageCreateInfoLocked(
    VkFormat format, uint32_t width, uint32_t height, VkImageTiling tiling) {
    const VkEmulation::ImageSupportInfo* maybeImageSupportInfo = nullptr;
    for (const auto& supportInfo : mImageSupportInfo) {
        if (supportInfo.format == format && supportInfo.supported) {
            maybeImageSupportInfo = &supportInfo;
            break;
        }
    }
    if (!maybeImageSupportInfo) {
        GFXSTREAM_ERROR("Format %s [%d] is not supported.", string_VkFormat(format), format);
        return nullptr;
    }
    const VkEmulation::ImageSupportInfo& imageSupportInfo = *maybeImageSupportInfo;
    const VkFormatProperties& formatProperties = imageSupportInfo.formatProps2.formatProperties;

    constexpr std::pair<VkFormatFeatureFlags, VkImageUsageFlags> formatUsagePairs[] = {
        {VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT,
         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT},
        {VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT,
         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT},
        {VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, VK_IMAGE_USAGE_SAMPLED_BIT},
        {VK_FORMAT_FEATURE_TRANSFER_SRC_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        {VK_FORMAT_FEATURE_TRANSFER_DST_BIT, VK_IMAGE_USAGE_TRANSFER_DST_BIT},
        {VK_FORMAT_FEATURE_BLIT_SRC_BIT, VK_IMAGE_USAGE_TRANSFER_SRC_BIT},
    };
    VkFormatFeatureFlags tilingFeatures = (tiling == VK_IMAGE_TILING_OPTIMAL)
                                              ? formatProperties.optimalTilingFeatures
                                              : formatProperties.linearTilingFeatures;

    VkImageUsageFlags usage = 0;
    for (const auto& formatUsage : formatUsagePairs) {
        usage |= (tilingFeatures & formatUsage.first) ? formatUsage.second : 0u;
    }

    return std::make_unique<VkImageCreateInfo>(VkImageCreateInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        // The caller is responsible to fill pNext.
        .pNext = nullptr,
        .flags = imageSupportInfo.createFlags,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent =
            {
                .width = width,
                .height = height,
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = tiling,
        .usage = usage,
        // The caller is responsible to fill sharingMode.
        .sharingMode = VK_SHARING_MODE_MAX_ENUM,
        // The caller is responsible to fill queueFamilyIndexCount.
        .queueFamilyIndexCount = 0,
        // The caller is responsible to fill pQueueFamilyIndices.
        .pQueueFamilyIndices = nullptr,
        // The caller is responsible to fill initialLayout.
        .initialLayout = VK_IMAGE_LAYOUT_MAX_ENUM,
    });
}

std::unique_ptr<VkImageCreateInfo> VkEmulation::generateColorBufferVkImageCreateInfo(
    VkFormat format, uint32_t width, uint32_t height, VkImageTiling tiling) {
    std::lock_guard<std::mutex> lock(mMutex);
    return generateColorBufferVkImageCreateInfoLocked(format, width, height, tiling);
}

bool VkEmulation::updateMemReqsForExtMem(std::optional<ExternalHandleInfo> extMemHandleInfo,
                                         VkMemoryRequirements* pMemReqs) {
#if defined(__QNX__)
    if (STREAM_HANDLE_TYPE_PLATFORM_SCREEN_BUFFER_QNX == extMemHandleInfo->streamHandleType) {
        VkScreenBufferPropertiesQNX screenBufferProps = {
            VK_STRUCTURE_TYPE_SCREEN_BUFFER_PROPERTIES_QNX,
            0,
        };
        VkResult queryRes = dvk->vkGetScreenBufferPropertiesQNX(
            device, (screen_buffer_t)extMemHandleInfo->handle, &screenBufferProps);
        if (VK_SUCCESS != queryRes) {
            GFXSTREAM_ERROR("Failed to get QNX Screen Buffer properties, VK error: %s",
                            string_VkResult(queryRes));
            return false;
        }
        if (screenBufferProps.allocationSize < pMemReqs->size) {
            GFXSTREAM_ERROR(
                "QNX Screen buffer allocationSize (0x%lx) is not large enough for ColorBuffer "
                "image "
                "size requirements (0x%lx)",
                screenBufferProps.allocationSize, pMemReqs->size);
            return false;
        }
        // Change memory requirements to the actual allocationSize; this may be larger
        // than the original memory requirements
        pMemReqs->size = screenBufferProps.allocationSize;
        // Mask the memoryTypeBits with the ones available for screen_buffer import
        pMemReqs->memoryTypeBits = screenBufferProps.memoryTypeBits;
    }
#endif

    return true;
}

// TODO(liyl): Currently we can only specify required memoryProperty
// and initial layout for a color buffer.
//
// Ideally we would like to specify a memory type index directly from
// localAllocInfo.memoryTypeIndex when allocating color buffers in
// vkAllocateMemory(). But this type index mechanism breaks "Modify the
// allocation size and type index to suit the resulting image memory
// size." which seems to be needed to keep the Android/Fuchsia guest
// memory type index consistent across guest allocations, and without
// which those guests might end up import allocating from a color buffer
// with mismatched type indices.
//
// We should make it so the guest can only allocate external images/
// buffers of one type index for image and one type index for buffer
// to begin with, via filtering from the host.

bool VkEmulation::createVkColorBufferLocked(uint32_t width, uint32_t height, GLenum internalFormat,
                                            FrameworkFormat frameworkFormat,
                                            uint32_t colorBufferHandle, bool vulkanOnly,
                                            uint32_t memoryProperty) {
    if (!isFormatVulkanCompatible(internalFormat)) {
        GFXSTREAM_ERROR("Failed to create Vk ColorBuffer: format:%d not compatible.",
                        internalFormat);
        return false;
    }

    // Check the ExternalObjectManager for an external memory handle provided for import
    auto extMemHandleInfo =
        ExternalObjectManager::get()->removeResourceExternalHandleInfo(colorBufferHandle);
    if (extMemHandleInfo && !mDeviceInfo.supportsExternalMemoryImport) {
        GFXSTREAM_ERROR(
            "Failed to initialize Vk ColorBuffer -- extMemHandleInfo provided, but device does "
            "not support externalMemoryImport");
        return false;
    }

    VkEmulation::ColorBufferInfo res;

    res.handle = colorBufferHandle;
    res.width = width;
    res.height = height;
    res.memoryProperty = memoryProperty;
    res.internalFormat = internalFormat;
    res.frameworkFormat = frameworkFormat;
    res.frameworkStride = 0;

    if (vulkanOnly) {
        res.vulkanMode = VkEmulation::VulkanMode::VulkanOnly;
    }

    mColorBuffers[colorBufferHandle] = res;
    auto infoPtr = &mColorBuffers[colorBufferHandle];

    VkFormat vkFormat;
    switch (infoPtr->frameworkFormat) {
        case FrameworkFormat::FRAMEWORK_FORMAT_GL_COMPATIBLE:
            vkFormat = glFormat2VkFormat(infoPtr->internalFormat);
            break;
        case FrameworkFormat::FRAMEWORK_FORMAT_NV12:
            vkFormat = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
            break;
        case FrameworkFormat::FRAMEWORK_FORMAT_P010:
            vkFormat = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
            break;
        case FrameworkFormat::FRAMEWORK_FORMAT_YV12:
        case FrameworkFormat::FRAMEWORK_FORMAT_YUV_420_888:
            vkFormat = VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
            break;
        default:
            GFXSTREAM_ERROR("WARNING: unhandled framework format %d\n", infoPtr->frameworkFormat);
            vkFormat = glFormat2VkFormat(infoPtr->internalFormat);
            break;
    }

    VkImageTiling tiling = (infoPtr->memoryProperty & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
                               ? VK_IMAGE_TILING_LINEAR
                               : VK_IMAGE_TILING_OPTIMAL;
    std::unique_ptr<VkImageCreateInfo> imageCi = generateColorBufferVkImageCreateInfoLocked(
        vkFormat, infoPtr->width, infoPtr->height, tiling);
    // pNext will be filled later.
    if (imageCi == nullptr) {
        // it can happen if the format is not supported
        return false;
    }
    imageCi->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCi->queueFamilyIndexCount = 0;
    imageCi->pQueueFamilyIndices = nullptr;
    imageCi->initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // Create the image. If external memory is supported, make it external.
    VkExternalMemoryImageCreateInfo extImageCi = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        0,
        static_cast<VkExternalMemoryHandleTypeFlags>(getDefaultExternalMemoryHandleType()),
    };
#if defined(__APPLE__)
    if (mInstanceSupportsMoltenVK) {
        // Using a different handle type when in MoltenVK mode
        extImageCi.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
    }
#endif

    VkExternalMemoryImageCreateInfo* extImageCiPtr = nullptr;

    if (extMemHandleInfo || mDeviceInfo.supportsExternalMemoryExport) {
        extImageCiPtr = &extImageCi;
    }

    imageCi->pNext = extImageCiPtr;

    auto vk = mDvk;

    VkResult createRes = vk->vkCreateImage(mDevice, imageCi.get(), nullptr, &infoPtr->image);
    if (createRes != VK_SUCCESS) {
        GFXSTREAM_DEBUG("Failed to create Vulkan image for ColorBuffer %d, error: %s",
                        colorBufferHandle, string_VkResult(createRes));
        return false;
    }

    bool useDedicated = mUseDedicatedAllocations;

    infoPtr->imageCreateInfoShallow = vk_make_orphan_copy(*imageCi);
    infoPtr->currentQueueFamilyIndex = mQueueFamilyIndex;

    VkMemoryRequirements memReqs;
    if (!useDedicated && vk->vkGetImageMemoryRequirements2KHR) {
        VkMemoryDedicatedRequirements dedicated_reqs{
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, nullptr};
        VkMemoryRequirements2 reqs{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated_reqs};

        VkImageMemoryRequirementsInfo2 info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                                            nullptr, infoPtr->image};
        vk->vkGetImageMemoryRequirements2KHR(mDevice, &info, &reqs);
        useDedicated = dedicated_reqs.requiresDedicatedAllocation;
        memReqs = reqs.memoryRequirements;
    } else {
        vk->vkGetImageMemoryRequirements(mDevice, infoPtr->image, &memReqs);
    }

    if (extMemHandleInfo) {
        infoPtr->memory.handleInfo = extMemHandleInfo;
        infoPtr->memory.dedicatedAllocation = true;
        // External memory might change the memReqs for allocation
        if (!updateMemReqsForExtMem(extMemHandleInfo, &memReqs)) {
            GFXSTREAM_ERROR(
                "Failed to update memReqs for ColorBuffer memory allocation with external memory: "
                "%d\n",
                colorBufferHandle);
            return false;
        }
#if defined(__APPLE_)
        // importExtMemoryHandleToVkColorBuffer is not supported with MoltenVK
        if (mInstanceSupportsMoltenVK) {
            GFXSTREAM_WARNING("extMemhandleInfo import in ColorBuffer creation is unexpected.");
            infoPtr->memory.externalMetalHandle = nullptr;
        }
#endif
    }

    // Currently we only care about two memory properties: DEVICE_LOCAL
    // and HOST_VISIBLE; other memory properties specified in
    // rcSetColorBufferVulkanMode2() call will be ignored for now.
    infoPtr->memoryProperty = infoPtr->memoryProperty & (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
                                                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    infoPtr->memory.size = memReqs.size;

    // Determine memory type.
    infoPtr->memory.typeIndex =
        getValidMemoryTypeIndex(memReqs.memoryTypeBits, infoPtr->memoryProperty);

    const VkFormat imageVkFormat = infoPtr->imageCreateInfoShallow.format;
    GFXSTREAM_DEBUG(
        "ColorBuffer %u, %ux%u, %s, "
        "Memory [size: %llu, type: %d, props: %u / %u]",
        colorBufferHandle, infoPtr->width, infoPtr->height, string_VkFormat(imageVkFormat),
        infoPtr->memory.size, infoPtr->memory.typeIndex,
        mDeviceInfo.memProps.memoryTypes[infoPtr->memory.typeIndex].propertyFlags,
        infoPtr->memoryProperty);

    const bool isHostVisible = (infoPtr->memoryProperty & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    Optional<uint64_t> deviceAlignment =
        (!extMemHandleInfo && isHostVisible) ? Optional<uint64_t>(memReqs.alignment) : kNullopt;
    Optional<VkImage> dedicatedImage = useDedicated ? Optional<VkImage>(infoPtr->image) : kNullopt;
    if (extMemHandleInfo) {
        VkMemoryDedicatedAllocateInfo dedicatedInfo = {
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            nullptr,
            VK_NULL_HANDLE,
            VK_NULL_HANDLE,
        };
        VkMemoryDedicatedAllocateInfo* dedicatedInfoPtr = nullptr;
        if (useDedicated) {
            dedicatedInfo.image = *dedicatedImage;
            dedicatedInfoPtr = &dedicatedInfo;
        }
        if (!importExternalMemory(vk, mDevice, &infoPtr->memory, dedicatedInfoPtr,
                                  &infoPtr->memory.memory)) {
            GFXSTREAM_ERROR("Failed to import external memory%s for colorBuffer: %d\n",
                            dedicatedInfoPtr ? " (dedicated)" : "", colorBufferHandle);
            return false;
        }

        infoPtr->externalMemoryCompatible = true;
    } else {
        bool allocRes = allocExternalMemory(vk, &infoPtr->memory,
                                            deviceAlignment, kNullopt, dedicatedImage);
        if (!allocRes) {
            GFXSTREAM_ERROR("Failed to allocate ColorBuffer with Vulkan backing.");
            return false;
        }

        infoPtr->externalMemoryCompatible = mDeviceInfo.supportsExternalMemoryExport;
    }

    infoPtr->memory.pageOffset = reinterpret_cast<uint64_t>(infoPtr->memory.mappedPtr) % kPageSize;
    if (deviceAlignment.hasValue()) {
        infoPtr->memory.bindOffset =
            infoPtr->memory.pageOffset ? kPageSize - infoPtr->memory.pageOffset : 0u;
    } else {
        // Allocated as aligned..
        infoPtr->memory.bindOffset = 0;
    }

    VkResult bindImageMemoryRes = vk->vkBindImageMemory(
        mDevice, infoPtr->image, infoPtr->memory.memory, infoPtr->memory.bindOffset);

    if (bindImageMemoryRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to bind image memory. Error: %s",
                        string_VkResult(bindImageMemoryRes));
        return false;
    }

    VkSamplerYcbcrConversionInfo ycbcrInfo = {VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
                                              nullptr, VK_NULL_HANDLE};
    const bool addConversion = formatRequiresYcbcrConversion(imageVkFormat);
    if (addConversion) {
        if (!mDeviceInfo.supportsSamplerYcbcrConversion) {
            GFXSTREAM_ERROR(
                "VkFormat: %d requires conversion, but device does not have required extension "
                " for conversion (%s)",
                imageVkFormat, VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME);
            return false;
        }
        VkSamplerYcbcrConversionCreateInfo ycbcrCreateInfo = {
            VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
            nullptr,
            imageVkFormat,
            VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY,
            VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
            {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
             VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
            VK_CHROMA_LOCATION_MIDPOINT,
            VK_CHROMA_LOCATION_MIDPOINT,
            VK_FILTER_NEAREST,
            VK_FALSE};

        createRes = vk->vkCreateSamplerYcbcrConversion(mDevice, &ycbcrCreateInfo, nullptr,
                                                       &infoPtr->ycbcrConversion);
        if (createRes != VK_SUCCESS) {
            GFXSTREAM_DEBUG(
                "Failed to create Vulkan ycbcrConversion for ColorBuffer %d with format %s [%d], "
                "Error: %s",
                colorBufferHandle, string_VkFormat(imageVkFormat), imageVkFormat,
                string_VkResult(createRes));
            return false;
        }
        ycbcrInfo.conversion = infoPtr->ycbcrConversion;
    }

    const VkImageViewCreateInfo imageViewCi = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = addConversion ? &ycbcrInfo : nullptr,
        .flags = 0,
        .image = infoPtr->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = imageVkFormat,
        .components =
            {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            },
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };
    createRes = vk->vkCreateImageView(mDevice, &imageViewCi, nullptr, &infoPtr->imageView);
    if (createRes != VK_SUCCESS) {
        GFXSTREAM_DEBUG("Failed to create Vulkan image view for ColorBuffer %d, Error: %s",
                        colorBufferHandle, string_VkResult(createRes));
        return false;
    }

    mDebugUtilsHelper.addDebugLabel(infoPtr->image, "ColorBuffer:%d", colorBufferHandle);
    mDebugUtilsHelper.addDebugLabel(infoPtr->imageView, "ColorBuffer:%d", colorBufferHandle);
    mDebugUtilsHelper.addDebugLabel(infoPtr->memory.memory, "ColorBuffer:%d", colorBufferHandle);

    infoPtr->initialized = true;

    return true;
}

bool VkEmulation::isFormatSupported(GLenum format) {
    VkFormat vkFormat = glFormat2VkFormat(format);
    bool supported = !gfxstream::vk::formatIsDepthOrStencil(vkFormat);
    // TODO(b/356603558): add proper Vulkan querying, for now preserve existing assumption
    if (!supported) {
        for (size_t i = 0; i < mImageSupportInfo.size(); ++i) {
            // Only enable depth/stencil if it is usable as an attachment
            if (mImageSupportInfo[i].format == vkFormat &&
                gfxstream::vk::formatIsDepthOrStencil(mImageSupportInfo[i].format) &&
                mImageSupportInfo[i].supported &&
                mImageSupportInfo[i].formatProps2.formatProperties.optimalTilingFeatures &
                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                supported = true;
            }
        }
    }
    return supported;
}

bool VkEmulation::createVkColorBuffer(uint32_t width, uint32_t height, GLenum internalFormat,
                                      FrameworkFormat frameworkFormat, uint32_t colorBufferHandle,
                                      bool vulkanOnly, uint32_t memoryProperty) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (infoPtr) {
        GFXSTREAM_DEBUG("ColorBuffer already exists for handle: %d", colorBufferHandle);
        return false;
    }

    return createVkColorBufferLocked(width, height, internalFormat, frameworkFormat,
                                     colorBufferHandle, vulkanOnly, memoryProperty);
}

std::optional<VkEmulation::VkColorBufferMemoryExport> VkEmulation::exportColorBufferMemory(
    uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    if (!mDeviceInfo.supportsExternalMemoryExport && mDeviceInfo.supportsExternalMemoryImport) {
        return std::nullopt;
    }

    auto info = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!info) {
        return std::nullopt;
    }

    if ((info->vulkanMode != VkEmulation::VulkanMode::VulkanOnly) &&
        !mDeviceInfo.glInteropSupported) {
        return std::nullopt;
    }

    if (info->frameworkFormat != FRAMEWORK_FORMAT_GL_COMPATIBLE) {
        return std::nullopt;
    }

    auto handleInfo = info->memory.handleInfo;
    if (!handleInfo) {
        GFXSTREAM_ERROR("Could not export ColorBuffer memory, no external handle info available");
        return std::nullopt;
    }

    auto dupHandle = dupExternalMemory(handleInfo);
    if (!dupHandle) {
        GFXSTREAM_ERROR("Could not dup external memory handle: 0x%x, with handleType: %d",
                        handleInfo->handle, handleInfo->streamHandleType);
        return std::nullopt;
    }

    info->glExported = true;

    return VkColorBufferMemoryExport{
        .handleInfo = *dupHandle,
        .size = info->memory.size,
        .linearTiling = info->imageCreateInfoShallow.tiling == VK_IMAGE_TILING_LINEAR,
        .dedicatedAllocation = info->memory.dedicatedAllocation,
    };
}

bool VkEmulation::teardownVkColorBufferLocked(uint32_t colorBufferHandle) {
    auto vk = mDvk;

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);

    if (!infoPtr) return false;

    if (infoPtr->initialized) {
        auto& info = *infoPtr;
        {
            gfxstream::base::AutoLock queueLock(*mQueueLock);
            VK_CHECK(vk->vkQueueWaitIdle(mQueue));
        }
        vk->vkDestroyImageView(mDevice, info.imageView, nullptr);
        if (mDeviceInfo.hasSamplerYcbcrConversionExtension) {
            vk->vkDestroySamplerYcbcrConversion(mDevice, info.ycbcrConversion, nullptr);
        }
        vk->vkDestroyImage(mDevice, info.image, nullptr);
        freeExternalMemoryLocked(vk, &info.memory);
    }

    mColorBuffers.erase(colorBufferHandle);

    return true;
}

bool VkEmulation::teardownVkColorBuffer(uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);
    return teardownVkColorBufferLocked(colorBufferHandle);
}

std::optional<VkEmulation::ColorBufferInfo> VkEmulation::getColorBufferInfo(
    uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!infoPtr) {
        return std::nullopt;
    }

    return *infoPtr;
}

bool VkEmulation::colorBufferNeedsUpdateBetweenGlAndVk(
    const VkEmulation::ColorBufferInfo& colorBufferInfo) {
    // GL is not used.
    if (colorBufferInfo.vulkanMode == VkEmulation::VulkanMode::VulkanOnly) {
        return false;
    }

    // YUV formats require extra conversions.
    if (colorBufferInfo.frameworkFormat != FrameworkFormat::FRAMEWORK_FORMAT_GL_COMPATIBLE) {
        return true;
    }

    // GL and VK are sharing the same underlying memory.
    if (colorBufferInfo.glExported) {
        return false;
    }

    return true;
}

bool VkEmulation::colorBufferNeedsUpdateBetweenGlAndVk(uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        return false;
    }

    return colorBufferNeedsUpdateBetweenGlAndVk(*colorBufferInfo);
}

bool VkEmulation::readColorBufferToBytes(uint32_t colorBufferHandle, std::vector<uint8_t>* bytes) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_DEBUG("Failed to read from ColorBuffer:%d, not found.", colorBufferHandle);
        bytes->clear();
        return false;
    }

    VkDeviceSize bytesNeeded = 0;
    bool result = getFormatTransferInfo(colorBufferInfo->imageCreateInfoShallow.format,
                                        colorBufferInfo->imageCreateInfoShallow.extent.width,
                                        colorBufferInfo->imageCreateInfoShallow.extent.height,
                                        &bytesNeeded, nullptr);
    if (!result) {
        GFXSTREAM_ERROR("Failed to read from ColorBuffer:%d, failed to get read size.",
                        colorBufferHandle);
        return false;
    }

    bytes->resize(bytesNeeded);

    result = readColorBufferToBytesLocked(
        colorBufferHandle, 0, 0, colorBufferInfo->imageCreateInfoShallow.extent.width,
        colorBufferInfo->imageCreateInfoShallow.extent.height, bytes->data(), bytes->size());
    if (!result) {
        GFXSTREAM_ERROR("Failed to read from ColorBuffer:%d, failed to get read size.",
                        colorBufferHandle);
        return false;
    }

    return true;
}

bool VkEmulation::readColorBufferToBytes(uint32_t colorBufferHandle, uint32_t x, uint32_t y,
                                         uint32_t w, uint32_t h, void* outPixels,
                                         uint64_t outPixelsSize) {
    std::lock_guard<std::mutex> lock(mMutex);
    return readColorBufferToBytesLocked(colorBufferHandle, x, y, w, h, outPixels, outPixelsSize);
}

bool VkEmulation::readColorBufferToBytesLocked(uint32_t colorBufferHandle, uint32_t x, uint32_t y,
                                               uint32_t w, uint32_t h, void* outPixels,
                                               uint64_t outPixelsSize) {
    auto vk = mDvk;

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_ERROR("Failed to read from ColorBuffer:%d, not found.", colorBufferHandle);
        return false;
    }

    if (!colorBufferInfo->image) {
        GFXSTREAM_ERROR("Failed to read from ColorBuffer:%d, no VkImage.", colorBufferHandle);
        return false;
    }

    if (x != 0 || y != 0 || w != colorBufferInfo->imageCreateInfoShallow.extent.width ||
        h != colorBufferInfo->imageCreateInfoShallow.extent.height) {
        GFXSTREAM_ERROR("Failed to read from ColorBuffer:%d, unhandled subrect.",
                        colorBufferHandle);
        return false;
    }

    VkDeviceSize bufferCopySize = 0;
    std::vector<VkBufferImageCopy> bufferImageCopies;
    if (!getFormatTransferInfo(colorBufferInfo->imageCreateInfoShallow.format,
                               colorBufferInfo->imageCreateInfoShallow.extent.width,
                               colorBufferInfo->imageCreateInfoShallow.extent.height,
                               &bufferCopySize, &bufferImageCopies)) {
        GFXSTREAM_ERROR("Failed to read ColorBuffer:%d, unable to get transfer info.",
                        colorBufferHandle);
        return false;
    }

    // Avoid transitioning from VK_IMAGE_LAYOUT_UNDEFINED. Unfortunetly, Android does not
    // yet have a mechanism for sharing the expected VkImageLayout. However, the Vulkan
    // spec's image layout transition sections says "If the old layout is
    // VK_IMAGE_LAYOUT_UNDEFINED, the contents of that range may be discarded." Some
    // Vulkan drivers have been observed to actually perform the discard which leads to
    // ColorBuffer-s being unintentionally cleared. See go/ahb-vkimagelayout for a more
    // thorough write up.
    if (colorBufferInfo->currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
        colorBufferInfo->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }

    // Record our synchronization commands.
    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vk->vkBeginCommandBuffer(mCommandBuffer, &beginInfo));

    mDebugUtilsHelper.cmdBeginDebugLabel(mCommandBuffer, "readColorBufferToBytes(ColorBuffer:%d)",
                                         colorBufferHandle);

    const VkImageLayout currentLayout = colorBufferInfo->currentLayout;
    const VkImageLayout transferSrcLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    const VkImageMemoryBarrier toTransferSrcImageBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT,
        .oldLayout = currentLayout,
        .newLayout = transferSrcLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorBufferInfo->image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vk->vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toTransferSrcImageBarrier);

    vk->vkCmdCopyImageToBuffer(mCommandBuffer, colorBufferInfo->image,
                               transferSrcLayout, mStaging.mBuffer,
                               bufferImageCopies.size(), bufferImageCopies.data());

    // Change back to original layout
    if (currentLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
        // Transfer back to original layout.
        const VkImageMemoryBarrier toCurrentLayoutImageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_HOST_READ_BIT,
            .dstAccessMask = VK_ACCESS_NONE_KHR,
            .oldLayout = transferSrcLayout,
            .newLayout = colorBufferInfo->currentLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = colorBufferInfo->image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vk->vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &toCurrentLayoutImageBarrier);
    } else {
        colorBufferInfo->currentLayout = transferSrcLayout;
    }

    mDebugUtilsHelper.cmdEndDebugLabel(mCommandBuffer);

    VK_CHECK(vk->vkEndCommandBuffer(mCommandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueSubmit(mQueue, 1, &submitInfo, mCommandBufferFence));
    }

    static constexpr uint64_t ANB_MAX_WAIT_NS = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    VkResult waitRes =
        vk->vkWaitForFences(mDevice, 1, &mCommandBufferFence, VK_TRUE, ANB_MAX_WAIT_NS);
    if (waitRes == VK_TIMEOUT) {
        // Give a warning and try once more on a timeout error
        GFXSTREAM_ERROR(
            "readColorBufferToBytesLocked vkWaitForFences failed with timeout error "
            "(cb:%d, x:%d, y:%d, w:%d, h:%d, bufferCopySize:%llu), retrying...",
            colorBufferHandle, x, y, w, h, bufferCopySize);
        waitRes =
            vk->vkWaitForFences(mDevice, 1, &mCommandBufferFence, VK_TRUE, ANB_MAX_WAIT_NS * 2);
    }

    VK_CHECK(waitRes);

    VK_CHECK(vk->vkResetFences(mDevice, 1, &mCommandBufferFence));

    if (!mStaging.mIsHostCoherent) {
        // Invalidate host cache lines to ensure the subsequent readback
        // will see the latest writes made by the GPU.
        const VkMappedMemoryRange toInvalidate = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = mStaging.mMemory,
            .offset = 0,
            .size = VK_WHOLE_SIZE,
        };

        VK_CHECK(vk->vkInvalidateMappedMemoryRanges(mDevice, 1, &toInvalidate));
    }

    if (bufferCopySize > outPixelsSize) {
        GFXSTREAM_ERROR(
            "Invalid buffer size for readColorBufferToBytes operation."
            "Required: %llu, Actual: %llu",
            bufferCopySize, outPixelsSize);
        bufferCopySize = outPixelsSize;
    }
    std::memcpy(outPixels, mStaging.mMappedPtr, bufferCopySize);

    return true;
}

bool VkEmulation::updateColorBufferFromBytes(uint32_t colorBufferHandle,
                                             const std::vector<uint8_t>& bytes) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_DEBUG("Failed to update ColorBuffer:%d, not found.", colorBufferHandle);
        return false;
    }

    return updateColorBufferFromBytesLocked(
        colorBufferHandle, 0, 0, colorBufferInfo->imageCreateInfoShallow.extent.width,
        colorBufferInfo->imageCreateInfoShallow.extent.height, bytes.data(), bytes.size());
}

bool VkEmulation::updateColorBufferFromBytes(uint32_t colorBufferHandle, uint32_t x, uint32_t y,
                                             uint32_t w, uint32_t h, const void* pixels) {
    std::lock_guard<std::mutex> lock(mMutex);
    return updateColorBufferFromBytesLocked(colorBufferHandle, x, y, w, h, pixels, 0);
}

static void convertRgbToRgbaPixels(void* dst, const void* src, uint32_t w, uint32_t h) {
    const size_t pixelCount = w * h;
    const uint8_t* srcBytes = reinterpret_cast<const uint8_t*>(src);
    uint32_t* dstPixels = reinterpret_cast<uint32_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t r = *(srcBytes++);
        const uint8_t g = *(srcBytes++);
        const uint8_t b = *(srcBytes++);
        *(dstPixels++) = 0xff000000 | (b << 16) | (g << 8) | r;
    }
}

static void convertRgba4ToBGRA4Pixels(void* dst, const void* src, uint32_t w, uint32_t h) {
    const size_t pixelCount = w * h;
    const uint16_t* srcPixels = reinterpret_cast<const uint16_t*>(src);
    uint16_t* dstPixels = reinterpret_cast<uint16_t*>(dst);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint16_t rgba4_pixel = srcPixels[i];
        const uint8_t red = (rgba4_pixel >> 12) & 0xF;
        const uint8_t green = (rgba4_pixel >> 8) & 0xF;
        const uint8_t blue = (rgba4_pixel >> 4) & 0xF;
        const uint8_t alpha = rgba4_pixel & 0xF;
        dstPixels[i] = (blue << 12) | (green << 8) | (red << 4) | alpha;
    }
}

bool VkEmulation::updateColorBufferFromBytesLocked(uint32_t colorBufferHandle, uint32_t x,
                                                   uint32_t y, uint32_t w, uint32_t h,
                                                   const void* pixels, size_t inputPixelsSize) {
    auto vk = mDvk;

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_ERROR("Failed to update ColorBuffer:%d, not found.", colorBufferHandle);
        return false;
    }

    if (!colorBufferInfo->image) {
        GFXSTREAM_ERROR("Failed to update ColorBuffer:%d, no VkImage.", colorBufferHandle);
        return false;
    }

    if (x != 0 || y != 0 || w != colorBufferInfo->imageCreateInfoShallow.extent.width ||
        h != colorBufferInfo->imageCreateInfoShallow.extent.height) {
        GFXSTREAM_ERROR("Failed to update ColorBuffer:%d, unhandled subrect.", colorBufferHandle);
        return false;
    }

    const VkFormat creationFormat = colorBufferInfo->imageCreateInfoShallow.format;
    VkDeviceSize dstBufferSize = 0;
    std::vector<VkBufferImageCopy> bufferImageCopies;
    if (!getFormatTransferInfo(creationFormat,
                               colorBufferInfo->imageCreateInfoShallow.extent.width,
                               colorBufferInfo->imageCreateInfoShallow.extent.height,
                               &dstBufferSize, &bufferImageCopies)) {
        GFXSTREAM_ERROR("Failed to update ColorBuffer:%d, unable to get transfer info.",
                        colorBufferHandle);
        return false;
    }

    const VkDeviceSize stagingBufferSize = mStaging.mAllocationSize;
    if (dstBufferSize > stagingBufferSize) {
        GFXSTREAM_ERROR("Failed to update ColorBuffer:%d, transfer size %" PRIu64
                        " too large for staging buffer size:%" PRIu64 ".",
                        colorBufferHandle, dstBufferSize, stagingBufferSize);
        return false;
    }
    const bool isRGBA4onBGRA4 = (colorBufferInfo->internalFormat == GL_RGBA4_OES) &&
                          (creationFormat == VK_FORMAT_B4G4R4A4_UNORM_PACK16);
    const bool isThreeByteRgb =
        (colorBufferInfo->internalFormat == GL_RGB || colorBufferInfo->internalFormat == GL_RGB8);
    const size_t expectedInputSize = (isThreeByteRgb ? dstBufferSize / 4 * 3 : dstBufferSize);

    if (inputPixelsSize != 0 && inputPixelsSize != expectedInputSize) {
        GFXSTREAM_ERROR(
            "Unexpected contents size when trying to update ColorBuffer:%d, "
            "provided:%zu expected:%zu",
            colorBufferHandle, inputPixelsSize, expectedInputSize);
        return false;
    }

    // Copy the data into the staging memory first, then use vkCmdCopyBufferToImage
    // to update the color buffer image.
    auto* stagingBufferPtr = mStaging.mMappedPtr;
    if (isThreeByteRgb) {
        // Convert RGB to RGBA, since only for these types glFormat2VkFormat() makes
        // an incompatible choice of 4-byte backing VK_FORMAT_R8G8B8A8_UNORM.
        // b/281550953
        convertRgbToRgbaPixels(stagingBufferPtr, pixels, w, h);
    } else if(isRGBA4onBGRA4) {
        convertRgba4ToBGRA4Pixels(stagingBufferPtr, pixels, w, h);
    } else {
        std::memcpy(stagingBufferPtr, pixels, dstBufferSize);
    }

    if (!mStaging.mIsHostCoherent) {
        // Flush writes manually now if the memory is not coherent
        const VkMappedMemoryRange flushRange = {
            VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, 0,
            mStaging.mMemory, 0, VK_WHOLE_SIZE
        };
        VK_CHECK(vk->vkFlushMappedMemoryRanges(mDevice, 1, &flushRange));
    }

    // NOTE: Host vulkan state might not know the correct layout of the
    // destination image, as guest grallocs are designed to be used by either
    // GL or Vulkan. Consequently, we typically avoid image transitions from
    // VK_IMAGE_LAYOUT_UNDEFINED as Vulkan spec allows the contents to be
    // discarded (and some drivers have been observed doing it). You can
    // check go/ahb-vkimagelayout for more information. But since this
    // function does not allow subrects (see above), it will write the
    // provided contents onto the entirety of the target buffer, meaning this
    // risk of discarding data should not impact anything.

    // Record our synchronization commands.
    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vk->vkBeginCommandBuffer(mCommandBuffer, &beginInfo));

    mDebugUtilsHelper.cmdBeginDebugLabel(
        mCommandBuffer, "updateColorBufferFromBytes(ColorBuffer:%d)", colorBufferHandle);

    const bool isSnapshotLoad = VkDecoderGlobalState::get()->isSnapshotCurrentlyLoading();
    VkImageLayout currentLayout = colorBufferInfo->currentLayout;
    if (isSnapshotLoad) {
        currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    const VkImageMemoryBarrier toTransferDstImageBarrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        .oldLayout = currentLayout,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = colorBufferInfo->image,
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    vk->vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &toTransferDstImageBarrier);

    // Copy from staging buffer to color buffer image
    vk->vkCmdCopyBufferToImage(mCommandBuffer, mStaging.mBuffer, colorBufferInfo->image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, bufferImageCopies.size(),
                               bufferImageCopies.data());

    if (colorBufferInfo->currentLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
        const VkImageMemoryBarrier toCurrentLayoutImageBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_HOST_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_NONE_KHR,
            .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            .newLayout = colorBufferInfo->currentLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = colorBufferInfo->image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        vk->vkCmdPipelineBarrier(mCommandBuffer, VK_PIPELINE_STAGE_HOST_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &toCurrentLayoutImageBarrier);
    } else {
        colorBufferInfo->currentLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    }

    mDebugUtilsHelper.cmdEndDebugLabel(mCommandBuffer);

    VK_CHECK(vk->vkEndCommandBuffer(mCommandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueSubmit(mQueue, 1, &submitInfo, mCommandBufferFence));
    }

    static constexpr uint64_t ANB_MAX_WAIT_NS = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    VK_CHECK(vk->vkWaitForFences(mDevice, 1, &mCommandBufferFence, VK_TRUE, ANB_MAX_WAIT_NS));

    VK_CHECK(vk->vkResetFences(mDevice, 1, &mCommandBufferFence));

    return true;
}

std::optional<ExternalHandleInfo> VkEmulation::dupColorBufferExtMemoryHandle(
    uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);

    if (!infoPtr) {
        return std::nullopt;
    }

    auto handleInfo = infoPtr->memory.handleInfo;
    if (!handleInfo) {
        GFXSTREAM_ERROR(
            "Could not dup ColorBuffer external memory handle, no external handle info available");
        return std::nullopt;
    }

    return dupExternalMemory(handleInfo);
}

#ifdef __APPLE__
MTLResource_id VkEmulation::getColorBufferMetalMemoryHandle(uint32_t colorBuffer) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBuffer);

    if (!infoPtr) {
        // Color buffer not found; this is usually OK.
        return nullptr;
    }

    return infoPtr->memory.externalMetalHandle;
}

// TODO(b/351765838): Temporary function for MoltenVK
VkImage VkEmulation::getColorBufferVkImage(uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);

    if (!infoPtr) {
        // Color buffer not found; this is usually OK.
        return nullptr;
    }

    return infoPtr->image;
}
#endif  // __APPLE__

bool VkEmulation::setColorBufferVulkanMode(uint32_t colorBuffer, uint32_t vulkanMode) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBuffer);

    if (!infoPtr) {
        return false;
    }

    infoPtr->vulkanMode = static_cast<VkEmulation::VulkanMode>(vulkanMode);

    return true;
}

int32_t VkEmulation::mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa, uint64_t size) {
    std::lock_guard<std::mutex> lock(mMutex);

    VkEmulation::ExternalMemoryInfo* memoryInfoPtr = nullptr;

    auto colorBufferInfoPtr = gfxstream::base::find(mColorBuffers, bufferHandle);
    if (colorBufferInfoPtr) {
        memoryInfoPtr = &colorBufferInfoPtr->memory;
    }
    auto bufferInfoPtr = gfxstream::base::find(mBuffers, bufferHandle);
    if (bufferInfoPtr) {
        memoryInfoPtr = &bufferInfoPtr->memory;
    }

    if (!memoryInfoPtr) {
        return VK_ERROR_INVALID_EXTERNAL_HANDLE;
    }

    // memory should be already mapped to host.
    if (!memoryInfoPtr->mappedPtr) {
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    memoryInfoPtr->gpa = gpa;
    memoryInfoPtr->pageAlignedHva =
        reinterpret_cast<uint8_t*>(memoryInfoPtr->mappedPtr) + memoryInfoPtr->bindOffset;

    size_t rawSize = memoryInfoPtr->size + memoryInfoPtr->pageOffset;
    if (size && size < rawSize) {
        rawSize = size;
    }

    memoryInfoPtr->sizeToPage = ((rawSize + kPageSize - 1) >> kPageBits) << kPageBits;

    GFXSTREAM_DEBUG("mapGpaToColorBuffer: hva = %p, pageAlignedHva = %p -> [ 0x%" PRIxPTR
                    ", 0x%" PRIxPTR " ]",
                    memoryInfoPtr->mappedPtr, memoryInfoPtr->pageAlignedHva, memoryInfoPtr->gpa,
                    memoryInfoPtr->gpa + memoryInfoPtr->sizeToPage);

    if (mOccupiedGpas.find(gpa) != mOccupiedGpas.end()) {
        // emugl::emugl_crash_reporter("FATAL: already mapped gpa 0x%lx! ", gpa);
        return VK_ERROR_MEMORY_MAP_FAILED;
    }

    get_gfxstream_vm_operations().map_user_memory(gpa, memoryInfoPtr->pageAlignedHva,
                                               memoryInfoPtr->sizeToPage);

    mOccupiedGpas.insert(gpa);

    return memoryInfoPtr->pageOffset;
}

bool VkEmulation::getBufferAllocationInfo(uint32_t bufferHandle, VkDeviceSize* outSize,
                                          uint32_t* outMemoryTypeIndex,
                                          bool* outMemoryIsDedicatedAlloc) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto info = gfxstream::base::find(mBuffers, bufferHandle);
    if (!info) {
        return false;
    }

    if (outSize) {
        *outSize = info->memory.size;
    }

    if (outMemoryTypeIndex) {
        *outMemoryTypeIndex = info->memory.typeIndex;
    }

    if (outMemoryIsDedicatedAlloc) {
        *outMemoryIsDedicatedAlloc = info->memory.dedicatedAllocation;
    }

    return true;
}

bool VkEmulation::setupVkBuffer(uint64_t size, uint32_t bufferHandle, bool vulkanOnly,
                                uint32_t memoryProperty) {
    if (vulkanOnly == false) {
        GFXSTREAM_ERROR("Data buffers should be vulkanOnly. Setup failed.");
        return false;
    }

    auto vk = mDvk;

    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mBuffers, bufferHandle);

    // Already setup
    if (infoPtr) {
        return true;
    }

    VkEmulation::BufferInfo res;

    res.handle = bufferHandle;

    res.size = size;
    res.usageFlags = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT |
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    res.createFlags = 0;

    res.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    // Create the buffer. If external memory is supported, make it external.
    VkExternalMemoryBufferCreateInfo extBufferCi = {
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
        0,
        static_cast<VkExternalMemoryHandleTypeFlags>(getDefaultExternalMemoryHandleType()),
    };

    void* extBufferCiPtr = nullptr;
    if (mDeviceInfo.supportsExternalMemoryImport || mDeviceInfo.supportsExternalMemoryExport) {
        extBufferCiPtr = &extBufferCi;
    }

    VkBufferCreateInfo bufferCi = {
        VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        extBufferCiPtr,
        res.createFlags,
        res.size,
        res.usageFlags,
        res.sharingMode,
        /* queueFamilyIndexCount */ 0,
        /* pQueueFamilyIndices */ nullptr,
    };

    VkResult createRes = vk->vkCreateBuffer(mDevice, &bufferCi, nullptr, &res.buffer);

    if (createRes != VK_SUCCESS) {
        GFXSTREAM_WARNING("Failed to create Vulkan Buffer for Buffer %d, Error: %s", bufferHandle,
                          string_VkResult(createRes));
        return false;
    }
    bool useDedicated = false;
    VkMemoryRequirements memReqs;
    if (vk->vkGetBufferMemoryRequirements2KHR) {
        VkMemoryDedicatedRequirements dedicated_reqs{
            VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS, nullptr};
        VkMemoryRequirements2 reqs{VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, &dedicated_reqs};

        VkBufferMemoryRequirementsInfo2 info{VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                                             nullptr, res.buffer};
        vk->vkGetBufferMemoryRequirements2KHR(mDevice, &info, &reqs);
        useDedicated = dedicated_reqs.requiresDedicatedAllocation;
        memReqs = reqs.memoryRequirements;
    } else {
        vk->vkGetBufferMemoryRequirements(mDevice, res.buffer, &memReqs);
    }

    // Currently we only care about two memory properties: DEVICE_LOCAL
    // and HOST_VISIBLE; other memory properties specified in
    // rcSetColorBufferVulkanMode2() call will be ignored for now.
    memoryProperty = memoryProperty &
                     (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    res.memory.size = memReqs.size;

    // Determine memory type.
    res.memory.typeIndex = getValidMemoryTypeIndex(memReqs.memoryTypeBits, memoryProperty);

    GFXSTREAM_DEBUG(
        "Buffer %d "
        "allocation size and type index: %lu, %d, "
        "allocated memory property: %d, "
        "requested memory property: %d",
        bufferHandle, res.memory.size, res.memory.typeIndex,
        mDeviceInfo.memProps.memoryTypes[res.memory.typeIndex].propertyFlags, memoryProperty);

    bool isHostVisible = memoryProperty & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    Optional<uint64_t> deviceAlignment =
        isHostVisible ? Optional<uint64_t>(memReqs.alignment) : kNullopt;
    Optional<VkBuffer> dedicated_buffer = useDedicated ? Optional<VkBuffer>(res.buffer) : kNullopt;
    bool allocRes = allocExternalMemory(vk, &res.memory, deviceAlignment, dedicated_buffer);

    if (!allocRes) {
        GFXSTREAM_WARNING("Failed to allocate ColorBuffer with Vulkan backing.");
    }

    res.memory.pageOffset = reinterpret_cast<uint64_t>(res.memory.mappedPtr) % kPageSize;
    res.memory.bindOffset = res.memory.pageOffset ? kPageSize - res.memory.pageOffset : 0u;

    VkResult bindBufferMemoryRes =
        vk->vkBindBufferMemory(mDevice, res.buffer, res.memory.memory, 0);

    if (bindBufferMemoryRes != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to bind buffer memory. Error: %s\n",
                        string_VkResult(bindBufferMemoryRes));
        return bindBufferMemoryRes;
    }

    bool isHostVisibleMemory = memoryProperty & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

    if (isHostVisibleMemory) {
        VkResult mapMemoryRes = vk->vkMapMemory(mDevice, res.memory.memory, 0, res.memory.size, {},
                                                &res.memory.mappedPtr);

        if (mapMemoryRes != VK_SUCCESS) {
            GFXSTREAM_ERROR("Failed to map image memory. Error: %s\n",
                            string_VkResult(mapMemoryRes));
            return false;
        }
    }

    res.glExported = false;

    mBuffers[bufferHandle] = res;

    mDebugUtilsHelper.addDebugLabel(res.buffer, "Buffer:%d", bufferHandle);
    mDebugUtilsHelper.addDebugLabel(res.memory.memory, "Buffer:%d", bufferHandle);

    return allocRes;
}

bool VkEmulation::teardownVkBuffer(uint32_t bufferHandle) {
    auto vk = mDvk;
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mBuffers, bufferHandle);
    if (!infoPtr) return false;
    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueWaitIdle(mQueue));
    }
    auto& info = *infoPtr;

    vk->vkDestroyBuffer(mDevice, info.buffer, nullptr);
    freeExternalMemoryLocked(vk, &info.memory);
    mBuffers.erase(bufferHandle);

    return true;
}

std::optional<ExternalHandleInfo> VkEmulation::dupBufferExtMemoryHandle(uint32_t bufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mBuffers, bufferHandle);
    if (!infoPtr) {
        return std::nullopt;
    }

    auto handleInfo = infoPtr->memory.handleInfo;
    if (!handleInfo) {
        GFXSTREAM_ERROR(
            "Could not dup Buffer external memory handle, no external handle info available");
        return std::nullopt;
    }

    return dupExternalMemory(handleInfo);
}

#ifdef __APPLE__
MTLResource_id VkEmulation::getBufferMetalMemoryHandle(uint32_t bufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mBuffers, bufferHandle);
    if (!infoPtr) {
        // Color buffer not found; this is usually OK.
        return nullptr;
    }

    return infoPtr->memory.externalMetalHandle;
}
#endif

bool VkEmulation::readBufferToBytes(uint32_t bufferHandle, uint64_t offset, uint64_t size,
                                    void* outBytes) {
    auto vk = mDvk;

    std::lock_guard<std::mutex> lock(mMutex);

    auto bufferInfo = gfxstream::base::find(mBuffers, bufferHandle);
    if (!bufferInfo) {
        GFXSTREAM_ERROR("Failed to read from Buffer:%d, not found.", bufferHandle);
        return false;
    }

    const auto& stagingBufferInfo = mStaging;
    if (size > stagingBufferInfo.mAllocationSize) {
        GFXSTREAM_ERROR("Failed to read from Buffer:%d, staging buffer too small.", bufferHandle);
        return false;
    }

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vk->vkBeginCommandBuffer(mCommandBuffer, &beginInfo));

    mDebugUtilsHelper.cmdBeginDebugLabel(mCommandBuffer, "readBufferToBytes(Buffer:%d)",
                                         bufferHandle);

    const VkBufferCopy bufferCopy = {
        .srcOffset = offset,
        .dstOffset = 0,
        .size = size,
    };
    vk->vkCmdCopyBuffer(mCommandBuffer, bufferInfo->buffer, stagingBufferInfo.mBuffer, 1,
                        &bufferCopy);

    mDebugUtilsHelper.cmdEndDebugLabel(mCommandBuffer);

    VK_CHECK(vk->vkEndCommandBuffer(mCommandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueSubmit(mQueue, 1, &submitInfo, mCommandBufferFence));
    }

    static constexpr uint64_t ANB_MAX_WAIT_NS = 5ULL * 1000ULL * 1000ULL * 1000ULL;

    VK_CHECK(vk->vkWaitForFences(mDevice, 1, &mCommandBufferFence, VK_TRUE, ANB_MAX_WAIT_NS));

    VK_CHECK(vk->vkResetFences(mDevice, 1, &mCommandBufferFence));

    if (!stagingBufferInfo.mIsHostCoherent) {
        // Invalidate host cache lines to ensure the subsequent readback
        // will see the latest writes made by the GPU.
        const VkMappedMemoryRange toInvalidate = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = stagingBufferInfo.mMemory,
            .offset = 0,
            .size = size,
        };

        VK_CHECK(vk->vkInvalidateMappedMemoryRanges(mDevice, 1, &toInvalidate));
    }

    const void* srcPtr = reinterpret_cast<const void*>(
        reinterpret_cast<const char*>(stagingBufferInfo.mMappedPtr));
    void* dstPtr = outBytes;
    void* dstPtrOffset = reinterpret_cast<void*>(reinterpret_cast<char*>(dstPtr) + offset);
    std::memcpy(dstPtrOffset, srcPtr, size);

    return true;
}

bool VkEmulation::updateBufferFromBytes(uint32_t bufferHandle, uint64_t offset, uint64_t size,
                                        const void* bytes) {
    auto vk = mDvk;

    std::lock_guard<std::mutex> lock(mMutex);

    auto bufferInfo = gfxstream::base::find(mBuffers, bufferHandle);
    if (!bufferInfo) {
        GFXSTREAM_ERROR("Failed to update Buffer:%d, not found.", bufferHandle);
        return false;
    }

    const auto& stagingBufferInfo = mStaging;
    if (size > stagingBufferInfo.mAllocationSize) {
        GFXSTREAM_ERROR("Failed to update Buffer:%d, staging buffer too small.", bufferHandle);
        return false;
    }

    const void* srcPtr = bytes;
    const void* srcPtrOffset =
        reinterpret_cast<const void*>(reinterpret_cast<const char*>(srcPtr) + offset);
    void* dstPtr = stagingBufferInfo.mMappedPtr;
    std::memcpy(dstPtr, srcPtrOffset, size);

    const VkMappedMemoryRange toFlush = {
        .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
        .pNext = nullptr,
        .memory = stagingBufferInfo.mMemory,
        .offset = 0,
        .size = size,
    };
    VK_CHECK(vk->vkFlushMappedMemoryRanges(mDevice, 1, &toFlush));

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    VK_CHECK(vk->vkBeginCommandBuffer(mCommandBuffer, &beginInfo));

    mDebugUtilsHelper.cmdBeginDebugLabel(mCommandBuffer, "updateBufferFromBytes(Buffer:%d)",
                                         bufferHandle);

    const VkBufferCopy bufferCopy = {
        .srcOffset = 0,
        .dstOffset = offset,
        .size = size,
    };
    vk->vkCmdCopyBuffer(mCommandBuffer, stagingBufferInfo.mBuffer, bufferInfo->buffer, 1,
                        &bufferCopy);

    mDebugUtilsHelper.cmdEndDebugLabel(mCommandBuffer);

    VK_CHECK(vk->vkEndCommandBuffer(mCommandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &mCommandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };

    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueSubmit(mQueue, 1, &submitInfo, mCommandBufferFence));
    }

    static constexpr uint64_t ANB_MAX_WAIT_NS = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    VK_CHECK(vk->vkWaitForFences(mDevice, 1, &mCommandBufferFence, VK_TRUE, ANB_MAX_WAIT_NS));

    VK_CHECK(vk->vkResetFences(mDevice, 1, &mCommandBufferFence));

    return true;
}

VkExternalMemoryHandleTypeFlags VkEmulation::transformExternalMemoryHandleTypeFlags_tohost(
    VkExternalMemoryHandleTypeFlags bits) {
    VkExternalMemoryHandleTypeFlags res = bits;

    // Drop OPAQUE_FD_BIT if it was set. Host's default external memory bits
    // may set them again below
    if (bits & VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT) {
        res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        res |= getDefaultExternalMemoryHandleType();
    }

#ifdef _WIN32
    res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;
#endif

    // Replace guest AHardwareBuffer bits with host's default external memory bits
    if (bits & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID) {
        res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
        res |= getDefaultExternalMemoryHandleType();
    }

    // Replace guest Zircon VMO bits with host's default external memory bits
    if (bits & VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA) {
        res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_ZIRCON_VMO_BIT_FUCHSIA;
        res |= getDefaultExternalMemoryHandleType();
    }

    // If the host does not support dmabuf, replace guest Linux DMA_BUF bits with
    // the host's default external memory bits,
    if (!mDeviceInfo.supportsDmaBuf && (bits & VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT)) {
        res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
        res |= getDefaultExternalMemoryHandleType();
    }

    return res;
}

VkExternalMemoryHandleTypeFlags VkEmulation::transformExternalMemoryHandleTypeFlags_fromhost(
    VkExternalMemoryHandleTypeFlags hostBits,
    VkExternalMemoryHandleTypeFlags wantedGuestHandleType) {
    VkExternalMemoryHandleTypeFlags res = hostBits;

    VkExternalMemoryHandleTypeFlagBits handleTypeUsed = getDefaultExternalMemoryHandleType();
#if defined(__APPLE__)
    if (mInstanceSupportsMoltenVK) {
        // Using a different handle type when in MoltenVK mode
        handleTypeUsed = VK_EXTERNAL_MEMORY_HANDLE_TYPE_MTLHEAP_BIT_EXT;
    }
#endif
    if ((res & handleTypeUsed) == handleTypeUsed) {
        res &= ~handleTypeUsed;
        res |= wantedGuestHandleType;
    }

#ifdef _WIN32
    res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
    res &= ~VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;
#endif

    return res;
}

VkExternalMemoryProperties VkEmulation::transformExternalMemoryProperties_tohost(
    VkExternalMemoryProperties props) {
    VkExternalMemoryProperties res = props;
    res.exportFromImportedHandleTypes =
        transformExternalMemoryHandleTypeFlags_tohost(props.exportFromImportedHandleTypes);
    res.compatibleHandleTypes =
        transformExternalMemoryHandleTypeFlags_tohost(props.compatibleHandleTypes);
    return res;
}

VkExternalMemoryProperties VkEmulation::transformExternalMemoryProperties_fromhost(
    VkExternalMemoryProperties props, VkExternalMemoryHandleTypeFlags wantedGuestHandleType) {
    VkExternalMemoryProperties res = props;
    res.exportFromImportedHandleTypes = transformExternalMemoryHandleTypeFlags_fromhost(
        props.exportFromImportedHandleTypes, wantedGuestHandleType);
    res.compatibleHandleTypes = transformExternalMemoryHandleTypeFlags_fromhost(
        props.compatibleHandleTypes, wantedGuestHandleType);
    return res;
}

void VkEmulation::setColorBufferCurrentLayout(uint32_t colorBufferHandle, VkImageLayout layout) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!infoPtr) {
        GFXSTREAM_ERROR("Invalid ColorBuffer handle %d.", static_cast<int>(colorBufferHandle));
        return;
    }
    infoPtr->currentLayout = layout;
}

VkImageLayout VkEmulation::getColorBufferCurrentLayout(uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!infoPtr) {
        GFXSTREAM_ERROR("Invalid ColorBuffer handle %d.", static_cast<int>(colorBufferHandle));
        return VK_IMAGE_LAYOUT_UNDEFINED;
    }
    return infoPtr->currentLayout;
}

// Allocate a ready to use VkCommandBuffer for queue transfer. The caller needs
// to signal the returned VkFence when the VkCommandBuffer completes.
std::tuple<VkCommandBuffer, VkFence> VkEmulation::allocateQueueTransferCommandBufferLocked() {
    auto vk = mDvk;
    // Check if a command buffer in the pool is ready to use. If the associated
    // VkFence is ready, vkGetFenceStatus will return VK_SUCCESS, and the
    // associated command buffer should be ready to use, so we return that
    // command buffer with the associated VkFence. If the associated VkFence is
    // not ready, vkGetFenceStatus will return VK_NOT_READY, we will continue to
    // search and test the next command buffer. If the VkFence is in an error
    // state, vkGetFenceStatus will return with other VkResult variants, we will
    // abort.
    for (auto& [commandBuffer, fence] : mTransferQueueCommandBufferPool) {
        auto res = vk->vkGetFenceStatus(mDevice, fence);
        if (res == VK_SUCCESS) {
            VK_CHECK(vk->vkResetFences(mDevice, 1, &fence));
            VK_CHECK(vk->vkResetCommandBuffer(commandBuffer,
                                              VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT));
            return std::make_tuple(commandBuffer, fence);
        }
        if (res == VK_NOT_READY) {
            continue;
        }
        // We either have a device lost, or an invalid fence state. For the device lost case,
        // VK_CHECK will ensure we capture the relevant streams.
        VK_CHECK(res);
    }
    VkCommandBuffer commandBuffer;
    VkCommandBufferAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = mCommandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VK_CHECK(vk->vkAllocateCommandBuffers(mDevice, &allocateInfo, &commandBuffer));
    VkFence fence;
    VkFenceCreateInfo fenceCi = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    VK_CHECK(vk->vkCreateFence(mDevice, &fenceCi, nullptr, &fence));

    const int cbIndex = static_cast<int>(mTransferQueueCommandBufferPool.size());
    mTransferQueueCommandBufferPool.emplace_back(commandBuffer, fence);

    GFXSTREAM_DEBUG(
        "Create a new command buffer for queue transfer for a total of %d "
        "transfer command buffers",
        (cbIndex + 1));

    mDebugUtilsHelper.addDebugLabel(commandBuffer, "QueueTransferCommandBuffer:%d", cbIndex);

    return std::make_tuple(commandBuffer, fence);
}

const VkImageLayout kGuestUseDefaultImageLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

void VkEmulation::releaseColorBufferForGuestUse(uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto infoPtr = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!infoPtr) {
        GFXSTREAM_ERROR("Failed to find ColorBuffer handle %d.",
                        static_cast<int>(colorBufferHandle));
        return;
    }

    std::optional<VkImageMemoryBarrier> layoutTransitionBarrier;
    if (infoPtr->currentLayout != kGuestUseDefaultImageLayout) {
        layoutTransitionBarrier = VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = infoPtr->currentLayout,
            .newLayout = kGuestUseDefaultImageLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = infoPtr->image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        infoPtr->currentLayout = kGuestUseDefaultImageLayout;
    }

    std::optional<VkImageMemoryBarrier> queueTransferBarrier;
    if (infoPtr->currentQueueFamilyIndex != VK_QUEUE_FAMILY_EXTERNAL) {
        queueTransferBarrier = VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = infoPtr->currentLayout,
            .newLayout = infoPtr->currentLayout,
            .srcQueueFamilyIndex = infoPtr->currentQueueFamilyIndex,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL,
            .image = infoPtr->image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        infoPtr->currentQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    }

    if (!layoutTransitionBarrier && !queueTransferBarrier) {
        return;
    }

    auto vk = mDvk;
    auto [commandBuffer, fence] = allocateQueueTransferCommandBufferLocked();

    VK_CHECK(vk->vkResetCommandBuffer(commandBuffer, 0));

    const VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    VK_CHECK(vk->vkBeginCommandBuffer(commandBuffer, &beginInfo));

    mDebugUtilsHelper.cmdBeginDebugLabel(
        commandBuffer, "releaseColorBufferForGuestUse(ColorBuffer:%d)", colorBufferHandle);

    if (layoutTransitionBarrier) {
        vk->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &layoutTransitionBarrier.value());
    }
    if (queueTransferBarrier) {
        vk->vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                 VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &queueTransferBarrier.value());
    }

    mDebugUtilsHelper.cmdEndDebugLabel(commandBuffer);

    VK_CHECK(vk->vkEndCommandBuffer(commandBuffer));

    const VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    {
        gfxstream::base::AutoLock queueLock(*mQueueLock);
        VK_CHECK(vk->vkQueueSubmit(mQueue, 1, &submitInfo, fence));
    }

    static constexpr uint64_t ANB_MAX_WAIT_NS = 5ULL * 1000ULL * 1000ULL * 1000ULL;
    VK_CHECK(vk->vkWaitForFences(mDevice, 1, &fence, VK_TRUE, ANB_MAX_WAIT_NS));
}

std::unique_ptr<BorrowedImageInfoVk> VkEmulation::borrowColorBufferForComposition(
    uint32_t colorBufferHandle, bool colorBufferIsTarget) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_ERROR("Invalid ColorBuffer handle %d.", static_cast<int>(colorBufferHandle));
        return nullptr;
    }

    auto compositorInfo = std::make_unique<BorrowedImageInfoVk>();
    compositorInfo->id = colorBufferInfo->handle;
    compositorInfo->width = colorBufferInfo->imageCreateInfoShallow.extent.width;
    compositorInfo->height = colorBufferInfo->imageCreateInfoShallow.extent.height;
    compositorInfo->image = colorBufferInfo->image;
    compositorInfo->imageView = colorBufferInfo->imageView;
    compositorInfo->imageCreateInfo = colorBufferInfo->imageCreateInfoShallow;
    compositorInfo->preBorrowLayout = colorBufferInfo->currentLayout;
    compositorInfo->preBorrowQueueFamilyIndex = colorBufferInfo->currentQueueFamilyIndex;
    if (colorBufferIsTarget && mDisplayVk) {
        // Instruct the compositor to perform the layout transition after use so
        // that it is ready to be blitted to the display.
        compositorInfo->postBorrowQueueFamilyIndex = mQueueFamilyIndex;
        compositorInfo->postBorrowLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    } else {
        // Instruct the compositor to perform the queue transfer release after use
        // so that the color buffer can be acquired by the guest.
        compositorInfo->postBorrowQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        compositorInfo->postBorrowLayout = colorBufferInfo->currentLayout;

        if (compositorInfo->postBorrowLayout == VK_IMAGE_LAYOUT_UNDEFINED) {
            compositorInfo->postBorrowLayout = kGuestUseDefaultImageLayout;
        }
    }

    colorBufferInfo->currentLayout = compositorInfo->postBorrowLayout;
    colorBufferInfo->currentQueueFamilyIndex = compositorInfo->postBorrowQueueFamilyIndex;

    return compositorInfo;
}

std::unique_ptr<BorrowedImageInfoVk> VkEmulation::borrowColorBufferForDisplay(
    uint32_t colorBufferHandle) {
    std::lock_guard<std::mutex> lock(mMutex);

    auto colorBufferInfo = gfxstream::base::find(mColorBuffers, colorBufferHandle);
    if (!colorBufferInfo) {
        GFXSTREAM_ERROR("Invalid ColorBuffer handle %d.", static_cast<int>(colorBufferHandle));
        return nullptr;
    }

    auto compositorInfo = std::make_unique<BorrowedImageInfoVk>();
    compositorInfo->id = colorBufferInfo->handle;
    compositorInfo->width = colorBufferInfo->imageCreateInfoShallow.extent.width;
    compositorInfo->height = colorBufferInfo->imageCreateInfoShallow.extent.height;
    compositorInfo->image = colorBufferInfo->image;
    compositorInfo->imageView = colorBufferInfo->imageView;
    compositorInfo->imageCreateInfo = colorBufferInfo->imageCreateInfoShallow;
    compositorInfo->preBorrowLayout = colorBufferInfo->currentLayout;
    compositorInfo->preBorrowQueueFamilyIndex = mQueueFamilyIndex;

    // Instruct the display to perform the queue transfer release after use so
    // that the color buffer can be acquired by the guest.
    compositorInfo->postBorrowQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
    compositorInfo->postBorrowLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    colorBufferInfo->currentLayout = compositorInfo->postBorrowLayout;
    colorBufferInfo->currentQueueFamilyIndex = compositorInfo->postBorrowQueueFamilyIndex;

    return compositorInfo;
}

std::optional<VkEmulation::RepresentativeColorBufferMemoryTypeInfo>
VkEmulation::findRepresentativeColorBufferMemoryTypeIndexLocked() {
    constexpr const uint32_t kArbitraryWidth = 64;
    constexpr const uint32_t kArbitraryHeight = 64;
    constexpr const uint32_t kArbitraryHandle = std::numeric_limits<uint32_t>::max();
    if (!createVkColorBufferLocked(kArbitraryWidth, kArbitraryHeight, GL_RGBA8,
                                   FrameworkFormat::FRAMEWORK_FORMAT_GL_COMPATIBLE,
                                   kArbitraryHandle, true, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        GFXSTREAM_ERROR("Failed to setup memory type index test ColorBuffer.");
        return std::nullopt;
    }

    uint32_t hostMemoryTypeIndex = 0;
    if (!getColorBufferAllocationInfoLocked(kArbitraryHandle, nullptr, &hostMemoryTypeIndex,
                                            nullptr, nullptr)) {
        GFXSTREAM_ERROR("Failed to lookup memory type index test ColorBuffer.");
        return std::nullopt;
    }

    if (!teardownVkColorBufferLocked(kArbitraryHandle)) {
        GFXSTREAM_ERROR("Failed to clean up memory type index test ColorBuffer.");
        return std::nullopt;
    }

    EmulatedPhysicalDeviceMemoryProperties helper(mDeviceInfo.memProps, hostMemoryTypeIndex,
                                                  mFeatures);
    uint32_t guestMemoryTypeIndex = helper.getGuestColorBufferMemoryTypeIndex();

    return VkEmulation::RepresentativeColorBufferMemoryTypeInfo{
        .hostMemoryTypeIndex = hostMemoryTypeIndex,
        .guestMemoryTypeIndex = guestMemoryTypeIndex,
    };
}

}  // namespace vk
}  // namespace gfxstream
