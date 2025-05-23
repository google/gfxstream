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
#include <functional>
#include <optional>

#include "render-utils/stream.h"

namespace gfxstream {

// Consumer Concept
//
// AddressSpaceGraphicsContext's are each associated with a consumer that
// takes data off the auxiliary buffer and to_host, while sending back data
// over the auxiliary buffer / from_host.
//
// will read the commands and write back data.
//
// The consumer type is fixed at startup. The interface is as follows:

// Called by the consumer, implemented in AddressSpaceGraphicsContext:
//
// Called when the consumer doesn't find anything to
// read in to_host. Will make the consumer sleep
// until another Ping(NotifyAvailable).

enum class AsgOnUnavailableReadStatus {
    kContinue = 0,
    kExit = 1,
    kSleep = 2,
    kPauseForSnapshot = 3,
    kResumeAfterSnapshot = 4,
};
using OnUnavailableReadCallback = std::function<AsgOnUnavailableReadStatus()>;

// Unpacks a type 2 transfer into host pointer and size.
using GetPtrCallback =
    std::function<char*(uint64_t)>;

using GetConfigCallback = std::function<void()>;

struct ConsumerCallbacks {
    OnUnavailableReadCallback onUnavailableRead;
    GetPtrCallback getPtr;
};

static constexpr const size_t kAsgConsumerRingStorageSize = 12288;
static constexpr const size_t kAsgPageSize = 4096;
static constexpr const size_t kAsgBlockSize = 16ULL * 1048576ULL;

struct AsgConsumerCreateInfo {
    uint32_t version = 0;

    char* ring_storage = nullptr;

    char* buffer = nullptr;

    // The size of the auxiliary buffer.
    uint32_t buffer_size = 0;

    // The flush interval of the auxiliary buffer.
    uint32_t buffer_flush_interval = 0;

    // The callbacks that the created `RenderChannel` can use to interact
    // with the creator.
    ConsumerCallbacks callbacks;

    // If created from a virtio gpu context, the context id.
    std::optional<uint32_t> virtioGpuContextId;

    // If created from a virtio gpu context, the context name.
    std::optional<std::string> virtioGpuContextName;

    // If created from a virtio gpu context, the capset id.
    std::optional<uint32_t> virtioGpuCapsetId;
};

using ConsumerHandle = void*;

using ConsumerCreateCallback =
    std::function<ConsumerHandle(const AsgConsumerCreateInfo& info, gfxstream::Stream*)>;

using ConsumerDestroyCallback =
    std::function<void(ConsumerHandle)>;

using ConsumerPreSaveCallback =
    std::function<void(ConsumerHandle)>;

using ConsumerGlobalPreSaveCallback =
    std::function<void()>;

using ConsumerSaveCallback =
    std::function<void(ConsumerHandle, gfxstream::Stream*)>;

using ConsumerGlobalPostSaveCallback =
    std::function<void()>;

using ConsumerPostSaveCallback =
    std::function<void(ConsumerHandle)>;

using ConsumerPostLoadCallback =
    std::function<void(ConsumerHandle)>;

using ConsumerGlobalPreLoadCallback =
    std::function<void()>;

// Reloads the underlying ASG ring config in case it was cleared on host
// memory mapping.
//
// This is a historical leftover for implementing `ASG_GET_CONFIG`.
//
// TODO: find out if this is still needed.
using ConsumerReloadRingConfig =
    std::function<void(ConsumerHandle)>;

struct ConsumerInterface {
    ConsumerCreateCallback create;
    ConsumerDestroyCallback destroy;

    ConsumerPreSaveCallback preSave;
    ConsumerGlobalPreSaveCallback globalPreSave;

    ConsumerSaveCallback save;

    ConsumerGlobalPostSaveCallback globalPostSave;
    ConsumerPostSaveCallback postSave;

    ConsumerPostLoadCallback postLoad;

    ConsumerGlobalPreLoadCallback globalPreLoad;

    ConsumerReloadRingConfig reloadRingConfig;
};

} // namespace gfxstream
