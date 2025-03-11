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

#include "DeviceOpTracker.h"

#include <algorithm>
#include <type_traits>

#include "gfxstream/host/logging.h"
#include "host-common/GfxstreamFatalError.h"

namespace gfxstream {
namespace vk {
namespace {

using emugl::ABORT_REASON_OTHER;
using emugl::FatalError;

constexpr const size_t kSizeLoggingThreshold = 200;
constexpr const auto kSizeLoggingTimeThreshold = std::chrono::seconds(1);

constexpr const auto kAutoDeleteTimeThreshold = std::chrono::seconds(5);

template <typename T>
inline constexpr bool always_false_v = false;

}  // namespace

DeviceOpTracker::DeviceOpTracker(VkDevice device, VulkanDispatch* deviceDispatch)
    : mDevice(device), mDeviceDispatch(deviceDispatch) {}

void DeviceOpTracker::AddPendingGarbage(DeviceOpWaitable waitable, VkFence fence) {
    std::lock_guard<std::mutex> lock(mPendingGarbageMutex);

    mPendingGarbage.push_back(PendingGarbage{
        .waitable = std::move(waitable),
        .obj = fence,
        .timepoint = std::chrono::system_clock::now(),
    });

    if (mPendingGarbage.size() > kSizeLoggingThreshold) {
        GFXSTREAM_WARNING("VkDevice:%p has %d pending garbage objects.", mDevice,
                          mPendingGarbage.size());
    }
}

void DeviceOpTracker::AddPendingGarbage(DeviceOpWaitable waitable, VkSemaphore semaphore) {
    std::lock_guard<std::mutex> lock(mPendingGarbageMutex);

    mPendingGarbage.push_back(PendingGarbage{
        .waitable = std::move(waitable),
        .obj = semaphore,
        .timepoint = std::chrono::system_clock::now(),
    });

    if (mPendingGarbage.size() > kSizeLoggingThreshold) {
        GFXSTREAM_WARNING("VkDevice:%p has %d pending garbage objects.", mDevice,
                          mPendingGarbage.size());
    }
}

void DeviceOpTracker::PollAndProcessGarbage() {
    std::lock_guard<std::mutex> pollFunctionsLock(mPollFunctionsMutex);
    mPollFunctions.erase(std::remove_if(mPollFunctions.begin(), mPollFunctions.end(),
                                        [](const PollFunction& pollingFunc) {
                                            DeviceOpStatus status = pollingFunc.func();
                                            return status != DeviceOpStatus::kPending;
                                        }),
                         mPollFunctions.end());

    if (mPollFunctions.size() > kSizeLoggingThreshold) {
        // Only report old-enough objects to avoid reporting lots of pending waitables
        // when many requests have been done in a small amount of time.
        const auto now = std::chrono::system_clock::now();
        const auto old = now - kSizeLoggingTimeThreshold;
        size_t numOldFuncs = std::count_if(
            mPollFunctions.begin(), mPollFunctions.end(), [old](const PollFunction& pollingFunc) {
                return (pollingFunc.timepoint < old);
            });
        if (numOldFuncs > kSizeLoggingThreshold) {
            //TODO(b/382028853): should be a warning
            GFXSTREAM_DEBUG(
                "VkDevice:%p has %d pending waitables, %d taking more than %d milliseconds.",
                mDevice, mPollFunctions.size(), numOldFuncs,
                std::chrono::duration_cast<std::chrono::milliseconds>(kSizeLoggingTimeThreshold));
        }
    }

    const auto now = std::chrono::system_clock::now();
    const auto old = now - kAutoDeleteTimeThreshold;
    {
        std::lock_guard<std::mutex> pendingGarbageLock(mPendingGarbageMutex);

        // Assuming that pending garbage is added to the queue in the roughly the order
        // they are used, encountering an unsignaled/pending waitable likely means that
        // all pending garbage after is also still pending. This might not necessarily
        // always be the case but it is a simple heuristic to try to minimize the amount
        // of work performed here as it is expected that this function will be called
        // while processing other guest vulkan functions.
        auto firstPendingIt = std::find_if(mPendingGarbage.begin(), mPendingGarbage.end(),
                                           [old](const PendingGarbage& pendingGarbage) {
                                               if (pendingGarbage.timepoint < old) {
                                                   return /*still pending=*/false;
                                               }
                                               return !IsDone(pendingGarbage.waitable);
                                           });

        for (auto it = mPendingGarbage.begin(); it != firstPendingIt; it++) {
            PendingGarbage& pendingGarbage = *it;

            if (pendingGarbage.timepoint < old) {
                const auto difference = std::chrono::duration_cast<std::chrono::milliseconds>(
                    pendingGarbage.timepoint - now);
                GFXSTREAM_WARNING(
                    "VkDevice:%p had a waitable pending for %d milliseconds. Leaking object.",
                    mDevice, difference.count());
                continue;
            }

            std::visit(
                [this](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, VkFence>) {
                        mDeviceDispatch->vkDestroyFence(mDevice, arg, nullptr);
                    } else if constexpr (std::is_same_v<T, VkSemaphore>) {
                        mDeviceDispatch->vkDestroySemaphore(mDevice, arg, nullptr);
                    } else {
                        static_assert(always_false_v<T>, "non-exhaustive visitor!");
                    }
                },
                pendingGarbage.obj);
        }

        mPendingGarbage.erase(mPendingGarbage.begin(), firstPendingIt);

        if (mPendingGarbage.size() > kSizeLoggingThreshold) {
            GFXSTREAM_WARNING("VkDevice:%p has %d pending garbage objects.", mDevice,
                              mPendingGarbage.size());
        }
    }
}

void DeviceOpTracker::OnDestroyDevice() {
    mDeviceDispatch->vkDeviceWaitIdle(mDevice);

    PollAndProcessGarbage();

    {
        std::lock_guard<std::mutex> lock(mPendingGarbageMutex);
        if (!mPendingGarbage.empty()) {
            GFXSTREAM_WARNING("VkDevice:%p has %d leaking garbage objects on destruction.", mDevice,
                              mPendingGarbage.size());
        }
    }
}

void DeviceOpTracker::AddPendingDeviceOp(std::function<DeviceOpStatus()> pollFunction) {
    std::lock_guard<std::mutex> lock(mPollFunctionsMutex);
    mPollFunctions.push_back(PollFunction{
        .func = std::move(pollFunction),
        .timepoint = std::chrono::system_clock::now(),
    });
}

DeviceOpBuilder::DeviceOpBuilder(DeviceOpTracker& tracker) : mTracker(tracker) {}

DeviceOpBuilder::~DeviceOpBuilder() {
    if (!mSubmittedFence) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "Invalid usage: failed to call OnQueueSubmittedWithFence().";
    }
}

