// Copyright 2025 The Android Open Source Project
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

#include "swap_chain_state_vk.h"

#include <cinttypes>
#include <unordered_set>

#include "gfxstream/common/logging.h"
#include "vulkan/vk_enum_string_helper.h"
#include "vulkan/vk_utils.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

void swap(SwapchainCreateInfoWrapper& a, SwapchainCreateInfoWrapper& b) {
    std::swap(a.mQueueFamilyIndices, b.mQueueFamilyIndices);
    std::swap(a.mCreateInfo, b.mCreateInfo);
    // The C++ spec guarantees that after std::swap is called, all iterators and references of the
    // container remain valid, and the past-the-end iterator is invalidated. Therefore, no need to
    // reset the VkSwapchainCreateInfoKHR::pQueueFamilyIndices.
}

}  // namespace

SwapchainCreateInfoWrapper::SwapchainCreateInfoWrapper(const VkSwapchainCreateInfoKHR& createInfo)
    : mCreateInfo(createInfo) {
    if (createInfo.pNext) {
        GFXSTREAM_FATAL("VkSwapchainCreateInfoKHR with pNext in the chain is not supported.");
    }

    if (createInfo.pQueueFamilyIndices && (createInfo.queueFamilyIndexCount > 0)) {
        setQueueFamilyIndices(std::vector<uint32_t>(
            createInfo.pQueueFamilyIndices,
            createInfo.pQueueFamilyIndices + createInfo.queueFamilyIndexCount));
    } else {
        setQueueFamilyIndices({});
    }
}

SwapchainCreateInfoWrapper::SwapchainCreateInfoWrapper(const SwapchainCreateInfoWrapper& other)
    : mCreateInfo(other.mCreateInfo) {
    if (other.mCreateInfo.pNext) {
        GFXSTREAM_FATAL("VkSwapchainCreateInfoKHR with pNext in the chain is not supported.");
    }
    setQueueFamilyIndices(other.mQueueFamilyIndices);
}

SwapchainCreateInfoWrapper& SwapchainCreateInfoWrapper::operator=(
    const SwapchainCreateInfoWrapper& other) {
    SwapchainCreateInfoWrapper tmp(other);
    swap(*this, tmp);
    return *this;
}

void SwapchainCreateInfoWrapper::setQueueFamilyIndices(
    const std::vector<uint32_t>& queueFamilyIndices) {
    mQueueFamilyIndices = queueFamilyIndices;
    mCreateInfo.queueFamilyIndexCount = static_cast<uint32_t>(mQueueFamilyIndices.size());
    if (mQueueFamilyIndices.empty()) {
        mCreateInfo.pQueueFamilyIndices = nullptr;
    } else {
        mCreateInfo.pQueueFamilyIndices = queueFamilyIndices.data();
    }
}

std::unique_ptr<SwapChainStateVk> SwapChainStateVk::createSwapChainVk(
    const VulkanDispatch& vk, VkDevice vkDevice, const VkSwapchainCreateInfoKHR& swapChainCi) {
    std::unique_ptr<SwapChainStateVk> swapChainVk(new SwapChainStateVk(vk, vkDevice));
    if (swapChainVk->initSwapChainStateVk(swapChainCi) != VK_SUCCESS) {
        return nullptr;
    }
    return swapChainVk;
}

SwapChainStateVk::SwapChainStateVk(const VulkanDispatch& vk, VkDevice vkDevice)
    : m_vk(vk),
      m_vkDevice(vkDevice),
      m_vkSwapChain(VK_NULL_HANDLE),
      m_vkImages(0),
      m_vkImageViews(0) {}

VkResult SwapChainStateVk::initSwapChainStateVk(const VkSwapchainCreateInfoKHR& swapChainCi) {
    VkResult res = m_vk.vkCreateSwapchainKHR(m_vkDevice, &swapChainCi, nullptr, &m_vkSwapChain);
    if (res == VK_ERROR_INITIALIZATION_FAILED) return res;
    VK_CHECK(res);
    uint32_t imageCount = 0;
    VK_CHECK(m_vk.vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapChain, &imageCount, nullptr));
    m_vkImageExtent = swapChainCi.imageExtent;
    m_vkImages.resize(imageCount);
    VK_CHECK(
        m_vk.vkGetSwapchainImagesKHR(m_vkDevice, m_vkSwapChain, &imageCount, m_vkImages.data()));

    m_vkImageViews.resize(imageCount);
    m_vkRenderPasses.resize(imageCount);
    m_vkFramebuffers.resize(imageCount);

    for (size_t i = 0; i < m_vkImages.size(); i++) {
        VkImageViewCreateInfo imageViewCi = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = m_vkImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = k_vkFormat,
            .components = {.r = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                           .a = VK_COMPONENT_SWIZZLE_IDENTITY},
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1}};
        VK_CHECK(m_vk.vkCreateImageView(m_vkDevice, &imageViewCi, nullptr, &m_vkImageViews[i]));

        VkAttachmentDescription colorAttachment = {
            .format = k_vkFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        const VkAttachmentReference colorAttachmentRef = {
            .attachment = 0,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };

        const VkSubpassDescription subpass = {
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorAttachmentRef,
        };

        const VkSubpassDependency subpassDependency = {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = 0,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        };

        const VkRenderPassCreateInfo renderPassCi = {
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorAttachment,
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &subpassDependency,
        };

        VK_CHECK(m_vk.vkCreateRenderPass(m_vkDevice, &renderPassCi, nullptr, &m_vkRenderPasses[i]));

        const VkFramebufferCreateInfo framebufferCi = {
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .flags = 0,
            .renderPass = m_vkRenderPasses[i],
            .attachmentCount = 1,
            .pAttachments = &m_vkImageViews[i],
            .width = m_vkImageExtent.width,
            .height = m_vkImageExtent.height,
            .layers = 1,
        };
        VK_CHECK(m_vk.vkCreateFramebuffer(m_vkDevice, &framebufferCi, nullptr, &m_vkFramebuffers[i]));
    }

    return VK_SUCCESS;
}

