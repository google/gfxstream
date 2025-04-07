// Copyright 2015 The Android Open Source Project
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

#include "aemu/base/Compiler.h"
#include "aemu/base/files/Stream.h"

namespace android {
namespace base {

// An implementation of the Stream interface for a given char*.
class InplaceStream : public Stream {
public:
    using Buffer = char*;

    InplaceStream(Buffer buf, uint32_t buflen) :
        mData(buf), mDataLen(buflen) { }

    int writtenSize() const;
    int readPos() const;
    int readSize() const;

    const char* currentRead() const;
    char* currentWrite() const;
    ssize_t advanceRead(size_t size);
    ssize_t advanceWrite(size_t size);

    // Stream interface implementation.
    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

    // A way to put/get strings/buffers in-place as well.
    // Returns nullptr if the size of the resulting string
    // is <= 0 (but with a null terminator, that just means
    // something went wrong, so should never occur).
    // Please make sure input |str| is actually
    // null-terminated.
    void putStringNullTerminated(const char* str);
    const char* getStringNullTerminated();

    // Snapshot support.
    void save(Stream* stream) const;
    void load(Stream* stream);

private:
    DISALLOW_COPY_AND_ASSIGN(InplaceStream);

    Buffer mData = nullptr;
    int mDataLen = 0;
    int mReadPos = 0;
    int mWritePos = 0;
};

}  // namespace base
}  // namespace android
