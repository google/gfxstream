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

#include "gfxstream/BumpPool.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

namespace gfxstream {
namespace base {

TEST(BumpPool, Basic) {
    BumpPool pool(64);

    void* p1 = pool.alloc(1);
    ASSERT_NE(p1, nullptr);

    void* p2 = pool.alloc(8);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(static_cast<uint8_t*>(p2), static_cast<uint8_t*>(p1) + sizeof(uint64_t));

    pool.freeAll();

    void* p3 = pool.alloc(8);
    EXPECT_EQ(p3, p1);
}

TEST(BumpPool, FallbackAndResize) {
    BumpPool pool(64);

    void* p1 = pool.alloc(128);
    ASSERT_NE(p1, nullptr);
    std::memset(p1, 0xAB, 128);

    pool.freeAll();

    void* p2 = pool.alloc(128);
    ASSERT_NE(p2, nullptr);
    void* p3 = pool.alloc(8);
    ASSERT_NE(p3, nullptr);
    EXPECT_EQ(static_cast<uint8_t*>(p3), static_cast<uint8_t*>(p2) + 128);
}

TEST(BumpPool, OverflowReturnsNullptr) {
    BumpPool pool(4096);

    for (size_t offset = 0; offset < sizeof(uint64_t) - 1; ++offset) {
        EXPECT_EQ(pool.alloc(SIZE_MAX - offset), nullptr);
    }

    // Verify pool state remains intact after rejected overflow allocations.
    void* p1 = pool.alloc(8);
    ASSERT_NE(p1, nullptr);

    for (size_t offset = 0; offset < sizeof(uint64_t) - 1; ++offset) {
        EXPECT_EQ(pool.alloc(SIZE_MAX - offset), nullptr);
    }

    void* p2 = pool.alloc(8);
    ASSERT_NE(p2, nullptr);
    EXPECT_EQ(static_cast<uint8_t*>(p2), static_cast<uint8_t*>(p1) + sizeof(uint64_t));
}

}  // namespace base
}  // namespace gfxstream
