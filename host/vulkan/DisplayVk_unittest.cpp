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

// Note: needs to be included before DisplayVk to avoid conflicts
// between gtest and x11 headers.
#include <gtest/gtest.h>

#include "DisplayVk.h"

#include "BorrowedImageVk.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/host/testing/SampleApplication.h"
#include "gfxstream/host/testing/OSWindow.h"
#include "gfxstream/host/testing/VkTestUtils.h"
#include "vulkan/VulkanDispatch.h"

namespace gfxstream {
namespace vk {
namespace {

class DisplayVkTest : public ::testing::Test {
   protected:
    using RenderTexture = RenderTextureVk;

    static void SetUpTestCase() { k_vk = vkDispatch(false); }

    void SetUp() override {
        // skip the test when testing without a window
        if (!shouldUseWindow()) {
            GTEST_SKIP();
        }
        ASSERT_NE(k_vk, nullptr);

        createInstance();
        createWindowAndSurface();
        m_window = createOrGetTestWindow(0, 0, k_width, k_height);
        pickPhysicalDevice();
        createLogicalDevice();
        k_vk->vkGetDeviceQueue(m_vkDevice, m_compositorQueueFamilyIndex, 0, &m_compositorVkQueue);
        m_compositorVkQueueLock = std::make_shared<gfxstream::base::Lock>();
        k_vk->vkGetDeviceQueue(m_vkDevice, m_swapChainQueueFamilyIndex, 0, &m_swapChainVkQueue);
        m_swapChainVkQueueLock = std::make_shared<gfxstream::base::Lock>();
        VkCommandPoolCreateInfo commandPoolCi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_compositorQueueFamilyIndex};
        ASSERT_EQ(k_vk->vkCreateCommandPool(m_vkDevice, &commandPoolCi, nullptr, &m_vkCommandPool),
                  VK_SUCCESS);
        m_displayVk = std::make_unique<DisplayVk>(
            *k_vk, m_vkPhysicalDevice, m_swapChainQueueFamilyIndex, m_compositorQueueFamilyIndex,
            m_vkDevice, m_compositorVkQueue, m_compositorVkQueueLock, m_swapChainVkQueue,
            m_swapChainVkQueueLock);
        m_displaySurface = std::make_unique<gfxstream::DisplaySurface>(
            k_width, k_height,
            DisplaySurfaceVk::create(*k_vk, m_vkInstance, m_window->getNativeWindow()));
        ASSERT_NE(m_displaySurface, nullptr);
        m_displayVk->bindToSurface(m_displaySurface.get());
    }

    void TearDown() override {
        if (shouldUseWindow()) {
            ASSERT_EQ(k_vk->vkQueueWaitIdle(m_compositorVkQueue), VK_SUCCESS);
            ASSERT_EQ(k_vk->vkQueueWaitIdle(m_swapChainVkQueue), VK_SUCCESS);

            m_displayVk.reset();
            k_vk->vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
            k_vk->vkDestroyDevice(m_vkDevice, nullptr);
            k_vk->vkDestroySurfaceKHR(m_vkInstance, m_vkSurface, nullptr);
            k_vk->vkDestroyInstance(m_vkInstance, nullptr);
        }
    }

    std::unique_ptr<BorrowedImageInfoVk> createBorrowedImageInfo(
        const std::unique_ptr<const RenderTexture>& texture) {
        static uint32_t sTextureId = 0;

        auto info = std::make_unique<BorrowedImageInfoVk>();
        info->id = sTextureId++;
        info->width = texture->m_vkImageCreateInfo.extent.width;
        info->height = texture->m_vkImageCreateInfo.extent.height;
        info->image = texture->m_vkImage;
        info->imageCreateInfo = texture->m_vkImageCreateInfo;
        info->preBorrowLayout = RenderTexture::k_vkImageLayout;
        info->preBorrowQueueFamilyIndex = m_compositorQueueFamilyIndex;
        info->postBorrowLayout = RenderTexture::k_vkImageLayout;
        info->postBorrowQueueFamilyIndex = m_compositorQueueFamilyIndex;
        return info;
    }

    static const VulkanDispatch* k_vk;
    static constexpr uint32_t k_width = 0x100;
    static constexpr uint32_t k_height = 0x100;

    OSWindow *m_window;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkSurfaceKHR m_vkSurface = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    uint32_t m_swapChainQueueFamilyIndex = 0;
    uint32_t m_compositorQueueFamilyIndex = 0;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkQueue m_compositorVkQueue = VK_NULL_HANDLE;
    std::shared_ptr<gfxstream::base::Lock> m_compositorVkQueueLock;
    VkQueue m_swapChainVkQueue = VK_NULL_HANDLE;
    std::shared_ptr<gfxstream::base::Lock> m_swapChainVkQueueLock;
    VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
    std::unique_ptr<DisplayVk> m_displayVk = nullptr;
    std::unique_ptr<DisplaySurface> m_displaySurface = nullptr;

