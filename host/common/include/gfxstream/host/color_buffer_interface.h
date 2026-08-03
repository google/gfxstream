// Copyright 2026 The Android Open Source Project
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
#include <cstdint>
#include <memory>
#include <optional>

#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/gfxstream_format.h"
#include "render-utils/Renderer.h"

namespace gfxstream {
namespace host {

namespace gl {
class ColorBufferGl;
}  // namespace gl

namespace vk {
class ColorBufferVk;
}  // namespace vk

enum class Backend { GL, VK };

// A (mostly) generic interface to a `ColorBuffer` so that the various backends
// interact with `ColorBuffer`s without needing to depend on all of the various
// underlying `ColorBuffer` implementations.
class IColorBuffer {
   public:
    virtual ~IColorBuffer() = default;

    virtual uint32_t getHndl() const = 0;
    virtual uint32_t getWidth() const = 0;
    virtual uint32_t getHeight() const = 0;

    virtual void touch() = 0;

    virtual gl::ColorBufferGl* getColorBufferGl() = 0;
    virtual vk::ColorBufferVk* getColorBufferVk() = 0;

    virtual bool invalidateForBackend(Backend backend) = 0;
    virtual bool flushFromBackend(Backend backend) = 0;
    virtual bool importHandle(void* handle, bool preserveContent) = 0;

    virtual void readToBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                             void* outPixels, uint64_t outPixelsSize) = 0;
    virtual void readToBytesScaled(int pixelsWidth, int pixelsHeight, int pixelsRotation,
                                   const Rect& rect, GfxstreamFormat pixelsFormat, void* outPixels,
                                   const std::optional<std::array<float, 16>>& colorTransform) = 0;
    virtual void readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                                uint32_t outPixelsSize) = 0;

    virtual bool updateFromBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                                 const void* pixels, void* metadata = nullptr) = 0;

    virtual std::optional<BlobDescriptorInfo> exportBlob() = 0;
};

// A (mostly) generic shared owning reference  to a `ColorBuffer` so that the
// various backends can have shared ownership of a `ColorBuffer` without needed
// to depend on the underlying `ColorBuffer` implementations.
using IColorBufferRef = std::shared_ptr<IColorBuffer>;

}  // namespace host
}  // namespace gfxstream
