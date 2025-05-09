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


#include <gtest/gtest.h>

#include "CompositorVk.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <glm/gtx/matrix_transform_2d.hpp>
#include <memory>
#include <optional>

#include "BorrowedImageVk.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/ImageUtils.h"
#include "gfxstream/host/testing/VkTestUtils.h"
#include "vulkan/VulkanDispatch.h"
#include "vulkan/vk_util.h"

#ifdef BAZEL_CURRENT_REPOSITORY
#include "rules_cc/cc/runfiles/runfiles.h"
#endif

namespace gfxstream {
namespace vk {
namespace {

static constexpr const bool kDefaultSaveImageIfComparisonFailed = false;

std::string GetTestDataPath(const std::string& basename) {
#ifdef BAZEL_CURRENT_REPOSITORY
    // https://github.com/bazelbuild/rules_cc/blob/main/cc/runfiles/runfiles.h
    using rules_cc::cc::runfiles::Runfiles;
    static Runfiles* sRunfiles = []() -> Runfiles* {
        std::string error;
        auto* runfiles = Runfiles::CreateForTest(&error);
        if (runfiles == nullptr) {
            ADD_FAILURE() << "Failed to load runfiles: " << error;
            return nullptr;
        }
        return runfiles;
    }();
    if (sRunfiles == nullptr) {
        ADD_FAILURE() << "Testdata runfiles not available.";
        return "";
    }
    const std::vector<std::string> possiblePaths = {
        std::string("_main/host/vulkan/testdata/") + basename,
        std::string("host/vulkan/testdata/") + basename,
    };
    for (const std::string& possiblePath : possiblePaths) {
        const std::string path = sRunfiles->Rlocation(possiblePath);
        if (!path.empty() && std::filesystem::exists(path)) {
            return path;
        }
    }
    ADD_FAILURE() << "Failed to find " << basename << " testdata file.";
    return "";

#else
    const std::filesystem::path currentPath = gfxstream::base::getProgramDirectory();
    return (currentPath / "testdata" / basename).string();
#endif
}

static constexpr const uint32_t kColorBlack = 0xFF000000;
static constexpr const uint32_t kColorRed = 0xFF0000FF;
static constexpr const uint32_t kColorGreen = 0xFF00FF00;
static constexpr const uint32_t kDefaultImageWidth = 256;
static constexpr const uint32_t kDefaultImageHeight = 256;

class CompositorVkTest : public ::testing::Test {
   protected:
    using TargetImage = RenderResourceVk<VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT>;
    using SourceImage = RenderTextureVk;

    static void SetUpTestCase() { k_vk = vkDispatch(false); }

    void SetUp() override {
#if defined(__APPLE__) && defined(__arm64__)
        GTEST_SKIP() << "Skipping all test on Apple M2, as they are failing, see b/263494782";
#endif
        ASSERT_NE(k_vk, nullptr);
        createInstance();
        pickPhysicalDevice();
        createLogicalDevice();

        if (!supportsFeatures(TargetImage::k_vkFormat, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            GTEST_SKIP() << "Skipping test as format " << TargetImage::k_vkFormat
                         << " does not support VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT";
        }
        if (!supportsFeatures(SourceImage::k_vkFormat, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            GTEST_SKIP() << "Skipping test as format " << SourceImage::k_vkFormat
                         << " does not support VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT";
        }

        const VkCommandPoolCreateInfo commandPoolCi = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .queueFamilyIndex = m_compositorQueueFamilyIndex,
        };
        ASSERT_EQ(k_vk->vkCreateCommandPool(m_vkDevice, &commandPoolCi, nullptr, &m_vkCommandPool),
                  VK_SUCCESS);

        k_vk->vkGetDeviceQueue(m_vkDevice, m_compositorQueueFamilyIndex, 0, &m_compositorVkQueue);
        ASSERT_NE(m_compositorVkQueue, VK_NULL_HANDLE);

        m_compositorVkQueueLock = std::make_shared<gfxstream::base::Lock>();
    }

    void TearDown() override {
#if defined(__APPLE__) && defined(__arm64__)
        return;
#endif

        k_vk->vkDestroyCommandPool(m_vkDevice, m_vkCommandPool, nullptr);
        k_vk->vkDestroyDevice(m_vkDevice, nullptr);
        m_vkDevice = VK_NULL_HANDLE;
        k_vk->vkDestroyInstance(m_vkInstance, nullptr);
        m_vkInstance = VK_NULL_HANDLE;
    }

    std::unique_ptr<CompositorVk> createCompositor() {
        return CompositorVk::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                    m_compositorVkQueueLock, m_compositorQueueFamilyIndex,
                                    /*maxFramesInFlight=*/3);
    }

