// Copyright 2026 The Android Open Source Project
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

#include "gfxstream/memory/heap_memory.h"

#ifdef _WIN32
#include <malloc.h>
#endif

#include <stdlib.h>

#include <algorithm>
#include <limits>
#include <utility>

#include "gfxstream/Macros.h"

namespace gfxstream {
namespace base {

HeapMemory::HeapMemory(size_t alignment, size_t size) {
    if (alignment == 0 || size == 0) {
        return;
    }
    alignment = std::max<size_t>(alignment, sizeof(void*));
    if ((alignment & (alignment - 1)) != 0) {
        return;
    }
    if (size > std::numeric_limits<size_t>::max() - (alignment - 1)) {
        return;
    }
    size = ALIGN(size, alignment);
#ifdef _WIN32
    mAddr = _aligned_malloc(size, alignment);
#else
    mAddr = aligned_alloc(alignment, size);
#endif
}

HeapMemory::~HeapMemory() { reset(); }

HeapMemory::HeapMemory(HeapMemory&& other) noexcept
    : mAddr(std::exchange(other.mAddr, nullptr)) {}

HeapMemory& HeapMemory::operator=(HeapMemory&& other) noexcept {
    if (this != &other) {
        reset();
        mAddr = std::exchange(other.mAddr, nullptr);
    }
    return *this;
}

void HeapMemory::reset() {
    if (mAddr) {
#ifdef _WIN32
        _aligned_free(mAddr);
#else
        free(mAddr);
#endif
        mAddr = nullptr;
    }
}

void* HeapMemory::getAddr() const { return mAddr; }

}  // namespace base
}  // namespace gfxstream