VkFence DeviceOpBuilder::CreateFenceForOp() {
    const VkFenceCreateInfo fenceCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
    };
    VkFence fence = VK_NULL_HANDLE;
    VkResult result = mTracker.mDeviceDispatch->vkCreateFence(mTracker.mDevice, &fenceCreateInfo,
                                                              nullptr, &fence);

    mCreatedFence = fence;
    if (result != VK_SUCCESS) {
        GFXSTREAM_ERROR("DeviceOpBuilder failed to create VkFence!");
        return VK_NULL_HANDLE;
    }
    return fence;
}

DeviceOpWaitable DeviceOpBuilder::OnQueueSubmittedWithFence(VkFence fence) {
    if (mCreatedFence.has_value() && fence != mCreatedFence) {
        GFXSTREAM_ABORT(FatalError(ABORT_REASON_OTHER))
            << "Invalid usage: failed to call OnQueueSubmittedWithFence() with the fence "
            << "requested from CreateFenceForOp.";
    }
    mSubmittedFence = fence;

    const bool destroyFenceOnCompletion = mCreatedFence.has_value();

    std::shared_ptr<std::promise<void>> promise = std::make_shared<std::promise<void>>();
    DeviceOpWaitable future = promise->get_future().share();

    mTracker.AddPendingDeviceOp([device = mTracker.mDevice,
                                 deviceDispatch = mTracker.mDeviceDispatch, fence,
                                 promise = std::move(promise), destroyFenceOnCompletion] {
        if (fence == VK_NULL_HANDLE) {
            return DeviceOpStatus::kDone;
        }

        VkResult result = deviceDispatch->vkGetFenceStatus(device, fence);
        if (result == VK_NOT_READY) {
            return DeviceOpStatus::kPending;
        }

        if (destroyFenceOnCompletion) {
            deviceDispatch->vkDestroyFence(device, fence, nullptr);
        }
        promise->set_value();

        return result == VK_SUCCESS ? DeviceOpStatus::kDone : DeviceOpStatus::kFailure;
    });

    return future;
}

}  // namespace vk
}  // namespace gfxstream
