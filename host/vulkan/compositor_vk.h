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

#ifndef COMPOSITOR_VK_H
#define COMPOSITOR_VK_H

#include <array>
#include <deque>
#include <future>
#include <glm/glm.hpp>
#include <list>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "borrowed_image_vk.h"
#include "compositor.h"
#include "debug_utils_helper.h"
#include "gfxstream/LruCache.h"
#include "gfxstream/host/borrowed_image.h"
#include "gfxstream/host/gfxstream_format.h"
#include "gfxstream/synchronization/Lock.h"
#include "goldfish_vk_dispatch.h"
#include "host/hwc2.h"
#include "vulkan/vk_utils.h"

namespace gfxstream {
namespace host {
namespace vk {

// This wrapper helps add type checking for a common pattern in the compositor
// where all of the non-YUV formats can use a common resource but each YUV format
// needs a custom resource due to the need to use immutable samplers for YUV images.
struct YuvOrDefaultGfxstreamFormat {
    YuvOrDefaultGfxstreamFormat() : underlying(GfxstreamFormat::UNKNOWN) {}

    YuvOrDefaultGfxstreamFormat(GfxstreamFormat format)
        : underlying(IsYuvFormat(format) ? format : GfxstreamFormat::UNKNOWN) {}

    YuvOrDefaultGfxstreamFormat& operator=(GfxstreamFormat format) {
        underlying = IsYuvFormat(format) ? format : GfxstreamFormat::UNKNOWN;
        return *this;
    }

    GfxstreamFormat underlying = GfxstreamFormat::UNKNOWN;

