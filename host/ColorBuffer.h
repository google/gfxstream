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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <GLES3/gl3.h>

#include <memory>

#include "FrameworkFormats.h"
#include "Handle.h"
#include "Hwc2.h"
#include "gfxstream/host/borrowed_image.h"
#include "gfxstream/host/external_object_manager.h"
#include "render-utils/Renderer.h"
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

class ColorBuffer : public LazySnapshotObj<ColorBuffer> {
   public:
    static std::shared_ptr<ColorBuffer> create(gl::EmulationGl* emulationGl,
                                               vk::VkEmulation* emulationVk, uint32_t width,
                                               uint32_t height, GLenum format,
                                               FrameworkFormat frameworkFormat, HandleType handle,
                                               gfxstream::Stream* stream = nullptr);

    static std::shared_ptr<ColorBuffer> onLoad(gl::EmulationGl* emulationGl,
                                               vk::VkEmulation* emulationVk,
                                               gfxstream::Stream* stream);
    void onSave(gfxstream::Stream* stream);
    void restore();

    HandleType getHndl() const;
    uint32_t getWidth() const;
    uint32_t getHeight() const;
    GLenum getFormat() const;
    FrameworkFormat getFrameworkFormat() const;

    void readToBytes(int x, int y, int width, int height, GLenum pixelsFormat, GLenum pixelsType,
                     void* outPixels, uint64_t outPixelsSize);
    void readToBytesScaled(int pixelsWidth, int pixelsHeight, GLenum pixelsFormat,
                           GLenum pixelsType, int pixelsRotation, Rect rect, void* outPixels);
    void readYuvToBytes(int x, int y, int width, int height, void* outPixels, uint32_t outPixelsSize);

    bool updateFromBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                         GLenum pixelsType, const void* pixels);
    bool updateFromBytes(int x, int y, int width, int height, FrameworkFormat frameworkFormat,
                         GLenum pixelsFormat, GLenum pixelsType, const void* pixels,
                         void* metadata = nullptr);
    bool updateGlFromBytes(const void* bytes, std::size_t bytesSize);

    enum class UsedApi {
        kGl,
        kVk,
    };
    std::unique_ptr<BorrowedImageInfo> borrowForComposition(UsedApi api, bool isTarget);
    std::unique_ptr<BorrowedImageInfo> borrowForDisplay(UsedApi api);

    bool flushFromGl();
    bool flushFromVk();
    bool flushFromVkBytes(const void* bytes, size_t bytesSize);
    bool invalidateForGl();
    bool invalidateForVk();

    std::optional<BlobDescriptorInfo> exportBlob();

#if GFXSTREAM_ENABLE_HOST_GLES
    GLuint glOpGetTexture();
    bool glOpBlitFromCurrentReadBuffer();
    bool glOpBindToTexture();
    bool glOpBindToTexture2();
    bool glOpBindToRenderbuffer();
    void glOpReadback(unsigned char* img, bool readbackBgra);
    void glOpReadbackAsync(GLuint buffer, bool readbackBgra);
    bool glOpImportEglNativePixmap(void* pixmap, bool preserveContent);
    void glOpSwapYuvTexturesAndUpdate(GLenum format, GLenum type, FrameworkFormat frameworkFormat,
                                      GLuint* textures);
    bool glOpReadContents(size_t* outNumBytes, void* outContents);
    bool glOpIsFastBlitSupported() const;
    void glOpPostLayer(const ComposeLayer& l, int frameWidth, int frameHeight);
    void glOpPostViewportScaledWithOverlay(float rotation, float dx, float dy);
#endif

   private:
    ColorBuffer() = default;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};

typedef std::shared_ptr<ColorBuffer> ColorBufferPtr;

struct ColorBufferRef {
    ColorBufferPtr cb;
    uint32_t refcount;  // number of client-side references

    // Tracks whether opened at least once. In O+,
    // color buffers can be created/closed immediately,
    // but then registered (opened) afterwards.
    bool opened;

    // Tracks the time when this buffer got a close request while not being
    // opened yet.
    uint64_t closedTs;
};

typedef std::unordered_map<HandleType, ColorBufferRef> ColorBufferMap;
typedef std::unordered_multiset<HandleType> ColorBufferSet;

}  // namespace gfxstream
