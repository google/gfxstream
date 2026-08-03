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

#include <array>
#include <memory>
#include <optional>

#include "gfxstream/host/color_buffer_interface.h"
#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/framework_formats.h"
#include "gfxstream/host/gfxstream_format.h"
#include "gfxstream/host/hwc2.h"
#include "gfxstream/host/handle.h"
#include "render-utils/Renderer.h"
#include "render-utils/stream.h"
#include "gfxstream/host/lazy_snapshot_object.h"

namespace gfxstream {
namespace host {
namespace gl {
class EmulationGl;
}  // namespace gl
}  // namespace host
}  // namespace gfxstream

namespace gfxstream {
namespace host {
namespace vk {
class VkEmulation;
}  // namespace vk
}  // namespace host
}  // namespace gfxstream

namespace gfxstream {
namespace host {

class ColorBuffer : public IColorBuffer, public LazySnapshotObj<ColorBuffer> {
   public:
    static std::shared_ptr<ColorBuffer> create(gl::EmulationGl* emulationGl,
                                               vk::VkEmulation* emulationVk, uint32_t width,
                                               uint32_t height, GfxstreamFormat format,
                                               HandleType handle, Stream* stream = nullptr);

    static std::shared_ptr<ColorBuffer> onLoad(gl::EmulationGl* emulationGl,
                                               vk::VkEmulation* emulationVk, Stream* stream);
    void onSave(Stream* stream);
    void restore();
    void touch() override;

    gl::ColorBufferGl* getColorBufferGl() override;
    vk::ColorBufferVk* getColorBufferVk() override;

    HandleType getHndl() const override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;
    GfxstreamFormat getFormat() const;

    void readToBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                     void* outPixels, uint64_t outPixelsSize) override;
    void readToBytesScaled(int pixelsWidth, int pixelsHeight, int pixelsRotation, const Rect& rect,
                           GfxstreamFormat pixelsFormat, void* outPixels,
                           const std::optional<std::array<float, 16>>& colorTransform) override;
    void readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                        uint32_t outPixelsSize) override;

    bool updateFromBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                         const void* pixels, void* metadata = nullptr) override;
    bool updateGlFromBytes(const void* bytes, std::size_t bytesSize);

    bool invalidateForBackend(Backend backend) override;
    bool flushFromBackend(Backend backend) override;
    bool importHandle(void* handle, bool preserveContent) override;

    bool flushFromGl();
    bool flushFromVk();
    bool flushFromVkBytes(const void* bytes, size_t bytesSize);

    std::optional<BlobDescriptorInfo> exportBlob() override;

   private:
    ColorBuffer() = default;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};

using ColorBufferPtr = std::shared_ptr<ColorBuffer>;

using ColorBufferSet = std::unordered_multiset<HandleType>;

}  // namespace host
}  // namespace gfxstream
