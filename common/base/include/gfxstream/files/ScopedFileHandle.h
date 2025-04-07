// Copyright 2014 The Android Open Source Project
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
// nothing here
#else  // _WIN32

#include "aemu/base/Compiler.h"

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

namespace android {
namespace base {

// Helper class used to wrap a Win32  file HANDLE that will be closed when
// the instance is destroyed, unless the release() method was called
// before that.
class ScopedFileHandle {
public:
    // Constructor takes ownership of |handle|.
    explicit ScopedFileHandle(HANDLE handle = INVALID_HANDLE_VALUE)
        : handle_(handle) {}

    // Destructor calls close() method.
    ~ScopedFileHandle() { close(); }

    // Returns true iff the wrapped HANDLE value is valid.
    bool valid() const { return handle_ != INVALID_HANDLE_VALUE; }

    // Return current HANDLE value. Does _not_ transfer ownership.
    HANDLE get() const { return handle_; }

    // Return current HANDLE value, transferring ownership to the caller.
    HANDLE release() {
        HANDLE h = handle_;
        handle_ = INVALID_HANDLE_VALUE;
        return h;
    }

    // Close handle if it is not invalid.
    void close() {
        if (handle_ != INVALID_HANDLE_VALUE) {
            ::CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
        }
    }

    // Swap the content of two ScopedFileHandle instances.
    void swap(ScopedFileHandle* other) {
        HANDLE handle = handle_;
        handle_ = other->handle_;
        other->handle_ = handle;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ScopedFileHandle);

    HANDLE handle_;
};

}  // namespace base
}  // namespace android

#endif  // _WIN32
