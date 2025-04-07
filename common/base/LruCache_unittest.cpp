// Copyright 2022 The Android Open Source Project
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

#include "aemu/base/LruCache.h"

#include <cstdint>
#include <string>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace android {
namespace base {
namespace {

using ::testing::IsNull;
using ::testing::Pointee;
using ::testing::StrEq;

TEST(LruCache, Basic) {
    LruCache<int, std::string> lru(3);

    ASSERT_THAT(lru.get(0), IsNull());
    ASSERT_THAT(lru.get(1), IsNull());
    ASSERT_THAT(lru.get(2), IsNull());
    ASSERT_THAT(lru.get(3), IsNull());

    lru.set(1, "hello");
    lru.set(2, "world");

    ASSERT_THAT(lru.get(0), IsNull());
    ASSERT_THAT(lru.get(1), Pointee(StrEq("hello")));
    ASSERT_THAT(lru.get(2), Pointee(StrEq("world")));
    ASSERT_THAT(lru.get(3), IsNull());

    lru.set(2, "world");
    lru.set(2, "world");
    lru.set(2, "world");
    lru.set(2, "world");

    ASSERT_THAT(lru.get(0), IsNull());
    ASSERT_THAT(lru.get(1), Pointee(StrEq("hello")));
    ASSERT_THAT(lru.get(2), Pointee(StrEq("world")));
    ASSERT_THAT(lru.get(3), IsNull());

    lru.set(2, "helloworld");

    ASSERT_THAT(lru.get(0), IsNull());
    ASSERT_THAT(lru.get(1), Pointee(StrEq("hello")));
    ASSERT_THAT(lru.get(2), Pointee(StrEq("helloworld")));
    ASSERT_THAT(lru.get(3), IsNull());

    lru.set(3, "foo");

    ASSERT_THAT(lru.get(0), IsNull());
    ASSERT_THAT(lru.get(1), Pointee(StrEq("hello")));
    ASSERT_THAT(lru.get(2), Pointee(StrEq("helloworld")));
    ASSERT_THAT(lru.get(3), Pointee(StrEq("foo")));

    lru.set(0, "bar");

    ASSERT_THAT(lru.get(0), Pointee(StrEq("bar")));
    ASSERT_THAT(lru.get(1), IsNull());
    ASSERT_THAT(lru.get(2), Pointee(StrEq("helloworld")));
    ASSERT_THAT(lru.get(3), Pointee(StrEq("foo")));

    lru.remove(2);

    ASSERT_THAT(lru.get(0), Pointee(StrEq("bar")));
    ASSERT_THAT(lru.get(1), IsNull());
    ASSERT_THAT(lru.get(2), IsNull());
    ASSERT_THAT(lru.get(3), Pointee(StrEq("foo")));

    lru.remove(1);
    lru.remove(5);

    ASSERT_THAT(lru.get(0), Pointee(StrEq("bar")));
    ASSERT_THAT(lru.get(1), IsNull());
    ASSERT_THAT(lru.get(2), IsNull());
    ASSERT_THAT(lru.get(3), Pointee(StrEq("foo")));
}

}  // namespace
}  // namespace base
}  // namespace android