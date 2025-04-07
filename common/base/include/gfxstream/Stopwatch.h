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

#include "aemu/base/Compiler.h"
#include "android/utils/debug.h"
#include <stdio.h>

namespace android {
namespace base {

class Stopwatch final {
public:
    Stopwatch();

    // Get the current elapsed time, microseconds.
    uint64_t elapsedUs() const;

    // Restart the stopwatch and return the current elapsed time, microseconds.
    uint64_t restartUs();

    static double sec(uint64_t us) { return us / 1000000.0; }
    static double ms(uint64_t us) { return us / 1000.0; }

private:
    uint64_t mStartUs;
};

// A class for convenient time tracking in a scope.
template <class Counter, int Divisor = 1>
class RaiiTimeTracker final {
    static_assert(Divisor > 0, "Bad divisor value");

    DISALLOW_COPY_ASSIGN_AND_MOVE(RaiiTimeTracker);

public:
    RaiiTimeTracker(Counter& time) : mTime(time) {}
    ~RaiiTimeTracker() { mTime += mSw.elapsedUs() / Divisor; }

private:
    Stopwatch mSw;
    Counter& mTime;
};

//
// A utility function to measure the time taken by the passed function |func|.
// Adds the elapsed time to the |time| parameter, returns exactly the same value
// as |func| returned.
// Usage:
//
//      int elapsedUs = 0;
//      measure(elapsedUs, [] { longRunningFunc1(); });
//      auto result = measure(elapsedUs, [&] { return longFunc2(); });
//      printf(..., elapsedUs);
//

template <class Counter, class Func>
auto measure(Counter& time, Func&& f) -> decltype(f()) {
    RaiiTimeTracker<Counter> rtt(time);
    return f();
}

template <class Counter, class Func>
auto measureMs(Counter& time, Func&& f) -> decltype(f()) {
    RaiiTimeTracker<Counter, 1000> rtt(time);
    return f();
}

// Some macros to have standard format of time measurements printed out.
#define STOPWATCH_PRINT_SPLIT(sw) \
    dinfo("%s %.03f ms", __func__, (sw).restartUs() / 1000.0f);

#define STOPWATCH_PRINT(sw) \
    dinfo("%s %.03f ms total\n", __func__, (sw).elapsedUs() / 1000.0f);

}  // namespace base
}  // namespace android
