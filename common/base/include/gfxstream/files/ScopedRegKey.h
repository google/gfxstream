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

#if !defined(_WIN32) && !defined(_WIN64)
#error "Only compile this file when targetting Windows!"
#endif

#include "aemu/base/Compiler.h"
#include <algorithm>

#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>

namespace android {
namespace base {

// Helper class used to wrap a Win32 HKEY that will be closed when
// the instance is destroyed, unless the release() method was called
// before that.
class ScopedRegKey {
public:
    // Default destructor is used to wrap an invalid HKEY value.
    ScopedRegKey() : hkey_(0), valid_(false) {}

    // Constructor takes ownership of |HKEY|.
    explicit ScopedRegKey(HKEY hkey) : hkey_(hkey), valid_(true) {}

    // Constructor takes ownership of |HKEY|.
    explicit ScopedRegKey(HKEY hkey, bool valid_) : hkey_(hkey), valid_(valid_) {}

    // Destructor calls close() method.
    ~ScopedRegKey() { close(); }

    // Returns true iff the wrapped HKEY value is valid.
    bool valid() const { return valid_; }

    // Return current HKEY value.  undefined if !valid_
    HKEY get() const { return hkey_; }

    // Return current HKEY value, transferring ownership to the caller.
    HKEY release() {
        HKEY h = hkey_;
        hkey_ = 0;
        valid_ = false;
        return h;
    }

    // Close HKEY if it is not invalid.
    void close() {
        if (valid_) {
            ::RegCloseKey(hkey_);
            hkey_ = 0;
            valid_ = false;
        }
    }

    // Swap the content of two ScopedRegKey instances.
    void swap(ScopedRegKey* other) {
        std::swap(hkey_, other->hkey_);
        std::swap(valid_, other->valid_);
    }

private:
    DISALLOW_COPY_AND_ASSIGN(ScopedRegKey);

    HKEY hkey_;
    bool valid_;
};

}  // namespace base
}  // namespace android
