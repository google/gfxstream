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

#include <cstdint>

// A macro to define piece of code only for debugging. This is useful when
// checks are much bigger than a single assert(), e.g.:
//  class Lock {
//      ANDROID_IF_DEBUG(bool isLocked() { ... })
//      ...
//  };
//
//  void foo::bar() { assert(mLock.isLocked()); }
//
// Lock::isLocked() won't exist in release binaries at all.
//
#ifdef NDEBUG
#define ANDROID_IF_DEBUG(x)
#else   // !NDEBUG
#define ANDROID_IF_DEBUG(x) x
#endif  // !NDEBUG

namespace android {
namespace base {

// Checks if the current process has a debugger attached.
bool IsDebuggerAttached();

// Suspends the current process for up to |timeoutMs| until a debugger attaches.
// |timeoutMs| == -1 means 'no timeout'
bool WaitForDebugger(int64_t timeoutMs = -1);

// Issues a break into debugger (if one attached). Without a debugger it could
// do anything, but most probably will crash.
void DebugBreak();

}  // namespace base
}  // namespace android
