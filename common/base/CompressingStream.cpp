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

#include "aemu/base/files/CompressingStream.h"

#include "aemu/base/files/StreamSerializing.h"

#include "lz4.h"

#include <errno.h>

namespace android {
namespace base {

CompressingStream::CompressingStream(Stream& output)
    : mOutput(output) {
    mLzStream = reinterpret_cast<void *>(LZ4_createStream());
}

CompressingStream::~CompressingStream() {
    saveBuffer(&mOutput, mBuffer);
    LZ4_freeStream((LZ4_stream_t*)mLzStream);
}

ssize_t CompressingStream::read(void*, size_t) {
    return -EPERM;
}

ssize_t CompressingStream::write(const void* buffer, size_t size) {
    if (!size) {
        return 0;
    }

    size_t outSize = 0;
    outSize = LZ4_compressBound(size);
    auto oldSize = mBuffer.size();
    mBuffer.resize_noinit(mBuffer.size() + outSize);
    const auto outBuffer = mBuffer.data() + oldSize;
    int written = 0;
    written = LZ4_compress_fast_continue((LZ4_stream_t*)mLzStream,
                                         (const char*)buffer,
                                         outBuffer, size, outSize, 1);

    if (!written) {
        return -EIO;
    }
    mBuffer.resize(oldSize + written);
    return size;
}

}  // namespace base
}  // namespace android
