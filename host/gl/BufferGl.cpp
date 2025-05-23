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

#include "BufferGl.h"

namespace gfxstream {
namespace gl {

BufferGl::BufferGl(uint64_t size, HandleType handle, ContextHelper* helper)
    : mSize(size), mHandle(handle), mContextHelper(helper) {}

// static
std::unique_ptr<BufferGl> BufferGl::create(uint64_t size, HandleType handle, ContextHelper* helper) {
    RecursiveScopedContextBind bind(helper);
    if (!bind.isOk()) {
        return NULL;
    }

    std::unique_ptr<BufferGl> buffer(new BufferGl(size, handle, helper));

    /*
    // TODO: GL_EXT_external_buffer
    s_gles2.glGenBuffers(1, &buffer->m_buffer);
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, buffer->m_buffer);
    s_gles2.glBufferData(GL_ARRAY_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
    */

    return buffer;
}

void BufferGl::read(uint64_t offset, uint64_t size, void* bytes) {
    RecursiveScopedContextBind bind(mContextHelper);
    if (!bind.isOk()) {
        return;
    }

    // Note: Gfxstream does not yet support GL_EXT_external_buffer so BufferGl reads are
    // currently a no-op from the host point-of-view when the guest is not using ANGLE.
    // Instead, the guest shadow buffer contains the source of truth of the buffer
    // contents.
    //
    // For completeness, this is not fully correct as a guest that is not using ANGLE
    // could still have native users of Vulkan. In such cases, the guest shadow buffer
    // contents are not yet sync'ed with the Vulkan contents. However, this has not yet
    // been observed to be an issue.

    /*
    // TODO: GL_EXT_external_buffer
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    void* mapped = s_gles2.glMapBufferRange(GL_ARRAY_BUFFER, offset, size, GL_MAP_READ_BIT);
    std::memcpy(bytes, mapped, size);
    s_gles2.glUnmapBuffer(GL_ARRAY_BUFFER);
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, 0);
    */
}

void BufferGl::subUpdate(uint64_t offset, uint64_t size, const void* bytes) {
    RecursiveScopedContextBind bind(mContextHelper);
    if (!bind.isOk()) {
        return;
    }

    // Note: Gfxstream does not yet support GL_EXT_external_buffer so BufferGl writes are
    // currently a no-op from the host point-of-view when the guest is not using ANGLE.
    // Instead, the guest shadow buffer contains the source of truth of the buffer
    // contents.
    //
    // For completeness, this is not fully correct as a guest that is not using ANGLE
    // could still have native users of Vulkan. In such cases, the guest shadow buffer
    // contents are not yet sync'ed with the Vulkan contents. However, this has not yet
    // been observed to be an issue.

    /*
    // TODO: GL_EXT_external_buffer
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, m_buffer);
    s_gles2.glBufferSubData(GL_ARRAY_BUFFER, offset, size, bytes);
    s_gles2.glBindBuffer(GL_ARRAY_BUFFER, 0);
    */
}

/*static*/
std::unique_ptr<BufferGl> BufferGl::onLoad(gfxstream::Stream* stream, ContextHelper* helper) {
    const auto size = static_cast<uint64_t>(stream->getBe64());
    const auto handle = static_cast<HandleType>(stream->getBe32());
    return std::unique_ptr<BufferGl>(new BufferGl(size, handle, helper));
}

void BufferGl::onSave(gfxstream::Stream* stream) {
    stream->putBe64(mSize);
    stream->putBe32(mHandle);
}

}  // namespace gl
}  // namespace gfxstream
