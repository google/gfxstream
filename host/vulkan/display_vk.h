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

#ifndef DISPLAY_VK_H
#define DISPLAY_VK_H

#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

#include "gfxstream/host/borrowed_image.h"
#include "compositor_vk.h"
#include "gfxstream/host/display.h"
#include "display_surface_vk.h"
#include "host/hwc2.h"
#include "swap_chain_state_vk.h"
#include "gfxstream/synchronization/Lock.h"
#include "goldfish_vk_dispatch.h"

// The DisplayVk class holds the Vulkan and other states required to draw a
// frame in a host window.

namespace gfxstream {
namespace host {
namespace vk {

class DisplayVk : public Display {
   public:
    DisplayVk(const VulkanDispatch&, VkPhysicalDevice, VkDevice, CompositorVk* compositorVk,
              uint32_t compositorQueueFamilyIndex, VkQueue compositorVkQueue,
              std::shared_ptr<gfxstream::base::Lock> compositorVkQueueLock,
              uint32_t swapChainQueueFamilyIndex, VkQueue swapChainVkQueue,
              std::shared_ptr<gfxstream::base::Lock> swapChainVkQueueLock);
    ~DisplayVk();

    PostResult post(const BorrowedImageInfo* info, float rotationDegrees);

    void drainQueues();
    void clear();

   protected:
    void bindToSurfaceImpl(DisplaySurface* surface) override;
    void surfaceUpdated(DisplaySurface* surface) override;
    void unbindFromSurfaceImpl() override;

   private:
    void destroySwapchain();
    bool recreateSwapchain();

    // The success component of the result is false when the swapchain is no longer valid and
    // bindToSurface() needs to be called again. When the success component is true, the waitable
    // component of the returned result is a future that will complete when the GPU side of work
    // completes. The caller is responsible to guarantee the synchronization and the layout of
    // ColorBufferCompositionInfo::m_vkImage is VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL.
    PostResult postImpl(const BorrowedImageInfo* info, float rotationDegrees,
                        const std::optional<std::array<float, 16>>& colorTransform);

    VkFormatFeatureFlags getFormatFeatures(VkFormat, VkImageTiling);
    bool canPost(const VkImageCreateInfo&);

    const VulkanDispatch& m_vk;
    VkPhysicalDevice m_vkPhysicalDevice;
    VkDevice m_vkDevice;
    CompositorVk* m_compositorVk;  // TODO(b/442394091): temporary addition, refactor compositor to
                                   // separate drawing routines like TextureDraw in GL side

    uint32_t m_compositorQueueFamilyIndex;
    VkQueue m_compositorVkQueue;
    std::shared_ptr<gfxstream::base::Lock> m_compositorVkQueueLock;
    uint32_t m_swapChainQueueFamilyIndex;
    VkQueue m_swapChainVkQueue;
    std::shared_ptr<gfxstream::base::Lock> m_swapChainVkQueueLock;
    VkCommandPool m_vkCommandPool;

    class PostResource {
       public:
        const VkFence m_swapchainImageReleaseFence;
        const VkSemaphore m_swapchainImageAcquireSemaphore;
        const VkSemaphore m_swapchainImageReleaseSemaphore;
        const VkCommandBuffer m_vkCommandBuffer;
        static std::shared_ptr<PostResource> create(const VulkanDispatch&, VkDevice, VkCommandPool);
        ~PostResource();
        DISALLOW_COPY_ASSIGN_AND_MOVE(PostResource);

       private:
        PostResource(const VulkanDispatch&, VkDevice, VkCommandPool,
                     VkFence swapchainImageReleaseFence, VkSemaphore swapchainImageAcquireSemaphore,
                     VkSemaphore swapchainImageReleaseSemaphore, VkCommandBuffer);
        const VulkanDispatch& m_vk;
        const VkDevice m_vkDevice;
        const VkCommandPool m_vkCommandPool;
    };

    std::deque<std::shared_ptr<PostResource>> m_freePostResources;
    std::vector<std::optional<std::shared_future<std::shared_ptr<PostResource>>>>
        m_postResourceFutures;
    int m_inFlightFrameIndex;

    class ImageBorrowResource {
       public:
        const VkFence m_completeFence;
        const VkCommandBuffer m_vkCommandBuffer;
        static std::unique_ptr<ImageBorrowResource> create(const VulkanDispatch&, VkDevice,
                                                           VkCommandPool);
        ~ImageBorrowResource();
        DISALLOW_COPY_ASSIGN_AND_MOVE(ImageBorrowResource);

       private:
        ImageBorrowResource(const VulkanDispatch&, VkDevice, VkCommandPool, VkFence,
                            VkCommandBuffer);
        const VulkanDispatch& m_vk;
        const VkDevice m_vkDevice;
        const VkCommandPool m_vkCommandPool;
    };
    std::vector<std::unique_ptr<ImageBorrowResource>> m_imageBorrowResources;

    std::unique_ptr<SwapChainStateVk> m_swapChainStateVk;
    bool m_needToRecreateSwapChain = true;

    std::unordered_map<VkFormat, VkFormatProperties> m_vkFormatProperties;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream

#endif
