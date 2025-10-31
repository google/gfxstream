/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "post_worker_vk.h"

#include "host/frame_buffer.h"
#include "gfxstream/common/logging.h"
#include "vulkan/display_vk.h"

namespace gfxstream {
namespace host {
namespace vk {

PostWorkerVk::PostWorkerVk(FrameBuffer* fb, Compositor* compositor, vk::DisplayVk* displayVk)
    : PostWorker(false, fb, compositor), m_displayVk(displayVk) {}

std::shared_future<void> PostWorkerVk::postImpl(ColorBuffer* cb) {
    std::shared_future<void> completedFuture = std::async(std::launch::deferred, [] {}).share();
    completedFuture.wait();

    if (!m_displayVk) {
        GFXSTREAM_FATAL("PostWorker missing DisplayVk.");
    }

    constexpr const int kMaxPostRetries = 2;
    for (int i = 0; i < kMaxPostRetries; i++) {
        const auto imageInfo = mFb->borrowColorBufferForDisplay(cb->getHndl());
        const float rotationDegrees = static_cast<float>(mFb->getZrot());
        auto result = m_displayVk->post(imageInfo.get(), rotationDegrees);
        if (result.success) {
            return result.postCompletedWaitable;
        }
    }

    GFXSTREAM_ERROR("Failed to post ColorBuffer after %d retries.", kMaxPostRetries);
    return completedFuture;
}

void PostWorkerVk::viewportImpl(int width, int height) {
    GFXSTREAM_ERROR("PostWorker with Vulkan doesn't support viewport");
}

void PostWorkerVk::clearImpl() {
    m_displayVk->clear();
}

void PostWorkerVk::exitImpl() {}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream