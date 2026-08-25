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

#include "color_buffer.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "host/gl/emulation_gl.h"
#endif
#include "frame_buffer.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/gfxstream_format.h"
#include "vulkan/color_buffer_vk.h"
#include "vulkan/vk_common_operations.h"

namespace gfxstream {
namespace host {
namespace {

#if GFXSTREAM_ENABLE_HOST_GLES
using gl::ColorBufferGl;
#endif
using vk::ColorBufferVk;

// ColorBufferVk natively supports YUV images. However, ColorBufferGl
// needs to emulate YUV support by having an underlying RGBA texture
// and adding in additional YUV<->RGBA conversions when needed. The
// memory should not be shared between the VK YUV image and the GL RGBA
// texture.
bool shouldAttemptExternalMemorySharing(GfxstreamFormat format) {
    return !gfxstream::host::IsYuvFormat(format);
}

}  // namespace

class ColorBuffer::Impl : public LazySnapshotObj<ColorBuffer::Impl> {
   public:
    static std::unique_ptr<Impl> create(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        uint32_t width, uint32_t height, GfxstreamFormat format,
                                        HandleType handle, gfxstream::Stream* stream = nullptr,
                                        bool linear = false);

    static std::unique_ptr<Impl> onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        gfxstream::Stream* stream);

    void onSave(gfxstream::Stream* stream);
    void restore();

    HandleType getHndl() const { return mHandle; }
    uint32_t getWidth() const { return mWidth; }
    uint32_t getHeight() const { return mHeight; }
    GfxstreamFormat getFormat() const { return mFormat; }

    void readToBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                     void* outPixels, uint64_t outPixelsSize);
    void readToBytesScaled(int pixelsWidth, int pixelsHeight, int pixelsRotation, Rect rect,
                           GfxstreamFormat pixelsFormat, void* outPixels,
                           const std::optional<std::array<float, 16>>& colorTransform);
    void readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                        uint32_t outPixelsSize);

    bool updateFromBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                         const void* pixels, void* metadata = nullptr);
    bool updateGlFromBytes(const void* bytes, std::size_t bytesSize);

    bool flushFromGl();
    bool flushFromVk();
    bool flushFromVkBytes(const void* bytes, size_t bytesSize);
    bool invalidateForGl();
    bool invalidateForVk();
    bool invalidateForBackend(Backend backend);
    bool flushFromBackend(Backend backend);
    bool importHandle(void* handle, bool preserveContent);

    std::optional<BlobDescriptorInfo> exportBlob();

    gl::ColorBufferGl* getColorBufferGl() const {
#if GFXSTREAM_ENABLE_HOST_GLES
        return mColorBufferGl.get();
#else
        return nullptr;
#endif
    }

    vk::ColorBufferVk* getColorBufferVk() const { return mColorBufferVk.get(); }

   private:
    Impl(HandleType, uint32_t width, uint32_t height, GfxstreamFormat format);

    const HandleType mHandle;
    const uint32_t mWidth;
    const uint32_t mHeight;
    const GfxstreamFormat mFormat;

#if GFXSTREAM_ENABLE_HOST_GLES
    // If GL emulation is enabled.
    std::unique_ptr<ColorBufferGl> mColorBufferGl;
#endif

    // If Vk emulation is enabled.
    std::unique_ptr<ColorBufferVk> mColorBufferVk;

    bool mGlAndVkAreSharingExternalMemory = false;
    bool mGlTexDirty = false;
};

ColorBuffer::Impl::Impl(HandleType handle, uint32_t width, uint32_t height, GfxstreamFormat format)
    : mHandle(handle), mWidth(width), mHeight(height), mFormat(format) {}

/*static*/
std::unique_ptr<ColorBuffer::Impl> ColorBuffer::Impl::create(
    gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk, uint32_t width, uint32_t height,
    GfxstreamFormat format, HandleType handle, gfxstream::Stream* stream, bool linear) {
    std::unique_ptr<Impl> colorBuffer(new Impl(handle, width, height, format));

    if (stream) {
        // When vk snapshot enabled, mNeedRestore will be touched and set to false immediately.
        colorBuffer->mNeedRestore = true;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (emulationGl) {
        if (stream) {
            colorBuffer->mColorBufferGl = emulationGl->loadColorBuffer(stream);
        } else {
            colorBuffer->mColorBufferGl =
                emulationGl->createColorBuffer(width, height, format, handle);
        }
        if (!colorBuffer->mColorBufferGl) {
            GFXSTREAM_ERROR("Failed to initialize ColorBufferGl.");
            return nullptr;
        }
    }
#endif

    if (emulationVk) {
#if GFXSTREAM_ENABLE_HOST_GLES
        const bool vulkanOnly = colorBuffer->mColorBufferGl == nullptr;
#else
        const bool vulkanOnly = true;
#endif
        const uint32_t memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT |
            (linear ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : 0);
        const uint32_t mipLevels = 1;
        colorBuffer->mColorBufferVk = vk::ColorBufferVk::create(
            *emulationVk, handle, width, height, format, vulkanOnly, memoryProperty, mipLevels);
        if (!colorBuffer->mColorBufferVk) {
            if (emulationGl) {
                // Historically, ColorBufferVk setup was deferred until the first actual Vulkan
                // usage. This allowed ColorBufferVk setup failures to be unintentionally avoided.
            } else {
                GFXSTREAM_ERROR("Failed to initialize ColorBufferVk.");
                return nullptr;
            }
        }
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    bool vkSnapshotEnabled = emulationVk && emulationVk->getFeatures().VulkanSnapshots.enabled();

    if ((!stream || vkSnapshotEnabled) && colorBuffer->mColorBufferGl &&
        colorBuffer->mColorBufferVk && shouldAttemptExternalMemorySharing(format)) {
        colorBuffer->touch();
        auto memoryExport = emulationVk->exportColorBufferMemory(handle);
        if (memoryExport) {
            if (colorBuffer->mColorBufferGl->importMemory(
                    memoryExport->handleInfo.toManagedDescriptor(), memoryExport->size,
                    memoryExport->dedicatedAllocation, memoryExport->linearTiling)) {
                colorBuffer->mGlAndVkAreSharingExternalMemory = true;
            } else {
                GFXSTREAM_ERROR("Failed to import memory to ColorBufferGl:%d", handle);
            }
        }
    }
#endif

    if (colorBuffer->mColorBufferVk && stream) {
        auto behavior = colorBuffer->mGlAndVkAreSharingExternalMemory
                            ? vk::LoadImageBehavior::SkipImageContent
                            : vk::LoadImageBehavior::LoadImageContent;
        colorBuffer->mColorBufferVk->onLoad(stream, behavior);
    }

    return colorBuffer;
}

/*static*/
std::unique_ptr<ColorBuffer::Impl> ColorBuffer::Impl::onLoad(gl::EmulationGl* emulationGl,
                                                             vk::VkEmulation* emulationVk,
                                                             gfxstream::Stream* stream) {
    const auto handle = static_cast<HandleType>(stream->getBe32());
    const auto width = static_cast<uint32_t>(stream->getBe32());
    const auto height = static_cast<uint32_t>(stream->getBe32());
    const auto format = static_cast<GfxstreamFormat>(stream->getBe32());

    std::unique_ptr<Impl> colorBuffer =
        Impl::create(emulationGl, emulationVk, width, height, format, handle, stream);

    return colorBuffer;
}

void ColorBuffer::Impl::onSave(gfxstream::Stream* stream) {
    stream->putBe32(getHndl());
    stream->putBe32(mWidth);
    stream->putBe32(mHeight);
    stream->putBe32(static_cast<uint32_t>(mFormat));

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->onSave(stream);
    }
#endif
    if (mColorBufferVk) {
        auto behavior = mGlAndVkAreSharingExternalMemory ? vk::SaveImageBehavior::SkipImageContent
                                                         : vk::SaveImageBehavior::SaveImageContent;
        mColorBufferVk->onSave(stream, behavior);
    }
}

void ColorBuffer::Impl::restore() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->restore();
    }
#endif
}

void ColorBuffer::Impl::readToBytes(int x, int y, int width, int height,
                                    GfxstreamFormat pixelsFormat, void* outPixels,
                                    uint64_t outPixelsSize) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->readPixels(x, y, width, height, pixelsFormat, outPixels, outPixelsSize);
        return;
    }
#endif

    if (mColorBufferVk) {
        mColorBufferVk->readToBytes(x, y, width, height, outPixels, outPixelsSize);
        return;
    }

    GFXSTREAM_FATAL("%s: No ColorBuffer impl", __func__);
}

void ColorBuffer::Impl::readToBytesScaled(
    int pixelsWidth, int pixelsHeight, int pixelsRotation, Rect rect, GfxstreamFormat pixelsFormat,
    void* outPixels, const std::optional<std::array<float, 16>>& colorTransform) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->readPixelsScaled(pixelsWidth, pixelsHeight, pixelsRotation, rect,
                                         pixelsFormat, outPixels, colorTransform);
        return;
    }
#endif

    if (mColorBufferVk) {
        mColorBufferVk->readPixelsScaled(pixelsWidth, pixelsHeight, pixelsRotation, rect,
                                         pixelsFormat, outPixels, colorTransform);
        return;
    }

    GFXSTREAM_FATAL("%s: No ColorBuffer impl", __func__);
}

void ColorBuffer::Impl::readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                                       uint32_t outPixelsSize) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->readPixelsYUVCached(x, y, width, height, outPixels, outPixelsSize);
        return;
    }
#endif

    if (mColorBufferVk) {
        mColorBufferVk->readToBytes(x, y, width, height, outPixels, outPixelsSize);
        return;
    }

    GFXSTREAM_FATAL("%s: No ColorBuffer impl", __func__);
}

bool ColorBuffer::Impl::updateFromBytes(int x, int y, int width, int height,
                                        GfxstreamFormat pixelsFormat, const void* pixels,
                                        void* metadata) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        bool res = mColorBufferGl->subUpdate(x, y, width, height, pixelsFormat, pixels, metadata);
        if (res) {
            flushFromGl();
        }
        return res;
    }
#endif

    if (mColorBufferVk) {
        return mColorBufferVk->updateFromBytes(x, y, width, height, pixels);
    }

    GFXSTREAM_FATAL("%s: No ColorBuffer impl", __func__);
    return false;
}

bool ColorBuffer::Impl::updateGlFromBytes(const void* bytes, std::size_t bytesSize) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        touch();

        return mColorBufferGl->replaceContents(bytes, bytesSize);
    }
#endif

    return true;
}

bool ColorBuffer::Impl::invalidateForBackend(Backend backend) {
    return backend == Backend::VK ? invalidateForVk() : invalidateForGl();
}

bool ColorBuffer::Impl::flushFromBackend(Backend backend) {
    return backend == Backend::VK ? flushFromVk() : flushFromGl();
}

bool ColorBuffer::Impl::importHandle(void* handle, bool preserveContent) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        return mColorBufferGl->importEglNativePixmap(handle, preserveContent);
    }
#endif
    return false;
}

bool ColorBuffer::Impl::flushFromGl() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

    // ColorBufferGl is currently considered the "main" backing. If this changes,
    // the "main"  should be updated from the current contents of the GL backing.
    mGlTexDirty = true;
    return true;
}

bool ColorBuffer::Impl::flushFromVk() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }
    std::vector<uint8_t> contents;
    if (!mColorBufferVk->readToBytes(&contents)) {
        GFXSTREAM_ERROR("Failed to get VK contents for ColorBuffer:%d", mHandle);
        return false;
    }

    if (contents.empty()) {
        return false;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (!mColorBufferGl->replaceContents(contents.data(), contents.size())) {
        GFXSTREAM_ERROR("Failed to set GL contents for ColorBuffer:%d", mHandle);
        return false;
    }
#endif
    mGlTexDirty = false;
    return true;
}

bool ColorBuffer::Impl::flushFromVkBytes(const void* bytes, size_t bytesSize) {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        if (!mColorBufferGl->replaceContents(bytes, bytesSize)) {
            GFXSTREAM_ERROR("Failed to update ColorBuffer:%d GL backing from VK bytes.", mHandle);
            return false;
        }
    }
#endif
    mGlTexDirty = false;
    return true;
}

bool ColorBuffer::Impl::invalidateForGl() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

    // ColorBufferGl is currently considered the "main" backing. If this changes,
    // the GL backing should be updated from the "main" backing.
    return true;
}

bool ColorBuffer::Impl::invalidateForVk() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

    if (!mGlTexDirty) {
        return true;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    std::vector<uint8_t> contents;
    if (!mColorBufferGl->readContents(&contents)) {
        GFXSTREAM_ERROR("Failed to get GL contents size for ColorBuffer:%d", mHandle);
        return false;
    }

    if (!mColorBufferVk->updateFromBytes(contents)) {
        GFXSTREAM_ERROR("Failed to set VK contents for ColorBuffer:%d", mHandle);
        return false;
    }
#endif
    mGlTexDirty = false;
    return true;
}

