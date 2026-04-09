// Copyright 2022 The Android Open Source Project
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

#include <future>
#include <optional>
#include <vector>

#include "gfxstream/host/borrowed_image.h"
#include "hwc2.h"
#include "render-utils/Renderer.h"

namespace gfxstream {
namespace host {

//  Thread hostile and should only be called from the same single thread.
class Compositor {
   public:
    virtual ~Compositor() {}

    struct CompositionRequestLayer {
        std::unique_ptr<BorrowedImageInfo> source;
        ComposeLayer props;
    };

    struct CompositionRequest {
        std::unique_ptr<BorrowedImageInfo> target;
        std::vector<CompositionRequestLayer> layers;
    };

    using CompositionFinishedWaitable = std::shared_future<void>;

    virtual CompositionFinishedWaitable compose(const CompositionRequest& compositionRequest) = 0;

    virtual void onImageDestroyed(uint32_t imageId) {}

    virtual void setScreenMask(int width, int height, const uint8_t* rgbaData) = 0;
    virtual void setScreenBackground(int width, int height, const uint8_t* rgbaData) = 0;

    struct DisplayLayout {
        Rect displayRect;
        int screenWidth;
        int screenHeight;
    };
    std::optional<DisplayLayout> getDisplayLayout() const {
        return m_displayLayout;
    }
    void setDisplayLayout(int screenWidth, int screenHeight, const Rect& displayRect) {
        if (displayRect.size.w > 0 && displayRect.size.h > 0) {
            DisplayLayout layout;
            layout.screenWidth = screenWidth;
            layout.screenHeight = screenHeight;
            layout.displayRect = displayRect;
            m_displayLayout = layout;
        } else {
            // Use to reset
            m_displayLayout = std::nullopt;
        }
    }

    // Calculates the display rectangle for a target resolution, returns false if there is no
    // display layout provided for the composition
    bool getScaledDisplayRect(Rect& outScaledDisplayRect, int targetWidth, int targetHeight) {
        if (!m_displayLayout) {
            return false;
        }
        const Pos& dPos = m_displayLayout->displayRect.pos;
        const Size& dSize = m_displayLayout->displayRect.size;

        // Calculate scaled display frame position and size based on the target resolution
        outScaledDisplayRect.pos.x = (dPos.x * targetWidth) / m_displayLayout->screenWidth;
        outScaledDisplayRect.pos.y = (dPos.y * targetHeight) / m_displayLayout->screenHeight;
        outScaledDisplayRect.size.w = (dSize.w * targetWidth) / m_displayLayout->screenWidth;
        outScaledDisplayRect.size.h = (dSize.h * targetHeight) / m_displayLayout->screenHeight;
        return true;
    }

   private:
    std::optional<DisplayLayout> m_displayLayout;
};

}  // namespace host
}  // namespace gfxstream
