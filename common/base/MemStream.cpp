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

#include "aemu/base/files/MemStream.h"

#include "aemu/base/files/StreamSerializing.h"

#include <algorithm>
#include <utility>

#include <string.h>

namespace android {
namespace base {

MemStream::MemStream(int reserveSize) {
    mData.reserve(reserveSize);
}

MemStream::MemStream(Buffer&& data) : mData(std::move(data)) {}

ssize_t MemStream::read(void* buffer, size_t size) {
    if (!buffer) {
        return 0;
    }
    const auto sizeToRead = std::min<int>(size, readSize());
    memcpy(buffer, mData.data() + mReadPos, sizeToRead);
    mReadPos += sizeToRead;
    return sizeToRead;
}

ssize_t MemStream::write(const void* buffer, size_t size) {
    if (!buffer) {
        return 0;
    }
    mData.insert(mData.end(), (const char*)buffer, (const char*)buffer + size);
    return size;
}

int MemStream::writtenSize() const {
    return (int)mData.size();
}

int MemStream::readPos() const {
    return mReadPos;
}

int MemStream::readSize() const {
    return mData.size() - mReadPos;
}

void MemStream::save(Stream* stream) const {
    saveBuffer(stream, mData);
}

void MemStream::load(Stream* stream) {
    loadBuffer(stream, &mData);
    mReadPos = 0;
}

void MemStream::rewind() {
    mReadPos = 0;
}

}  // namespace base
}  // namespace android