std::optional<BlobDescriptorInfo> ColorBuffer::Impl::exportBlob() {
    if (!mColorBufferVk) {
        return std::nullopt;
    }

    return mColorBufferVk->exportBlob();
}

////////////////////////////////////////////////////////////////////////////////////////////////

/*static*/
std::shared_ptr<ColorBuffer> ColorBuffer::create(gl::EmulationGl* emulationGl,
                                                 vk::VkEmulation* emulationVk, uint32_t width,
                                                 uint32_t height, GfxstreamFormat format,
                                                 HandleType handle, gfxstream::Stream* stream,
                                                 bool linear) {
    std::shared_ptr<ColorBuffer> colorbuffer(new ColorBuffer());

    colorbuffer->mImpl = ColorBuffer::Impl::create(emulationGl, emulationVk, width, height, format,
                                                   handle, stream, linear);
    if (!colorbuffer->mImpl) {
        return nullptr;
    }

    return colorbuffer;
}

/*static*/
std::shared_ptr<ColorBuffer> ColorBuffer::onLoad(gl::EmulationGl* emulationGl,
                                                 vk::VkEmulation* emulationVk,
                                                 gfxstream::Stream* stream) {
    std::shared_ptr<ColorBuffer> colorbuffer(new ColorBuffer());

    colorbuffer->mImpl = ColorBuffer::Impl::onLoad(emulationGl, emulationVk, stream);
    if (!colorbuffer->mImpl) {
        return nullptr;
    }
    colorbuffer->mNeedRestore = true;

    return colorbuffer;
}

void ColorBuffer::onSave(gfxstream::Stream* stream) { mImpl->onSave(stream); }

void ColorBuffer::restore() { mImpl->touch(); }

void ColorBuffer::touch() { mImpl->touch(); }

HandleType ColorBuffer::getHndl() const { return mImpl->getHndl(); }

gl::ColorBufferGl* ColorBuffer::getColorBufferGl() { return mImpl->getColorBufferGl(); }

vk::ColorBufferVk* ColorBuffer::getColorBufferVk() { return mImpl->getColorBufferVk(); }

uint32_t ColorBuffer::getWidth() const { return mImpl->getWidth(); }

uint32_t ColorBuffer::getHeight() const { return mImpl->getHeight(); }

GfxstreamFormat ColorBuffer::getFormat() const { return mImpl->getFormat(); }

void ColorBuffer::readToBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                              void* outPixels, uint64_t outPixelsSize) {
    mImpl->readToBytes(x, y, width, height, pixelsFormat, outPixels, outPixelsSize);
}

void ColorBuffer::readToBytesScaled(int pixelsWidth, int pixelsHeight, int pixelsRotation,
                                    const Rect& rect, GfxstreamFormat pixelsFormat, void* outPixels,
                                    const std::optional<std::array<float, 16>>& colorTransform) {
    mImpl->readToBytesScaled(pixelsWidth, pixelsHeight, pixelsRotation, rect, pixelsFormat,
                             outPixels, colorTransform);
}

void ColorBuffer::readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                                 uint32_t outPixelsSize) {
    mImpl->readYuvToBytes(x, y, width, height, outPixels, outPixelsSize);
}

bool ColorBuffer::updateFromBytes(int x, int y, int width, int height, GfxstreamFormat pixelsFormat,
                                  const void* pixels, void* metadata) {
    return mImpl->updateFromBytes(x, y, width, height, pixelsFormat, pixels, metadata);
}

bool ColorBuffer::updateGlFromBytes(const void* bytes, std::size_t bytesSize) {
    return mImpl->updateGlFromBytes(bytes, bytesSize);
}

bool ColorBuffer::invalidateForBackend(Backend backend) {
    return mImpl->invalidateForBackend(backend);
}

bool ColorBuffer::flushFromBackend(Backend backend) {
    return mImpl->flushFromBackend(backend);
}

bool ColorBuffer::importHandle(void* handle, bool preserveContent) {
    return mImpl->importHandle(handle, preserveContent);
}

bool ColorBuffer::flushFromGl() { return mImpl->flushFromGl(); }

bool ColorBuffer::flushFromVk() { return mImpl->flushFromVk(); }

bool ColorBuffer::flushFromVkBytes(const void* bytes, size_t bytesSize) {
    return mImpl->flushFromVkBytes(bytes, bytesSize);
}

std::optional<BlobDescriptorInfo> ColorBuffer::exportBlob() { return mImpl->exportBlob(); }

}  // namespace host
}  // namespace gfxstream
