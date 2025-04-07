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

#pragma once

#include "aemu/base/Compiler.h"

#include <errno.h>
#ifdef _MSC_VER
#include "aemu/base/msvc.h"
#else
#include <unistd.h>
#endif

namespace android {
namespace base {

// Helper class to hold an integer file descriptor, and have the 'close'
// function called automatically on scope exit, unless the 'release'
// method was called previously.
class ScopedFd {
public:
    // Default constructor will hold an invalid descriptor.
    ScopedFd() : fd_(-1) {}

    // Constructor takes ownership of |fd|.
    explicit ScopedFd(int fd) : fd_(fd) {}

    // Make it movable.
    ScopedFd(ScopedFd&& other) : fd_(other.fd_) {
        other.fd_ = -1;
    }

    ScopedFd& operator=(ScopedFd&& other) {
        swap(&other);
        return *this;
    }

    // Destructor calls close().
    ~ScopedFd() { close(); }

    // Return the file descriptor value, does _not_ transfer ownership.
    int get() const { return fd_; }

    // Return the file descriptor value, transfers ownership to the caller.
    int release() {
        int fd = fd_;
        fd_ = -1;
        return fd;
    }

    // Return true iff the file descriptor is valid.
    bool valid() const { return fd_ >= 0; }

    // Close the file descriptor (and make the wrapped value invalid).
    void close() {
        if (fd_ != -1) {
            int save_errno = errno;
            ::close(fd_);
            fd_ = -1;
            errno = save_errno;
        }
    }

    // Swap two ScopedFd instances.
    void swap(ScopedFd* other) {
        int fd = fd_;
        fd_ = other->fd_;
        other->fd_ = fd;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ScopedFd);

    int fd_;
};

}  // namespace base
}  // namespace android
