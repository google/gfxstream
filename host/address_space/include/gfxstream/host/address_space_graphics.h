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

#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/host/address_space_device.h"
#include "gfxstream/host/address_space_graphics_types.h"
#include "gfxstream/host/address_space_service.h"
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

    static void setConsumer(ConsumerInterface);
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
    void saveAllocation(gfxstream::Stream* stream, const Allocation& alloc) const;
    void loadAllocation(gfxstream::Stream* stream, Allocation& alloc);

    // For ConsumerCallbacks
    AsgOnUnavailableReadStatus onUnavailableRead();

    // Data layout
    uint32_t mVersion = 1;
    Allocation mRingAllocation;
    Allocation mBufferAllocation;
    Allocation mCombinedAllocation;

    // Consumer storage
    ConsumerCallbacks mConsumerCallbacks;
    ConsumerInterface mConsumerInterface;
    void* mCurrentConsumer = 0;

    // Communication with consumer
    struct ConsumerState {
        bool wakeupRequested = false;
        bool pausePreSnapshotRequested = false;
        bool resumePostSnapshotRequested = false;
        bool exiting = false;

        bool hasPendingWork() const {
            return wakeupRequested || pausePreSnapshotRequested ||
                   resumePostSnapshotRequested || exiting;
        }
    };
    mutable std::mutex mConsumerStateMutex;
    mutable std::condition_variable mConsumerStateCv;
    mutable ConsumerState mConsumerState GUARDED_BY(mConsumerStateMutex);

    struct VirtioGpuInfo {
        uint32_t contextId = 0;
        uint32_t capsetId = 0;
        std::optional<std::string> name;
    };
    std::optional<VirtioGpuInfo> mVirtioGpuInfo;
};

}  // namespace host
}  // namespace gfxstream
