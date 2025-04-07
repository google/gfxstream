// Copyright 2017 The Android Open Source Project
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

#ifndef _WIN32
#include <unistd.h>
#else
#include <stdint.h>
#endif

namespace android {
namespace base {

// This is a cross-platform version of pread(2)/pwrite(2) syscalls, with the
// following from their POSIX versions:
//
// - On Windows it utilizes ReadFile()/WriteFile() + OVERLAPPED structure to
//   pass the position in. As per MSDN, this way APIs still update the current
//   file position (as opposed to the POSIX ones)
//
// - Windows version doesn't return most of the |errno| error codes, using
//   EINVAL for most of the issues and EAGAIN if it's a nonblocking FD.
//
// That's why the best way of using android::base::pread() and pwrite() is to
// never use it together with regular read()/write() calls, especially in
// multi-threaded code. Nobody can predict the active read position when using
// them on different platforms.
//

#ifndef _WIN32
using ::pread;
using ::pwrite;
#else
int64_t pread(int fd, void* buf, size_t count, int64_t offset);
int64_t pwrite(int fd, const void* buf, size_t count, int64_t offset);
#endif

}  // namespace base
}  // namespace android
