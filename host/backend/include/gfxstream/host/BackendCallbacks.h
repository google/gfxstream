// Copyright 2024 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <functional>

#include "gfxstream/CancelableFuture.h"

namespace gfxstream {
namespace host {

struct BackendCallbacks {
    using RegisterProcessCleanupCallbackFunc =
        std::function<void(void* key, uint64_t contextId, std::function<void()> callback)>;
    RegisterProcessCleanupCallbackFunc registerProcessCleanupCallback;

    using UnregisterProcessCleanupCallbackFunc = std::function<void(void* key)>;
    UnregisterProcessCleanupCallbackFunc unregisterProcessCleanupCallback;

    using InvalidateColorBufferFunc = std::function<void(uint32_t colorBufferHandle)>;
    InvalidateColorBufferFunc invalidateColorBuffer;

    using FlushColorBufferFunc = std::function<void(uint32_t colorBufferHandle)>;
    FlushColorBufferFunc flushColorBuffer;

    using FlushColorBufferFromBytesFunc =
        std::function<void(uint32_t colorBufferHandle, const void* bytes, size_t bytesSize)>;
    FlushColorBufferFromBytesFunc flushColorBufferFromBytes;

    using ScheduleAsyncWorkFunc =
        std::function<CancelableFuture(std::function<void()> work, std::string description)>;
    ScheduleAsyncWorkFunc scheduleAsyncWork;

    using RegisterVulkanInstanceFunc = std::function<void(uint64_t id, const char* appName)>;
    RegisterVulkanInstanceFunc registerVulkanInstance;

    using UnregisterVulkanInstanceFunc = std::function<void(uint64_t id)>;
    UnregisterVulkanInstanceFunc unregisterVulkanInstance;
};

}  // namespace host
}  // namespace gfxstream