SwapChainStateVk::~SwapChainStateVk() {
    for (auto imageView : m_vkImageViews) {
        m_vk.vkDestroyImageView(m_vkDevice, imageView, nullptr);
    }
    if (m_vkSwapChain != VK_NULL_HANDLE) {
        m_vk.vkDestroySwapchainKHR(m_vkDevice, m_vkSwapChain, nullptr);
    }
}

std::vector<const char*> SwapChainStateVk::getRequiredInstanceExtensions() {
    return {
            VK_KHR_SURFACE_EXTENSION_NAME,
#ifdef _WIN32
            VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
#endif
#ifdef __APPLE__
            VK_EXT_METAL_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
            VK_KHR_XCB_SURFACE_EXTENSION_NAME,
#endif
#ifdef VK_USE_PLATFORM_SCREEN_QNX
            VK_QNX_SCREEN_SURFACE_EXTENSION_NAME,
#endif
    };
}

std::vector<const char*> SwapChainStateVk::getRequiredDeviceExtensions() {
    return {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
}

bool SwapChainStateVk::validateQueueFamilyProperties(const VulkanDispatch& vk,
                                                     VkPhysicalDevice physicalDevice,
                                                     VkSurfaceKHR surface,
                                                     uint32_t queueFamilyIndex) {
    VkBool32 presentSupport = VK_FALSE;
    VK_CHECK(vk.vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface,
                                                     &presentSupport));
    return presentSupport;
}

