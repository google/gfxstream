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

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "vulkan/emulated_textures/AstcTexture.h"
#include "vulkan/emulated_textures/GpuDecompressionPipeline.h"
#include "goldfish_vk_dispatch.h"
#include "vulkan/vulkan.h"

namespace gfxstream {
namespace vk {

class CompressedImageInfo {
   public:
    // Static methods

    // Returns the format that this image will be converted to, if the original format isn't
    // natively supported by the GPU.
    static VkFormat getOutputFormat(VkFormat compFmt);

    // Returns the image format used to store the compressed data. Each pixel in the compressed
    // mipmaps will hold an entire compressed block.
    static VkFormat getCompressedMipmapsFormat(VkFormat compFmt);

    static bool needEmulatedAlpha(VkFormat format);

    // Returns a VkImageCopy to copy to/from the compressed data
    static VkImageCopy getCompressedMipmapsImageCopy(const VkImageCopy& origRegion,
                                                     const CompressedImageInfo& srcImg,
                                                     const CompressedImageInfo& dstImg,
                                                     bool needEmulatedSrc, bool needEmulatedDst);
    static VkImageCopy2 getCompressedMipmapsImageCopy(const VkImageCopy2& origRegion,
                                                      const CompressedImageInfo& srcImg,
                                                      const CompressedImageInfo& dstImg,
                                                      bool needEmulatedSrc, bool needEmulatedDst);


    // Constructors

    // TODO(gregschlom) Delete these constructors once we switch to holding a
    //  std::unique_ptr<CompressedImageInfo>
    CompressedImageInfo() = default;
    explicit CompressedImageInfo(VkDevice device);

    CompressedImageInfo(VkDevice device, const VkImageCreateInfo& createInfo,
                        GpuDecompressionPipelineManager* pipelineManager);

    // Public methods

    // Returns the VkImageCreateInfo needed to create the output image
    VkImageCreateInfo getOutputCreateInfo(const VkImageCreateInfo& createInfo) const;

    // Creates the compressed mipmap images, that is the VkImages holding the compressed data
    void createCompressedMipmapImages(VulkanDispatch* vk, const VkImageCreateInfo& createInfo);

    // Initializes the resources needed to perform CPU decompression of ASTC textures
    void initAstcCpuDecompression(VulkanDispatch* vk, VkPhysicalDevice physicalDevice);

    // Should be called when the guest calls vkCmdPipelineBarrier.
    // This function checks if the image barrier transitions the compressed image to a layout where
    // it will be read from, and if so, it decompresses the image.
    //
    // outputBarriers: any barrier that needs to be passed to the vkCmdPipelineBarrier call will be
    // added to this vector.
    // Returns whether image decompression happened.
    // Note: the global lock must be held when calling this method, because we call into
    // GpuDecompressionPipelineManager.
    bool decompressIfNeeded(VulkanDispatch* vk, VkCommandBuffer commandBuffer,
                            VkPipelineStageFlags srcStageMask, VkPipelineStageFlags dstStageMask,
                            const VkImageMemoryBarrier& targetBarrier,
                            std::vector<VkImageMemoryBarrier>& outputBarriers);

    void decompressOnCpu(VkCommandBuffer commandBuffer, uint8_t* srcAstcData, size_t astcDataSize,
                         VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount,
                         const VkBufferImageCopy* pRegions, const VkDecoderContext& context);

    void decompressOnCpu(VkCommandBuffer commandBuffer, uint8_t* srcAstcData, size_t astcDataSize,
                         const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo, const VkDecoderContext& context);

    VkMemoryRequirements getMemoryRequirements() const;

    VkResult bindCompressedMipmapsMemory(VulkanDispatch* vk, VkDeviceMemory memory,
                                         VkDeviceSize memoryOffset);

    // Given a VkBufferImageCopy object for the original image, returns a new
    // VkBufferImageCopy that points to the same location in the compressed mipmap images.
    VkBufferImageCopy getBufferImageCopy(const VkBufferImageCopy& origRegion) const;
    VkBufferImageCopy2 getBufferImageCopy(const VkBufferImageCopy2& origRegion) const;

