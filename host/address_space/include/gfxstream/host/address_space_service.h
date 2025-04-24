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

#include <memory>

#include "aemu/base/files/Stream.h"

namespace gfxstream {
namespace host {

enum class AddressSpaceDeviceType {
    Graphics = ADDRESS_SPACE_CONTEXT_TYPE_GRAPHICS,
    Media = ADDRESS_SPACE_CONTEXT_TYPE_MEDIA,
    Sensors = ADDRESS_SPACE_CONTEXT_TYPE_SENSORS,
    Power = ADDRESS_SPACE_CONTEXT_TYPE_POWER,
    GenericPipe = ADDRESS_SPACE_CONTEXT_TYPE_GENERIC_PIPE,
    HostMemoryAllocator = ADDRESS_SPACE_CONTEXT_TYPE_HOST_MEMORY_ALLOCATOR,
    SharedSlotsHostMemoryAllocator = ADDRESS_SPACE_CONTEXT_TYPE_SHARED_SLOTS_HOST_MEMORY_ALLOCATOR,
    VirtioGpuGraphics = ADDRESS_SPACE_CONTEXT_TYPE_VIRTIO_GPU_GRAPHICS,
};

class AddressSpaceDeviceContext {
  public:
    virtual ~AddressSpaceDeviceContext() {}
    virtual void perform(AddressSpaceDevicePingInfo *info) = 0;
    virtual AddressSpaceDeviceType getDeviceType() const = 0;
    virtual void save(android::base::Stream* stream) const = 0;
    virtual bool load(android::base::Stream* stream) = 0;

    virtual void preSave() const { }
    virtual void postSave() const { }
};

struct AddressSpaceContextDescription {
    AddressSpaceDevicePingInfo* pingInfo = nullptr;
    uint64_t pingInfoGpa = 0;  // for snapshots
    std::unique_ptr<AddressSpaceDeviceContext> device_context;
};

} // namespace host
} // namespace gfxstream