    template <typename SourceOrTargetImage>
    std::unique_ptr<const SourceOrTargetImage> createImageWithColor(uint32_t sourceWidth,
                                                                    uint32_t sourceHeight,
                                                                    uint32_t sourceColor) {
        auto source =
            SourceOrTargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                        m_vkCommandPool, sourceWidth, sourceHeight);
        if (source == nullptr) {
            return nullptr;
        }

        std::vector<uint32_t> sourcePixels(sourceWidth * sourceHeight, sourceColor);
        if (!source->write(sourcePixels)) {
            return nullptr;
        }

        return source;
    }

    std::unique_ptr<const SourceImage> createSourceImageFromPng(const std::string& filename) {
        uint32_t sourceWidth;
        uint32_t sourceHeight;
        std::vector<uint32_t> sourcePixels;
        if (!LoadRGBAFromPng(filename, &sourceWidth, &sourceHeight, &sourcePixels)) {
            return nullptr;
        }

        auto source =
            SourceImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                m_vkCommandPool, sourceWidth, sourceHeight);
        if (source == nullptr) {
            return nullptr;
        }

        if (!source->write(sourcePixels)) {
            return nullptr;
        }

        return source;
    }

    bool isRGBAPixelNear(uint32_t actualPixel, uint32_t expectedPixel) {
        const uint8_t* actualRGBA = reinterpret_cast<const uint8_t*>(&actualPixel);
        const uint8_t* expectedRGBA = reinterpret_cast<const uint8_t*>(&expectedPixel);

        constexpr const uint32_t kRGBA8888Tolerance = 2;
        for (uint32_t channel = 0; channel < 4; channel++) {
            const uint8_t actualChannel = actualRGBA[channel];
            const uint8_t expectedChannel = expectedRGBA[channel];

            if ((std::max(actualChannel, expectedChannel) -
                 std::min(actualChannel, expectedChannel)) > kRGBA8888Tolerance) {
                return false;
            }
        }
        return true;
    }

    bool compareRGBAPixels(const uint32_t* actualPixels, const uint32_t* expectedPixels,
                           const uint32_t width, const uint32_t height) {
        bool comparisonFailed = false;

        uint32_t reportedIncorrectPixels = 0;
        constexpr const uint32_t kMaxReportedIncorrectPixels = 10;

        for (uint32_t y = 0; y < width; y++) {
            for (uint32_t x = 0; x < height; x++) {
                const uint32_t actualPixel = actualPixels[y * height + x];
                const uint32_t expectedPixel = expectedPixels[y * width + x];
                if (!isRGBAPixelNear(actualPixel, expectedPixel)) {
                    comparisonFailed = true;
                    if (reportedIncorrectPixels < kMaxReportedIncorrectPixels) {
                        reportedIncorrectPixels++;
                        const uint8_t* actualRGBA = reinterpret_cast<const uint8_t*>(&actualPixel);
                        const uint8_t* expectedRGBA =
                            reinterpret_cast<const uint8_t*>(&expectedPixel);
                        ADD_FAILURE()
                            << "Pixel comparison failed at (" << x << ", " << y << ") "
                            << " with actual "
                            << " r:" << static_cast<int>(actualRGBA[0])
                            << " g:" << static_cast<int>(actualRGBA[1])
                            << " b:" << static_cast<int>(actualRGBA[2])
                            << " a:" << static_cast<int>(actualRGBA[3]) << " but expected "
                            << " r:" << static_cast<int>(expectedRGBA[0])
                            << " g:" << static_cast<int>(expectedRGBA[1])
                            << " b:" << static_cast<int>(expectedRGBA[2])
                            << " a:" << static_cast<int>(expectedRGBA[3]);
                    }
                }
            }
        }
        return comparisonFailed;
    }

