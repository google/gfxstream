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

#include "AstcTexture.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <optional>
#include <vector>

#include "gfxstream/HealthMonitor.h"
#include "gfxstream/host/logging.h"
#include "vulkan/vk_util.h"

namespace gfxstream {
namespace vk {
namespace {

using std::chrono::milliseconds;

// Print stats each time we decompress this many pixels:
constexpr uint64_t kProcessedPixelsLogInterval = 10'000'000;

std::atomic<uint64_t> pixels_processed = 0;
std::atomic<uint64_t> ms_elapsed = 0;
std::atomic<int64_t> bytes_used = 0;

uint32_t mipmapSize(uint32_t size, uint32_t mipLevel) {
    return std::max<uint32_t>(size >> mipLevel, 1);
}

bool isRegionValid(const VkBufferImageCopy& region, uint32_t width, uint32_t height) {
    // TODO(gregschlom) deal with those cases. See details at:
    // https://registry.khronos.org/vulkan/specs/1.0-extensions/html/chap20.html#copies-buffers-images-addressing
    // https://stackoverflow.com/questions/46501832/vulkan-vkbufferimagecopy-for-partial-transfer

    if (region.bufferRowLength != 0 || region.bufferImageHeight != 0) {
        GFXSTREAM_WARNING("ASTC CPU decompression skipped: non-packed buffer");
        return false;
    }
    if (region.imageOffset.x != 0 || region.imageOffset.y != 0) {
        GFXSTREAM_WARNING("ASTC CPU decompression skipped: imageOffset is non-zero");
        return false;
    }
    if (region.imageExtent.width != width || region.imageExtent.height != height) {
        GFXSTREAM_WARNING(
            "ASTC CPU decompression skipped: imageExtent is less than the entire image");
        return false;
    }
    return true;
}

}  // namespace

AstcTexture::AstcTexture(VulkanDispatch* vk, VkDevice device, VkPhysicalDevice physicalDevice,
                         VkExtent3D imgSize, uint32_t blockWidth, uint32_t blockHeight,
                         AstcCpuDecompressor* decompressor)
    : mVk(vk),
      mDevice(device),
      mPhysicalDevice(physicalDevice),
      mImgSize(imgSize),
      mBlockWidth(blockWidth),
      mBlockHeight(blockHeight),
      mDecompressor(decompressor) {}

AstcTexture::~AstcTexture() { destroyVkBuffer(); }

bool AstcTexture::canDecompressOnCpu() const { return mDecompressor->available(); }

uint8_t* AstcTexture::createVkBufferAndMapMemory(size_t bufferSize) {
    VkResult res;
    mBufferSize = bufferSize;  // Save the buffer size, for statistics purpose only
    bytes_used += bufferSize;

    if (mDecompBuffer || mDecompBufferMemory) {
        GFXSTREAM_WARNING(
            "ASTC CPU decompression failed: tried to decompress same image more than once.");
        return nullptr;
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = bufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    res = mVk->vkCreateBuffer(mDevice, &bufferInfo, nullptr, &mDecompBuffer);
    if (res != VK_SUCCESS) {
        GFXSTREAM_WARNING("ASTC CPU decompression: vkCreateBuffer failed: %d", res);
        mDecompBuffer = VK_NULL_HANDLE;
        return nullptr;
    }

    VkMemoryRequirements memRequirements;
    mVk->vkGetBufferMemoryRequirements(mDevice, mDecompBuffer, &memRequirements);

    std::optional<uint32_t> memIndex = vk_util::findMemoryType(
        mVk, mPhysicalDevice, memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
            VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (!memIndex) {
        // Do it again, but without VK_MEMORY_PROPERTY_HOST_CACHED_BIT this time
        memIndex = vk_util::findMemoryType(
            mVk, mPhysicalDevice, memRequirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (!memIndex) {
        GFXSTREAM_WARNING(
            "ASTC CPU decompression: no suitable memory type to decompress the image");
        return nullptr;
    }

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memRequirements.size,
        .memoryTypeIndex = *memIndex,
    };
    res = mVk->vkAllocateMemory(mDevice, &allocInfo, nullptr, &mDecompBufferMemory);
    if (res != VK_SUCCESS) {
        GFXSTREAM_WARNING("ASTC CPU decompression: vkAllocateMemory failed: %d", res);
        mDecompBufferMemory = VK_NULL_HANDLE;
        return nullptr;
    }

    res = mVk->vkBindBufferMemory(mDevice, mDecompBuffer, mDecompBufferMemory, 0);
    if (res != VK_SUCCESS) {
        GFXSTREAM_WARNING("ASTC CPU decompression: vkBindBufferMemory failed: %d", res);
        return nullptr;
    }

    uint8_t* decompData;
    res = mVk->vkMapMemory(mDevice, mDecompBufferMemory, 0, bufferSize, 0, (void**)&decompData);
    if (res != VK_SUCCESS) {
        GFXSTREAM_WARNING("ASTC CPU decompression: vkMapMemory failed: %d", res);
        return nullptr;
    }

    return decompData;
}

void AstcTexture::destroyVkBuffer() {
    bytes_used -= mBufferSize;
    if (mVk && mDevice) {
        mVk->vkDestroyBuffer(mDevice, mDecompBuffer, nullptr);
        mVk->vkFreeMemory(mDevice, mDecompBufferMemory, nullptr);
        mDecompBuffer = VK_NULL_HANDLE;
        mDecompBufferMemory = VK_NULL_HANDLE;
    }
}

template<typename T>
void AstcTexture::on_vkCmdCopyBufferToImageImpl(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                            size_t astcDataSize, VkImage dstImage,
                                            VkImageLayout dstImageLayout, uint32_t regionCount,
                                            const T* pRegions,
                                            const VkDecoderContext& context) {
    auto watchdog =
        WATCHDOG_BUILDER(context.healthMonitor, "AstcTexture::on_vkCmdCopyBufferToImageImpl").build();
    auto start_time = std::chrono::steady_clock::now();
    mSuccess = false;
    size_t decompSize = 0;  // How many bytes we need to hold the decompressed data

    // Holds extra data about the region
    struct RegionInfo {
        uint32_t width;           // actual width (ie: mipmap width)
        uint32_t height;          // actual height (ie: mipmap height)
        uint32_t compressedSize;  // size of ASTC data for that region
    };

    std::vector<RegionInfo> regionInfos;
    regionInfos.reserve(regionCount);

    // Make a copy of the regions and update the buffer offset of each to reflect the
    // correct location of the decompressed data
    std::vector<VkBufferImageCopy> decompRegions(regionCount);
    for (size_t i = 0; i < regionCount; ++i) {
        decompRegions[i] = VkBufferImageCopy {
            pRegions[i].bufferOffset,
            pRegions[i].bufferRowLength,
            pRegions[i].bufferImageHeight,
            pRegions[i].imageSubresource,
            pRegions[i].imageOffset,
            pRegions[i].imageExtent
        };
    }
    for (auto& decompRegion : decompRegions) {
        const uint32_t mipLevel = decompRegion.imageSubresource.mipLevel;
        const uint32_t width = mipmapSize(mImgSize.width, mipLevel);
        const uint32_t height = mipmapSize(mImgSize.height, mipLevel);
        const uint32_t numAstcBlocks = ((width + mBlockWidth - 1) / mBlockWidth) *
                                       ((height + mBlockHeight - 1) / mBlockHeight);
        const uint32_t compressedSize = numAstcBlocks * 16;
        // We haven't updated decompRegion.bufferOffset yet, so it's still the _compressed_ offset.
        const uint32_t compressedDataOffset = decompRegion.bufferOffset;

        // Do all the precondition checks
        if (!isRegionValid(decompRegion, width, height)) return;
        if (compressedDataOffset + compressedSize > astcDataSize) {
            GFXSTREAM_WARNING(
                "ASTC CPU decompression: data out of bounds. Offset: %llu, Size: %llu, Total %llu",
                compressedDataOffset, compressedSize, astcDataSize);
            return;
        }

        decompRegion.bufferOffset = decompSize;
        decompSize += width * height * 4;
        regionInfos.push_back({width, height, compressedSize});
    }

    // Create a new VkBuffer to hold the decompressed data
    uint8_t* decompData = createVkBufferAndMapMemory(decompSize);
    if (!decompData) {
        destroyVkBuffer();  // The destructor would have done it anyway, but may as well do it early
        return;
    }

    // Decompress each region
    for (uint32_t i = 0; i < regionCount; i++) {
        const auto& compRegion = pRegions[i];
        const auto& decompRegion = decompRegions[i];
        const auto& regionInfo = regionInfos[i];

        int32_t status = mDecompressor->decompress(
            regionInfo.width, regionInfo.height, mBlockWidth, mBlockHeight,
            srcAstcData + compRegion.bufferOffset, regionInfo.compressedSize,
            decompData + decompRegion.bufferOffset);

        if (status != 0) {
            GFXSTREAM_WARNING("ASTC CPU decompression failed: %s.",
                              mDecompressor->getStatusString(status));
            mVk->vkUnmapMemory(mDevice, mDecompBufferMemory);
            destroyVkBuffer();
            return;
        }
    }

    mVk->vkUnmapMemory(mDevice, mDecompBufferMemory);

    // Finally, actually copy the buffer to the image
    mVk->vkCmdCopyBufferToImage(commandBuffer, mDecompBuffer, dstImage, dstImageLayout,
                                decompRegions.size(), decompRegions.data());

    mSuccess = true;
    auto end_time = std::chrono::steady_clock::now();

    // Compute stats
    pixels_processed += decompSize / 4;
    ms_elapsed += std::chrono::duration_cast<milliseconds>(end_time - start_time).count();

    uint64_t total_pixels = pixels_processed.load();
    uint64_t total_time = ms_elapsed.load();

    if (total_pixels >= kProcessedPixelsLogInterval && total_time > 0) {
        pixels_processed.store(0);
        ms_elapsed.store(0);
        GFXSTREAM_INFO(
            "ASTC CPU decompression: %.2f Mpix in %.2f seconds (%.2f Mpix/s). Total mem: %.2f MB",
            total_pixels / 1'000'000.0, total_time / 1000.0,
            (float)total_pixels / total_time / 1000.0, bytes_used / 1000000.0);
    }
}

void AstcTexture::on_vkCmdCopyBufferToImage(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                size_t astcDataSize, VkImage dstImage,
                                VkImageLayout dstImageLayout, uint32_t regionCount,
                                const VkBufferImageCopy* pRegions,
                                const VkDecoderContext& context) {
    on_vkCmdCopyBufferToImageImpl(commandBuffer, srcAstcData, astcDataSize, dstImage, dstImageLayout, regionCount, pRegions, context);
}

void AstcTexture::on_vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, uint8_t* srcAstcData,
                                size_t astcDataSize, const VkCopyBufferToImageInfo2* pCopyBufferToImageInfo,
                                const VkDecoderContext& context) {
    on_vkCmdCopyBufferToImageImpl(commandBuffer,
                                  srcAstcData,
                                  astcDataSize,
                                  pCopyBufferToImageInfo->dstImage,
                                  pCopyBufferToImageInfo->dstImageLayout,
                                  pCopyBufferToImageInfo->regionCount,
                                  pCopyBufferToImageInfo->pRegions,
                                  context);
}

}  // namespace vk
}  // namespace gfxstream
