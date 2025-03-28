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
#include "gl/EmulationGl.h"
#endif

#include "host-common/GfxstreamFatalError.h"
#include "host-common/logging.h"
#include "vulkan/ColorBufferVk.h"
#include "vulkan/VkCommonOperations.h"
#include "FrameBuffer.h"

using emugl::ABORT_REASON_OTHER;
using emugl::FatalError;

namespace gfxstream {
namespace {

// ColorBufferVk natively supports YUV images. However, ColorBufferGl
// needs to emulate YUV support by having an underlying RGBA texture
// and adding in additional YUV<->RGBA conversions when needed. The
// memory should not be shared between the VK YUV image and the GL RGBA
// texture.
bool shouldAttemptExternalMemorySharing(FrameworkFormat format) {
    return format == FrameworkFormat::FRAMEWORK_FORMAT_GL_COMPATIBLE;
}

}  // namespace

ColorBuffer::ColorBuffer(HandleType handle, uint32_t width, uint32_t height, GLenum format,
                         FrameworkFormat frameworkFormat)
    : mHandle(handle),
      mWidth(width),
      mHeight(height),
      mFormat(format),
      mFrameworkFormat(frameworkFormat) {}

/*static*/
std::shared_ptr<ColorBuffer> ColorBuffer::create(gl::EmulationGl* emulationGl,
                                                 vk::VkEmulation* emulationVk, uint32_t width,
                                                 uint32_t height, GLenum format,
                                                 FrameworkFormat frameworkFormat, HandleType handle,
                                                 android::base::Stream* stream) {
    std::shared_ptr<ColorBuffer> colorBuffer(
        new ColorBuffer(handle, width, height, format, frameworkFormat));

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
            ERR("Failed to initialize ColorBufferGl.");
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
                ERR("Failed to initialize ColorBufferVk.");
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
                ERR("Failed to import memory to ColorBufferGl:%d", handle);
            }
        }
    }
#endif

    return colorBuffer;
}

/*static*/
std::shared_ptr<ColorBuffer> ColorBuffer::onLoad(gl::EmulationGl* emulationGl,
                                                 vk::VkEmulation* emulationVk,
                                                 android::base::Stream* stream) {
    const auto handle = static_cast<HandleType>(stream->getBe32());
    const auto width = static_cast<uint32_t>(stream->getBe32());
    const auto height = static_cast<uint32_t>(stream->getBe32());
    const auto format = static_cast<GLenum>(stream->getBe32());
    const auto frameworkFormat = static_cast<FrameworkFormat>(stream->getBe32());

    std::shared_ptr<ColorBuffer> colorBuffer = ColorBuffer::create(
        emulationGl, emulationVk, width, height, format, frameworkFormat, handle, stream);

    return colorBuffer;
}

void ColorBuffer::onSave(android::base::Stream* stream) {
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

void ColorBuffer::restore() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        mColorBufferGl->restore();
    }
#endif
}

void ColorBuffer::readToBytes(int x, int y, int width, int height, GLenum pixelsFormat,
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

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "No ColorBuffer impl?";
}

void ColorBuffer::readToBytesScaled(int pixelsWidth, int pixelsHeight, GLenum pixelsFormat,
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

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "Unimplemented.";
}

void ColorBuffer::readYuvToBytes(int x, int y, int width, int height, void* outPixels,
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

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "No ColorBuffer impl?";
}

bool ColorBuffer::updateFromBytes(int x, int y, int width, int height,
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

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "No ColorBuffer impl?";
    return false;
}

bool ColorBuffer::updateFromBytes(int x, int y, int width, int height, GLenum pixelsFormat,
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

    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "No ColorBuffer impl?";
    return false;
}

bool ColorBuffer::updateGlFromBytes(const void* bytes, std::size_t bytesSize) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        touch();

        return mColorBufferGl->replaceContents(bytes, bytesSize);
    }
#endif

    return true;
}

std::unique_ptr<BorrowedImageInfo> ColorBuffer::borrowForComposition(UsedApi api, bool isTarget) {
    switch (api) {
        case UsedApi::kGl: {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (!mColorBufferGl) {
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
            }
            return mColorBufferGl->getBorrowedImageInfo();
#endif
        }
        case UsedApi::kVk: {
            if (!mColorBufferVk) {
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
            }
            return mColorBufferVk->borrowForComposition(isTarget);
        }
    }
    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "Unimplemented";
    return nullptr;
}

std::unique_ptr<BorrowedImageInfo> ColorBuffer::borrowForDisplay(UsedApi api) {
    switch (api) {
        case UsedApi::kGl: {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (!mColorBufferGl) {
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
            }
            return mColorBufferGl->getBorrowedImageInfo();
#endif
        }
        case UsedApi::kVk: {
            if (!mColorBufferVk) {
                GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
            }
            return mColorBufferVk->borrowForDisplay();
        }
    }
    GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "Unimplemented";
    return nullptr;
}

