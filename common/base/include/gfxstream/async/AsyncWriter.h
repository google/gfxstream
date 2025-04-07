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

#include "aemu/base/async/AsyncStatus.h"
#include "aemu/base/async/Looper.h"

#include <stdint.h>

namespace android {
namespace base {

class AsyncWriter {
public:
    AsyncWriter() :
            mBuffer(NULL),
            mBufferSize(0U),
            mPos(0U),
            mFdWatch(NULL) {}

    void reset(const void* buffer,
               size_t bufferSize,
               Looper::FdWatch* watch);

    AsyncStatus run();
    size_t written() { return mPos; }

private:
    const uint8_t* mBuffer;
    size_t mBufferSize;
    size_t mPos;
    Looper::FdWatch* mFdWatch;
};

}  // namespace base
}  // namespace android
