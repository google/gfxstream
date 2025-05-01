// Copyright 2019 The Android Open Source Project
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
#include <optional>
#include <vector>

#include "aemu/base/ring_buffer.h"
#include "aemu/base/threads/FunctorThread.h"
#include "gfxstream/host/address_space_device.h"
#include "gfxstream/host/address_space_service.h"
#include "gfxstream/synchronization/MessageChannel.h"
#include "render-utils/address_space_graphics_types.h"
#include "render-utils/address_space_operations.h"

namespace gfxstream {
namespace host {

struct Allocation {
    char* buffer = nullptr;
    size_t blockIndex = 0;
    uint64_t offsetIntoPhys = 0;
    uint64_t size = 0;
    std::optional<uint32_t> dedicatedContextHandle;
    uint64_t hostmemId = 0;
    bool isView = false;
};

class AddressSpaceGraphicsContext : public AddressSpaceDeviceContext {
  public:
    AddressSpaceGraphicsContext(const struct AddressSpaceCreateInfo& create);
    ~AddressSpaceGraphicsContext();

    static void setConsumer(android::emulation::asg::ConsumerInterface);
    static void clear();

    void perform(AddressSpaceDevicePingInfo* info) override;
    AddressSpaceDeviceType getDeviceType() const override;

    void save(gfxstream::Stream* stream) const override;
    bool load(gfxstream::Stream* stream) override;

    void preSave() const override;
    void postSave() const override;

    static void globalStatePreSave();
    static void globalStateSave(gfxstream::Stream*);
    static void globalStatePostSave();

    static bool globalStateLoad(gfxstream::Stream*,
                                const std::optional<AddressSpaceDeviceLoadResources>& resources);

    enum AllocType {
        AllocTypeRing,
        AllocTypeBuffer,
        AllocTypeCombined,
    };

  private:
    void saveRingConfig(gfxstream::Stream* stream, const struct asg_ring_config& config) const;
    void saveAllocation(gfxstream::Stream* stream, const Allocation& alloc) const;

    void loadRingConfig(gfxstream::Stream* stream, struct asg_ring_config& config);
    void loadAllocation(gfxstream::Stream* stream, Allocation& alloc);

    // For consumer communication
    enum ConsumerCommand {
        Wakeup = 0,
        Sleep = 1,
        Exit = 2,
        PausePreSnapshot = 3,
        ResumePostSnapshot = 4,
    };

    // For ConsumerCallbacks
    int onUnavailableRead();

    // Data layout
    uint32_t mVersion = 1;
    Allocation mRingAllocation;
    Allocation mBufferAllocation;
    Allocation mCombinedAllocation;
    struct asg_context mHostContext = {};

    // Consumer storage
    android::emulation::asg::ConsumerCallbacks mConsumerCallbacks;
    android::emulation::asg::ConsumerInterface mConsumerInterface;
    void* mCurrentConsumer = 0;

    // Communication with consumer
    mutable gfxstream::base::MessageChannel<ConsumerCommand, 4> mConsumerMessages;
    uint32_t mExiting = 0;
    // For onUnavailableRead
    uint32_t mUnavailableReadCount = 0;

    struct VirtioGpuInfo {
        uint32_t contextId = 0;
        uint32_t capsetId = 0;
        std::optional<std::string> name;
    };
    std::optional<VirtioGpuInfo> mVirtioGpuInfo;
    // To save the ring config if it is cleared on hostmem map
    struct asg_ring_config mSavedConfig;
};

}  // namespace host
}  // namespace gfxstream
