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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdlib>

#include "render-utils/IOStream.h"

namespace gfxstream {
namespace vk {

class TrivialStream : public IOStream {
   public:
    TrivialStream() : IOStream(4) {}
    virtual ~TrivialStream() = default;

    void* allocBuffer(size_t minSize) {
        size_t allocSize = (m_bufsize < minSize ? minSize : m_bufsize);
        if (!m_buf) {
            m_buf = (unsigned char*)malloc(allocSize);
        } else if (m_bufsize < allocSize) {
            unsigned char* p = (unsigned char*)realloc(m_buf, allocSize);
            if (p != NULL) {
                m_buf = p;
                m_bufsize = allocSize;
            } else {
                GFXSTREAM_ERROR("realloc (%zu) failed", allocSize);
                free(m_buf);
                m_buf = NULL;
                m_bufsize = 0;
            }
        }

        return m_buf;
    }

    int commitBuffer(size_t size) {
        if (size == 0) return 0;
        return writeFully(m_buf, size);
    }

    int writeFully(const void* buf, size_t len) { return 0; }

    const unsigned char* readFully(void* buf, size_t len) { return NULL; }

    virtual void* getDmaForReading(uint64_t guest_paddr) { return nullptr; }
    virtual void unlockDma(uint64_t guest_paddr) {}

   protected:
    virtual const unsigned char* readRaw(void* buf, size_t* inout_len) { return nullptr; }
    virtual void onSave(android::base::Stream* stream) {}
    virtual unsigned char* onLoad(android::base::Stream* stream) { return nullptr; }
};

}  // namespace vk
}  // namespace gfxstream