    void compareImageWithGoldenPng(const TargetImage* target, const std::string& filename,
                                   const bool saveImageIfComparisonFailed) {
        const uint32_t targetWidth = target->m_width;
        const uint32_t targetHeight = target->m_height;
        const auto targetPixelsOpt = target->read();
        ASSERT_TRUE(targetPixelsOpt.has_value());
        const auto& targetPixels = *targetPixelsOpt;

        uint32_t goldenWidth;
        uint32_t goldenHeight;
        std::vector<uint32_t> goldenPixels;
        const bool loadedGolden =
            LoadRGBAFromPng(filename, &goldenWidth, &goldenHeight, &goldenPixels);
        EXPECT_TRUE(loadedGolden) << "Failed to load golden image from " << filename;

        bool comparisonFailed = !loadedGolden;

        if (loadedGolden) {
            EXPECT_EQ(target->m_width, goldenWidth)
                << "Invalid width comparison with golden image from " << filename;
            EXPECT_EQ(target->m_height, goldenHeight)
                << "Invalid height comparison with golden image from " << filename;
            if (targetWidth != goldenWidth || targetHeight != goldenHeight) {
                comparisonFailed = true;
            }
            if (!comparisonFailed) {
                comparisonFailed = compareRGBAPixels(targetPixels.data(), goldenPixels.data(),
                                                     goldenWidth, goldenHeight);
            }
        }

        if (saveImageIfComparisonFailed && comparisonFailed) {
            const std::string output = (std::filesystem::temp_directory_path() /
                                        std::filesystem::path(filename).filename())
                                           .string();
            SaveRGBAToPng(targetWidth, targetHeight, targetPixels.data(), output);
            ADD_FAILURE() << "Saved composition result to " << output;
        }
    }

    template <typename SourceOrTargetImage>
    std::unique_ptr<BorrowedImageInfoVk> createBorrowedImageInfo(const SourceOrTargetImage* image) {
        static int sImageId = 0;

        auto ret = std::make_unique<BorrowedImageInfoVk>();
        ret->id = sImageId++;
        ret->width = image->m_width;
        ret->height = image->m_height;
        ret->image = image->m_vkImage;
        ret->imageCreateInfo = image->m_vkImageCreateInfo;
        ret->imageView = image->m_vkImageView;
        ret->preBorrowLayout = SourceOrTargetImage::k_vkImageLayout;
        ret->preBorrowQueueFamilyIndex = m_compositorQueueFamilyIndex;
        ret->postBorrowLayout = SourceOrTargetImage::k_vkImageLayout;
        ret->postBorrowQueueFamilyIndex = m_compositorQueueFamilyIndex;
        return ret;
    }

    void checkImageFilledWith(const TargetImage* image, uint32_t expectedColor) {
        auto actualPixelsOpt = image->read();
        ASSERT_TRUE(actualPixelsOpt.has_value());
        auto& actualPixels = *actualPixelsOpt;

        const std::vector<uint32_t> expectedPixels(image->numOfPixels(), expectedColor);
        compareRGBAPixels(actualPixels.data(), expectedPixels.data(), image->m_width,
                          image->m_height);
    }

    void fillImageWith(const TargetImage* image, uint32_t color) {
        const std::vector<uint32_t> pixels(image->numOfPixels(), color);
        ASSERT_TRUE(image->write(pixels)) << "Failed to fill image with color:" << color;
        checkImageFilledWith(image, color);
    }

    static const VulkanDispatch* k_vk;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    VkPhysicalDevice m_vkPhysicalDevice = VK_NULL_HANDLE;
    uint32_t m_compositorQueueFamilyIndex = 0;
    VkDevice m_vkDevice = VK_NULL_HANDLE;
    VkCommandPool m_vkCommandPool = VK_NULL_HANDLE;
    VkQueue m_compositorVkQueue = VK_NULL_HANDLE;
    std::shared_ptr<gfxstream::base::Lock> m_compositorVkQueueLock;

