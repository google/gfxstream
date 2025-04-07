// Copyright 2020 The Android Open Source Project
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

#include <atomic>
#include <memory>
#include <string>

/*Example usage:
#include "GLcommon/GLESmacros.h"
// inside function where you want to trace
MEM_TRACE("<Group name>")

Implementation:
The tracker registers hooks to tcmalloc allocator for
functions explicitly declared as being tracked. Now, we tracker every APIs in
emugl translator library as "EMUGL".  When the hooks are
invoked, the callback function gets the backtraces as a list of program
counters. Then, it walks up the stack and checks if any registerd
function is used in this malloc/free operation. Once hit, it will record
stats for total allocation and live memory for each individual function.

*/
namespace android {
namespace base {

class MemoryTracker {
    DISALLOW_COPY_ASSIGN_AND_MOVE(MemoryTracker);

public:
    struct MallocStats {
        std::atomic<int64_t> mAllocated{0};
        std::atomic<int64_t> mLive{0};
        std::atomic<int64_t> mPeak{0};
    };

    static MemoryTracker* get();
    MemoryTracker();
    bool addToGroup(const std::string& group, const std::string& func);
    std::string printUsage(int verbosity = 0);
    void start();
    void stop();
    bool isEnabled();
    std::unique_ptr<MemoryTracker::MallocStats> getUsage(
            const std::string& group);

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace base
}  // namespace android
