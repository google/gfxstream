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

#include <unordered_map>

#include "render-utils/address_space_operations.h"
#include "render-utils/stream.h"

namespace gfxstream {
namespace host {

constexpr const int kAsgWriteBufferSize = 1048576;
constexpr const int kAsgWriteStepSize = 262144;
constexpr const int kAsgDataRingSize = 524288;
constexpr const int kAsgDrawFlushInterval = 10000;

int gfxstream_address_space_save_memory_state(gfxstream::Stream* stream);
int gfxstream_address_space_load_memory_state(gfxstream::Stream* stream);

// Resources which can not be directly reloaded by ASG.
struct AddressSpaceDeviceLoadResources {
    // ASGs may use memory backed by an external memory allocation (e.g. a
    // Virtio GPU blob resource with a host shmem allocation). These external
    // memory allocations can not be directly saved and loaded via
    // `gfxstream::Stream` and may not have the same `void*` across save
    // and load.
    struct ExternalMemory {
        void* externalAddress = nullptr;
        uint64_t externalAddressSize = 0;
    };
    // Maps ASG handle to the dedicated external memory.
    std::unordered_map<uint32_t, ExternalMemory> contextExternalMemoryMap;
};

// Sets the resources that can be used during a load which can not be loaded
// directly from by ASG.
int gfxstream_address_space_set_load_resources(AddressSpaceDeviceLoadResources resources);

address_space_device_control_ops GetAsgOperations();

void* sAddressSpaceDeviceGetHostPtr(uint64_t gpa);

}  // namespace host
}  // namespace gfxstream
