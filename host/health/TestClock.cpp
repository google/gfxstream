/*
 * Copyright (C) 2025 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gfxstream/TestClock.h"

#include <mutex>

namespace gfxstream {
namespace base {
namespace {

static std::mutex gInternalTimeMutex;
static double gInternalTime = 0.0;

}  // namespace

/*static*/ TestClock::time_point TestClock::now() {
    std::lock_guard<std::mutex> lock(gInternalTimeMutex);
    return time_point(duration(gInternalTime));
}

/*static*/ void TestClock::advance(double secondsPassed) {
    std::lock_guard<std::mutex> lock(gInternalTimeMutex);
    gInternalTime += secondsPassed;
}

/*static*/ void TestClock::reset() {
    std::lock_guard<std::mutex> lock(gInternalTimeMutex);
    gInternalTime = 0.0;
}

}  // namespace base
}  // namespace gfxstream