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

#include "aemu/base/files/StdioStream.h"

#include <assert.h>
#include <errno.h>

namespace android {
namespace base {

StdioStream::StdioStream(FILE* file, Ownership ownership) :
    mFile(file), mOwnership(ownership) {}

StdioStream::StdioStream(StdioStream&& other)
    : mFile(other.mFile), mOwnership(other.mOwnership) {
    other.mFile = nullptr;
}

StdioStream& StdioStream::operator=(StdioStream&& other) {
    assert(this != &other);
    close();
    mFile = other.mFile;
    mOwnership = other.mOwnership;
    other.mFile = nullptr;
    return *this;
}

StdioStream::~StdioStream() {
    close();
}

ssize_t StdioStream::read(void* buffer, size_t size) {
    size_t res = ::fread(buffer, 1, size, mFile);
    if (res < size) {
        if (!::feof(mFile)) {
            errno = ::ferror(mFile);
        }
    }
    return static_cast<ssize_t>(res);
}

ssize_t StdioStream::write(const void* buffer, size_t size) {
    size_t res = ::fwrite(buffer, 1, size, mFile);
    if (res < size) {
        if (!::feof(mFile)) {
            errno = ::ferror(mFile);
        }
    }
    return static_cast<ssize_t>(res);
}

void StdioStream::close() {
    if (mOwnership == kOwner && mFile) {
        ::fclose(mFile);
        mFile = nullptr;
    }
}

}  // namespace base
}  // namespace android
