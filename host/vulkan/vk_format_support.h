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

#pragma once

#include <vector>

#include <vulkan/vulkan.h>

#include "gfxstream/host/gfxstream_format.h"

namespace gfxstream {
namespace host {
namespace vk {

struct ImageSupportInfo {
    // Input parameters
    VkFormat format;
    VkImageType type;
    VkImageTiling tiling;
    VkImageUsageFlags usageFlags;
    VkImageCreateFlags createFlags;

    // Output parameters
    bool supported = false;
    bool supportsExternalMemory = false;
    bool requiresDedicatedAllocation = false;
    VkFormatProperties2 formatProps2{};
    VkImageFormatProperties2 imageFormatProps2{};
    VkExternalImageFormatProperties extFormatProps{};
};

class ImageSupport {
  public:
    static ImageSupport GetDefaultUnpopulatedImageSupport();

    bool IsFormatSupported(GfxstreamFormat format) const;

    const ImageSupportInfo* GetSupportedInfo(VkFormat format) const;

  private:
    friend class VkEmulation;

    ImageSupport() {}

    std::vector<ImageSupportInfo> mSupportInfos;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream

