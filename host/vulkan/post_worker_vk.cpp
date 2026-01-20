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
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/window_operations.h"
#include "gfxstream/common/logging.h"
#include "vulkan/display_vk.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

hwc_transform_t getTransformFromRotation(int rotation) {
    switch (static_cast<int>(rotation / 90)) {
        case 1:
            return HWC_TRANSFORM_ROT_270;
        case 2:
            return HWC_TRANSFORM_ROT_180;
        case 3:
            return HWC_TRANSFORM_ROT_90;
        default:
            return HWC_TRANSFORM_NONE;
    }
}

}  // namespace

PostWorkerVk::PostWorkerVk(FrameBuffer* fb, Compositor* compositor, vk::DisplayVk* displayVk)
    : PostWorker(false, fb, compositor), m_displayVk(displayVk) {}

std::shared_future<void> PostWorkerVk::postImpl(ColorBuffer* cb,
              const std::optional<std::array<float, 16>>& colorTransform) {
    std::shared_future<void> completedFuture = std::async(std::launch::deferred, [] {}).share();
    completedFuture.wait();

    if (!m_displayVk) {
        GFXSTREAM_FATAL("PostWorker missing DisplayVk.");
    }

    std::vector<std::unique_ptr<BorrowedImageInfo>> borrowedImages;
    DisplayVk::Post postCmd;

    auto addPostImage = [&](ColorBuffer* colorBuffer, int32_t x, int32_t y, int32_t w, int32_t h,
                            float rotation,
                            const std::optional<std::array<float, 16>>& transform = std::nullopt) {
        auto info = mFb->borrowColorBufferForDisplay(colorBuffer->getHndl());
        if (!info) return;

        DisplayVk::PostLayer layer;
        layer.info = info.get();
        layer.rotationDegrees = rotation;
        layer.colorTransform = transform;
        layer.displayFrame = {
            .left = x,
            .top = y,
            .right = x + w,
            .bottom = y + h,
        };
        borrowedImages.push_back(std::move(info));
        postCmd.layers.push_back(layer);
    };

    const auto& multiDisplay = get_gfxstream_multi_display_operations();
    const bool pixel_fold = multiDisplay.is_pixel_fold();

    if (pixel_fold) {
#ifdef CONFIG_AEMU
        if (!get_gfxstream_should_skip_draw()) {
            const float dpr = mFb->getDpr();
            const float px = mFb->getPx();
            const float py = mFb->getPy();
            const int windowWidth = mFb->windowWidth();
            const int windowHeight = mFb->windowHeight();
            const float zRot = static_cast<float>(mFb->getZrot());

            // Calculate "excess" space (difference between viewport and content size)
            // m_viewportWidth/Height are updated in viewportImpl
            const float excessW = static_cast<float>(m_viewportWidth) - windowWidth * dpr;
            const float excessH = static_cast<float>(m_viewportHeight) - windowHeight * dpr;

            // Calculate offsets based on scroll position (px, py)
            // px/py are 0.0 to 1.0
            const int32_t x = static_cast<int32_t>(px * excessW);
            const int32_t y = static_cast<int32_t>(py * excessH);
            const int32_t w = static_cast<int32_t>(windowWidth * dpr);
            const int32_t h = static_cast<int32_t>(windowHeight * dpr);

            addPostImage(cb, x, y, w, h, zRot, colorTransform);
        }
#endif
    } else if (multiDisplay.is_multi_display_enabled()) {
        if (multiDisplay.is_multi_window()) {
            int32_t previousDisplayId = -1;
            uint32_t currentDisplayId;
            uint32_t currentDisplayColorBufferHandle;
            while (multiDisplay.get_next_display_info(previousDisplayId, &currentDisplayId,
                                                      /*x=*/nullptr,
                                                      /*y=*/nullptr,
                                                      /*w=*/nullptr,
                                                      /*h=*/nullptr,
                                                      /*dpi=*/nullptr,
                                                      /*flags=*/nullptr,
                                                      &currentDisplayColorBufferHandle)) {
                previousDisplayId = currentDisplayId;

                if (currentDisplayColorBufferHandle == 0) {
                    continue;
                }
                get_gfxstream_window_operations().paint_multi_display_window(
                    currentDisplayId, currentDisplayColorBufferHandle);
            }
            // Main window post
             addPostImage(cb, 0, 0, 0, 0, static_cast<float>(mFb->getZrot()), colorTransform);
        } else {
            uint32_t combinedDisplayW = 0;
            uint32_t combinedDisplayH = 0;
            multiDisplay.get_combined_size(&combinedDisplayW, &combinedDisplayH);
            postCmd.frameWidth = combinedDisplayW;
            postCmd.frameHeight = combinedDisplayH;

            int32_t previousDisplayId = -1;
            uint32_t currentDisplayId;
            int32_t currentDisplayOffsetX;
            int32_t currentDisplayOffsetY;
            uint32_t currentDisplayW;
            uint32_t currentDisplayH;
            uint32_t currentDisplayColorBufferHandle;
            while (multiDisplay.get_next_display_info(
                previousDisplayId, &currentDisplayId, &currentDisplayOffsetX,
                &currentDisplayOffsetY, &currentDisplayW, &currentDisplayH,
                /*dpi=*/nullptr,
                /*flags=*/nullptr, &currentDisplayColorBufferHandle)) {
                previousDisplayId = currentDisplayId;

                if (currentDisplayW == 0 || currentDisplayH == 0 ||
                    (currentDisplayId != 0 && currentDisplayColorBufferHandle == 0)) {
                    continue;
                }

                ColorBuffer* currentCb =
                    currentDisplayId == 0
                        ? cb
                        : mFb->findColorBuffer(currentDisplayColorBufferHandle).get();
                if (!currentCb) {
                    continue;
                }

                float rotation = static_cast<float>(mFb->getZrot());
                const auto transform = getTransformFromRotation(mFb->getZrot());
                if (transform == HWC_TRANSFORM_ROT_90 || transform == HWC_TRANSFORM_ROT_270) {
                    std::swap(currentDisplayW, currentDisplayH);
                }
                addPostImage(currentCb, currentDisplayOffsetX, currentDisplayOffsetY,
                             currentDisplayW, currentDisplayH, rotation, colorTransform);
            }
        }
    } else if (get_gfxstream_window_operations().is_folded()) {
        // TODO: Implement fold logic if needed (similar to GL)
        // For now simple post
        addPostImage(cb, 0, 0, 0, 0, static_cast<float>(mFb->getZrot()), colorTransform);
    } else {
        // Simple case: single display, no special mode
        addPostImage(cb, 0, 0, 0, 0, static_cast<float>(mFb->getZrot()), colorTransform);
    }

    constexpr const int kMaxPostRetries = 2;
    for (int i = 0; i < kMaxPostRetries; i++) {
        auto result = m_displayVk->post(postCmd);
        if (result.success) {
            return result.postCompletedWaitable;
        }
    }

    GFXSTREAM_ERROR("Failed to post ColorBuffer after %d retries.", kMaxPostRetries);
    return completedFuture;
}

void PostWorkerVk::viewportImpl(int width, int height) {
    const float dpr = mFb->getDpr();
    m_viewportWidth = width * dpr;
    m_viewportHeight = height * dpr;
}

void PostWorkerVk::clearImpl() {
    m_displayVk->clear();
}

void PostWorkerVk::exitImpl() {}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream