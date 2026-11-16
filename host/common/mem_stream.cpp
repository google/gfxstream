// Copyright 2019 The Android Open Source Project
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

#include "gfxstream/host/mem_stream.h"

#include <algorithm>
#include <string.h>
#include <utility>

#include "gfxstream/host/stream_utils.h"

namespace gfxstream {
namespace host {

MemStream::MemStream(size_t reserveSize) {
    mData.reserve(reserveSize);
}

MemStream::MemStream(Buffer&& data) : mData(std::move(data)) {}

ssize_t MemStream::read(void* buffer, size_t size) {
    if (!size) {
        return 0;
    }
    if (!buffer) {
        addError("%s: no buffer", __func__);
        return -1;
    }
    const size_t readableSize = readSize();
    if (size > readableSize) {
        addError("Memory stream requested read size %zu is bigger than readable size %zu", size,
                 readableSize);
        if (!readableSize) {
            return 0;
        }
        size = readableSize;
    }
    memcpy(buffer, mData.data() + mReadPos, size);
    mReadPos += size;
    return size;
}

ssize_t MemStream::write(const void* buffer, size_t size) {
    if (!size) {
        return 0;
    }
    if (!buffer) {
        addError("%s: no buffer", __func__);
        return -1;
    }
    mData.insert(mData.end(), (const char*)buffer, (const char*)buffer + size);
    return size;
}

size_t MemStream::writtenSize() const {
    return mData.size();
}

size_t MemStream::readPos() const {
    return mReadPos;
}

size_t MemStream::readSize() const {
    if (mData.size() < mReadPos) {
        addError("Invalid state: size %zu < readPos %zu", mData.size(), mReadPos);
        return 0;
    }
    return mData.size() - mReadPos;
}

void MemStream::save(Stream* stream) const {
    if (!saveBuffer(stream, mData)) {
        stream->addError("%s: failed to save MemStream (%zu bytes)", __func__, mData.size());
    }
}

void MemStream::load(Stream* stream) {
    if (!loadBuffer(stream, &mData)) {
        stream->addError("%s: failed to load MemStream", __func__);
    }
    mReadPos = 0;
}

void MemStream::rewind() {
    mReadPos = 0;
}

void saveStream(Stream* stream, const MemStream& memStream) {
    memStream.save(stream);
}

void loadStream(Stream* stream, MemStream* memStream) {
    memStream->load(stream);
}

}  // namespace host
}  // namespace gfxstream
