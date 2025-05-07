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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#pragma once

#include "gfxstream/host/AstcCpuDecompressor.h"
#include "vulkan/VkDecoderContext.h"
#include "goldfish_vk_dispatch.h"
#include "vulkan/vulkan.h"

namespace gfxstream {
namespace vk {

// Holds the resources necessary to perform CPU ASTC decompression of a single texture.
class AstcTexture {
   public:
    AstcTexture(VulkanDispatch* vk, VkDevice device, VkPhysicalDevice physicalDevice,
                VkExtent3D imgSize, uint32_t blockWidth, uint32_t blockHeight,
                AstcCpuDecompressor* decompressor);

    ~AstcTexture();

    // Whether we're able to decompress ASTC textures on the CPU
    bool canDecompressOnCpu() const;

    // Whether this texture was successfully decompressed on the CPU
    bool successfullyDecompressed() const { return mSuccess; }

    void on_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                   size_t astcDataSize, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount,
                                   const VkBufferImageCopy* pRegions,
                                   const VkDecoderContext& context);
    void on_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                   size_t astcDataSize, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo,
                                   const VkDecoderContext& context);

   private:

    template<typename T>
    void on_vkCmdCopyBufferToImageImpl(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                   size_t astcDataSize, VkImage dstImage,
                                   VkImageLayout dstImageLayout, uint32_t regionCount,
                                   const T* pRegions,
                                   const VkDecoderContext& context);

    uint8_t* createVkBufferAndMapMemory(size_t bufferSize);
    void destroyVkBuffer();

    bool mSuccess = false;  // true if the image was successfully decompressed
    VulkanDispatch* mVk;
    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VkExtent3D mImgSize;
    uint32_t mBlockWidth;
    uint32_t mBlockHeight;
    VkBuffer mDecompBuffer = VK_NULL_HANDLE;              // VkBuffer of the decompressed image
    VkDeviceMemory mDecompBufferMemory = VK_NULL_HANDLE;  // Memory of the decompressed image
    uint64_t mBufferSize = 0;                             // Size of the decompressed image
    AstcCpuDecompressor* mDecompressor;
};

}  // namespace vk
}  // namespace gfxstream
