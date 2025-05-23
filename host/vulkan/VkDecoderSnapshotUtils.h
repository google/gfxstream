// Copyright 2024 The Android Open Source Project
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

#include "vulkan/VkDecoderInternalStructs.h"

namespace gfxstream {
namespace vk {
struct StateBlock {
    VkPhysicalDevice physicalDevice;
    const PhysicalDeviceInfo* physicalDeviceInfo;
    VkDevice device;
    VulkanDispatch* deviceDispatch;
    VkQueue queue;
    VkCommandPool commandPool;
};
void saveImageContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkImage image,
                      const ImageInfo* imageInfo);
void loadImageContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkImage image,
                      const ImageInfo* imageInfo);
void saveBufferContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkBuffer buffer,
                       const BufferInfo* bufferInfo);
void loadBufferContent(gfxstream::Stream* stream, StateBlock* stateBlock, VkBuffer buffer,
                       const BufferInfo* bufferInfo);
}  // namespace vk
}  // namespace gfxstream