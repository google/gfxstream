// Copyright (C) 2025 The Android Open Source Project
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

#include <stdint.h>

namespace gfxstream {
namespace host {

struct RepresentativeColorBufferMemoryTypeInfo {
    // The host memory type index used for Buffer/ColorBuffer allocations.
    uint32_t hostMemoryTypeIndex;

    // The guest memory type index that will be returned to guest processes querying
    // the memory type index of host AHardwareBuffer/ColorBuffer allocations. This may
    // point to an emulated memory type so that the host can control which memory flags are
    // exposed to the guest (i.e. hide VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT from the guest).
    uint32_t guestMemoryTypeIndex;
};

}  // namespace host
}  // namespace gfxstream
