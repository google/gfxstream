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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "ColorBufferVk.h"

#include "VkCommonOperations.h"

namespace gfxstream {
namespace vk {

/*static*/
std::unique_ptr<ColorBufferVk> ColorBufferVk::create(VkEmulation& vkEmulation, uint32_t handle,
                                                     uint32_t width, uint32_t height, GLenum format,
                                                     FrameworkFormat frameworkFormat,
                                                     bool vulkanOnly, uint32_t memoryProperty,
                                                     android::base::Stream* stream) {
    if (!vkEmulation.createVkColorBuffer(width, height, format, frameworkFormat, handle, vulkanOnly,
                                         memoryProperty)) {
        GFXSTREAM_DEBUG("Failed to create ColorBufferVk:%d", handle);
        return nullptr;
    }
    if (vkEmulation.getFeatures().VulkanSnapshots.enabled && stream) {
        VkImageLayout currentLayout = static_cast<VkImageLayout>(stream->getBe32());
        vkEmulation.setColorBufferCurrentLayout(handle, currentLayout);
    }
    return std::unique_ptr<ColorBufferVk>(new ColorBufferVk(vkEmulation, handle));
}

void ColorBufferVk::onSave(android::base::Stream* stream) {
    if (!mVkEmulation.getFeatures().VulkanSnapshots.enabled) {
        return;
    }
    stream->putBe32(static_cast<uint32_t>(mVkEmulation.getColorBufferCurrentLayout(mHandle)));
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

bool ColorBufferVk::updateFromBytes(const std::vector<uint8_t>& bytes) {
    return mVkEmulation.updateColorBufferFromBytes(mHandle, bytes);
}

bool ColorBufferVk::updateFromBytes(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                                    const void* bytes) {
    return mVkEmulation.updateColorBufferFromBytes(mHandle, x, y, w, h, bytes);
}

std::unique_ptr<BorrowedImageInfo> ColorBufferVk::borrowForComposition(bool colorBufferIsTarget) {
    return mVkEmulation.borrowColorBufferForComposition(mHandle, colorBufferIsTarget);
}

std::unique_ptr<BorrowedImageInfo> ColorBufferVk::borrowForDisplay() {
    return mVkEmulation.borrowColorBufferForDisplay(mHandle);
}

std::optional<BlobDescriptorInfo> ColorBufferVk::exportBlob() {
    auto info = mVkEmulation.exportColorBufferMemory(mHandle);
    if (info) {
        return BlobDescriptorInfo{
            .descriptorInfo =
                {
                    .descriptor = info->handleInfo.toManagedDescriptor(),
                    .streamHandleType = info->handleInfo.streamHandleType,
                },
            .caching = 0,
            .vulkanInfoOpt = std::nullopt,
        };
    } else {
        return std::nullopt;
    }
}

}  // namespace vk
}  // namespace gfxstream
