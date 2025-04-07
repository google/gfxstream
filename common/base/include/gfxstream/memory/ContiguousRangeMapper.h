// Copyright 2018 The Android Open Source Project
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

#include <functional>

#include <inttypes.h>

// Convenience class ContiguousRangeMapper which makes it easier to collect
// contiguous ranges and run some function over the coalesced range.

namespace android {
namespace base {

class ContiguousRangeMapper {
public:
    using Func = std::function<void(uintptr_t, uintptr_t)>;

    ContiguousRangeMapper(Func&& mapFunc, uintptr_t batchSize = 0) :
        mMapFunc(std::move(mapFunc)), mBatchSize(batchSize) { }

    // add(): adds [start, start + size) to the internally tracked range.
    // mMapFunc on the previous range runs if the new piece is not contiguous.

    void add(uintptr_t start, uintptr_t size);

    // Signals end of collection of pieces, running mMapFunc over any
    // outstanding range.
    void finish();

    // Destructor all runs mMapFunc if there is an outstanding range.
    ~ContiguousRangeMapper();

private:
    Func mMapFunc = {};
    uintptr_t mBatchSize = 0;
    bool mHasRange = false;
    uintptr_t mStart = 0;
    uintptr_t mEnd = 0;
    DISALLOW_COPY_ASSIGN_AND_MOVE(ContiguousRangeMapper);
};

}  // namespace base
}  // namespace android
