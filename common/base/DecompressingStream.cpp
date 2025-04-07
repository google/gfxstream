// Copyright 2020 The Android Open Source Project
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

#include "aemu/base/files/DecompressingStream.h"

#include "aemu/base/files/StreamSerializing.h"

#include "lz4.h"

#include <errno.h>
#include <cassert>

namespace android {
namespace base {

DecompressingStream::DecompressingStream(Stream& input) {
    mLzStream = reinterpret_cast<void *>(LZ4_createStreamDecode());
    loadBuffer(&input, &mBuffer);
}

DecompressingStream::~DecompressingStream() {
    LZ4_freeStreamDecode((LZ4_streamDecode_t*)mLzStream);
}

ssize_t DecompressingStream::read(void* buffer, size_t size) {
    assert(mBufferPos < mBuffer.size() ||
           (mBufferPos == mBuffer.size() && size == 0));
    if (!size) {
        return 0;
    }
    int read = 0;
    read = LZ4_decompress_fast_continue(
           (LZ4_streamDecode_t*)mLzStream, mBuffer.data() + mBufferPos,
           (char*)buffer, size);
    if (!read) {
        return -EIO;
    }
    mBufferPos += read;
    assert(mBufferPos <= mBuffer.size());
    return size;
}

ssize_t DecompressingStream::write(const void*, size_t) {
    return -EPERM;
}

}  // namespace base
}  // namespace android
