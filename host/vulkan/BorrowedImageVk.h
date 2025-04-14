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

#pragma once

#include <cstdint>
#include <vector>

#include "gfxstream/host/borrowed_image.h"
#include "goldfish_vk_dispatch.h"

namespace gfxstream {
namespace vk {

struct BorrowedImageInfoVk : public BorrowedImageInfo {
    VkImage image = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkImageCreateInfo imageCreateInfo = {};

    // The image layout that `image` is in before composition.
    //
    // This is currently ignored for composition target images as
    // composition targets are expected to be cleared during
    // composition.
    VkImageLayout preBorrowLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // The queue family index that owns `image` before composition.
    uint32_t preBorrowQueueFamilyIndex = 0;

    // The image layout that `image` should be transitioned to
    // after composition.
    //
    // This is currently ignored for composition target images as
    // composition targets are expected to be transitioned to
    // VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL after composition for
    // blitting to display images.
    VkImageLayout postBorrowLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    // The queue family index that `image` should be transitioned to
    // after composition.
    uint32_t postBorrowQueueFamilyIndex = 0;
};

// The caller should always record the queue transfer barriers with stages that supoort
// VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT.
void addNeededBarriersToUseBorrowedImage(
    const BorrowedImageInfoVk& borrowedImageInfo, uint32_t usedQueueFamilyIndex,
    VkImageLayout usedInitialImageLayout, VkImageLayout usedFinalImageLayout,
    VkAccessFlags usedAccessMask, std::vector<VkImageMemoryBarrier>* preUseQueueTransferBarriers,
    std::vector<VkImageMemoryBarrier>* preUseLayoutTransitionBarriers,
    std::vector<VkImageMemoryBarrier>* postUseLayoutTransitionBarriers,
    std::vector<VkImageMemoryBarrier>* postUseQueueTransferBarriers);

}  // namespace vk
}  // namespace gfxstream