std::optional<SwapchainCreateInfoWrapper> SwapChainStateVk::createSwapChainCi(
    const VulkanDispatch& vk, VkSurfaceKHR surface, VkPhysicalDevice physicalDevice, uint32_t width,
    uint32_t height, const std::unordered_set<uint32_t>& queueFamilyIndices) {
    uint32_t formatCount = 0;
    VK_CHECK(
        vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VkResult res = vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                                           formats.data());
    // b/217226027: drivers may return VK_INCOMPLETE with pSurfaceFormatCount returned by
    // vkGetPhysicalDeviceSurfaceFormatsKHR. Retry here as a work around to the potential driver
    // bug.
    if (res == VK_INCOMPLETE) {
        formatCount = (formatCount + 1) * 2;
        GFXSTREAM_INFO(
            "VK_INCOMPLETE returned by vkGetPhysicalDeviceSurfaceFormatsKHR. A possible driver "
            "bug. Retry with *pSurfaceFormatCount = %" PRIu32 ".",
            formatCount);
        formats.resize(formatCount);
        res = vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount,
                                                      formats.data());
        formats.resize(formatCount);
    }
    if (res == VK_INCOMPLETE) {
        GFXSTREAM_INFO(
            "VK_INCOMPLETE still returned by vkGetPhysicalDeviceSurfaceFormatsKHR with retry. A "
            "possible driver bug.");
    } else {
        VK_CHECK(res);
    }
    auto iSurfaceFormat =
        std::find_if(formats.begin(), formats.end(), [](const VkSurfaceFormatKHR& format) {
            return format.format == k_vkFormat && format.colorSpace == k_vkColorSpace;
        });
    if (iSurfaceFormat == formats.end()) {
        GFXSTREAM_ERROR("Failed to create swapchain: the format(%#" PRIx64
                        ") with color space(%#" PRIx64 ") not supported.",
                        static_cast<uint64_t>(k_vkFormat), static_cast<uint64_t>(k_vkColorSpace));
        return std::nullopt;
    }

    uint32_t presentModeCount = 0;
    VK_CHECK(vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                          &presentModeCount, nullptr));
    std::vector<VkPresentModeKHR> presentModes_(presentModeCount);
    VK_CHECK(vk.vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface,
                                                          &presentModeCount, presentModes_.data()));
    std::unordered_set<VkPresentModeKHR> presentModes(presentModes_.begin(), presentModes_.end());
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    if (!presentModes.count(VK_PRESENT_MODE_FIFO_KHR)) {
        GFXSTREAM_ERROR("Failed to create swapchain: FIFO present mode not supported.");
        return std::nullopt;
    }
    VkFormatProperties formatProperties = {};
    vk.vkGetPhysicalDeviceFormatProperties(physicalDevice, k_vkFormat, &formatProperties);
    // According to the spec, a presentable image is equivalent to a non-presentable image created
    // with the VK_IMAGE_TILING_OPTIMAL tiling parameter.
    VkFormatFeatureFlags formatFeatures = formatProperties.optimalTilingFeatures;
    if (!(formatFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
        // According to VUID-vkCmdBlitImage-dstImage-02000, the format features of dstImage must
        // contain VK_FORMAT_FEATURE_BLIT_DST_BIT.
        GFXSTREAM_ERROR(
            "The format %s with the optimal tiling doesn't support VK_FORMAT_FEATURE_BLIT_DST_BIT. "
            "The supported features are %s.",
            string_VkFormat(k_vkFormat), string_VkFormatFeatureFlags(formatFeatures).c_str());
        return std::nullopt;
    }

    VkImageUsageFlags requiredUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkSurfaceCapabilitiesKHR surfaceCaps;
    VK_CHECK(vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfaceCaps));
    if ((surfaceCaps.supportedUsageFlags & requiredUsageFlags) != requiredUsageFlags) {
        GFXSTREAM_ERROR(
            "The supported usage flags of the presentable images is %s, and don't contain "
            "all required flags '%s'.",
            string_VkImageUsageFlags(surfaceCaps.supportedUsageFlags).c_str(),
            string_VkImageUsageFlags(requiredUsageFlags).c_str());
        return std::nullopt;
    }
    std::optional<VkExtent2D> maybeExtent = std::nullopt;
    if (surfaceCaps.currentExtent.width != UINT32_MAX && surfaceCaps.currentExtent.width == width &&
        surfaceCaps.currentExtent.height == height) {
        maybeExtent = surfaceCaps.currentExtent;
    } else if (width >= surfaceCaps.minImageExtent.width &&
               width <= surfaceCaps.maxImageExtent.width &&
               height >= surfaceCaps.minImageExtent.height &&
               height <= surfaceCaps.maxImageExtent.height) {
        maybeExtent = VkExtent2D({width, height});
    }
    if (!maybeExtent.has_value()) {
        GFXSTREAM_ERROR("Failed to create swapchain: extent(%" PRIu64 "x%" PRIu64
                        ") not supported.",
                        static_cast<uint64_t>(width), static_cast<uint64_t>(height));
        return std::nullopt;
    }
    auto extent = maybeExtent.value();
    uint32_t imageCount = surfaceCaps.minImageCount + 1;
    if (surfaceCaps.maxImageCount != 0 && surfaceCaps.maxImageCount < imageCount) {
        imageCount = surfaceCaps.maxImageCount;
    }
    SwapchainCreateInfoWrapper swapChainCi(VkSwapchainCreateInfoKHR{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = VkSwapchainCreateFlagsKHR{0},
        .surface = surface,
        .minImageCount = imageCount,
        .imageFormat = iSurfaceFormat->format,
        .imageColorSpace = iSurfaceFormat->colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = requiredUsageFlags,
        .imageSharingMode = VkSharingMode{},
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = surfaceCaps.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE});
    if (queueFamilyIndices.empty()) {
        GFXSTREAM_ERROR("Failed to create swapchain: no Vulkan queue family specified.");
        return std::nullopt;
    }
    if (queueFamilyIndices.size() == 1) {
        swapChainCi.mCreateInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapChainCi.setQueueFamilyIndices({});
    } else {
        swapChainCi.mCreateInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        swapChainCi.setQueueFamilyIndices(
            std::vector<uint32_t>(queueFamilyIndices.begin(), queueFamilyIndices.end()));
    }
    return std::optional(swapChainCi);
}

VkFormat SwapChainStateVk::getFormat() { return k_vkFormat; }

VkExtent2D SwapChainStateVk::getImageExtent() const { return m_vkImageExtent; }

const std::vector<VkImage>& SwapChainStateVk::getVkImages() const { return m_vkImages; }

const std::vector<VkImageView>& SwapChainStateVk::getVkImageViews() const { return m_vkImageViews; }

const std::vector<VkRenderPass>& SwapChainStateVk::getVkRenderPasses() const { return m_vkRenderPasses; }

const std::vector<VkFramebuffer>& SwapChainStateVk::getVkFramebuffers() const { return m_vkFramebuffers; }

VkSwapchainKHR SwapChainStateVk::getSwapChain() const { return m_vkSwapChain; }

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
