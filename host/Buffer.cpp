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
#include "gl/EmulationGl.h"
#endif

#include "vulkan/BufferVk.h"
#include "vulkan/VkCommonOperations.h"

namespace gfxstream {

Buffer::Buffer(HandleType handle, uint64_t size) : mHandle(handle), mSize(size) {}

/*static*/
std::shared_ptr<Buffer> Buffer::create(gl::EmulationGl* emulationGl, vk::VkEmulation* emulationVk,
                                       uint64_t size, HandleType handle) {
    std::shared_ptr<Buffer> buffer(new Buffer(handle, size));

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
std::shared_ptr<Buffer> Buffer::onLoad(gl::EmulationGl* emulationGl, vk::VkEmulation*,
                                       android::base::Stream* stream) {
    const auto handle = static_cast<HandleType>(stream->getBe32());
    const auto size = static_cast<uint64_t>(stream->getBe64());

    std::shared_ptr<Buffer> buffer(new Buffer(handle, size));

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

void Buffer::onSave(android::base::Stream* stream) {
    stream->putBe32(mHandle);
    stream->putBe64(mSize);

#if GFXSTREAM_ENABLE_HOST_GLES
    if (mBufferGl) {
        mBufferGl->onSave(stream);
    }
#endif
}

void Buffer::restore() {}

void Buffer::readToBytes(uint64_t offset, uint64_t size, void* outBytes) {
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

bool Buffer::updateFromBytes(uint64_t offset, uint64_t size, const void* bytes) {
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

std::optional<BlobDescriptorInfo> Buffer::exportBlob() {
    if (!mBufferVk) {
        return std::nullopt;
    }

    return mBufferVk->exportBlob();
}

}  // namespace gfxstream
