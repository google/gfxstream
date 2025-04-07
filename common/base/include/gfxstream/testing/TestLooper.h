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

#include "aemu/base/async/DefaultLooper.h"

namespace android {
namespace base {

// A TestLooper is a Looper implementation that allows its user to inspect all
// internals.
//
class TestLooper : public DefaultLooper {
public:
    using DefaultLooper::DefaultLooper;

    // Override nowMs/nowNs to allow overriding virtual time.
    Duration nowMs(ClockType clockType = ClockType::kHost) override;
    DurationNs nowNs(ClockType clockType = ClockType::kHost) override;

    void setVirtualTimeNs(DurationNs timeNs);

    // Add some functions to uncover the internal state
    const TimerSet& timers() const;
    const TimerList& activeTimers() const;
    const TimerList& pendingTimers() const;

    const FdWatchSet& fdWatches() const;
    const FdWatchList& pendingFdWatches() const;

    bool exitRequested() const;

    using DefaultLooper::runOneIterationWithDeadlineMs;

private:
    DurationNs mVirtualTimeNs = 0;
};

inline Looper::Duration TestLooper::nowMs(ClockType clockType) {
    if (clockType == ClockType::kVirtual) {
        return mVirtualTimeNs / 1000LL;
    } else {
        return DefaultLooper::nowMs(clockType);
    }
}

inline Looper::DurationNs TestLooper::nowNs(ClockType clockType) {
    if (clockType == ClockType::kVirtual) {
        return mVirtualTimeNs;
    } else {
        return DefaultLooper::nowNs(clockType);
    }
}

inline void TestLooper::setVirtualTimeNs(DurationNs timeNs) {
    mVirtualTimeNs = timeNs;
}

inline const DefaultLooper::TimerSet& TestLooper::timers() const {
    return mTimers;
}

inline const DefaultLooper::TimerList& TestLooper::activeTimers() const {
    return mActiveTimers;
}

inline const DefaultLooper::TimerList& TestLooper::pendingTimers() const {
    return mPendingTimers;
}

inline const DefaultLooper::FdWatchSet& TestLooper::fdWatches() const {
    return mFdWatches;
}

inline const DefaultLooper::FdWatchList& TestLooper::pendingFdWatches() const {
    return mPendingFdWatches;
}

inline bool TestLooper::exitRequested() const {
    return mForcedExit;
}

}  // namespace base
}  // namespace android
