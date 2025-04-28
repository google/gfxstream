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

#include "VulkanTestHelper.h"

#include "gfxstream/host/logging.h"

namespace gfxstream {
namespace vk {
namespace testing {
namespace {

using ::android::base::BumpPool;

bool validationErrorsFound = false;

// Called back by the Vulkan validation layer in case of a validation error
VKAPI_ATTR VkBool32 VKAPI_CALL validationCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        GFXSTREAM_ERROR("Validation Layer: \"%s\"", pCallbackData->pMessage);
        validationErrorsFound = true;
    }
    return VK_FALSE;
}

gfxstream::host::FeatureSet getGfxstreamFeatures() {
    gfxstream::host::FeatureSet features;
    // Enable so that we can have VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    features.GlDirectMem.enabled = true;
    return features;
}

}  // namespace

std::mutex VulkanTestHelper::mMutex;

VulkanTestHelper::VulkanTestHelper()
    : mLock(mMutex),
      mVk(vkDispatch(/*forTesting=*/true)),
      mLogger(),
      mMetricsLogger(android::base::CreateMetricsLogger()),
      mHealthMonitor(*mMetricsLogger),
      mVkEmu(VkEmulation::create(mVk, {}, getGfxstreamFeatures())),
      mBp(std::make_unique<BumpPool>()),
      mDecoderContext(VkDecoderContext{.processName = "vulkan_test",
                                       .gfxApiLogger = &mLogger,
                                       .healthMonitor = &mHealthMonitor,
                                       .metricsLogger = mMetricsLogger.get()}),
      mTestDispatch(mVk, mBp.get(), &mDecoderContext) {
    validationErrorsFound = false;
}

void VulkanTestHelper::destroy() {
    if (mDevice) {
        vk().vkDeviceWaitIdle(mDevice);
        if (mCommandPool) vk().vkDestroyCommandPool(mDevice, mCommandPool, nullptr);
        vk().vkDestroyDevice(mDevice, nullptr);
    }
    if (mInstance) {
        if (mDebugMessenger) {
            vk().vkDestroyDebugUtilsMessengerEXT(mInstance, mDebugMessenger, nullptr);
        }
        vk().vkDestroyInstance(mInstance, nullptr);
    }

    mCommandPool = VK_NULL_HANDLE;
    mDevice = VK_NULL_HANDLE;
    mInstance = VK_NULL_HANDLE;
    mDebugMessenger = VK_NULL_HANDLE;

    VkDecoderGlobalState::reset();
    mVkEmu.reset();
}

VulkanTestHelper::~VulkanTestHelper() {
    destroy();
    if (mFailOnValidationErrors && validationErrorsFound) {
        FATAL() << "Validation errors found. Aborting.";
    }
}

void VulkanTestHelper::initialize(const InitializationOptions& options) {
    mVkEmu->initFeatures(VkEmulation::Features{
        .astcLdrEmulationMode = options.astcLdrEmulationMode,
    });

    // Check that the validation layer is present
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    uint32_t layerCount;
    vk().vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vk().vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool layerFound = false;
    for (const auto& layerProperties : availableLayers) {
        if (strcmp(validationLayer, layerProperties.layerName) == 0) {
            layerFound = true;
            break;
        }
    }
    if (!layerFound) FATAL() << "Vulkan Validation Layer not found";

    // Create the instance
    VkApplicationInfo defaultAppInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "vulkan_test",
        .pEngineName = "vulkan_test",
        .apiVersion = VK_API_VERSION_1_1,
    };

    VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = validationCallback,
    };

    std::vector<const char*> extensions = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
    for (const auto& extName : options.enabledExtensions) {
        extensions.push_back(extName.c_str());
    }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo,
        .pApplicationInfo = options.appInfo ? &options.appInfo.value() : &defaultAppInfo,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = &validationLayer,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    VK_CHECK(vk().vkCreateInstance(&createInfo, nullptr, &mInstance));

    // Setup validation layer callbacks
    VK_CHECK(vk().vkCreateDebugUtilsMessengerEXT(mInstance, &debugCreateInfo, nullptr,
                                                 &mDebugMessenger));

    // Pick a physical device
    uint32_t deviceCount = 0;
    vk().vkEnumeratePhysicalDevices(mInstance, &deviceCount, nullptr);
    if (deviceCount == 0) FATAL() << "No Vulkan device found.";
    std::vector<VkPhysicalDevice> devices(deviceCount);
    VK_CHECK(vk().vkEnumeratePhysicalDevices(mInstance, &deviceCount, devices.data()));

    mPhysicalDevice = devices[0];
    assert(mPhysicalDevice != VK_NULL_HANDLE);

    // Create the logical device
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = getQueueFamilyIndex(VK_QUEUE_GRAPHICS_BIT),
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    VkDeviceCreateInfo deviceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledLayerCount = 1,
        .ppEnabledLayerNames = &validationLayer,
        .pEnabledFeatures = &options.deviceFeatures,
    };
    VK_CHECK(vk().vkCreateDevice(mPhysicalDevice, &deviceCreateInfo, nullptr, &mDevice));

    // Get a graphics queue
    vk().vkGetDeviceQueue(mDevice, queueCreateInfo.queueFamilyIndex, 0, &mGraphicsQueue);
    assert(mGraphicsQueue != VK_NULL_HANDLE);

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = queueCreateInfo.queueFamilyIndex,
    };
    VK_CHECK(vk().vkCreateCommandPool(mDevice, &poolInfo, nullptr, &mCommandPool));
}

bool VulkanTestHelper::hasValidationErrors() const { return validationErrorsFound; }

VkCommandBuffer VulkanTestHelper::beginCommandBuffer() {
    VkCommandBufferAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = unbox_VkCommandPool(mCommandPool),
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer;
    vk().vkAllocateCommandBuffers(mDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vk().vkBeginCommandBuffer(commandBuffer, &beginInfo);
    return commandBuffer;
}

void VulkanTestHelper::submitCommandBuffer(VkCommandBuffer commandBuffer) {
    vk().vkEndCommandBuffer(commandBuffer);
    auto cmdBuf = unbox_VkCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &cmdBuf,
    };
    vk().vkQueueSubmit(mGraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vk().vkQueueWaitIdle(mGraphicsQueue);
    vk().vkFreeCommandBuffers(mDevice, mCommandPool, 1, &cmdBuf);
}

uint32_t VulkanTestHelper::getQueueFamilyIndex(VkQueueFlagBits queueFlags) {
    uint32_t queueFamilyCount = 0;
    vk().vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount, nullptr);

    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vk().vkGetPhysicalDeviceQueueFamilyProperties(mPhysicalDevice, &queueFamilyCount,
                                                  queueFamilies.data());
    for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilies.size()); i++) {
        if (queueFamilies[i].queueFlags & queueFlags) {
            return i;
        }
    }

    FATAL() << "No queue family found matching the requested flags";
}

uint32_t VulkanTestHelper::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vk().vkGetPhysicalDeviceMemoryProperties(mPhysicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    FATAL() << "failed to find suitable memory type!";
}

void VulkanTestHelper::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                    VkMemoryPropertyFlags properties, VkBuffer& buffer,
                                    VkDeviceMemory& bufferMemory) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VK_CHECK(vk().vkCreateBuffer(mDevice, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memRequirements;
    vk().vkGetBufferMemoryRequirements(mDevice, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties),
    };
    VK_CHECK(vk().vkAllocateMemory(mDevice, &allocInfo, nullptr, &bufferMemory));

    vk().vkBindBufferMemory(mDevice, buffer, bufferMemory, 0);
}

void VulkanTestHelper::transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
                                             VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = unbox_VkImage(image),
        .subresourceRange =
            {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1,
            },
    };

    switch (oldLayout) {
        case VK_IMAGE_LAYOUT_UNDEFINED:
            barrier.srcAccessMask = 0;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            FATAL() << "Unsupported layout transition!";
    }

    switch (newLayout) {
        case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            break;
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            break;
        case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
            if (barrier.srcAccessMask == 0) {
                barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
            }
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            break;
        default:
            FATAL() << "Unsupported layout transition!";
    }
    vk().vkCmdPipelineBarrier(cmdBuf, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1,
                              &barrier);
}

}  // namespace testing
}  // namespace vk
}  // namespace gfxstream
