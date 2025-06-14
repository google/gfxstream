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

#pragma once

#include <memory>

#include "gfxstream/host/external_object_manager.h"
#include "Handle.h"
#include "render-utils/stream.h"
#include "snapshot/LazySnapshotObj.h"

namespace gfxstream {
namespace gl {
class EmulationGl;
}  // namespace gl
}  // namespace gfxstream

namespace gfxstream {
namespace vk {
class VkEmulation;
}  // namespace vk
}  // namespace gfxstream

namespace gfxstream {

class Buffer : public LazySnapshotObj<Buffer> {
   public:
    static std::shared_ptr<Buffer> create(gl::EmulationGl* emulationGl,
                                          vk::VkEmulation* emulationVk, uint64_t size,
                                          HandleType handle);

    static std::shared_ptr<Buffer> onLoad(gl::EmulationGl* emulationGl,
                                          vk::VkEmulation* emulationVk,
                                          gfxstream::Stream* stream);

    void onSave(gfxstream::Stream* stream);
    void restore();

    HandleType getHndl() const;
    uint64_t getSize() const;

    void readToBytes(uint64_t offset, uint64_t size, void* outBytes);
    bool updateFromBytes(uint64_t offset, uint64_t size, const void* bytes);
    std::optional<BlobDescriptorInfo> exportBlob();

   private:
    Buffer() = default;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};

typedef std::shared_ptr<Buffer> BufferPtr;

}  // namespace gfxstream