bool ColorBuffer::flushFromGl() {
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

bool ColorBuffer::flushFromVk() {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }
    std::vector<uint8_t> contents;
    if (!mColorBufferVk->readToBytes(&contents)) {
        ERR("Failed to get VK contents for ColorBuffer:%d", mHandle);
        return false;
    }

    if (contents.empty()) {
        return false;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (!mColorBufferGl->replaceContents(contents.data(), contents.size())) {
        ERR("Failed to set GL contents for ColorBuffer:%d", mHandle);
        return false;
    }
#endif
    mGlTexDirty = false;
    return true;
}

bool ColorBuffer::flushFromVkBytes(const void* bytes, size_t bytesSize) {
    if (!(mColorBufferGl && mColorBufferVk)) {
        return true;
    }

    if (mGlAndVkAreSharingExternalMemory) {
        return true;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mColorBufferGl) {
        if (!mColorBufferGl->replaceContents(bytes, bytesSize)) {
            ERR("Failed to update ColorBuffer:%d GL backing from VK bytes.", mHandle);
            return false;
        }
    }
#endif
    mGlTexDirty = false;
    return true;
}

bool ColorBuffer::invalidateForGl() {
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

bool ColorBuffer::invalidateForVk() {
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
        ERR("Failed to get GL contents size for ColorBuffer:%d", mHandle);
        return false;
    }

    std::vector<uint8_t> contents(contentsSize, 0);

    if (!mColorBufferGl->readContents(&contentsSize, contents.data())) {
        ERR("Failed to get GL contents for ColorBuffer:%d", mHandle);
        return false;
    }

    if (!mColorBufferVk->updateFromBytes(contents)) {
        ERR("Failed to set VK contents for ColorBuffer:%d", mHandle);
        return false;
    }
#endif
    mGlTexDirty = false;
    return true;
}

std::optional<BlobDescriptorInfo> ColorBuffer::exportBlob() {
    if (!mColorBufferVk) {
        return std::nullopt;
    }

    return mColorBufferVk->exportBlob();
}

#if GFXSTREAM_ENABLE_HOST_GLES
bool ColorBuffer::glOpBlitFromCurrentReadBuffer() {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    return mColorBufferGl->blitFromCurrentReadBuffer();
}

bool ColorBuffer::glOpBindToTexture() {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    return mColorBufferGl->bindToTexture();
}

bool ColorBuffer::glOpBindToTexture2() {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    return mColorBufferGl->bindToTexture2();
}

bool ColorBuffer::glOpBindToRenderbuffer() {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    return mColorBufferGl->bindToRenderbuffer();
}

GLuint ColorBuffer::glOpGetTexture() {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    return mColorBufferGl->getTexture();
}

void ColorBuffer::glOpReadback(unsigned char* img, bool readbackBgra) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    return mColorBufferGl->readback(img, readbackBgra);
}

void ColorBuffer::glOpReadbackAsync(GLuint buffer, bool readbackBgra) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    touch();

    mColorBufferGl->readbackAsync(buffer, readbackBgra);
}

bool ColorBuffer::glOpImportEglNativePixmap(void* pixmap, bool preserveContent) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    return mColorBufferGl->importEglNativePixmap(pixmap, preserveContent);
}

void ColorBuffer::glOpSwapYuvTexturesAndUpdate(GLenum format, GLenum type,
                                               FrameworkFormat frameworkFormat, GLuint* textures) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    mColorBufferGl->swapYUVTextures(frameworkFormat, textures);

    // This makes ColorBufferGl regenerate the RGBA texture using
    // YUVConverter::drawConvert() with the updated YUV textures.
    mColorBufferGl->subUpdate(0, 0, mWidth, mHeight, format, type, nullptr);

    flushFromGl();
}

bool ColorBuffer::glOpReadContents(size_t* outNumBytes, void* outContents) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    return mColorBufferGl->readContents(outNumBytes, outContents);
}

bool ColorBuffer::glOpIsFastBlitSupported() const {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    return mColorBufferGl->isFastBlitSupported();
}

void ColorBuffer::glOpPostLayer(const ComposeLayer& l, int frameWidth, int frameHeight) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    mColorBufferGl->postLayer(l, frameWidth, frameHeight);
}

void ColorBuffer::glOpPostViewportScaledWithOverlay(float rotation, float dx, float dy) {
    if (!mColorBufferGl) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER)) << "ColorBufferGl not available.";
    }

    mColorBufferGl->postViewportScaledWithOverlay(rotation, dx, dy);
}
#endif

}  // namespace gfxstream
