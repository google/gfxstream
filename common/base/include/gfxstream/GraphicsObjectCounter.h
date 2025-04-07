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

#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "aemu/base/Compiler.h"
#include "aemu/base/synchronization/Lock.h"

namespace android {
namespace base {

enum class GraphicsObjectType : int {
    NULLTYPE,
    COLORBUFFER,
    NUM_OBJECT_TYPES,
};

static constexpr size_t toIndex(GraphicsObjectType type) { return static_cast<size_t>(type); }

class GraphicsObjectCounter {
    DISALLOW_COPY_ASSIGN_AND_MOVE(GraphicsObjectCounter);

   public:
    GraphicsObjectCounter();
    void incCount(size_t type);
    void decCount(size_t type);
    std::vector<size_t> getCounts() const;
    std::string printUsage() const;
    static GraphicsObjectCounter* get();

   private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};
}  // namespace base
}  // namespace android