   private:
    void createInstance() {
        const VkApplicationInfo appInfo = {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "emulator CompositorVk unittest",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "No Engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_1,
        };
        const VkInstanceCreateInfo instanceCi = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pApplicationInfo = &appInfo,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = nullptr,
        };
        ASSERT_EQ(k_vk->vkCreateInstance(&instanceCi, nullptr, &m_vkInstance), VK_SUCCESS);
        ASSERT_NE(m_vkInstance, VK_NULL_HANDLE);
    }

    void pickPhysicalDevice() {
        uint32_t physicalDeviceCount = 0;
        ASSERT_EQ(k_vk->vkEnumeratePhysicalDevices(m_vkInstance, &physicalDeviceCount, nullptr),
                  VK_SUCCESS);
        ASSERT_GT(physicalDeviceCount, (uint32_t)0);
        std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
        ASSERT_EQ(k_vk->vkEnumeratePhysicalDevices(m_vkInstance, &physicalDeviceCount,
                                                   physicalDevices.data()),
                  VK_SUCCESS);
        for (const auto &device : physicalDevices) {
            uint32_t queueFamilyCount = 0;
            k_vk->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);
            ASSERT_GT(queueFamilyCount, (uint32_t)0);
            std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
            k_vk->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                                           queueFamilyProperties.data());
            uint32_t queueFamilyIndex = 0;
            for (; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++) {
                if (CompositorVk::queueSupportsComposition(
                        queueFamilyProperties[queueFamilyIndex])) {
                    break;
                }
            }
            if (queueFamilyIndex == queueFamilyCount) {
                continue;
            }

            m_compositorQueueFamilyIndex = queueFamilyIndex;
            m_vkPhysicalDevice = device;
            return;
        }
        FAIL() << "Can't find a suitable VkPhysicalDevice.";
    }

    void createLogicalDevice() {
        const float queuePriority = 1.0f;
        const VkDeviceQueueCreateInfo queueCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_compositorQueueFamilyIndex,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };
        const VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = nullptr,
        };
        const VkDeviceCreateInfo deviceCi = {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueCi,
            .enabledLayerCount = 0,
            .enabledExtensionCount = 0,
            .ppEnabledExtensionNames = nullptr,
        };
        ASSERT_EQ(k_vk->vkCreateDevice(m_vkPhysicalDevice, &deviceCi, nullptr, &m_vkDevice),
                  VK_SUCCESS);
        ASSERT_NE(m_vkDevice, VK_NULL_HANDLE);
    }

    bool supportsFeatures(VkFormat format, VkFormatFeatureFlags features) {
        VkFormatProperties formatProperties = {};
        k_vk->vkGetPhysicalDeviceFormatProperties(m_vkPhysicalDevice, format, &formatProperties);
        return (formatProperties.optimalTilingFeatures & features) == features;
    }
};

const VulkanDispatch* CompositorVkTest::k_vk = nullptr;

TEST_F(CompositorVkTest, QueueSupportsComposition) {
    VkQueueFamilyProperties properties = {};

    properties.queueFlags &= ~VK_QUEUE_GRAPHICS_BIT;
    ASSERT_FALSE(CompositorVk::queueSupportsComposition(properties));

    properties.queueFlags |= VK_QUEUE_GRAPHICS_BIT;
    ASSERT_TRUE(CompositorVk::queueSupportsComposition(properties));
}

TEST_F(CompositorVkTest, Init) { ASSERT_NE(createCompositor(), nullptr); }

TEST_F(CompositorVkTest, EmptyCompositionShouldDrawABlackFrame) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    std::vector<std::unique_ptr<const TargetImage>> targets;
    constexpr const uint32_t kNumImages = 10;
    for (uint32_t i = 0; i < kNumImages; i++) {
        auto target =
            TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                m_vkCommandPool, kDefaultImageWidth, kDefaultImageHeight);
        ASSERT_NE(target, nullptr);
        fillImageWith(target.get(), kColorRed);
        targets.emplace_back(std::move(target));
    }

    for (uint32_t i = 0; i < kNumImages; i++) {
        const Compositor::CompositionRequest compositionRequest = {
            .target = createBorrowedImageInfo(targets[i].get()),
            .layers = {},  // Note: this is empty!
        };

        auto compositionCompleteWaitable = compositor->compose(compositionRequest);
        compositionCompleteWaitable.wait();
    }

    for (const auto& target : targets) {
        checkImageFilledWith(target.get(), kColorBlack);
    }
}

