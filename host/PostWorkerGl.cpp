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
#include "PostWorkerGl.h"

#include "FrameBuffer.h"
#include "gfxstream/host/display_operations.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/window_operations.h"
#include "DisplayGl.h"
#include "DisplaySurfaceGl.h"

namespace gfxstream {

namespace {

using gl::DisplayGl;
using gl::DisplaySurfaceGl;
using gl::EmulationGl;
using gl::s_egl;

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

PostWorkerGl::PostWorkerGl(bool mainThreadPostingOnly, FrameBuffer* fb, Compositor* compositor,
                           DisplayGl* displayGl, gl::EmulationGl* emulationGl)
    : PostWorker(mainThreadPostingOnly, fb, compositor),
      m_displayGl(displayGl),
      mEmulationGl(emulationGl) {
    if (!m_displayGl) {
        GFXSTREAM_FATAL("PostWorker missing DisplayGl.");
    }
}

void PostWorkerGl::screenshot(ColorBuffer* cb, int screenwidth, int screenheight, GLenum format,
                              GLenum type, int skinRotation, void* pixels, Rect rect) {
    // See b/292237104.
    mFb->lock();
    cb->readToBytesScaled(screenwidth, screenheight, format, type, skinRotation, rect, pixels);
    mFb->unlock();
}

std::shared_future<void> PostWorkerGl::postImpl(ColorBuffer* cb) {
    if (!mContextBound || m_mainThreadPostingOnly) {
        // This might happen on headless mode
        // Also if posting on main thread, the context binding can get polluted easily, which
        // requires frequent rebinds.
        setupContext();
    }
    std::shared_future<void> completedFuture = std::async(std::launch::deferred, [] {}).share();
    completedFuture.wait();

    DisplayGl::Post post = {};

    ComposeLayer postLayerOptions = {
        .composeMode = HWC2_COMPOSITION_DEVICE,
        .blendMode = HWC2_BLEND_MODE_NONE,
        .alpha = 1.0f,
        .transform = HWC_TRANSFORM_NONE,
    };

    const auto& multiDisplay = get_gfxstream_multi_display_operations();
    const bool pixel_fold = multiDisplay.is_pixel_fold();
    if (pixel_fold) {
#ifdef CONFIG_AEMU
        if (get_gfxstream_should_skip_draw()) {
            post.layers.clear();
        } else {
            post.layers.push_back(postWithOverlay(cb));
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
            post.layers.push_back(postWithOverlay(cb));
        } else {
            uint32_t combinedDisplayW = 0;
            uint32_t combinedDisplayH = 0;
            multiDisplay.get_combined_size(&combinedDisplayW, &combinedDisplayH);

            post.frameWidth = combinedDisplayW;
            post.frameHeight = combinedDisplayH;

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

                const auto transform = getTransformFromRotation(mFb->getZrot());
                postLayerOptions.transform = transform;
                if ( transform == HWC_TRANSFORM_ROT_90 || transform == HWC_TRANSFORM_ROT_270) {
                    std::swap(currentDisplayW, currentDisplayH);
                }
                postLayerOptions.displayFrame = {
                    .left = static_cast<int>(currentDisplayOffsetX),
                    .top = static_cast<int>(currentDisplayOffsetY),
                    .right = static_cast<int>(currentDisplayOffsetX + currentDisplayW),
                    .bottom = static_cast<int>(currentDisplayOffsetY + currentDisplayH),
                };
                postLayerOptions.crop = {
                    .left = 0.0f,
                    .top = static_cast<float>(currentCb->getHeight()),
                    .right = static_cast<float>(currentCb->getWidth()),
                    .bottom = 0.0f,
                };

                post.layers.push_back(DisplayGl::PostLayer{
                    .colorBuffer = currentCb,
                    .layerOptions = postLayerOptions,
                });
            }
        }
    } else if (get_gfxstream_window_operations().is_folded()) {
        const float dpr = mFb->getDpr();

        post.frameWidth = m_viewportWidth / dpr;
        post.frameHeight = m_viewportHeight / dpr;

        int displayOffsetX;
        int displayOffsetY;
        int displayW;
        int displayH;
        get_gfxstream_window_operations().get_folded_area(&displayOffsetX, &displayOffsetY,
                                                          &displayW, &displayH);

        postLayerOptions.displayFrame = {
            .left = 0,
            .top = 0,
            .right = mFb->windowWidth(),
            .bottom = mFb->windowHeight(),
        };
        postLayerOptions.crop = {
            .left = static_cast<float>(displayOffsetX),
            .top = static_cast<float>(displayOffsetY + displayH),
            .right = static_cast<float>(displayOffsetX + displayW),
            .bottom = static_cast<float>(displayOffsetY),
        };
        postLayerOptions.transform = getTransformFromRotation(mFb->getZrot());

        post.layers.push_back(DisplayGl::PostLayer{
            .colorBuffer = cb,
            .layerOptions = postLayerOptions,
        });
    } else {
        post.layers.push_back(postWithOverlay(cb));
    }
    return m_displayGl->post(post);
}

DisplayGl::PostLayer PostWorkerGl::postWithOverlay(ColorBuffer* cb) {
    float dpr = mFb->getDpr();
    int windowWidth = mFb->windowWidth();
    int windowHeight = mFb->windowHeight();
    float px = mFb->getPx();
    float py = mFb->getPy();
    int zRot = mFb->getZrot();

    // Find the x and y values at the origin when "fully scrolled."
    // Multiply by 2 because the texture goes from -1 to 1, not 0 to 1.
    // Multiply the windowing coordinates by DPR because they ignore
    // DPR, but the viewport includes DPR.
    float fx = 2.f * (m_viewportWidth - windowWidth * dpr) / (float)m_viewportWidth;
    float fy = 2.f * (m_viewportHeight - windowHeight * dpr) / (float)m_viewportHeight;

    // finally, compute translation values
    float dx = px * fx;
    float dy = py * fy;

    return DisplayGl::PostLayer{
        .colorBuffer = cb,
        .overlayOptions =
            DisplayGl::PostLayer::OverlayOptions{
                .rotation = static_cast<float>(zRot),
                .dx = dx,
                .dy = dy,
            },
    };
}

// Called whenever the subwindow needs a refresh (FrameBuffer::setupSubWindow).
// This rebinds the subwindow context (to account for
// when the refresh is a display change, for instance)
// and resets the posting viewport.
void PostWorkerGl::viewportImpl(int width, int height) {
    setupContext();
    const float dpr = mFb->getDpr();
    m_viewportWidth = width * dpr;
    m_viewportHeight = height * dpr;

    if (!m_displayGl) {
        GFXSTREAM_FATAL("PostWorker missing DisplayGl.");
    }
    m_displayGl->viewport(m_viewportWidth, m_viewportHeight);
}

// Called when the subwindow refreshes, but there is no
// last posted color buffer to show to the user. Instead of
// displaying whatever happens to be in the back buffer,
// clear() is useful for outputting consistent colors.
void PostWorkerGl::clearImpl() {
    if (!mContextBound || m_mainThreadPostingOnly) {
        // This might happen on headless mode
        setupContext();
    }
    m_displayGl->clear();
}

std::shared_future<void> PostWorkerGl::composeImpl(const FlatComposeRequest& composeRequest) {
    if (!mContextBound || m_mainThreadPostingOnly) {
        // This might happen on headless mode
        setupContext();
    }
    return PostWorker::composeImpl(composeRequest);
}

void PostWorkerGl::setupContext() {
    std::lock_guard<std::mutex> lock(mMutex);
    const auto* surface = getBoundSurface();
    const DisplaySurfaceGl* surfaceGl = nullptr;
    if (surface) {
        surfaceGl = static_cast<const DisplaySurfaceGl*>(surface->getImpl());
    } else {
        // Create a fake context.
        // This could happen in AEMU with -qt-hide-window. Also due to an Intel bug
        // this needs to happen on post thread.
        // b/274313125
        if (!mFakeWindowSurface) {
            mFakeWindowSurface = mEmulationGl->createFakeWindowSurface();
        }
        if (!mFakeWindowSurface) {
            GFXSTREAM_ERROR("Post worker does not have a window surface.");
            return;
        }
        surfaceGl = static_cast<const DisplaySurfaceGl*>(mFakeWindowSurface->getImpl());
    }

    // It will be no-op if it rebinds to the same context.
    // We need to retry just in case the surface is changed.

    // Also we could not use the scope context helper here,
    // because (1) binds and unbinds happen in very different places;
    // (2) they both need to happen in post thread, but the d'tor
    // of PostWorker can happen in a different thread.
    if (!surfaceGl->bindContext()) {
        GFXSTREAM_ERROR("Failed to bind to post worker context.");
        return;
    }
    mContextBound = true;
}

void PostWorkerGl::exitImpl() {
    if (!mContextBound) {
        return;
    }
    s_egl.eglMakeCurrent(s_egl.eglGetDisplay(EGL_DEFAULT_DISPLAY), nullptr, nullptr, nullptr);
    mContextBound = false;
}

}  // namespace gfxstream
