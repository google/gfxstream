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

#pragma once

#include "aemu/base/CppMacros.h"
#include "aemu/base/files/Stream.h"

#include <vector>

namespace android {
namespace base {

// An implementation of the Stream interface on top of a vector.
class MemStream : public Stream {
public:
    using Buffer = std::vector<char>;

    MemStream(int reserveSize = 512);
    MemStream(Buffer&& data);

    MemStream(MemStream&& other) = default;
    MemStream& operator=(MemStream&& other) = default;

    int writtenSize() const;
    int readPos() const;
    int readSize() const;

    // Stream interface implementation.
    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

    // protobuf support
    void setProtobuf(void* pb) { mPb = pb; }
    void* getProtobuf() override { return mPb; }

    // Snapshot support.
    void save(Stream* stream) const;
    void load(Stream* stream);

    const Buffer& buffer() const { return mData; }

    void rewind();

private:
    DISALLOW_COPY_AND_ASSIGN(MemStream);

    Buffer mData;
    int mReadPos = 0;
    void* mPb = nullptr;
};

}  // namespace base
}  // namespace android
