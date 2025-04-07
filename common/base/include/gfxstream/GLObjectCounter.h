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

#include <memory>
#include <string>
#include <vector>

namespace android {
namespace base {
class GLObjectCounter {
    DISALLOW_COPY_ASSIGN_AND_MOVE(GLObjectCounter);

public:
    GLObjectCounter();
    static GLObjectCounter* get();
    void incCount(size_t type);
    void decCount(size_t type);
    std::vector<size_t> getCounts();
    std::string printUsage();

private:
    class Impl;
    std::unique_ptr<Impl> mImpl;
};
}  // namespace base
}  // namespace android
