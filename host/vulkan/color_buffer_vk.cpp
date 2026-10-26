// Copyright 2023 The Android Open Source Project
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

#include "color_buffer_vk.h"

#include "vk_common_operations.h"

namespace gfxstream {
namespace host {
namespace vk {

/*static*/
std::unique_ptr<ColorBufferVk> ColorBufferVk::create(VkEmulation& vkEmulation, uint32_t handle,
                                                     uint32_t width, uint32_t height,
                                                     GfxstreamFormat format, bool vulkanOnly,
                                                     uint32_t memoryProperty, uint32_t mipLevels) {
    if (!vkEmulation.createVkColorBuffer(width, height, format, handle, vulkanOnly,
                                         memoryProperty, mipLevels)) {
        GFXSTREAM_ERROR("Failed to create ColorBufferVk:%d", handle);
        return nullptr;
    }
    return std::unique_ptr<ColorBufferVk>(new ColorBufferVk(vkEmulation, handle));
}

/*static*/
std::unique_ptr<ColorBufferVk> ColorBufferVk::import(
    VkEmulation& vkEmulation, uint32_t handle,
    const struct ::stream_renderer_handle* importHandle,
    const struct ::stream_renderer_import_data* importData) {
    if (!vkEmulation.importColorBuffer(handle, importHandle, importData)) {
        GFXSTREAM_ERROR("Failed to import ColorBufferVk:%d", handle);
        return nullptr;
    }
    return std::unique_ptr<ColorBufferVk>(new ColorBufferVk(vkEmulation, handle));
}

bool ColorBufferVk::onLoad(gfxstream::Stream* stream, LoadImageBehavior behavior) {
    if (!mVkEmulation.getFeatures().VulkanSnapshots.enabled() || !stream) {
        return true;
    }
    VkImageLayout currentLayout = static_cast<VkImageLayout>(stream->getBe32());
    mVkEmulation.setColorBufferCurrentLayout(mHandle, currentLayout);

    if (behavior == LoadImageBehavior::SkipImageContent) {
        return true;
    }

    uint64_t size = stream->getBe64();
    if (size > 0) {
        GFXSTREAM_DEBUG("snapshot load: color buffer %u size=%" PRIu64, mHandle, size);
        std::vector<uint8_t> pixels(size);
        ssize_t ret = stream->read(pixels.data(), size);
        if (ret > 0 && static_cast<uint64_t>(ret) == size) {
            mVkEmulation.updateColorBufferFromBytes(mHandle, pixels);
        } else {
            GFXSTREAM_ERROR(
                "ColorBufferVk::onLoad failed to read %" PRIu64 " pixel bytes from stream, got %zd",
                size, ret);
            return false;
        }
    } else {
        // Clear pixels as this path is also used for color buffers with all-zero values
        mVkEmulation.clearColorBuffer(mHandle);
    }

    if (stream->hasErrors()) {
        GFXSTREAM_ERROR("ColorBufferVk::onLoad failed with errors: %s",
                        stream->getErrors().value_or("unknown error").c_str());
        return false;
    }
    return true;
}

bool ColorBufferVk::onSave(gfxstream::Stream* stream, SaveImageBehavior behavior) {
    if (!mVkEmulation.getFeatures().VulkanSnapshots.enabled()) {
        return true;
    }
    stream->putBe32(static_cast<uint32_t>(mVkEmulation.getColorBufferCurrentLayout(mHandle)));

    if (behavior == SaveImageBehavior::SkipImageContent) {
        return true;
    }

    std::vector<uint8_t> pixels;
    bool writePixels = readToBytes(&pixels);

    // To save storage and optimize for faster a load, check the pixel values
    // and don't write them into the snapshot if they are all zeros.
    if (writePixels) {
        const bool all_zero =
            std::all_of(pixels.begin(), pixels.end(), [](uint8_t pixel) { return pixel == 0; });
        if (all_zero) {
            GFXSTREAM_DEBUG(
                "snapshot save: skipping %zu pixel bytes for color buffer %u - zero color save",
                pixels.size(), mHandle);
            writePixels = false;
        }
    }

    if (writePixels) {
        GFXSTREAM_DEBUG("snapshot save: color buffer %u size=%zu", mHandle, pixels.size());
        uint64_t size = pixels.size();
        stream->putBe64(size);
        ssize_t written = stream->write(pixels.data(), size);
        if (written < 0 || static_cast<uint64_t>(written) != size) {
            GFXSTREAM_ERROR(
                "ColorBufferVk::onSave failed to write %" PRIu64 " pixel bytes from stream, got %zd",
                size, written);
            return false;
        }
    } else {
        stream->putBe64(0);
    }

    if (stream->hasErrors()) {
        GFXSTREAM_ERROR("ColorBufferVk::onSave failed with errors: %s",
                        stream->getErrors().value_or("unknown error").c_str());
        return false;
    }
    return true;
}

ColorBufferVk::ColorBufferVk(VkEmulation& vkEmulation, uint32_t handle)
    : mVkEmulation(vkEmulation), mHandle(handle) {}

ColorBufferVk::~ColorBufferVk() {
    if (!mVkEmulation.teardownVkColorBuffer(mHandle)) {
        GFXSTREAM_ERROR("Failed to destroy ColorBufferVk:%d", mHandle);
    }
}

bool ColorBufferVk::readToBytes(std::vector<uint8_t>* outBytes) {
    return mVkEmulation.readColorBufferToBytes(mHandle, outBytes);
}

bool ColorBufferVk::readToBytes(uint32_t x, uint32_t y, uint32_t w, uint32_t h, void* outBytes,
                                uint64_t outBytesSize) {
    return mVkEmulation.readColorBufferToBytes(mHandle, x, y, w, h, outBytes, outBytesSize);
}

bool ColorBufferVk::readPixelsScaled(int pixelsWidth, int pixelsHeight, int pixelsRotation,
                                     const Rect& rect, GfxstreamFormat pixelsFormat,
                                     void* outPixels,
                                     const std::optional<std::array<float, 16>>& colorTransform) {
    return mVkEmulation.readColorBufferPixelsScaled(mHandle, pixelsWidth, pixelsHeight,
                                                    static_cast<GFXSTREAM_ROTATION>(pixelsRotation), rect, pixelsFormat, outPixels,
                                                    colorTransform);
}

bool ColorBufferVk::updateFromBytes(const std::vector<uint8_t>& bytes) {
    return mVkEmulation.updateColorBufferFromBytes(mHandle, bytes);
}

bool ColorBufferVk::updateFromBytes(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    const void* bytes) {
    return mVkEmulation.updateColorBufferFromBytes(mHandle, x, y, w, h, bytes);
}

std::unique_ptr<ColorBufferVkImageInfo> ColorBufferVk::prepareForComposition(
    bool colorBufferIsTarget) {
    return mVkEmulation.prepareColorBufferForComposition(mHandle, colorBufferIsTarget);
}

std::unique_ptr<ColorBufferVkImageInfo> ColorBufferVk::prepareForDisplay() {
    return mVkEmulation.prepareColorBufferForDisplay(mHandle);
}

std::optional<BlobDescriptorInfo> ColorBufferVk::exportBlob() {
    uint32_t memoryTypeIndex = 0;
    if (!mVkEmulation.getColorBufferAllocationInfo(mHandle, nullptr, &memoryTypeIndex, nullptr,
                                                   nullptr)) {
        return std::nullopt;
    }
    auto info = mVkEmulation.exportColorBufferMemory(mHandle);
    if (info) {
        return BlobDescriptorInfo{
            .descriptorInfo =
                {
#if defined(__ANDROID__)
                    .handle = info->handleInfo.handle,
#else
                    .descriptor = info->handleInfo.toManagedDescriptor(),
#endif
                    .streamHandleType = info->handleInfo.streamHandleType,
                },
            .caching = 0,
            .vulkanInfoOpt = mVkEmulation.getVulkanInfo(memoryTypeIndex),
        };
    } else {
        return std::nullopt;
    }
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
