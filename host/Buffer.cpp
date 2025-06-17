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

#include "Buffer.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "BufferGl.h"
#include "EmulationGl.h"
#endif

#include "vulkan/BufferVk.h"
#include "vulkan/VkCommonOperations.h"

namespace gfxstream {

class Buffer::Impl : public LazySnapshotObj<Buffer::Impl> {
   public:
    static std::unique_ptr<Impl> create(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        uint64_t size, HandleType handle);

    static std::unique_ptr<Impl> onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                        gfxstream::Stream* stream);

    void onSave(gfxstream::Stream* stream);
    void restore();

    HandleType getHndl() const { return mHandle; }
    uint64_t getSize() const { return mSize; }

    void readToBytes(uint64_t offset, uint64_t size, void* outBytes);
    bool updateFromBytes(uint64_t offset, uint64_t size, const void* bytes);
    std::optional<BlobDescriptorInfo> exportBlob();

   private:
    Impl(HandleType handle, uint64_t size);

    const HandleType mHandle;
    const uint64_t mSize;

#if GFXSTREAM_ENABLE_HOST_GLES
    // If GL emulation is enabled.
    std::unique_ptr<gl::BufferGl> mBufferGl;
#endif

    // If Vk emulation is enabled.
    std::unique_ptr<vk::BufferVk> mBufferVk;
};

Buffer::Impl::Impl(HandleType handle, uint64_t size) : mHandle(handle), mSize(size) {}

/*static*/
std::unique_ptr<Buffer::Impl> Buffer::Impl::create(gl::EmulationGl* emulationGl,
                                                   vk::VkEmulation* emulationVk, uint64_t size,
                                                   HandleType handle) {
    std::unique_ptr<Buffer::Impl> buffer(new Buffer::Impl(handle, size));

#if GFXSTREAM_ENABLE_HOST_GLES
    if (emulationGl) {
        buffer->mBufferGl = emulationGl->createBuffer(size, handle);
        if (!buffer->mBufferGl) {
            GFXSTREAM_ERROR("Failed to initialize BufferGl.");
            return nullptr;
        }
    }
#endif

    if (emulationVk) {
        const bool vulkanOnly = emulationGl == nullptr;

        buffer->mBufferVk = vk::BufferVk::create(*emulationVk, handle, size, vulkanOnly);
        if (!buffer->mBufferVk) {
            GFXSTREAM_ERROR("Failed to initialize BufferVk.");
            return nullptr;
        }

        if (!vulkanOnly) {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (!buffer->mBufferGl) {
                GFXSTREAM_FATAL("Missing BufferGl?");
            }
#endif
            // TODO: external memory sharing.
        }
    }

    return buffer;
}

/*static*/
std::unique_ptr<Buffer::Impl> Buffer::Impl::onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation*,
                                                   gfxstream::Stream* stream) {
    const auto handle = static_cast<HandleType>(stream->getBe32());
    const auto size = static_cast<uint64_t>(stream->getBe64());

    std::unique_ptr<Buffer::Impl> buffer(new Buffer::Impl(handle, size));

#if GFXSTREAM_ENABLE_HOST_GLES
    if (emulationGl) {
        buffer->mBufferGl = emulationGl->loadBuffer(stream);
        if (!buffer->mBufferGl) {
            GFXSTREAM_ERROR("Failed to load BufferGl.");
            return nullptr;
        }
    }
#endif

    buffer->mNeedRestore = true;

    return buffer;
}

void Buffer::Impl::onSave(gfxstream::Stream* stream) {
    stream->putBe32(mHandle);
    stream->putBe64(mSize);

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mBufferGl) {
        mBufferGl->onSave(stream);
    }
#endif
}

void Buffer::Impl::restore() {}

void Buffer::Impl::readToBytes(uint64_t offset, uint64_t size, void* outBytes) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mBufferGl) {
        mBufferGl->read(offset, size, outBytes);
        return;
    }
#endif

    if (mBufferVk) {
        mBufferVk->readToBytes(offset, size, outBytes);
        return;
    }

    GFXSTREAM_FATAL("No Buffer impl?");
}

bool Buffer::Impl::updateFromBytes(uint64_t offset, uint64_t size, const void* bytes) {
    touch();

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mBufferGl) {
        mBufferGl->subUpdate(offset, size, bytes);
        return true;
    }
#endif

    if (mBufferVk) {
        return mBufferVk->updateFromBytes(offset, size, bytes);
    }

    GFXSTREAM_FATAL("No Buffer impl?");
    return false;
}

std::optional<BlobDescriptorInfo> Buffer::Impl::exportBlob() {
    if (!mBufferVk) {
        return std::nullopt;
    }

    return mBufferVk->exportBlob();
}

/*static*/
std::shared_ptr<Buffer> Buffer::create(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                       uint64_t size, HandleType handle) {
    std::shared_ptr<Buffer> buffer(new Buffer());
    buffer->mImpl = Buffer::Impl::create(emulationGl, emulationVk, size, handle);
    if (!buffer->mImpl) {
        return nullptr;
    }
    return buffer;
}

/*static*/
std::shared_ptr<Buffer> Buffer::onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                       gfxstream::Stream* stream) {
    std::shared_ptr<Buffer> buffer(new Buffer());
    buffer->mImpl = Buffer::Impl::onLoad(emulationGl, emulationVk, stream);
    if (!buffer->mImpl) {
        return nullptr;
    }
    buffer->mNeedRestore = true;
    return buffer;
}

void Buffer::onSave(gfxstream::Stream* stream) { mImpl->onSave(stream); }

void Buffer::restore() { mImpl->touch(); }

HandleType Buffer::getHndl() const { return mImpl->getHndl(); }

uint64_t Buffer::getSize() const { return mImpl->getSize(); }

void Buffer::readToBytes(uint64_t offset, uint64_t size, void* outBytes) {
    return mImpl->readToBytes(offset, size, outBytes);
}

bool Buffer::updateFromBytes(uint64_t offset, uint64_t size, const void* bytes) {
    return mImpl->updateFromBytes(offset, size, bytes);
}

std::optional<BlobDescriptorInfo> Buffer::exportBlob() { return mImpl->exportBlob(); }

}  // namespace gfxstream
