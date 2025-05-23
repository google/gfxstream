
// Copyright (C) 2019 The Android Open Source Project
// Copyright (C) 2019 Google Inc.
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

#include <functional>
#include <memory>
#include <vector>

namespace gfxstream {
namespace guest {

// WorkPool provides a way to queue several different + arbitrary wait / signal
// operations.  There is no specific imposed order to the operations; all
// ordering is derived from dependencies among the queued tasks.  The number of
// threads used is the number of concurrent tasks in flight.  Tasks are sent in
// groups, representing a collection that can be waited on (a wait group). 
class WorkPool {
public:
    using Task = std::function<void()>;
    using WaitGroupHandle = uint64_t;
    using TimeoutUs = uint64_t;

    WorkPool(int numInitialThreads = 4);
    ~WorkPool();

    WaitGroupHandle schedule(const std::vector<Task>& tasks);

    bool waitAny(WaitGroupHandle waitGroup, TimeoutUs timeout = -1);
    bool waitAll(WaitGroupHandle waitGroup, TimeoutUs timeout = -1);
private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};

} // namespace guest
} // namespace gfxstream