TEST_F(CompositorVkTest, SimpleComposition) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source = createSourceImageFromPng(GetTestDataPath("256x256_android.png"));
    ASSERT_NE(source, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, source->m_width, source->m_height);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 64,
                        .top = 32,
                        .right = 128,
                        .bottom = 160,
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(),
                              GetTestDataPath("256x256_golden_simple_composition.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, BlendPremultiplied) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source =
        createSourceImageFromPng(GetTestDataPath("256x256_android_with_transparency.png"));
    ASSERT_NE(source, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, source->m_width, source->m_height);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(),
                              GetTestDataPath("256x256_golden_blend_premultiplied.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, Crop) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source = createSourceImageFromPng(GetTestDataPath("256x256_android.png"));
    ASSERT_NE(source, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, source->m_width, source->m_height);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width) / 2.0f,
                        .bottom = static_cast<float>(source->m_height) / 2.0f,
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(), GetTestDataPath("256x256_golden_crop.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, SolidColor) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, 256, 256);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = nullptr,
        .props =
            {
                .composeMode = HWC2_COMPOSITION_SOLID_COLOR,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .alpha = 0.75f,
                .color =
                    {
                        .r = 255,
                        .g = 255,
                        .b = 0,
                        .a = 255,
                    },
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(), GetTestDataPath("256x256_golden_solid_color.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, SolidColorBelow) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source =
        createSourceImageFromPng(GetTestDataPath("256x256_android_with_transparency.png"));
    ASSERT_NE(source, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, source->m_width, source->m_height);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = nullptr,
        .props =
            {
                .composeMode = HWC2_COMPOSITION_SOLID_COLOR,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .alpha = 1.0f,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 255,
                        .a = 255,
                    },
            },
    });
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(), GetTestDataPath("256x256_golden_solid_color_below.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, SolidColorAbove) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source = createSourceImageFromPng(GetTestDataPath("256x256_android.png"));
    ASSERT_NE(source, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, source->m_width, source->m_height);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = nullptr,
        .props =
            {
                .composeMode = HWC2_COMPOSITION_SOLID_COLOR,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .alpha = 1.0f,
                .color =
                    {
                        .r = 0,
                        .g = 255,
                        .b = 0,
                        .a = 127,
                    },
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(), GetTestDataPath("256x256_golden_solid_color_above.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

TEST_F(CompositorVkTest, Transformations) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source = createSourceImageFromPng(GetTestDataPath("256x256_android.png"));
    ASSERT_NE(source, nullptr);

    Compositor::CompositionRequest compositionRequest;
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 32,
                        .top = 32,
                        .right = 224,
                        .bottom = 224,
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    const std::unordered_map<hwc_transform_t, std::string> transformToGolden = {
        {HWC_TRANSFORM_NONE, "256x256_golden_transform_none.png"},
        {HWC_TRANSFORM_FLIP_H, "256x256_golden_transform_fliph.png"},
        {HWC_TRANSFORM_FLIP_V, "256x256_golden_transform_flipv.png"},
        {HWC_TRANSFORM_ROT_90, "256x256_golden_transform_rot90.png"},
        {HWC_TRANSFORM_ROT_180, "256x256_golden_transform_rot180.png"},
        {HWC_TRANSFORM_ROT_270, "256x256_golden_transform_rot270.png"},
        {HWC_TRANSFORM_FLIP_H_ROT_90, "256x256_golden_transform_fliphrot90.png"},
        {HWC_TRANSFORM_FLIP_V_ROT_90, "256x256_golden_transform_flipvrot90.png"},
    };

    for (const auto& [transform, golden] : transformToGolden) {
        auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice,
                                          m_compositorVkQueue, m_vkCommandPool, 256, 256);
        ASSERT_NE(target, nullptr);
        fillImageWith(target.get(), kColorBlack);

        compositionRequest.target = createBorrowedImageInfo(target.get());
        compositionRequest.layers[0].props.transform = transform;
        compositionRequest.layers[0].source = createBorrowedImageInfo(source.get());

        auto compositionCompleteWaitable = compositor->compose(compositionRequest);
        compositionCompleteWaitable.wait();

        compareImageWithGoldenPng(target.get(), GetTestDataPath(golden),
                                  kDefaultSaveImageIfComparisonFailed);
    }
}

TEST_F(CompositorVkTest, MultipleTargetsComposition) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    constexpr const uint32_t kNumCompostions = 10;

    auto source = createImageWithColor<SourceImage>(256, 256, kColorGreen);
    ASSERT_NE(source, nullptr);

    std::vector<std::unique_ptr<const TargetImage>> targets;
    for (uint32_t i = 0; i < kNumCompostions; i++) {
        auto target = createImageWithColor<TargetImage>(256, 256, kColorBlack);
        ASSERT_NE(target, nullptr);
        targets.emplace_back(std::move(target));
    }

    Compositor::CompositionRequest compositionRequest = {};
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = 0,
                        .bottom = static_cast<int>(source->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source->m_width),
                        .bottom = static_cast<float>(source->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });

    const uint32_t displayFrameWidth = 256 / kNumCompostions;
    for (uint32_t i = 0; i < kNumCompostions; i++) {
        const auto& target = targets[i];

        compositionRequest.target = createBorrowedImageInfo(target.get());
        compositionRequest.layers[0].source = createBorrowedImageInfo(source.get()),
        compositionRequest.layers[0].props.displayFrame.left = (i + 0) * displayFrameWidth;
        compositionRequest.layers[0].props.displayFrame.right = (i + 1) * displayFrameWidth;

        auto compositionCompleteWaitable = compositor->compose(compositionRequest);
        compositionCompleteWaitable.wait();

        compareImageWithGoldenPng(
            target.get(),
            GetTestDataPath("256x256_golden_multiple_targets_" + std::to_string(i) + ".png"),
            kDefaultSaveImageIfComparisonFailed);
    }
}

