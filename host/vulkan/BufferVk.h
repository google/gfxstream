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

#include <memory>
#include <vector>

#include "gfxstream/host/external_object_manager.h"

namespace gfxstream {
namespace vk {

class VkEmulation;

class BufferVk {
   public:
    static std::unique_ptr<BufferVk> create(VkEmulation& vkEmulation, uint32_t handle,
                                            uint64_t size, bool vulkanOnly);

    ~BufferVk();

    void readToBytes(uint64_t offset, uint64_t size, void* outBytes);

    bool updateFromBytes(uint64_t offset, uint64_t size, const void* bytes);

    std::optional<BlobDescriptorInfo> exportBlob();

   private:
    BufferVk(VkEmulation& vkEmulation, uint32_t handle);

    VkEmulation& mVkEmulation;

    const uint32_t mHandle;
};

}  // namespace vk
}  // namespace gfxstream
