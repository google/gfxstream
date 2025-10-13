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
#pragma once

#include <future>
#include <optional>

#include "gfxstream/host/display_surface_user.h"
#include "post_worker.h"
#include "host/gl/display_gl.h"
#include "host/gl/emulation_gl.h"

namespace gfxstream {
namespace host {
namespace gl {

class DisplayGl;
class EmulationGl;
class RecursiveScopedContextBind;

class PostWorkerGl : public PostWorker, public DisplaySurfaceUser {
   public:
    PostWorkerGl(bool mainThreadPostingOnly, FrameBuffer* fb, Compositor* compositor,
                 gl::DisplayGl* displayGl, gl::EmulationGl* emulationGl);

   protected:
    std::shared_future<void> postImpl(ColorBuffer* cb) override;
    void viewportImpl(int width, int height) override;
    void clearImpl() override;
    void exitImpl() override;
    std::shared_future<void> composeImpl(const FlatComposeRequest& composeRequest) override;

    void bindToSurfaceImpl(DisplaySurface* surface) override {}
    void surfaceUpdated(DisplaySurface* surface) override {}
    void unbindFromSurfaceImpl() override {}

   private:
    void setupContext();
    gl::DisplayGl::PostLayer postWithOverlay(ColorBuffer* cb);

   private:
    // TODO(b/233939967): conslidate DisplayGl and DisplayVk into
    // `Display* const m_display`.
    gl::DisplayGl* const m_displayGl;

    int m_viewportWidth = 0;
    int m_viewportHeight = 0;

    bool mContextBound = false;
    std::unique_ptr<DisplaySurface> mFakeWindowSurface = nullptr;
    gl::EmulationGl* mEmulationGl;
};

}  // namespace gl
}  // namespace host
}  // namespace gfxstream