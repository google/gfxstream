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

#include "android/base/files/ScopedFd.h"

#ifndef _MSC_VER
#include <unistd.h>
#endif
#include <fcntl.h>

#include <gtest/gtest.h>

namespace android {
namespace base {

namespace {

// The path of a file that can always be opened for reading on any platform.
#ifdef _WIN32
static const char kNullFile[] = "NUL";
#else
static const char kNullFile[] = "/dev/null";
#endif

int OpenNull() {
    return ::open(kNullFile, O_RDONLY);
}

}  // namespace

TEST(ScopedFd, DefaultConstructor) {
    ScopedFd f;
    EXPECT_FALSE(f.valid());
    EXPECT_EQ(-1, f.get());
}

TEST(ScopedFd, Constructor) {
    ScopedFd f(OpenNull());
    EXPECT_TRUE(f.valid());
}

TEST(ScopedFd, Release) {
    ScopedFd f(OpenNull());
    EXPECT_TRUE(f.valid());
    int fd = f.release();
    EXPECT_FALSE(f.valid());
    EXPECT_NE(-1, fd);
    ::close(fd);
}

TEST(ScopedFd, Close) {
    ScopedFd f(OpenNull());
    EXPECT_TRUE(f.valid());
    f.close();
    EXPECT_FALSE(f.valid());
}

TEST(ScopedFd, Swap) {
    ScopedFd f1;
    ScopedFd f2(OpenNull());
    EXPECT_FALSE(f1.valid());
    EXPECT_TRUE(f2.valid());
    int fd = f2.get();
    f1.swap(&f2);
    EXPECT_FALSE(f2.valid());
    EXPECT_TRUE(f1.valid());
    EXPECT_EQ(fd, f1.get());
}


}  // namespace base
}  // namespace android
