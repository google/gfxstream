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

#include "aemu/base/async/Looper.h"

namespace android {
namespace base {

// Convenience class used to implement per-thread Looper instances.
// This allows one to call ThreadLooper::get() from any place to retrieve
// a thread-local Looper instance.
class ThreadLooper {
public:
    // Retrieve the current thread's Looper instance.
    //
    // If setLooper() was called previously on this thread, return the
    // corresponding value. Otherwise, on first call, use Looper:create()
    // and store the new instsance in thread-local storage. Note that this
    // new instance will be freed automatically when the thread exits.
    static Looper* get();

    // Sets the looper for the current thread. Must be called before get()
    // for the current thread. |looper| is a Looper instance that cannot be
    // NULL, and will be returned by future calls to get().
    //
    // Note that |looper| will be stored in thread-local storage but will
    // not be destroyed when the thread exits (unless |own| is true).
    static void setLooper(Looper* looper, bool own = false);

    // Run the specified std::function on the main loop.
    using Closure = std::function<void()>;
    static void runOnMainLooper(Closure&& func);

    // runOnMainLooper(), except waits until |func| finishes, then
    // returns to the calling thread.
    // If callled in actual main looper, it will immediately execute func and
    // return.
    static void runOnMainLooperAndWaitForCompletion(Closure&& func);

    // Reset the main runner, used by test code when clearing the main looper.
    static void clearMainRunner();
};

}  // namespace base
}  // namespace android