TEST_F(CompositorVkTest, MultipleLayers) {
    auto compositor = createCompositor();
    ASSERT_NE(compositor, nullptr);

    auto source1 = createSourceImageFromPng(GetTestDataPath("256x256_android.png"));
    ASSERT_NE(source1, nullptr);

    auto source2 =
        createSourceImageFromPng(GetTestDataPath("256x256_android_with_transparency.png"));
    ASSERT_NE(source2, nullptr);

    auto target = TargetImage::create(*k_vk, m_vkDevice, m_vkPhysicalDevice, m_compositorVkQueue,
                                      m_vkCommandPool, kDefaultImageWidth, kDefaultImageHeight);
    ASSERT_NE(target, nullptr);
    fillImageWith(target.get(), kColorBlack);

    Compositor::CompositionRequest compositionRequest = {
        .target = createBorrowedImageInfo(target.get()),
    };
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source1.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 32.0,
                        .top = 32.0,
                        .right = static_cast<float>(source1->m_width) - 32.0f,
                        .bottom = static_cast<float>(source1->m_height) - 32.0f,
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_NONE,
            },
    });
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source2.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width),
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source2->m_width) / 2.0f,
                        .bottom = static_cast<float>(source2->m_height) / 2.0f,
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_ROT_90,
            },
    });
    compositionRequest.layers.emplace_back(Compositor::CompositionRequestLayer{
        .source = createBorrowedImageInfo(source2.get()),
        .props =
            {
                .composeMode = HWC2_COMPOSITION_DEVICE,
                .displayFrame =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<int>(target->m_width) / 2,
                        .bottom = static_cast<int>(target->m_height),
                    },
                .crop =
                    {
                        .left = 0,
                        .top = 0,
                        .right = static_cast<float>(source2->m_width),
                        .bottom = static_cast<float>(source2->m_height),
                    },
                .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
                .alpha = 1.0,
                .color =
                    {
                        .r = 0,
                        .g = 0,
                        .b = 0,
                        .a = 0,
                    },
                .transform = HWC_TRANSFORM_FLIP_V_ROT_90,
            },
    });

    auto compositionCompleteWaitable = compositor->compose(compositionRequest);
    compositionCompleteWaitable.wait();

    compareImageWithGoldenPng(target.get(), GetTestDataPath("256x256_golden_multiple_layers.png"),
                              kDefaultSaveImageIfComparisonFailed);
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream
