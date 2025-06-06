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

#include "ColorBuffer.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "EmulationGl.h"
#endif
#include "FrameBuffer.h"
#include "gfxstream/common/logging.h"
#include "vulkan/ColorBufferVk.h"
#include "vulkan/VkCommonOperations.h"

namespace gfxstream {
namespace {

using gl::ColorBufferGl;
using vk::ColorBufferVk;

// ColorBufferVk natively supports YUV images. However, ColorBufferGl
// needs to emulate YUV support by having an underlying RGBA texture
// and adding in additional YUV<->RGBA conversions when needed. The
// memory should not be shared between the VK YUV image and the GL RGBA
// texture.
bool shouldAttemptExternalMemorySharing(FrameworkFormat format) {
    return format == FrameworkFormat::FRAMEWORK_FORMAT_GL_COMPATIBLE;
}

}  // namespace

class ColorBuffer::Impl : public LazySnapshotObj<ColorBuffer::Impl> {
   public:
    static std::unique_ptr<Impl> create(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        uint32_t width, uint32_t height, GLenum format,
                                        FrameworkFormat frameworkFormat, HandleType handle,
                                        gfxstream::Stream* stream = nullptr);

    static std::unique_ptr<Impl> onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        gfxstream::Stream* stream);

    void onSave(gfxstream::Stream* stream);
    void restore();

    HandleType getHndl() const { return mHandle; }
    uint32_t getWidth() const { return mWidth; }
    uint32_t getHeight() const { return mHeight; }
    GLenum getFormat() const { return mFormat; }
    FrameworkFormat getFrameworkFormat() const { return mFrameworkFormat; }

    void readToBytes(int x, int y, int width, int height, GLenum pixelsFormat, GLenum pixelsType,
                     void* outPixels, uint64_t outPixelsSize);
    void readToBytesScaled(int pixelsWidth, int pixelsHeight, GLenum pixelsFormat,
                           GLenum pixelsType, int pixelsRotation, Rect rect, void* outPixels);
    void readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                        uint32_t outPixelsSize);

    bool updateFromBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                         GLenum pixelsType, const void* pixels);
    bool updateFromBytes(int x, int y, int width, int height, FrameworkFormat frameworkFormat,
                         GLenum pixelsFormat, GLenum pixelsType, const void* pixels,
                         void* metadata = nullptr);
    bool updateGlFromBytes(const void* bytes, std::size_t bytesSize);

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
    Impl(HandleType, uint32_t width, uint32_t height, GLenum format,
         FrameworkFormat frameworkFormat);

    const HandleType mHandle;
    const uint32_t mWidth;
    const uint32_t mHeight;
    const GLenum mFormat;
    const FrameworkFormat mFrameworkFormat;

#if GFXSTREAM_ENABLE_HOST_GLES
    // If GL emulation is enabled.
    std::unique_ptr<ColorBufferGl> mColorBufferGl;
#endif

    // If Vk emulation is enabled.
    std::unique_ptr<ColorBufferVk> mColorBufferVk;

    bool mGlAndVkAreSharingExternalMemory = false;
    bool mGlTexDirty = false;
};

ColorBuffer::Impl::Impl(HandleType handle, uint32_t width, uint32_t height, GLenum format,
                        FrameworkFormat frameworkFormat)
    : mHandle(handle),
      mWidth(width),
      mHeight(height),
      mFormat(format),
      mFrameworkFormat(frameworkFormat) {}

/*static*/
std::unique_ptr<ColorBuffer::Impl> ColorBuffer::Impl::create(
    gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk, uint32_t width, uint32_t height,
    GLenum format, FrameworkFormat frameworkFormat, HandleType handle, gfxstream::Stream* stream) {
    std::unique_ptr<Impl> colorBuffer(new Impl(handle, width, height, format, frameworkFormat));

    if (stream) {
        // When vk snapshot enabled, mNeedRestore will be touched and set to false immediately.
        colorBuffer->mNeedRestore = true;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (emulationGl) {
        if (stream) {
            colorBuffer->mColorBufferGl = emulationGl->loadColorBuffer(stream);
            assert(width == colorBuffer->mColorBufferGl->getWidth());
            assert(height == colorBuffer->mColorBufferGl->getHeight());
            assert(frameworkFormat == colorBuffer->mColorBufferGl->getFrameworkFormat());
        } else {
            colorBuffer->mColorBufferGl =
                emulationGl->createColorBuffer(width, height, format, frameworkFormat, handle);
        }
        if (!colorBuffer->mColorBufferGl) {
            GFXSTREAM_ERROR("Failed to initialize ColorBufferGl.");
            return nullptr;
        }
    }
#endif

    if (emulationVk) {
        const bool vulkanOnly = colorBuffer->mColorBufferGl == nullptr;
        const uint32_t memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        colorBuffer->mColorBufferVk =
            vk::ColorBufferVk::create(*emulationVk, handle, width, height, format, frameworkFormat,
                                      vulkanOnly, memoryProperty, stream);
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
    bool vkSnapshotEnabled = emulationVk && emulationVk->getFeatures().VulkanSnapshots.enabled;

    if ((!stream || vkSnapshotEnabled) && colorBuffer->mColorBufferGl && colorBuffer->mColorBufferVk &&
        shouldAttemptExternalMemorySharing(frameworkFormat)) {
        colorBuffer->touch();
        auto memoryExport = emulationVk->exportColorBufferMemory(handle);
        if (memoryExport) {
            if (colorBuffer->mColorBufferGl->importMemory(
                    memoryExport->handleInfo.toManagedDescriptor(),
                    memoryExport->size, memoryExport->dedicatedAllocation,
                    memoryExport->linearTiling)) {
                colorBuffer->mGlAndVkAreSharingExternalMemory = true;
            } else {
                GFXSTREAM_ERROR("Failed to import memory to ColorBufferGl:%d", handle);
            }
        }
    }
#endif

    return colorBuffer;
}

/*static*/
std::unique_ptr<ColorBuffer::Impl> ColorBuffer::Impl::onLoad(gl::EmulationGl* emulationGl,
                                                             vk::VkEmulation* emulationVk,
                                                             gfxstream::Stream* stream) {
    const auto handle = static_cast<HandleType>(stream->getBe32());
    const auto width = static_cast<uint32_t>(stream->getBe32());
    const auto height = static_cast<uint32_t>(stream->getBe32());
    const auto format = static_cast<GLenum>(stream->getBe32());
    const auto frameworkFormat = static_cast<FrameworkFormat>(stream->getBe32());

    std::unique_ptr<Impl> colorBuffer = Impl::create(emulationGl, emulationVk, width, height,
                                                     format, frameworkFormat, handle, stream);

    return colorBuffer;
}

void ColorBuffer::Impl::onSave(gfxstream::Stream* stream) {
    stream->putBe32(getHndl());
    stream->putBe32(mWidth);
    stream->putBe32(mHeight);
    stream->putBe32(static_cast<uint32_t>(mFormat));
    stream->putBe32(static_cast<uint32_t>(mFrameworkFormat));

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->onSave(stream);
    }
#endif
    if (mColorBufferVk) {
        mColorBufferVk->onSave(stream);
    }
}

void ColorBuffer::Impl::restore() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->restore();
    }
#endif
}

void ColorBuffer::Impl::readToBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                                    GLenum pixelsType, void* outPixels, uint64_t outPixelsSize) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->readPixels(x, y, width, height, pixelsFormat, pixelsType, outPixels);
        return;
    }
#endif

    if (mColorBufferVk) {
        mColorBufferVk->readToBytes(x, y, width, height, outPixels, outPixelsSize);
        return;
    }

    GFXSTREAM_FATAL("No ColorBuffer impl");
}

void ColorBuffer::Impl::readToBytesScaled(int pixelsWidth, int pixelsHeight, GLenum pixelsFormat,
                                          GLenum pixelsType, int pixelsRotation, Rect rect,
                                          void* outPixels) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->readPixelsScaled(pixelsWidth, pixelsHeight, pixelsFormat, pixelsType,
                                         pixelsRotation, rect, outPixels);
        return;
    }
#endif

    GFXSTREAM_FATAL("Unimplemented");
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

    GFXSTREAM_FATAL("No ColorBuffer impl");
}

bool ColorBuffer::Impl::updateFromBytes(int x, int y, int width, int height,
                                        FrameworkFormat frameworkFormat, GLenum pixelsFormat,
                                        GLenum pixelsType, const void* pixels, void* metadata) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->subUpdateFromFrameworkFormat(x, y, width, height, frameworkFormat,
                                                     pixelsFormat, pixelsType, pixels, metadata);
        flushFromGl();
        return true;
    }
#endif

    if (mColorBufferVk) {
        bool success = mColorBufferVk->updateFromBytes(x, y, width, height, pixels);
        if (!success) return success;
        flushFromVk();
        return true;
    }

    GFXSTREAM_FATAL("No ColorBuffer impl");
    return false;
}

bool ColorBuffer::Impl::updateFromBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                                        GLenum pixelsType, const void* pixels) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        bool res = mColorBufferGl->subUpdate(x, y, width, height, pixelsFormat, pixelsType, pixels);
        if (res) {
            flushFromGl();
        }
        return res;
    }
#endif

    if (mColorBufferVk) {
        return mColorBufferVk->updateFromBytes(x, y, width, height, pixels);
    }

    GFXSTREAM_FATAL("No ColorBuffer impl");
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

std::unique_ptr<BorrowedImageInfo> ColorBuffer::Impl::borrowForComposition(UsedApi api,
                                                                           bool isTarget) {
    switch (api) {
        case UsedApi::kGl: {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (!mColorBufferGl) {
                GFXSTREAM_FATAL("ColorBufferGl not available");
            }
            return mColorBufferGl->getBorrowedImageInfo();
#endif
        }
        case UsedApi::kVk: {
            if (!mColorBufferVk) {
                GFXSTREAM_FATAL("ColorBufferVk not available");
            }
            return mColorBufferVk->borrowForComposition(isTarget);
        }
    }
    GFXSTREAM_FATAL("Unimplemented");
    return nullptr;
}

std::unique_ptr<BorrowedImageInfo> ColorBuffer::Impl::borrowForDisplay(UsedApi api) {
    switch (api) {
        case UsedApi::kGl: {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (!mColorBufferGl) {
                GFXSTREAM_FATAL("ColorBufferGl not available");
            }
            return mColorBufferGl->getBorrowedImageInfo();
#endif
        }
        case UsedApi::kVk: {
            if (!mColorBufferVk) {
                GFXSTREAM_FATAL("ColorBufferVk not available");
            }
            return mColorBufferVk->borrowForDisplay();
        }
    }
    GFXSTREAM_FATAL("Unimplemented");
    return nullptr;
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
    std::size_t contentsSize = 0;
    if (!mColorBufferGl->readContents(&contentsSize, nullptr)) {
        GFXSTREAM_ERROR("Failed to get GL contents size for ColorBuffer:%d", mHandle);
        return false;
    }

    std::vector<uint8_t> contents(contentsSize, 0);

    if (!mColorBufferGl->readContents(&contentsSize, contents.data())) {
        GFXSTREAM_ERROR("Failed to get GL contents for ColorBuffer:%d", mHandle);
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

#if GFXSTREAM_ENABLE_HOST_GLES
bool ColorBuffer::Impl::glOpBlitFromCurrentReadBuffer() {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    return mColorBufferGl->blitFromCurrentReadBuffer();
}

bool ColorBuffer::Impl::glOpBindToTexture() {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    return mColorBufferGl->bindToTexture();
}

bool ColorBuffer::Impl::glOpBindToTexture2() {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    return mColorBufferGl->bindToTexture2();
}

bool ColorBuffer::Impl::glOpBindToRenderbuffer() {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    return mColorBufferGl->bindToRenderbuffer();
}

GLuint ColorBuffer::Impl::glOpGetTexture() {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    return mColorBufferGl->getTexture();
}

void ColorBuffer::Impl::glOpReadback(unsigned char* img, bool readbackBgra) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    return mColorBufferGl->readback(img, readbackBgra);
}

void ColorBuffer::Impl::glOpReadbackAsync(GLuint buffer, bool readbackBgra) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    touch();

    mColorBufferGl->readbackAsync(buffer, readbackBgra);
}

bool ColorBuffer::Impl::glOpImportEglNativePixmap(void* pixmap, bool preserveContent) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    return mColorBufferGl->importEglNativePixmap(pixmap, preserveContent);
}

void ColorBuffer::Impl::glOpSwapYuvTexturesAndUpdate(GLenum format, GLenum type,
                                                     FrameworkFormat frameworkFormat,
                                                     GLuint* textures) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    mColorBufferGl->swapYUVTextures(frameworkFormat, textures);

    // This makes ColorBufferGl regenerate the RGBA texture using
    // YUVConverter::drawConvert() with the updated YUV textures.
    mColorBufferGl->subUpdate(0, 0, mWidth, mHeight, format, type, nullptr);

    flushFromGl();
}

bool ColorBuffer::Impl::glOpReadContents(size_t* outNumBytes, void* outContents) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    return mColorBufferGl->readContents(outNumBytes, outContents);
}

bool ColorBuffer::Impl::glOpIsFastBlitSupported() const {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    return mColorBufferGl->isFastBlitSupported();
}

void ColorBuffer::Impl::glOpPostLayer(const ComposeLayer& l, int frameWidth, int frameHeight) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    mColorBufferGl->postLayer(l, frameWidth, frameHeight);
}

void ColorBuffer::Impl::glOpPostViewportScaledWithOverlay(float rotation, float dx, float dy) {
    if (!mColorBufferGl) {
        GFXSTREAM_FATAL("ColorBufferGl not available");
    }

    mColorBufferGl->postViewportScaledWithOverlay(rotation, dx, dy);
}
#endif

////////////////////////////////////////////////////////////////////////////////////////////////

/*static*/
std::shared_ptr<ColorBuffer> ColorBuffer::create(gl::EmulationGl* emulationGl,
                                                 vk::VkEmulation* emulationVk, uint32_t width,
                                                 uint32_t height, GLenum format,
                                                 FrameworkFormat frameworkFormat, HandleType handle,
                                                 gfxstream::Stream* stream) {
    std::shared_ptr<ColorBuffer> colorbuffer(new ColorBuffer());

    colorbuffer->mImpl = ColorBuffer::Impl::create(emulationGl, emulationVk, width, height, format,
                                                   frameworkFormat, handle, stream);
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

HandleType ColorBuffer::getHndl() const { return mImpl->getHndl(); }

uint32_t ColorBuffer::getWidth() const { return mImpl->getWidth(); }

uint32_t ColorBuffer::getHeight() const { return mImpl->getHeight(); }

GLenum ColorBuffer::getFormat() const { return mImpl->getFormat(); }

FrameworkFormat ColorBuffer::getFrameworkFormat() const { return mImpl->getFrameworkFormat(); }

void ColorBuffer::readToBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                              GLenum pixelsType, void* outPixels, uint64_t outPixelsSize) {
    mImpl->readToBytes(x, y, width, height, pixelsFormat, pixelsType, outPixels, outPixelsSize);
}

void ColorBuffer::readToBytesScaled(int pixelsWidth, int pixelsHeight, GLenum pixelsFormat,
                                    GLenum pixelsType, int pixelsRotation, Rect rect,
                                    void* outPixels) {
    mImpl->readToBytesScaled(pixelsWidth, pixelsHeight, pixelsFormat, pixelsType, pixelsRotation,
                             rect, outPixels);
}

void ColorBuffer::readYuvToBytes(int x, int y, int width, int height, void* outPixels,
                                 uint32_t outPixelsSize) {
    mImpl->readYuvToBytes(x, y, width, height, outPixels, outPixelsSize);
}

