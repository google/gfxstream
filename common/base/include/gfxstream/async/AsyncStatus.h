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

namespace android {
namespace base {

// A list of values indicating the state of ongoing asynchronous operations.
// This is returned by various methods of helper classes like AsyncReader
// or AsyncWriter to indicate to the caller what to do next.
//
// |kAsyncCompleted| means the operation completed properly.
// |kAsyncAgain| means the operation hasn't finished yet.
// |kAsyncError| indicates that an i/o error occured and was detected.
//
enum AsyncStatus {
    kAsyncCompleted = 0,
    kAsyncAgain,
    kAsyncError
};

}  // namespace base
}  // namespace android
