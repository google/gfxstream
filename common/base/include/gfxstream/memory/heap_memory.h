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
#pragma once

#include <cstddef>

namespace gfxstream {
namespace base {

class HeapMemory {
   public:
    HeapMemory(size_t alignment, size_t size);
    ~HeapMemory();

    HeapMemory(HeapMemory&& other) noexcept;
    HeapMemory& operator=(HeapMemory&& other) noexcept;

    HeapMemory(const HeapMemory&) = delete;
    HeapMemory& operator=(const HeapMemory&) = delete;

    void* getAddr() const;

   private:
    void reset();

    void* mAddr{nullptr};
};

}  // namespace base
}  // namespace gfxstream