    bool operator==(const YuvOrDefaultGfxstreamFormat& other) const {
        return underlying == other.underlying;
    }
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream

namespace std {
template <>
struct hash<gfxstream::host::vk::YuvOrDefaultGfxstreamFormat> {
    size_t operator()(gfxstream::host::vk::YuvOrDefaultGfxstreamFormat format) const {
        return hash<uint32_t>()(static_cast<uint32_t>(format.underlying));
    }
};
}  // namespace std

namespace gfxstream {
namespace host {
namespace vk {

// We do see a composition requests with 33 layers. (b/365603234)
// Inside hwc2, we will ask for surfaceflinger to
// do the composition, if the layers more than 48.
// If we see rendering error or significant time spent on updating
// descriptors in setComposition, we should tune this number.
static constexpr const uint32_t kMaxLayersPerFrame = 48;
static constexpr const uint32_t kMaxImmediateDrawsPerFrame = 16;
static const uint64_t kVkWaitForFencesTimeoutNsecs = 5ULL * 1000ULL * 1000ULL * 1000ULL;

// Base used to grant visibility to members to the vk_util::* helper classes.
struct CompositorVkBase : public vk_util::MultiCrtp<CompositorVkBase,         //
                                                    vk_util::FindMemoryType,  //
                                                    vk_util::RunSingleTimeCommand> {
    const VulkanDispatch& m_vk;
    const VkDevice m_vkDevice;
    const VkPhysicalDevice m_vkPhysicalDevice;
    const VkQueue m_vkQueue;
    const uint32_t m_queueFamilyIndex;
    vk_util::YcbcrSamplerPool* m_ycbcrSamplerPool;
    const DebugUtilsHelper m_debugUtilsHelper;
    std::shared_ptr<gfxstream::base::Lock> m_vkQueueLock;

    std::unordered_map<GfxstreamFormat /*rendertarget format*/, VkRenderPass> m_vkRenderPasses;

    struct GraphicsPipelineKey {
        GfxstreamFormat renderTargetFormat;
        YuvOrDefaultGfxstreamFormat sampledImageFormat;
        bool screenBlend;

        bool operator==(const GraphicsPipelineKey& other) const {
            return renderTargetFormat == other.renderTargetFormat &&
                   sampledImageFormat == other.sampledImageFormat &&
                   screenBlend == other.screenBlend;
        }
    };
    struct GraphicsPipelineHash {
        std::size_t operator()(const GraphicsPipelineKey& k) const {
            std::size_t h1 = std::hash<GfxstreamFormat>{}(k.renderTargetFormat);
            std::size_t h2 = std::hash<YuvOrDefaultGfxstreamFormat>{}(k.sampledImageFormat);
            return h1 ^ (h2 << 1);
        }
    };
    std::unordered_map<GraphicsPipelineKey, VkPipeline, GraphicsPipelineHash>
        m_vkGraphicsVkPipelines;

    VkBuffer m_vertexVkBuffer;
    VkDeviceMemory m_vertexVkDeviceMemory;
    VkBuffer m_indexVkBuffer;
    VkDeviceMemory m_indexVkDeviceMemory;
    VkDescriptorPool m_vkDescriptorPool;
    VkCommandPool m_vkCommandPool;
    VkSampler m_defaultSampler;

    struct PerFormatResources {
        // The descriptor set layout for the composition pipeline when sampling this format.
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        // The pipeline layout for the composition pipeline when sampling this format.
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    };
    std::unordered_map<YuvOrDefaultGfxstreamFormat, PerFormatResources> m_formatResources;

    // Unused image that is solely used to occupy the sampled image binding
    // when compositing a solid color layer.
    struct Image {
        VkImage m_vkImage = VK_NULL_HANDLE;
        VkImageView m_vkImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_vkImageMemory = VK_NULL_HANDLE;
        GfxstreamFormat m_imageFormat = GfxstreamFormat::UNKNOWN;
    };

    Image m_defaultImage;

    std::mutex mScreenImagesMutex;
    Image m_screenMaskImage;
    Image m_screenBackgroundImage;

    // The underlying storage for all of the uniform buffer objects.
    struct UniformBufferStorage {
        VkBuffer m_vkBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vkDeviceMemory = VK_NULL_HANDLE;
        VkDeviceSize m_stride = 0;
        VkDeviceSize m_size = 0;
        void* m_mappedPtr = nullptr;
    };

    UniformBufferStorage m_uniformStorage;

    // Keep in sync with vulkan/Compositor.frag.
    struct SampledImageBinding {
        // Include the image id to trigger a descriptor update to handle the case
        // that the VkImageView is recycled across different images (b/322998473).
        uint32_t sampledImageId = 0;
        VkImageView sampledImageView = VK_NULL_HANDLE;
        GfxstreamFormat sampledImageFormat = GfxstreamFormat::UNKNOWN;
    };

    // Keep in sync with vulkan/Compositor.vert.
    struct UniformBufferBinding {
        alignas(16) glm::mat4 positionTransform;
        alignas(16) glm::mat4 texCoordTransform;
        alignas(16) glm::mat4 colorTransform;
        alignas(16) glm::uvec4 mode;
        alignas(16) glm::vec4 alpha;
        alignas(16) glm::vec4 color;
    };

    // The cached contents of a given descriptor set.
    struct DescriptorSetContents {
        SampledImageBinding binding0;
        UniformBufferBinding binding1;
    };

    // The cached contents of all descriptors sets of a given frame.
    struct FrameDescriptorSetsContents {
        std::vector<DescriptorSetContents> descriptorSets;
    };

    friend bool operator==(const DescriptorSetContents& lhs, const DescriptorSetContents& rhs);

    friend bool operator==(const FrameDescriptorSetsContents& lhs,
                           const FrameDescriptorSetsContents& rhs);

    struct PerFrameResources {
        VkFence m_vkFence = VK_NULL_HANDLE;
        VkCommandBuffer m_vkCommandBuffer = VK_NULL_HANDLE;
        std::unordered_map<YuvOrDefaultGfxstreamFormat /*sampledImageFormat*/,
                           std::vector<VkDescriptorSet>>
            m_layerDescriptorSets;
        // Pointers into the underlying uniform buffer storage for the uniform
        // buffer of part of each descriptor set for each layer.
        std::unordered_map<YuvOrDefaultGfxstreamFormat /*sampledImageFormat*/,
                           std::vector<UniformBufferBinding*>>
            m_layerUboStorages;
        std::optional<FrameDescriptorSetsContents> m_vkDescriptorSetsContents;
    };
    std::vector<PerFrameResources> m_frameResources;
    std::deque<std::shared_future<PerFrameResources*>> m_availableFrameResources;

    // Immediate mode rendering resources for post draw operations
    struct ImmediateModeResources {
        std::vector<VkDescriptorSet> m_descriptorSets;
        std::vector<UniformBufferBinding*> m_uboStorages;
        uint32_t m_curDataIndex = 0;

        //TODO: implement a better 'available' list tracking
        std::mutex m_isFreeMutex;
        bool m_isFree = true;

        void init() {
            m_curDataIndex = 0;
            m_isFree = false;
        }
        void reset() {
            m_isFree = true;
        }
    };
    std::vector<ImmediateModeResources> m_immediateFrameResources;

    explicit CompositorVkBase(const VulkanDispatch& vk, VkDevice device,
                              VkPhysicalDevice physicalDevice, VkQueue queue,
                              std::shared_ptr<gfxstream::base::Lock> queueLock,
                              uint32_t queueFamilyIndex, uint32_t maxFramesInFlight,
                              vk_util::YcbcrSamplerPool* ycbcrPool, DebugUtilsHelper debugUtils)
        : m_vk(vk),
          m_vkDevice(device),
          m_vkPhysicalDevice(physicalDevice),
          m_vkQueue(queue),
          m_queueFamilyIndex(queueFamilyIndex),
          m_ycbcrSamplerPool(ycbcrPool),
          m_debugUtilsHelper(debugUtils),
          m_vkQueueLock(queueLock),
          m_vertexVkBuffer(VK_NULL_HANDLE),
          m_vertexVkDeviceMemory(VK_NULL_HANDLE),
          m_indexVkBuffer(VK_NULL_HANDLE),
          m_indexVkDeviceMemory(VK_NULL_HANDLE),
          m_vkDescriptorPool(VK_NULL_HANDLE),
          m_vkCommandPool(VK_NULL_HANDLE),
          m_defaultSampler(VK_NULL_HANDLE),
          m_frameResources(maxFramesInFlight),
          m_immediateFrameResources(maxFramesInFlight) {}
};

class CompositorVk : protected CompositorVkBase, public Compositor {
   public:
    static std::unique_ptr<CompositorVk> create(
        const VulkanDispatch& vk, VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice,
        VkQueue vkQueue, std::shared_ptr<gfxstream::base::Lock> queueLock,
        uint32_t queueFamilyIndex, uint32_t maxFramesInFlight, vk_util::YcbcrSamplerPool* ycbcrPool,
        DebugUtilsHelper debugUtils = DebugUtilsHelper::withUtilsDisabled());

