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

uint32_t getMissingExtensions(const std::vector<VkExtensionProperties>& currentProps,
                              uint32_t enabledExtensionCount,
                              const char* const* ppEnabledExtensionNames,
                              std::string& outMissingExtensions) {
    uint32_t numMissing = 0;
    for (uint32_t i = 0; i < enabledExtensionCount; i++) {
        const char* extName = ppEnabledExtensionNames[i];
        if (!extensionSupported(currentProps, extName)) {
            if (!outMissingExtensions.empty()) {
                outMissingExtensions += ',';
            }
            outMissingExtensions += extName;
            numMissing++;
        }
    }

    return numMissing;
}

uint32_t getMissingFeatures(const VkPhysicalDeviceFeatures& supported,
                            const VkPhysicalDeviceFeatures& requested,
                            std::string& outMissingFeatures) {

#define CHECK_FEATURE(feature)                     \
    if (requested.feature && !supported.feature) { \
        if (!outMissingFeatures.empty()) {         \
            outMissingFeatures += ",";             \
        }                                          \
        outMissingFeatures += #feature;            \
        numMissing++;                              \
    }

    uint32_t numMissing = 0;
    CHECK_FEATURE(robustBufferAccess);
    CHECK_FEATURE(fullDrawIndexUint32);
    CHECK_FEATURE(imageCubeArray);
    CHECK_FEATURE(independentBlend);
    CHECK_FEATURE(geometryShader);
    CHECK_FEATURE(tessellationShader);
    CHECK_FEATURE(sampleRateShading);
    CHECK_FEATURE(dualSrcBlend);
    CHECK_FEATURE(logicOp);
    CHECK_FEATURE(multiDrawIndirect);
    CHECK_FEATURE(drawIndirectFirstInstance);
    CHECK_FEATURE(depthClamp);
    CHECK_FEATURE(depthBiasClamp);
    CHECK_FEATURE(fillModeNonSolid);
    CHECK_FEATURE(depthBounds);
    CHECK_FEATURE(wideLines);
    CHECK_FEATURE(largePoints);
    CHECK_FEATURE(alphaToOne);
    CHECK_FEATURE(multiViewport);
    CHECK_FEATURE(samplerAnisotropy);
    CHECK_FEATURE(textureCompressionETC2);
    CHECK_FEATURE(textureCompressionASTC_LDR);
    CHECK_FEATURE(textureCompressionBC);
    CHECK_FEATURE(occlusionQueryPrecise);
    CHECK_FEATURE(pipelineStatisticsQuery);
    CHECK_FEATURE(vertexPipelineStoresAndAtomics);
    CHECK_FEATURE(fragmentStoresAndAtomics);
    CHECK_FEATURE(shaderTessellationAndGeometryPointSize);
    CHECK_FEATURE(shaderImageGatherExtended);
    CHECK_FEATURE(shaderStorageImageExtendedFormats);
    CHECK_FEATURE(shaderStorageImageMultisample);
    CHECK_FEATURE(shaderStorageImageReadWithoutFormat);
    CHECK_FEATURE(shaderStorageImageWriteWithoutFormat);
    CHECK_FEATURE(shaderUniformBufferArrayDynamicIndexing);
    CHECK_FEATURE(shaderSampledImageArrayDynamicIndexing);
    CHECK_FEATURE(shaderStorageBufferArrayDynamicIndexing);
    CHECK_FEATURE(shaderStorageImageArrayDynamicIndexing);
    CHECK_FEATURE(shaderClipDistance);
    CHECK_FEATURE(shaderCullDistance);
    CHECK_FEATURE(shaderFloat64);
    CHECK_FEATURE(shaderInt64);
    CHECK_FEATURE(shaderInt16);
    CHECK_FEATURE(shaderResourceResidency);
    CHECK_FEATURE(shaderResourceMinLod);
    CHECK_FEATURE(sparseBinding);
    CHECK_FEATURE(sparseResidencyBuffer);
    CHECK_FEATURE(sparseResidencyImage2D);
    CHECK_FEATURE(sparseResidencyImage3D);
    CHECK_FEATURE(sparseResidency2Samples);
    CHECK_FEATURE(sparseResidency4Samples);
    CHECK_FEATURE(sparseResidency8Samples);
    CHECK_FEATURE(sparseResidency16Samples);
    CHECK_FEATURE(sparseResidencyAliased);
    CHECK_FEATURE(variableMultisampleRate);
    CHECK_FEATURE(inheritedQueries);
#undef CHECK_FEATURE

    return numMissing;
}

