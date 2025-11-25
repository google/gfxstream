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

#include <array>
#include <optional>
#include <future>

#include "gfxstream/host/display_surface_user.h"
#include "host/post_worker.h"

namespace gfxstream {
namespace host {
namespace vk {

class DisplayVk;

class PostWorkerVk : public PostWorker {
   public:
    PostWorkerVk(FrameBuffer* fb, Compositor* compositor, vk::DisplayVk* displayGl);

   protected:
    std::shared_future<void> postImpl(
        ColorBuffer* cb, const std::optional<std::array<float, 16>>& colorTransform) override;
    void viewportImpl(int width, int height) override;
    void clearImpl() override;
    void exitImpl() override;

   private:
    // TODO(b/233939967): conslidate DisplayGl and DisplayVk into
    // `Display* const m_display`.
    //
    // The implementation for Vulkan native swapchain. Only initialized when
    // useVulkan is set when calling FrameBuffer::initialize(). PostWorker
    // doesn't take the ownership of this DisplayVk object.
    DisplayVk* const m_displayVk;
};

}  // namespace vk
}  // namespace host
}  // namespace gfxstream