    ~CompositorVk();

    CompositionFinishedWaitable compose(const CompositionRequest& compositionRequest) override;

    void setScreenMask(int width, int height, const uint8_t* rgbaData) override;
    void setScreenBackground(int width, int height, const uint8_t* rgbaData) override;

    void onImageDestroyed(uint32_t imageId) override;

    static bool queueSupportsComposition(const VkQueueFamilyProperties& properties) {
        return properties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
    }

    // Check if a screen mask image has been set for the final composition
    bool hasScreenMask() const { return (m_screenMaskImage.m_vkImage != VK_NULL_HANDLE); }
    bool hasScreenBackground() const { return (m_screenBackgroundImage.m_vkImage != VK_NULL_HANDLE); }

    struct ImageDrawParams {
        VkCommandBuffer commandBuffer;
        VkFormat targetFormat;
        uint32_t targetWidth;
        uint32_t targetHeight;
        VkRenderPass targetRenderPass;
        VkFramebuffer targetFramebuffer;
        ImmediateModeResources* frameResources;
        float rotationDegrees = 0.0f;
        bool useScreenBlend = false;
        std::optional<std::array<float, 16>> colorTransform;
    };

    void drawScreenMask(const ImageDrawParams& params);
    void drawScreenBackground(const ImageDrawParams& params);
    void drawImage(const ImageDrawParams& params, VkImageView imageView);

    ImmediateModeResources* acquireImmediateModeResources();
    void releaseImmediateModeResources(ImmediateModeResources* frameResources);

   private:
    explicit CompositorVk(const VulkanDispatch&, VkDevice, VkPhysicalDevice, VkQueue,
                          std::shared_ptr<gfxstream::base::Lock> queueLock,
                          uint32_t queueFamilyIndex, uint32_t maxFramesInFlight,
                          vk_util::YcbcrSamplerPool* ycbcrPool, DebugUtilsHelper debugUtils);

    bool setUpFormatResources();
    bool setUpRenderPasses();
    bool setUpGraphicsPipeline(const VkShaderModule vertShaderMod,
                               const VkShaderModule fragShaderMod,
                               const GfxstreamFormat samplerFormat);
    bool setUpGraphicsPipelines();
    bool setUpVertexBuffers();
    bool setUpDescriptorSets();
    bool setUpUniformBuffers();
    bool setUpCommandPool();
    bool setUpFences();
    bool setUpDefaultImage();
    bool setUpScreenMaskImage(uint32_t width, uint32_t height, const uint8_t* rgbaData);
    bool setUpScreenBackgroundImage(uint32_t width, uint32_t height, const uint8_t* rgbaData);
    bool setUpFrameResourceFutures();

    bool createImage(Image& img, uint32_t width, uint32_t height, const uint8_t* rgbaData,
                      const std::string& debugName);
    void destroyImage(Image& img);

    bool createUniformBufferStorage(UniformBufferStorage& storage, uint32_t numBuffersRequired);
    void destroyUniformBufferStorage(UniformBufferStorage& storage);

    std::optional<std::tuple<VkBuffer, VkDeviceMemory>> createBuffer(VkDeviceSize,
                                                                     VkBufferUsageFlags,
                                                                     VkMemoryPropertyFlags) const;
    std::tuple<VkBuffer, VkDeviceMemory> createStagingBufferWithData(const void* data,
                                                                     VkDeviceSize size) const;
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize) const;

    VkFormatFeatureFlags getFormatFeatures(VkFormat format, VkImageTiling tiling);

    // Check if the ColorBuffer can be used as a compose layer to be sampled from.
    bool canCompositeFrom(const VkImageCreateInfo& info);

    // A consolidated view of a `Compositor::CompositionRequestLayer` with only
    // the Vulkan components needed for command recording and submission.
    struct CompositionLayerVk {
        VkImage image = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        VkImageLayout preCompositionLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t preCompositionQueueFamilyIndex = 0;
        VkImageLayout postCompositionLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        uint32_t postCompositionQueueFamilyIndex = 0;
    };

    // A consolidated view of a `Compositor::CompositionRequest` with only
    // the Vulkan components needed for command recording and submission.
    struct CompositionVk {
        const BorrowedImageInfoVk* targetImage = nullptr;
        VkRenderPass targetRenderPass = VK_NULL_HANDLE;
        VkFramebuffer targetFramebuffer = VK_NULL_HANDLE;
        std::vector<VkPipeline> layersPipelines;
        std::vector<YuvOrDefaultGfxstreamFormat> layerSourceSamplerFormats;
        std::vector<const BorrowedImageInfoVk*> layersSourceImages;
        FrameDescriptorSetsContents layersDescriptorSets;
    };
    void buildCompositionVk(const CompositionRequest& compositionRequest,
                            CompositionVk* compositionVk);

    void updateDescriptorSetsIfChanged(const FrameDescriptorSetsContents& contents,
                                       PerFrameResources* frameResources);

    class RenderTarget {
       public:
        ~RenderTarget();

        DISALLOW_COPY_ASSIGN_AND_MOVE(RenderTarget);

       private:
        friend class CompositorVk;
        RenderTarget(const VulkanDispatch& vk, VkDevice vkDevice, VkImage vkImage,
                     VkImageView vkImageView, uint32_t width, uint32_t height,
                     VkRenderPass vkRenderPass);

        const VulkanDispatch& m_vk;
        VkDevice m_vkDevice;
        VkImage m_vkImage;
        VkFramebuffer m_vkFramebuffer;
        uint32_t m_width;
        uint32_t m_height;
    };

    // Gets the RenderTarget used for composing into the given image if it already exists,
    // otherwise creates it.
    RenderTarget* getOrCreateRenderTargetInfo(const BorrowedImageInfoVk& info);

    // Cached format properties used for checking if composition is supported with a given
    // format.
    std::unordered_map<VkFormat, VkFormatProperties> m_vkFormatProperties;

    uint32_t m_maxFramesInFlight = 0;

    static constexpr const VkFormat k_renderTargetFormat = VK_FORMAT_R8G8B8A8_UNORM;
    static constexpr const uint32_t k_renderTargetCacheSize = 128;
    // Maps from borrowed image ids to render target info.
    gfxstream::base::LruCache<uint32_t, std::unique_ptr<RenderTarget>> m_renderTargetCache;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream

#endif /* COMPOSITOR_VK_H */
