// Copyright 2025 The Android Open Source Project
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
#include <optional>

#include "gfxstream/ManagedDescriptor.h"
#include "gfxstream/memory/SharedMemory.h"

using gfxstream::base::DescriptorType;
using gfxstream::base::SharedMemory;

namespace gfxstream {
namespace base {

bool IsAndroidKernel6_6();
bool HasUdmabufDevice();

class UdmabufCreator {
   public:
    UdmabufCreator();
    ~UdmabufCreator();

    // true on success, false on failure
    bool init();

    std::optional<DescriptorType> handleFromSharedMemory(SharedMemory& memory);

   private:
    ManagedDescriptor mOsHandleCreator;
};

}  // namespace base
}  // namespace gfxstream
