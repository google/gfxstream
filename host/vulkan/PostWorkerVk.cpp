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
#include "PostWorkerVk.h"

#include "FrameBuffer.h"
#include "gfxstream/host/logging.h"
#include "vulkan/DisplayVk.h"

namespace gfxstream {

namespace {

using emugl::ABORT_REASON_OTHER;
using emugl::FatalError;

}  // namespace

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
        auto result = m_displayVk->post(imageInfo.get());
        if (result.success) {
            return result.postCompletedWaitable;
        }
    }

    GFXSTREAM_ERROR("Failed to post ColorBuffer after %d retries.", kMaxPostRetries);
    return completedFuture;
}

void PostWorkerVk::screenshot(ColorBuffer* cb, int width, int height, GLenum format, GLenum type,
                              int rotation, void* pixels, Rect rect) {
    GFXSTREAM_FATAL("Screenshot not supported with native Vulkan swapchain enabled.");
}

void PostWorkerVk::viewportImpl(int width, int height) {}

void PostWorkerVk::clearImpl() {
    GFXSTREAM_FATAL("PostWorker with Vulkan doesn't support clear");
}

void PostWorkerVk::exitImpl() {}

}  // namespace gfxstream