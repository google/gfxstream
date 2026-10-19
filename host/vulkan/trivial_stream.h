// Copyright 2025 The Android Open Source Project
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

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "gfxstream/host/iostream.h"

namespace gfxstream {
namespace host {
namespace vk {

class TrivialStream : public IOStream {
   public:
    TrivialStream() : IOStream(4) {}
    virtual ~TrivialStream() = default;

    void* allocBuffer(size_t minSize) override {
        size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
        if (mWriteBuffer.size() < allocSize) {
            mWriteBuffer.resize(allocSize);
        }
        return mWriteBuffer.data();
    }

    int commitBuffer(size_t size) override {
        if (size == 0) return 0;
        return writeFully(mWriteBuffer.data(), size);
    }

    int writeFully(const void* buf, size_t len) override { return 0; }

    const unsigned char* readFully(void* buf, size_t len) override { return NULL; }

    virtual void* getDmaForReading(uint64_t guest_paddr) override { return nullptr; }
    virtual void unlockDma(uint64_t guest_paddr) override {}

   protected:
    virtual const unsigned char* readRaw(void* buf, size_t* inout_len) override { return nullptr; }
    virtual void onSave(gfxstream::Stream* stream) override {}
    virtual unsigned char* onLoad(gfxstream::Stream* stream) override { return nullptr; }

   private:
    std::vector<uint8_t> mWriteBuffer;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
