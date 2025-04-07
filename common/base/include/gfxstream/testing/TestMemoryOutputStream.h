// Copyright 2016 The Android Open Source Project
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

#include "aemu/base/files/Stream.h"

#include <errno.h>
#include <string_view>
#include <vector>

namespace android {
namespace base {

// A convenience android::base::Stream implementation that collects
// all written data into a memory buffer, that can be retrieved with
// its view() method, and cleared with its reset() method.
// read() operations on the stream are forbidden.
class TestMemoryOutputStream : public Stream {
public:
    virtual ssize_t read(void* buffer, size_t len) override {
        errno = EINVAL;
        return -1;
    }

    virtual ssize_t write(const void* buffer, size_t len) override {
        mData.insert(mData.end(), static_cast<const char*>(buffer),
                     static_cast<const char*>(buffer) + len);
        return static_cast<ssize_t>(len);
    }

    std::string_view view() const {
        return std::string_view(mData.data(), mData.size());
    }

    void reset() { mData.clear(); }

private:
    std::vector<char> mData;
};

}  // namespace base
}  // namespace android