bool ColorBuffer::updateFromBytes(int x, int y, int width, int height, GLenum pixelsFormat,
                                  GLenum pixelsType, const void* pixels) {
    return mImpl->updateFromBytes(x, y, width, height, pixelsFormat, pixelsType, pixels);
}

bool ColorBuffer::updateFromBytes(int x, int y, int width, int height,
                                  FrameworkFormat frameworkFormat, GLenum pixelsFormat,
                                  GLenum pixelsType, const void* pixels, void* metadata) {
    return mImpl->updateFromBytes(x, y, width, height, frameworkFormat, pixelsFormat, pixelsType,
                                  pixels, metadata);
}

bool ColorBuffer::updateGlFromBytes(const void* bytes, std::size_t bytesSize) {
    return mImpl->updateGlFromBytes(bytes, bytesSize);
}

std::unique_ptr<BorrowedImageInfo> ColorBuffer::borrowForComposition(UsedApi api, bool isTarget) {
    return mImpl->borrowForComposition(api, isTarget);
}

std::unique_ptr<BorrowedImageInfo> ColorBuffer::borrowForDisplay(UsedApi api) {
    return mImpl->borrowForDisplay(api);
}

bool ColorBuffer::flushFromGl() { return mImpl->flushFromGl(); }

bool ColorBuffer::flushFromVk() { return mImpl->flushFromVk(); }

bool ColorBuffer::flushFromVkBytes(const void* bytes, size_t bytesSize) {
    return mImpl->flushFromVkBytes(bytes, bytesSize);
}

bool ColorBuffer::invalidateForGl() { return mImpl->invalidateForGl(); }

bool ColorBuffer::invalidateForVk() { return mImpl->invalidateForVk(); }

std::optional<BlobDescriptorInfo> ColorBuffer::exportBlob() { return mImpl->exportBlob(); }

#if GFXSTREAM_ENABLE_HOST_GLES
GLuint ColorBuffer::glOpGetTexture() { return mImpl->glOpGetTexture(); }

bool ColorBuffer::glOpBlitFromCurrentReadBuffer() { return mImpl->glOpBlitFromCurrentReadBuffer(); }

bool ColorBuffer::glOpBindToTexture() { return mImpl->glOpBindToTexture(); }

bool ColorBuffer::glOpBindToTexture2() { return mImpl->glOpBindToTexture2(); }

bool ColorBuffer::glOpBindToRenderbuffer() { return mImpl->glOpBindToRenderbuffer(); }

void ColorBuffer::glOpReadback(unsigned char* img, bool readbackBgra) {
    return mImpl->glOpReadback(img, readbackBgra);
}

void ColorBuffer::glOpReadbackAsync(GLuint buffer, bool readbackBgra) {
    return mImpl->glOpReadbackAsync(buffer, readbackBgra);
}

bool ColorBuffer::glOpImportEglNativePixmap(void* pixmap, bool preserveContent) {
    return mImpl->glOpImportEglNativePixmap(pixmap, preserveContent);
}

void ColorBuffer::glOpSwapYuvTexturesAndUpdate(GLenum format, GLenum type,
                                               FrameworkFormat frameworkFormat, GLuint* textures) {
    return mImpl->glOpSwapYuvTexturesAndUpdate(format, type, frameworkFormat, textures);
}

bool ColorBuffer::glOpReadContents(size_t* outNumBytes, void* outContents) {
    return mImpl->glOpReadContents(outNumBytes, outContents);
}

bool ColorBuffer::glOpIsFastBlitSupported() const { return mImpl->glOpIsFastBlitSupported(); }

void ColorBuffer::glOpPostLayer(const ComposeLayer& l, int frameWidth, int frameHeight) {
    return mImpl->glOpPostLayer(l, frameWidth, frameHeight);
}

void ColorBuffer::glOpPostViewportScaledWithOverlay(float rotation, float dx, float dy) {
    return mImpl->glOpPostViewportScaledWithOverlay(rotation, dx, dy);
}

#endif

}  // namespace gfxstream