   private:
    void createInstance() {
        VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                     .pNext = nullptr,
                                     .pApplicationName = "emulator SwapChainStateVk unittest",
                                     .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
                                     .pEngineName = "No Engine",
                                     .engineVersion = VK_MAKE_VERSION(1, 0, 0),
                                     .apiVersion = VK_API_VERSION_1_1};
        auto extensions = SwapChainStateVk::getRequiredInstanceExtensions();
        VkInstanceCreateInfo instanceCi = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()};
        ASSERT_EQ(k_vk->vkCreateInstance(&instanceCi, nullptr, &m_vkInstance), VK_SUCCESS);
        ASSERT_TRUE(m_vkInstance != VK_NULL_HANDLE);
    }

    void createWindowAndSurface() {
        m_window = createOrGetTestWindow(0, 0, k_width, k_height);
        ASSERT_NE(m_window, nullptr);
        // TODO(kaiyili, b/179477624): add support for other platforms
#ifdef _WIN32
        VkWin32SurfaceCreateInfoKHR surfaceCi = {
            .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
            .hinstance = GetModuleHandle(nullptr),
            .hwnd = m_window->getNativeWindow()};
        ASSERT_EQ(k_vk->vkCreateWin32SurfaceKHR(m_vkInstance, &surfaceCi, nullptr, &m_vkSurface),
                  VK_SUCCESS);
#endif
    }

    void pickPhysicalDevice() {
        uint32_t physicalDeviceCount = 0;
        ASSERT_EQ(k_vk->vkEnumeratePhysicalDevices(m_vkInstance, &physicalDeviceCount, nullptr),
                  VK_SUCCESS);
        ASSERT_GT(physicalDeviceCount, 0);
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        ASSERT_EQ(k_vk->vkEnumeratePhysicalDevices(m_vkInstance, &physicalDeviceCount,
                                                   physicalDevices.data()),
                  VK_SUCCESS);
        for (const auto &device : physicalDevices) {
            uint32_t queueFamilyCount = 0;
            k_vk->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            ASSERT_GT(queueFamilyCount, 0);
            std::vector<VkQueueFamilyProperties> queueProps(queueFamilyCount);
            k_vk->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                                           queueProps.data());
            std::optional<uint32_t> maybeSwapChainQueueFamilyIndex = std::nullopt;
            std::optional<uint32_t> maybeCompositorQueueFamilyIndex = std::nullopt;
            for (uint32_t queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount;
                 queueFamilyIndex++) {
                if (!maybeSwapChainQueueFamilyIndex.has_value() &&
                    SwapChainStateVk::validateQueueFamilyProperties(*k_vk, device, m_vkSurface,
                                                                    queueFamilyIndex) &&
                    SwapChainStateVk::createSwapChainCi(*k_vk, m_vkSurface, device, k_width,
                                                        k_height, {queueFamilyIndex})) {
                    maybeSwapChainQueueFamilyIndex = queueFamilyIndex;
                }
                if (!maybeCompositorQueueFamilyIndex.has_value() &&
                    CompositorVk::queueSupportsComposition(queueProps[queueFamilyIndex])) {
                    maybeCompositorQueueFamilyIndex = queueFamilyIndex;
                }
            }
            if (!maybeSwapChainQueueFamilyIndex.has_value() ||
                !maybeCompositorQueueFamilyIndex.has_value()) {
                continue;
            }
            m_swapChainQueueFamilyIndex = maybeSwapChainQueueFamilyIndex.value();
            m_compositorQueueFamilyIndex = maybeCompositorQueueFamilyIndex.value();
            m_vkPhysicalDevice = device;
            return;
        }
        FAIL() << "Can't find a suitable VkPhysicalDevice.";
    }

    void createLogicalDevice() {
        const float queuePriority = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> queueCis(0);
        for (auto queueFamilyIndex : std::unordered_set<uint32_t>(
                 {m_swapChainQueueFamilyIndex, m_compositorQueueFamilyIndex})) {
            VkDeviceQueueCreateInfo queueCi = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                               .queueFamilyIndex = queueFamilyIndex,
                                               .queueCount = 1,
                                               .pQueuePriorities = &queuePriority};
            queueCis.push_back(queueCi);
        }
        VkPhysicalDeviceFeatures2 features = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
                                              .pNext = nullptr};
        auto extensions = SwapChainStateVk::getRequiredDeviceExtensions();
        VkDeviceCreateInfo deviceCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features,
            .queueCreateInfoCount = static_cast<uint32_t>(queueCis.size()),
            .pQueueCreateInfos = queueCis.data(),
            .enabledLayerCount = 0,
            .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = nullptr};
        ASSERT_EQ(k_vk->vkCreateDevice(m_vkPhysicalDevice, &deviceCi, nullptr, &m_vkDevice),
                  VK_SUCCESS);
        ASSERT_TRUE(m_vkDevice != VK_NULL_HANDLE);
    }
};

