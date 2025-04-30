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

#include "VkDecoderGlobalState.h"
#include "VulkanDispatch.h"
#include "gfxstream/BumpPool.h"
#include "gfxstream/HealthMonitor.h"
#include "gfxstream/Metrics.h"
#include "utils/include/utils/GfxApiLogger.h"
#include "vulkan/VkCommonOperations.h"
#include "vulkan/VkDecoderContext.h"
#include "vulkan/testing/VkDecoderTestDispatch.h"

namespace gfxstream {
namespace vk {
namespace testing {

// This class provides facilities to write tests that call into the Vulkan API through VkDecoder and
// VkDecoderGlobalState.
//
// Usage:
//
//   TEST(MyVulkanTest, Test1) {
//     VulkanTestHelper vkTest;
//     vkTest.initialize();
//     // then use vkTest.vk() to start calling Vulkan APIs.
//   }
class VulkanTestHelper {
   public:
    // Only one instance of this class can exist at a time. (Enforced by locking a mutex on
    // construction). This is needed because VkDecoderGlobalState is a singleton, so we can't
    // allow tests to run in parallel.
    VulkanTestHelper();

    ~VulkanTestHelper();

    // Optional parameters for the `initialize()` function
    struct InitializationOptions {
        AstcEmulationMode astcLdrEmulationMode;
        std::optional<VkApplicationInfo> appInfo;
        VkPhysicalDeviceFeatures deviceFeatures;
        std::vector<std::string> enabledExtensions;  // enabled extensions for vkCreateInstance
    };

    void initialize(const InitializationOptions& options = {});

    // Destroys all the Vulkan objects. This is normally automatically called by the destructor but
    // can be called manually to allow checking if there are any validation errors at destruction.
    void destroy();

    // Whether the test should fail if there were Vulkan validation errors. Defaults to true.
    void failOnValidationErrors(bool value) { mFailOnValidationErrors = value; }

    bool hasValidationErrors() const;

    // Vulkan helper functions

    // Starts a command buffer
    VkCommandBuffer beginCommandBuffer();

    // Submits a command buffer
    void submitCommandBuffer(VkCommandBuffer commandBuffer);

    // Gets the index of a queue family that supports the requested queue flags, or aborts.
    uint32_t getQueueFamilyIndex(VkQueueFlagBits queueFlags);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    // Creates a new VkBuffer and associated memory
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, VkDeviceMemory& bufferMemory);

    // Calls vkCmdPipelineBarrier to change an image layout
    void transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image, VkImageLayout oldLayout,
                               VkImageLayout newLayout);

    // Accessors
    VkDecoderTestDispatch& vk() { return mTestDispatch; }
    VkInstance instance() { return mInstance; }
    VkDevice device() { return mDevice; }
    VkPhysicalDevice physDev() { return mPhysicalDevice; }
    VkCommandPool commandPool() { return mCommandPool; }
    VkQueue graphicsQueue() { return mGraphicsQueue; }

   private:
    static std::mutex mMutex;  // Locked for the entire lifetime of this class.
    std::lock_guard<std::mutex> mLock;
    VulkanDispatch* mVk;
    emugl::GfxApiLogger mLogger;
    std::unique_ptr<VkEmulation> mVkEmu;
    std::unique_ptr<::gfxstream::base::BumpPool> mBp;
    VkDecoderContext mDecoderContext;
    VkDecoderTestDispatch mTestDispatch;
    bool mFailOnValidationErrors = true;

    // Vulkan objects
    VkInstance mInstance = VK_NULL_HANDLE;
    VkPhysicalDevice mPhysicalDevice = VK_NULL_HANDLE;
    VkDevice mDevice = VK_NULL_HANDLE;
    VkCommandPool mCommandPool = VK_NULL_HANDLE;
    VkQueue mGraphicsQueue = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT mDebugMessenger = VK_NULL_HANDLE;
};

}  // namespace testing
}  // namespace vk
}  // namespace gfxstream
