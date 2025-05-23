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

#include <vulkan/vulkan.h>

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <variant>

#include "VulkanDispatch.h"
#include "gfxstream/ThreadAnnotations.h"

namespace gfxstream {
namespace vk {

class DeviceOpTracker;
using DeviceOpTrackerPtr = std::shared_ptr<DeviceOpTracker>;

using DeviceOpWaitable = std::shared_future<void>;

inline bool IsDone(const DeviceOpWaitable& waitable) {
    return waitable.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

enum class DeviceOpStatus { kPending, kDone, kFailure };

// Helper class to track the completion of host operations for a specific VkDevice.
class DeviceOpTracker {
   public:
    DeviceOpTracker(VkDevice device, VulkanDispatch* deviceDispatch);

    DeviceOpTracker(const DeviceOpTracker& rhs) = delete;
    DeviceOpTracker& operator=(const DeviceOpTracker& rhs) = delete;

    DeviceOpTracker(DeviceOpTracker&& rhs) = delete;
    DeviceOpTracker& operator=(DeviceOpTracker&& rhs) = delete;

    // Transfers ownership of the fence to this helper and marks that the given fence
    // can be destroyed once the waitable has finished.
    void AddPendingGarbage(DeviceOpWaitable waitable, VkFence fence);

    // Transfers ownership of the semaphore to this helper and marks that the given
    // semaphore can be destroyed once the waitable has finished.
    void AddPendingGarbage(DeviceOpWaitable waitable, VkSemaphore semaphore);

    // Checks for completion of previously submitted waitables and destroys dependent
    // objects.
    void PollAndProcessGarbage();

    void OnDestroyDevice();

   private:
    VkDevice mDevice = VK_NULL_HANDLE;
    VulkanDispatch* mDeviceDispatch = nullptr;

    friend class DeviceOpBuilder;

    using OpPollingFunction = std::function<DeviceOpStatus()>;

    void AddPendingDeviceOp(OpPollingFunction pollFunction);
    struct PollFunction {
        OpPollingFunction func;
        std::chrono::time_point<std::chrono::system_clock> timepoint;
    };
    std::mutex mPollFunctionsMutex;
    std::deque<PollFunction> mPollFunctions GUARDED_BY(mPollFunctionsMutex);

    struct PendingGarbage {
        DeviceOpWaitable waitable;
        std::variant<VkFence, VkSemaphore> obj;
        std::chrono::time_point<std::chrono::system_clock> timepoint;
    };
    std::mutex mPendingGarbageMutex;
    std::deque<PendingGarbage> mPendingGarbage GUARDED_BY(mPendingGarbageMutex);
};

class DeviceOpBuilder {
   public:
    DeviceOpBuilder(DeviceOpTracker& tracker);

    DeviceOpBuilder(const DeviceOpBuilder& rhs) = delete;
    DeviceOpBuilder& operator=(const DeviceOpBuilder& rhs) = delete;

    DeviceOpBuilder(DeviceOpBuilder&& rhs) = delete;
    DeviceOpBuilder& operator=(DeviceOpBuilder&& rhs) = delete;

    ~DeviceOpBuilder();

    // Returns a VkFence that can be used to track resource usage for
    // host ops if a VkFence is not already readily available. This
    // DeviceOpBuilder and its underlying DeviceOpTracker maintain
    // ownership of the VkFence and will destroy it when then host op
    // has completed.
    VkFence CreateFenceForOp();

    // Returns a waitable that can be used to check whether a host op
    // has completed.
    DeviceOpWaitable OnQueueSubmittedWithFence(VkFence fence);

   private:
    DeviceOpTracker& mTracker;

    std::optional<VkFence> mCreatedFence;
    std::optional<VkFence> mSubmittedFence;
};

}  // namespace vk
}  // namespace gfxstream