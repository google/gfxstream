// Copyright 2022 The Android Open Source Project
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

#include "BorrowedImageVk.h"

namespace gfxstream {
namespace vk {

void addNeededBarriersToUseBorrowedImage(
    const BorrowedImageInfoVk& borrowedImageInfo, uint32_t usedQueueFamilyIndex,
    VkImageLayout usedInitialImageLayout, VkImageLayout usedFinalImageLayout,
    VkAccessFlags usedAccessMask, std::vector<VkImageMemoryBarrier>* preUseQueueTransferBarriers,
    std::vector<VkImageMemoryBarrier>* preUseLayoutTransitionBarriers,
    std::vector<VkImageMemoryBarrier>* postUseLayoutTransitionBarriers,
    std::vector<VkImageMemoryBarrier>* postUseQueueTransferBarriers) {
    if (borrowedImageInfo.preBorrowQueueFamilyIndex != usedQueueFamilyIndex) {
        const VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = borrowedImageInfo.preBorrowLayout,
            .newLayout = borrowedImageInfo.preBorrowLayout,
            .srcQueueFamilyIndex = borrowedImageInfo.preBorrowQueueFamilyIndex,
            .dstQueueFamilyIndex = usedQueueFamilyIndex,
            .image = borrowedImageInfo.image,
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
    if (borrowedImageInfo.preBorrowLayout != usedInitialImageLayout &&
        usedInitialImageLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
        const VkImageMemoryBarrier layoutTransitionBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = usedAccessMask,
            .oldLayout = borrowedImageInfo.preBorrowLayout,
            .newLayout = usedInitialImageLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = borrowedImageInfo.image,
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
    if (borrowedImageInfo.postBorrowLayout != usedFinalImageLayout) {
        const VkImageMemoryBarrier layoutTransitionBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = usedAccessMask,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = usedFinalImageLayout,
            .newLayout = borrowedImageInfo.postBorrowLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = borrowedImageInfo.image,
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
    if (borrowedImageInfo.postBorrowQueueFamilyIndex != usedQueueFamilyIndex) {
        const VkImageMemoryBarrier queueTransferBarrier = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .pNext = nullptr,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .oldLayout = borrowedImageInfo.postBorrowLayout,
            .newLayout = borrowedImageInfo.postBorrowLayout,
            .srcQueueFamilyIndex = usedQueueFamilyIndex,
            .dstQueueFamilyIndex = borrowedImageInfo.postBorrowQueueFamilyIndex,
            .image = borrowedImageInfo.image,
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

}  // namespace vk
}  // namespace gfxstream
