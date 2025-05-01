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

#include <vector>

#include "render-utils/stream.h"

namespace gfxstream {

// An implementation of the Stream interface on top of a vector.
class MemStream : public Stream {
  public:
    using Buffer = std::vector<char>;

    MemStream(int reserveSize = 512);
    MemStream(Buffer&& data);

    MemStream(const MemStream&) = delete;
    MemStream& operator=(const MemStream&) = delete;

    MemStream(MemStream&& other) = default;
    MemStream& operator=(MemStream&& other) = default;

    int writtenSize() const;
    int readPos() const;
    int readSize() const;

    // Stream interface implementation.
    ssize_t read(void* buffer, size_t size) override;
    ssize_t write(const void* buffer, size_t size) override;

    // Snapshot support.
    void save(Stream* stream) const;
    void load(Stream* stream);

    const Buffer& buffer() const { return mData; }

    void rewind();

  private:
    Buffer mData;
    int mReadPos = 0;
    void* mPb = nullptr;
};

void saveStream(Stream* stream, const MemStream& memStream);
void loadStream(Stream* stream, MemStream* memStream);

}  // namespace gfxstream
