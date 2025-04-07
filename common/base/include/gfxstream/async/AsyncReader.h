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

// Helper class to help read data from a socket asynchronously.
//
// Call reset() to indicate how many bytes of data to read into a
// client-provided buffer, and a Looper::FdWatch to use.
//
// Then in the FdWatch callback, call run() on i/o read events.
// The function returns an AsyncStatus that indicates what to do
// after the run.
//
// Usage example:
//
//     AsyncReader myReader;
//     myReader.reset(myBuffer, sizeof myBuffer, myFdWatch);
//     ...
//     // when an event happens on myFdWatch
//     AsyncStatus status = myReader.run();
//     if (status == kCompleted) {
//         // finished reading the data.
//     } else if (status == kError) {
//         // i/o error (i.e. socket disconnection).
//     } else {  // really |status == kAgain|
//         // still more data needed to fill the buffer.
//     }
//
class AsyncReader {
public:
    AsyncReader() :
            mBuffer(NULL),
            mBufferSize(0U),
            mPos(0),
            mFdWatch(NULL) {}

    void reset(void* buffer, size_t buffSize, Looper::FdWatch* watch);

    AsyncStatus run();

private:
    uint8_t* mBuffer;
    size_t mBufferSize;
    size_t mPos;
    Looper::FdWatch* mFdWatch;
};

}  // namespace base
}  // namespace android