void clearShaderFloatControls2Features(VkPhysicalDeviceFeatures2* pFeatures) {
    if (!pFeatures) {
        return;
    }
    if (auto* fc2Features =
            vk_find_struct<VkPhysicalDeviceShaderFloatControls2Features>(pFeatures)) {
        fc2Features->shaderFloatControls2 = VK_FALSE;
    }
    if (auto* vulkan14Features = vk_find_struct<VkPhysicalDeviceVulkan14Features>(pFeatures)) {
        vulkan14Features->shaderFloatControls2 = VK_FALSE;
    }
}

VkResult validateShaderFloatControls2CreateInfo(const VkDeviceCreateInfo* pCreateInfo) {
    if (!pCreateInfo) {
        return VK_SUCCESS;
    }
    for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; ++i) {
        const char* extName =
            pCreateInfo->ppEnabledExtensionNames ? pCreateInfo->ppEnabledExtensionNames[i] : nullptr;
        if (extName && strcmp(extName, VK_KHR_SHADER_FLOAT_CONTROLS_2_EXTENSION_NAME) == 0) {
            return VK_ERROR_EXTENSION_NOT_PRESENT;
        }
    }
    const auto* fc2Features =
        vk_find_struct<VkPhysicalDeviceShaderFloatControls2Features>(pCreateInfo);
    const auto* vulkan14Features =
        vk_find_struct<VkPhysicalDeviceVulkan14Features>(pCreateInfo);
    if ((fc2Features != nullptr && fc2Features->shaderFloatControls2) ||
        (vulkan14Features != nullptr && vulkan14Features->shaderFloatControls2)) {
        return VK_ERROR_FEATURE_NOT_PRESENT;
    }
    return VK_SUCCESS;
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
        GfxstreamFormat::P010,
        GfxstreamFormat::P210,
    };
    for (const GfxstreamFormat format : kFormatsToPrepopulateSamplersFor) {
        YCbCrSamplerInfo unused;
        if (!getOrCreateSamplerInfo(format, &unused)) {
            GFXSTREAM_VERBOSE("YcbcrSamplerPool::init: format %s not supported for prepopulation",
                              ToString(format).c_str());
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
    if (mDvk && getOrCreateSamplerInfo(format, &info)) {
        return info.conversion;
    }
    return VK_NULL_HANDLE;
}

VkSampler YcbcrSamplerPool::getSampler(GfxstreamFormat format) {
    YCbCrSamplerInfo info;
    if (mDvk && getOrCreateSamplerInfo(format, &info)) {
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
    if (!outInfo) {
        GFXSTREAM_ERROR("%s: Invalid input", __func__);
        return false;
    }
    if (!mDvk) {
        GFXSTREAM_ERROR("Uninitialized YcbcrSamplerPool.");
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

    const VkFormatFeatureFlags kYcbcrFeatures =
        VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT |
        VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

    if ((formatFeatures & kYcbcrFeatures) == 0) {
        GFXSTREAM_INFO("Format %s does not support YCbCr sampler conversion", string_VkFormat(vkFormat));
        return false;
    }

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

void addNeededBarriersToUseImage(VkImage image, uint32_t preQueueFamilyIndex,
                                 VkImageLayout preLayout, uint32_t postQueueFamilyIndex,
                                 VkImageLayout postLayout, uint32_t usedQueueFamilyIndex,
                                 VkImageLayout usedInitialImageLayout,
                                 VkImageLayout usedFinalImageLayout, VkAccessFlags usedAccessMask,
                                 std::vector<VkImageMemoryBarrier>* preUseQueueTransferBarriers,
                                 std::vector<VkImageMemoryBarrier>* preUseLayoutTransitionBarriers,
                                 std::vector<VkImageMemoryBarrier>* postUseLayoutTransitionBarriers,
                                 std::vector<VkImageMemoryBarrier>* postUseQueueTransferBarriers) {
    if (preQueueFamilyIndex != usedQueueFamilyIndex) {
        const VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = preLayout,
            .newLayout = preLayout,
            .srcQueueFamilyIndex = preQueueFamilyIndex,
            .dstQueueFamilyIndex = usedQueueFamilyIndex,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        preUseQueueTransferBarriers->emplace_back(queueTransferBarrier);
    }
    if (preLayout != usedInitialImageLayout &&
        usedInitialImageLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
        const VkImageMemoryBarrier layoutTransitionBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = usedAccessMask,
            .oldLayout = preLayout,
            .newLayout = usedInitialImageLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        preUseLayoutTransitionBarriers->emplace_back(layoutTransitionBarrier);
    }
    if (postLayout != usedFinalImageLayout) {
        const VkImageMemoryBarrier layoutTransitionBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = usedAccessMask,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = usedFinalImageLayout,
            .newLayout = postLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        postUseLayoutTransitionBarriers->emplace_back(layoutTransitionBarrier);
    }
    if (postQueueFamilyIndex != usedQueueFamilyIndex) {
        const VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = postLayout,
            .newLayout = postLayout,
            .srcQueueFamilyIndex = usedQueueFamilyIndex,
            .dstQueueFamilyIndex = postQueueFamilyIndex,
            .image = image,
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };
        postUseQueueTransferBarriers->emplace_back(queueTransferBarrier);
    }
}

uint32_t vkFormatToAhbFormat(VkFormat format) {
    switch (format) {
        case VK_FORMAT_R8G8B8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SRGB:
            return 1;  // AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
        case VK_FORMAT_R8G8B8_UNORM:
            return 3;  // AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM
        case VK_FORMAT_R5G6B5_UNORM_PACK16:
            return 4;  // AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM
        case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_B8G8R8A8_SRGB:
            return 5;  // AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
        case VK_FORMAT_R16G16B16A16_SFLOAT:
            return 0x16;  // AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32:
            return 0x2b;  // AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM
        case VK_FORMAT_D16_UNORM:
            return 0x30;  // AHARDWAREBUFFER_FORMAT_D16_UNORM
        case VK_FORMAT_X8_D24_UNORM_PACK32:
            return 0x31;  // AHARDWAREBUFFER_FORMAT_D24_UNORM
        case VK_FORMAT_D24_UNORM_S8_UINT:
            return 0x32;  // AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT
        case VK_FORMAT_D32_SFLOAT:
            return 0x33;  // AHARDWAREBUFFER_FORMAT_D32_FLOAT
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return 0x34;  // AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT
        case VK_FORMAT_S8_UINT:
            return 0x35;  // AHARDWAREBUFFER_FORMAT_S8_UINT
        case VK_FORMAT_R8_UNORM:
            return 0x38;  // AHARDWAREBUFFER_FORMAT_R8_UNORM
        default:
            return 0;
    }
}

uint32_t ahbFormatBytesPerPixel(uint32_t ahbFormat) {
    switch (ahbFormat) {
        case 1:     // AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM
        case 2:     // AHARDWAREBUFFER_FORMAT_R8G8B8X8_UNORM
        case 5:     // AHARDWAREBUFFER_FORMAT_B8G8R8A8_UNORM
        case 0x2b:  // AHARDWAREBUFFER_FORMAT_R10G10B10A2_UNORM
        case 0x31:  // AHARDWAREBUFFER_FORMAT_D24_UNORM
        case 0x32:  // AHARDWAREBUFFER_FORMAT_D24_UNORM_S8_UINT
        case 0x33:  // AHARDWAREBUFFER_FORMAT_D32_FLOAT
            return 4;
        case 3:  // AHARDWAREBUFFER_FORMAT_R8G8B8_UNORM
            return 3;
        case 4:     // AHARDWAREBUFFER_FORMAT_R5G6B5_UNORM
        case 0x30:  // AHARDWAREBUFFER_FORMAT_D16_UNORM
            return 2;
        case 0x16:  // AHARDWAREBUFFER_FORMAT_R16G16B16A16_FLOAT
        case 0x34:  // AHARDWAREBUFFER_FORMAT_D32_FLOAT_S8_UINT
            return 8;
        case 0x35:  // AHARDWAREBUFFER_FORMAT_S8_UINT
        case 0x38:  // AHARDWAREBUFFER_FORMAT_R8_UNORM
            return 1;
        default:
            return 0;
    }
}

}  // namespace vk_util
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