    // Releases all the resources used by this class. It may no longer be used after calling this.
    void destroy(VulkanDispatch* vk);

    // Accessors

    bool isEtc2() const;
    bool isAstc() const;
    VkDevice device() const { return mDevice; }
    VkImage compressedMipmap(uint32_t level) { return mCompressedMipmaps[level]; }
    VkImage outputImage() { return mOutputImage; }
    void setOutputImage(VkImage image) { mOutputImage = image; }
    bool canDecompressOnCpu() { return mAstcTexture != nullptr; }
    bool successfullyDecompressedOnCpu() const {
        return mAstcTexture && mAstcTexture->successfullyDecompressed();
    }

   private:
    // Returns a vector of image barriers for the compressed mipmap images and the decompressed
    // image.
    std::vector<VkImageMemoryBarrier> getImageBarriers(const VkImageMemoryBarrier& srcBarrier);

    VkImageSubresourceRange getImageSubresourceRange(const VkImageSubresourceRange& range) const;

    // Initializes the compute shader pipeline to decompress the image.
    // No-op if this was already called successfully.
    VkResult initializeDecompressionPipeline(VulkanDispatch* vk, VkDevice device);

    // Runs the decompression shader
    void decompress(VulkanDispatch* vk, VkCommandBuffer commandBuffer,
                    const VkImageSubresourceRange& range);

    // Returns the size of the image at a given mip level
    VkExtent3D mipmapExtent(uint32_t level) const;
    // Returns the size of the compressed mipmaps at a given mip level. This is mipmapExtent divided
    // by the block size, and rounded up.
    VkExtent3D compressedMipmapExtent(uint32_t level) const;
    // Returns an extent into the compressed mipmaps. This divides the components of origExtent by
    // the block size, and the result is clamped to not exceed the compressed mipmap size.
    VkExtent3D compressedMipmapPortion(const VkExtent3D& origExtent, uint32_t level) const;

    // Member variables

    // The original compressed format of this image. E.g.: VK_FORMAT_ASTC_4x4_UNORM_BLOCK
    VkFormat mCompressedFormat = VK_FORMAT_UNDEFINED;
    // The format that we decompressed the image to. E.g.: VK_FORMAT_R8G8B8A8_UINT
    VkFormat mOutputFormat = VK_FORMAT_UNDEFINED;
    // The format that we use to store the compressed data, since the original compressed format
    // isn't available. This holds one compressed block per pixel. E.g.: VK_FORMAT_R32G32B32A32_UINT
    VkFormat mCompressedMipmapsFormat = VK_FORMAT_UNDEFINED;

    VkImageType mImageType = VK_IMAGE_TYPE_MAX_ENUM;
    uint32_t mMipLevels = 1;     // Number of mip levels in the image
    VkExtent3D mExtent = {};     // Size of the image
    VkExtent2D mBlock = {1, 1};  // Size of the compressed blocks
    uint32_t mLayerCount = 1;

    VkDevice mDevice = VK_NULL_HANDLE;
    VkImage mOutputImage = VK_NULL_HANDLE;

    // Compressed data. Each mip level of the original image is stored as a separate VkImage, and
    // each pixel in those images contains an entire compressed block.
    std::vector<VkImage> mCompressedMipmaps;

    // The memory offset that we will use for each compressed mipmap.
    std::vector<VkDeviceSize> mMipmapOffsets;

    VkMemoryRequirements mMemoryRequirements;

    // Used to perform CPU decompression of ASTC textures. Null for non-ASTC images.
    std::unique_ptr<AstcTexture> mAstcTexture = nullptr;

    // Vulkan resources used by the decompression pipeline
    GpuDecompressionPipelineManager* mPipelineManager = nullptr;
    GpuDecompressionPipeline* mDecompPipeline = nullptr;
    std::vector<VkDescriptorSet> mDecompDescriptorSets;
    VkDescriptorPool mDecompDescriptorPool = VK_NULL_HANDLE;
    std::vector<VkImageView> mCompressedMipmapsImageViews;
    std::vector<VkImageView> mOutputImageViews;
    bool mDecompPipelineInitialized = false;
};

}  // namespace vk
}  // namespace gfxstream
