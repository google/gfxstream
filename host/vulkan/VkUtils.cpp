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

#include "VkUtils.h"

namespace gfxstream {
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

bool YcbcrSamplerPool::init(const VulkanDispatch* vk, VkDevice device) {
    if (mDvk || !vk || device == VK_NULL_HANDLE) {
        // Already initialized or invalid parameters
        GFXSTREAM_ERROR("Cannot initialize YcbcrSamplerPool!");
        return false;
    }
    mDvk = vk;
    mDevice = device;

    // Create some conversion objects for known formats
    std::array<VkFormat, 2> prePopulateFormats = {
        VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM,
        VK_FORMAT_G8_B8R8_2PLANE_420_UNORM,
    };
    for (VkFormat format : prePopulateFormats) {
        YCbCrSamplerInfo temp;
        if (!getOrCreateSamplerInfo(format, &temp)) {
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
    mDvk = nullptr;
    mDevice = VK_NULL_HANDLE;
}

VkSamplerYcbcrConversion YcbcrSamplerPool::getConversion(VkFormat format) {
    YCbCrSamplerInfo info;
    if (getOrCreateSamplerInfo(format, &info)) {
        return info.conversion;
    }
    return VK_NULL_HANDLE;
}

VkSampler YcbcrSamplerPool::getSampler(VkFormat format) {
    YCbCrSamplerInfo info;
    if (getOrCreateSamplerInfo(format, &info)) {
        return info.sampler;
    }
    return VK_NULL_HANDLE;
}

std::vector<VkFormat> YcbcrSamplerPool::getAllFormats() const {
    std::vector<VkFormat> ret;
    std::lock_guard<std::mutex> lock(mMutex);
    ret.reserve(m_ycbcrSamplers.size());
    for (auto iter : m_ycbcrSamplers) {
        ret.push_back(iter.first);
    }
    return ret;
}

bool YcbcrSamplerPool::getOrCreateSamplerInfo(VkFormat format, YCbCrSamplerInfo* outInfo) {
    if (!outInfo || !mDvk) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mMutex);
    auto iter = m_ycbcrSamplers.find(format);
    if (iter != m_ycbcrSamplers.end()) {
        *outInfo = iter->second;
        return true;
    }

    GFXSTREAM_VERBOSE("Creating ycbcr sampler for format %s", string_VkFormat(format));
    outInfo->conversion = VK_NULL_HANDLE;
    outInfo->sampler = VK_NULL_HANDLE;

    // Create the VkSamplerYcbcrConversion object with correct format
    const VkSamplerYcbcrConversionCreateInfo ycbcrConversionCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO,
        .pNext = nullptr,
        .format = format,
        .ycbcrModel = VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_709,
        .ycbcrRange = VK_SAMPLER_YCBCR_RANGE_ITU_FULL,
        .components = {VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                       VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY},
        .xChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .yChromaOffset = VK_CHROMA_LOCATION_MIDPOINT,
        .chromaFilter = VK_FILTER_NEAREST,
        .forceExplicitReconstruction = VK_FALSE};
    VkResult res = mDvk->vkCreateSamplerYcbcrConversion(mDevice, &ycbcrConversionCreateInfo,
                                                        nullptr, &outInfo->conversion);
    if (res != VK_SUCCESS) {
        GFXSTREAM_ERROR("%s: Could not create ycbcr conversion for format: %s, err:%d", __func__,
                        string_VkFormat(format), res);
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

        // Make this a softer error than crashing the emulator
        GFXSTREAM_ERROR("%s: Could not create ycbcr sampler for format: %s, err:%d", __func__,
                        string_VkFormat(format), res);
        return false;
    }

    // Cache in the poool
    m_ycbcrSamplers[format] = *outInfo;

    return true;
}

}  // namespace vk_util
}  // namespace vk
}  // namespace gfxstream