const VulkanDispatch* DisplayVkTest::k_vk = nullptr;

TEST_F(DisplayVkTest, Init) {}

TEST_F(DisplayVkTest, PostWithoutSurfaceShouldntCrash) {
    uint32_t textureWidth = 20;
    uint32_t textureHeight = 40;
    DisplayVk displayVk(*k_vk, m_vkPhysicalDevice, m_swapChainQueueFamilyIndex,
                        m_compositorQueueFamilyIndex, m_vkDevice, m_compositorVkQueue,
                        m_compositorVkQueueLock, m_swapChainVkQueue, m_swapChainVkQueueLock);
    auto texture = RenderTexture::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                         m_vkCommandPool, textureWidth, textureHeight);
    std::vector<uint32_t> pixels(textureWidth * textureHeight, 0);
    ASSERT_TRUE(texture->write(pixels));
    const auto imageInfo = createBorrowedImageInfo(texture);
    displayVk.post(imageInfo.get());
}

TEST_F(DisplayVkTest, SimplePost) {
    uint32_t textureWidth = 20;
    uint32_t textureHeight = 40;
    auto texture = RenderTexture::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                         m_vkCommandPool, textureWidth, textureHeight);
    std::vector<uint32_t> pixels(textureWidth * textureHeight);
    for (int i = 0; i < textureHeight; i++) {
        for (int j = 0; j < textureWidth; j++) {
            uint8_t *pixel = reinterpret_cast<uint8_t *>(&pixels[i * textureWidth + j]);
            pixel[0] = static_cast<uint8_t>((i * 0xff / textureHeight) & 0xff);
            pixel[1] = static_cast<uint8_t>((j * 0xff / textureWidth) & 0xff);
            pixel[2] = 0;
            pixel[3] = 0xff;
        }
    }
    ASSERT_TRUE(texture->write(pixels));
    std::vector<std::shared_future<void>> waitForGpuFutures;
    for (uint32_t i = 0; i < 10; i++) {
        const auto imageInfo = createBorrowedImageInfo(texture);
        auto postResult = m_displayVk->post(imageInfo.get());
        ASSERT_TRUE(postResult.success);
        waitForGpuFutures.emplace_back(std::move(postResult.postCompletedWaitable));
    }
    for (auto &waitForGpuFuture : waitForGpuFutures) {
        waitForGpuFuture.wait();
    }
}

TEST_F(DisplayVkTest, PostTwoColorBuffers) {
    uint32_t textureWidth = 20;
    uint32_t textureHeight = 40;
    auto redTexture =
        RenderTexture::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                              m_vkCommandPool, textureWidth, textureHeight);
    auto greenTexture =
        RenderTexture::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                              m_vkCommandPool, textureWidth, textureHeight);
    uint32_t red = 0xff0000ff;
    uint32_t green = 0xff00ff00;
    std::vector<uint32_t> redPixels(textureWidth * textureHeight, red);
    std::vector<uint32_t> greenPixels(textureWidth * textureHeight, green);
    ASSERT_TRUE(redTexture->write(redPixels));
    ASSERT_TRUE(greenTexture->write(greenPixels));
    std::vector<std::shared_future<void>> waitForGpuFutures;
    for (uint32_t i = 0; i < 10; i++) {
        const auto redImageInfo = createBorrowedImageInfo(redTexture);
        const auto greenImageInfo = createBorrowedImageInfo(greenTexture);
        auto redPostResult = m_displayVk->post(redImageInfo.get());
        ASSERT_TRUE(redPostResult.success);
        waitForGpuFutures.emplace_back(std::move(redPostResult.postCompletedWaitable));

        auto greenPostResult = m_displayVk->post(greenImageInfo.get());
        ASSERT_TRUE(greenPostResult.success);
        waitForGpuFutures.emplace_back(std::move(greenPostResult.postCompletedWaitable));
    }
    for (auto &waitForGpuFuture : waitForGpuFutures) {
        waitForGpuFuture.wait();
    }
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream
