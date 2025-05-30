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

#include "gfxstream/host/borrowed_image.h"
#include "BorrowedImageVk.h"
#include "Compositor.h"
#include "DebugUtilsHelper.h"
#include "Hwc2.h"
#include "gfxstream/LruCache.h"
#include "gfxstream/synchronization/Lock.h"
#include "goldfish_vk_dispatch.h"
#include "vulkan/VkUtils.h"

namespace gfxstream {
namespace vk {

// We do see a composition requests with 33 layers. (b/365603234)
// Inside hwc2, we will ask for surfaceflinger to
// do the composition, if the layers more than 48.
// If we see rendering error or significant time spent on updating
// descriptors in setComposition, we should tune this number.
static constexpr const uint32_t kMaxLayersPerFrame = 48;
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
    const DebugUtilsHelper m_debugUtilsHelper;
    std::shared_ptr<gfxstream::base::Lock> m_vkQueueLock;
    VkDescriptorSetLayout m_vkDescriptorSetLayout;
    VkPipelineLayout m_vkPipelineLayout;
    struct PerFormatResources {
        VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;
        VkPipeline m_graphicsVkPipeline = VK_NULL_HANDLE;
    };
    std::unordered_map<VkFormat, PerFormatResources> m_formatResources;
    VkBuffer m_vertexVkBuffer;
    VkDeviceMemory m_vertexVkDeviceMemory;
    VkBuffer m_indexVkBuffer;
    VkDeviceMemory m_indexVkDeviceMemory;
    VkDescriptorPool m_vkDescriptorPool;
    VkCommandPool m_vkCommandPool;
    // TODO: create additional VkSampler-s for YCbCr layers.
    VkSampler m_vkSampler;
    // Unused image that is solely used to occupy the sampled image binding
    // when compositing a solid color layer.
    struct DefaultImage {
        VkImage m_vkImage = VK_NULL_HANDLE;
        VkImageView m_vkImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_vkImageMemory = VK_NULL_HANDLE;
    } m_defaultImage;

    // The underlying storage for all of the uniform buffer objects.
    struct UniformBufferStorage {
        VkBuffer m_vkBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_vkDeviceMemory = VK_NULL_HANDLE;
        VkDeviceSize m_stride = 0;
    } m_uniformStorage;

    // Keep in sync with vulkan/Compositor.frag.
    struct SamplerBinding {
        // Include the image id to trigger a descriptor update to handle the case
        // that the VkImageView is recycled across different images (b/322998473).
        uint32_t sampledImageId = 0;
        VkImageView sampledImageView = VK_NULL_HANDLE;
    };

    // Keep in sync with vulkan/Compositor.vert.
    struct UniformBufferBinding {
        alignas(16) glm::mat4 positionTransform;
        alignas(16) glm::mat4 texCoordTransform;
        alignas(16) glm::uvec4 mode;
        alignas(16) glm::vec4 alpha;
        alignas(16) glm::vec4 color;
    };

    // The cached contents of a given descriptor set.
    struct DescriptorSetContents {
        SamplerBinding binding0;
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
        std::vector<VkDescriptorSet> m_layerDescriptorSets;
        // Pointers into the underlying uniform buffer storage for the uniform
        // buffer of part of each descriptor set for each layer.
        std::vector<UniformBufferBinding*> m_layerUboStorages;
        std::optional<FrameDescriptorSetsContents> m_vkDescriptorSetsContents;
    };
    std::vector<PerFrameResources> m_frameResources;
    std::deque<std::shared_future<PerFrameResources*>> m_availableFrameResources;

    explicit CompositorVkBase(const VulkanDispatch& vk, VkDevice device,
                              VkPhysicalDevice physicalDevice, VkQueue queue,
                              std::shared_ptr<gfxstream::base::Lock> queueLock,
                              uint32_t queueFamilyIndex, uint32_t maxFramesInFlight,
                              DebugUtilsHelper debugUtils)
        : m_vk(vk),
          m_vkDevice(device),
          m_vkPhysicalDevice(physicalDevice),
          m_vkQueue(queue),
          m_queueFamilyIndex(queueFamilyIndex),
          m_debugUtilsHelper(debugUtils),
          m_vkQueueLock(queueLock),
          m_vkDescriptorSetLayout(VK_NULL_HANDLE),
          m_vkPipelineLayout(VK_NULL_HANDLE),
          m_vertexVkBuffer(VK_NULL_HANDLE),
          m_vertexVkDeviceMemory(VK_NULL_HANDLE),
          m_indexVkBuffer(VK_NULL_HANDLE),
          m_indexVkDeviceMemory(VK_NULL_HANDLE),
          m_vkDescriptorPool(VK_NULL_HANDLE),
          m_vkCommandPool(VK_NULL_HANDLE),
          m_vkSampler(VK_NULL_HANDLE),
          m_frameResources(maxFramesInFlight) {}
};

class CompositorVk : protected CompositorVkBase, public Compositor {
   public:
    static std::unique_ptr<CompositorVk> create(
        const VulkanDispatch& vk, VkDevice vkDevice, VkPhysicalDevice vkPhysicalDevice,
        VkQueue vkQueue, std::shared_ptr<gfxstream::base::Lock> queueLock, uint32_t queueFamilyIndex,
        uint32_t maxFramesInFlight,
        DebugUtilsHelper debugUtils = DebugUtilsHelper::withUtilsDisabled());

    ~CompositorVk();

    CompositionFinishedWaitable compose(const CompositionRequest& compositionRequest) override;

    void onImageDestroyed(uint32_t imageId) override;

    static bool queueSupportsComposition(const VkQueueFamilyProperties& properties) {
        return properties.queueFlags & VK_QUEUE_GRAPHICS_BIT;
    }

   private:
    explicit CompositorVk(const VulkanDispatch&, VkDevice, VkPhysicalDevice, VkQueue,
                          std::shared_ptr<gfxstream::base::Lock> queueLock, uint32_t queueFamilyIndex,
                          uint32_t maxFramesInFlight, DebugUtilsHelper debugUtils);

    void setUpGraphicsPipeline();
    void setUpVertexBuffers();
    void setUpSampler();
    void setUpDescriptorSets();
    void setUpUniformBuffers();
    void setUpCommandPool();
    void setUpFences();
    void setUpDefaultImage();
    void setUpFrameResourceFutures();

    std::optional<std::tuple<VkBuffer, VkDeviceMemory>> createBuffer(VkDeviceSize,
                                                                     VkBufferUsageFlags,
                                                                     VkMemoryPropertyFlags) const;
    std::tuple<VkBuffer, VkDeviceMemory> createStagingBufferWithData(const void* data,
                                                                     VkDeviceSize size) const;
    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize) const;

    VkFormatFeatureFlags getFormatFeatures(VkFormat format, VkImageTiling tiling);

    // Check if the ColorBuffer can be used as a compose layer to be sampled from.
    bool canCompositeFrom(const VkImageCreateInfo& info);

    // Check if the ColorBuffer can be used as a render target of a composition.
    bool canCompositeTo(const VkImageCreateInfo& info);

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
        VkPipeline pipeline = VK_NULL_HANDLE;
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
}  // namespace gfxstream

#endif /* COMPOSITOR_VK_H */
