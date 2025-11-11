/*
 * Copyright (C) 2011-2021 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "vk_utils.h"

#include <cstring>

#include "vk_format_utils.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace vk_util {
namespace {

std::unique_ptr<CallbacksWrapper<VkCheckCallbacks>> gVkCheckCallbacks =
    std::make_unique<CallbacksWrapper<VkCheckCallbacks>>(nullptr);

}  // namespace

void setVkCheckCallbacks(std::unique_ptr<VkCheckCallbacks> callbacks) {
    gVkCheckCallbacks = std::make_unique<CallbacksWrapper<VkCheckCallbacks>>(std::move(callbacks));
}

const CallbacksWrapper<VkCheckCallbacks>& getVkCheckCallbacks() { return *gVkCheckCallbacks; }

std::optional<uint32_t> findMemoryType(const VulkanDispatch* ivk, VkPhysicalDevice physicalDevice,
                                       uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    ivk->vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return std::nullopt;
}

bool extensionSupported(const std::vector<VkExtensionProperties>& currentProps,
                        const char* wantedExtName) {
    for (uint32_t i = 0; i < currentProps.size(); ++i) {
        if (!strcmp(wantedExtName, currentProps[i].extensionName)) {
            return true;
        }
    }
    return false;
}

bool extensionsSupported(const std::vector<VkExtensionProperties>& currentProps,
                         const std::vector<const char*>& wantedExtNames) {
    for (size_t i = 0; i < wantedExtNames.size(); ++i) {
        if (!extensionSupported(currentProps, wantedExtNames[i])) {
            GFXSTREAM_DEBUG("%s not found, bailing.", wantedExtNames[i]);
            return false;
        }
    }
    return true;
}

bool YcbcrSamplerPool::init(const VulkanDispatch* ivk, const VulkanDispatch* dvk,
                            VkPhysicalDevice physicalDevice, VkDevice device) {
    if (mDvk) {
        // Already initialized
        GFXSTREAM_ERROR("Failed to initialize YcbcrSamplerPool: already initialized");
        return false;
    }
    if (!dvk || !ivk || device == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE) {
        GFXSTREAM_ERROR("Failed to initialize YcbcrSamplerPool: invalid parameters");
        return false;
    }
    mIvk = ivk;
    mDvk = dvk;
    mDevice = device;
    mPhysicalDevice = physicalDevice;

    // Create some conversion objects for known formats
    const std::vector<GfxstreamFormat> kFormatsToPrepopulateSamplersFor = {
        GfxstreamFormat::NV12,
        GfxstreamFormat::NV21,
        GfxstreamFormat::YV12,
        GfxstreamFormat::YV21,
    };
    for (const GfxstreamFormat format : kFormatsToPrepopulateSamplersFor) {
        YCbCrSamplerInfo unused;
        if (!getOrCreateSamplerInfo(format, &unused)) {
            return false;
        }
    }
    return true;
}

void YcbcrSamplerPool::destroy() {
    std::lock_guard<std::mutex> lock(mMutex);
    for (auto iter : m_ycbcrSamplers) {
        mDvk->vkDestroySamplerYcbcrConversion(mDevice, iter.second.conversion, nullptr);
        mDvk->vkDestroySampler(mDevice, iter.second.sampler, nullptr);
    }
    m_ycbcrSamplers.clear();
    mIvk = nullptr;
    mDvk = nullptr;
    mDevice = VK_NULL_HANDLE;
    mPhysicalDevice = VK_NULL_HANDLE;
}

VkSamplerYcbcrConversion YcbcrSamplerPool::getConversion(GfxstreamFormat format) {
    YCbCrSamplerInfo info;
    if (getOrCreateSamplerInfo(format, &info)) {
        return info.conversion;
    }
    return VK_NULL_HANDLE;
}

VkSampler YcbcrSamplerPool::getSampler(GfxstreamFormat format) {
    YCbCrSamplerInfo info;
    if (getOrCreateSamplerInfo(format, &info)) {
        return info.sampler;
    }
    return VK_NULL_HANDLE;
}

std::vector<GfxstreamFormat> YcbcrSamplerPool::getAllFormats() const {
    std::vector<GfxstreamFormat> ret;
    std::lock_guard<std::mutex> lock(mMutex);
    ret.reserve(m_ycbcrSamplers.size());
    for (auto iter : m_ycbcrSamplers) {
        ret.push_back(iter.first);
    }
    return ret;
}

bool YcbcrSamplerPool::getOrCreateSamplerInfo(GfxstreamFormat format, YCbCrSamplerInfo* outInfo) {
    if (!outInfo || !mDvk) {
        GFXSTREAM_FATAL("Uninitialized  YcbcrSamplerPool.");
        return false;
    }

    auto vkFormatOpt = ToVkFormat(format);
    if (!vkFormatOpt) {
        const std::string formatString = ToString(format);
        GFXSTREAM_ERROR("Unhandled format %s.", formatString.c_str());
        return false;
    }
    const VkFormat vkFormat = *vkFormatOpt;

    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = m_ycbcrSamplers.find(format);
    if (iter != m_ycbcrSamplers.end()) {
        *outInfo = iter->second;
        return true;
    }

    GFXSTREAM_VERBOSE("Creating YCbCr sampler for %s", string_VkFormat(vkFormat));
    outInfo->conversion = VK_NULL_HANDLE;
    outInfo->sampler = VK_NULL_HANDLE;

    // TODO: move this to another common helper class in vk_util.h. (also
    // CompositorVk::getFormatFeatures) and better handling of the tiling mode.
    VkFormatProperties formatProperties = {};
    mIvk->vkGetPhysicalDeviceFormatProperties(mPhysicalDevice, vkFormat, &formatProperties);
    const VkFormatFeatureFlags formatFeatures = formatProperties.optimalTilingFeatures;

    // VUID-VkSamplerYcbcrConversionCreateInfo-xChromaOffset-01652
    // If the potential format features of the sampler Y′CBCR conversion do not support
    // VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT, xChromaOffset and yChromaOffset must not
    // be VK_CHROMA_LOCATION_MIDPOINT if the corresponding components are downsampled
    VkChromaLocation chromaLoc = VK_CHROMA_LOCATION_MIDPOINT;
    if ((formatFeatures & VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT) == 0) {
        chromaLoc = VK_CHROMA_LOCATION_COSITED_EVEN;
    }

    // The host vulkan driver is not aware of the plane ordering for YUV formats used
    // in the guest and simply knows that the format "layout" is one of:
    //
    //  * VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16
    //  * VK_FORMAT_G8_B8R8_2PLANE_420_UNORM
    //  * VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM
    //
    // With this, the host needs to adjust the component swizzle based on plane
    // ordering to ensure that the channels are interpreted correctly.
    //
    // From the Vulkan spec's "Sampler Y'CBCR Conversion" section:
    //
    //  * Y comes from the G-channel (after swizzle)
    //  * U (CB) comes from the B-channel (after swizzle)
    //  * V (CR) comes from the R-channel (after swizzle)
    //
    // See
    // https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html#textures-sampler-YCbCr-conversion
    //
    // To match the above, the sampler needs to swizzle such that:
    //
    //  * Y ends up in the G-channel
    //  * U (CB) ends up in the B-channel
    //  * V (CR) ends up in the R-channel
    auto chromaOrderingOpt = GetYuvChromaOrdering(format);
    if (!chromaOrderingOpt) {
        const std::string formatString = ToString(format);
        GFXSTREAM_ERROR("Unhandled format %s", formatString.c_str());
        return false;
    }
    auto chromaOrdering = *chromaOrderingOpt;
    const VkComponentMapping components =
        chromaOrdering == YuvChromaOrdering::UV ?
            VkComponentMapping{
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            } :
            VkComponentMapping{
                .r = VK_COMPONENT_SWIZZLE_B,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_R,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY,
            };

    // Create the VkSamplerYcbcrConversion object with correct format
    const VkSamplerYcbcrConversionCreateInfo ycbcrConversionCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .pNext = nullptr,
        .format = vkFormat,
        .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_601,
        .ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_NARROW,
        .components = components,
        .xChromaOffset = chromaLoc,
        .yChromaOffset = chromaLoc,
        .chromaFilter = VK_FILTER_NEAREST,
        .forceExplicitReconstruction = VK_FALSE,
    };
    VkResult res = mDvk->vkCreateSamplerYcbcrConversion(mDevice, &ycbcrConversionCreateInfo,
                                                        nullptr, &outInfo->conversion);
    if (res != VK_SUCCESS) {
        GFXSTREAM_ERROR("Failed to create VkSamplerYcbcrConversion for %s: %s.",
                        string_VkFormat(vkFormat), string_VkResult(res));
        return false;
    }

    // Create the VkSampler
    VkSamplerYcbcrConversionInfo conversionInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .pNext = nullptr,
        .conversion = outInfo->conversion,
    };

    // All address modes must be VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE when ycbcr is used
    constexpr const VkSamplerAddressMode kYcbcrSamplerMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

    const VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .pNext = &conversionInfo,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = kYcbcrSamplerMode,
        .addressModeV = kYcbcrSamplerMode,
        .addressModeW = kYcbcrSamplerMode,
        .mipLodBias = 0.0f,
        .anisotropyEnable = VK_FALSE,
        .maxAnisotropy = 1.0f,
        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,
        .minLod = 0.0f,
        .maxLod = 0.0f,
        .borderColor = VK_BORDER_COLOR_INT_TRANSPARENT_BLACK,
        .unnormalizedCoordinates = VK_FALSE,
    };

    res = mDvk->vkCreateSampler(mDevice, &samplerInfo, nullptr, &outInfo->sampler);
    if (res != VK_SUCCESS) {
        mDvk->vkDestroySamplerYcbcrConversion(mDevice, outInfo->conversion, nullptr);
        GFXSTREAM_ERROR("Failed to create VkSampler for %s: %s.", string_VkFormat(vkFormat),
                        string_VkResult(res));
        return false;
    }

    // Cache in the pool
    m_ycbcrSamplers[format] = *outInfo;

    return true;
}

}  // namespace vk_util
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
