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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef VK_QSRI_TIMELINE_H
#define VK_QSRI_TIMELINE_H

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <sstream>

#include "gfxstream/ThreadAnnotations.h"
#include "gfxstream/host/logging.h"

namespace gfxstream {
namespace vk {

class VkQsriTimeline {
   public:
    using Callback = std::function<void()>;

    void signalNextPresentAndPoll() {
        std::lock_guard<std::mutex> guard(mMutex);
        mPresentCount++;
        pollLocked();
    }

    void registerCallbackForNextPresentAndPoll(Callback callback) {
        std::lock_guard<std::mutex> guard(mMutex);
        uint64_t requestPresentCount = mRequestPresentCount;
        mRequestPresentCount++;
        mPendingCallbacks.emplace(requestPresentCount, std::move(callback));
        pollLocked();
    }

    VkQsriTimeline() : mPresentCount(0), mRequestPresentCount(0) {}
    ~VkQsriTimeline() {
        std::lock_guard<std::mutex> guard(mMutex);
        if (mPendingCallbacks.empty()) {
            return;
        }
        std::stringstream ss;
        ss << mPendingCallbacks.size()
           << " pending QSRI callbacks found when destroying the timeline, waiting for: ";
        for (auto& [requiredPresentCount, callback] : mPendingCallbacks) {
            callback();
            ss << requiredPresentCount << ", ";
        }
        ss << "just call all of callbacks.";
        GFXSTREAM_ERROR("%s", ss.str().c_str());
    }

   private:
    std::mutex mMutex;
    std::map<uint64_t, Callback> mPendingCallbacks GUARDED_BY(mMutex);
    uint64_t mPresentCount GUARDED_BY(mMutex) = 0;
    uint64_t mRequestPresentCount GUARDED_BY(mMutex) = 0;

    void pollLocked() REQUIRES(mMutex) {
        auto firstPendingCallback = mPendingCallbacks.lower_bound(mPresentCount);
        for (auto readyCallback = mPendingCallbacks.begin(); readyCallback != firstPendingCallback;
             readyCallback++) {
            readyCallback->second();
        }
        mPendingCallbacks.erase(mPendingCallbacks.begin(), firstPendingCallback);
    }
};

}  // namespace vk
}  // namespace gfxstream

#endif  // VK_QSRI_TIMELINE_H