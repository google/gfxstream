// Copyright (C) 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <atomic>
#include <future>

#include "color_buffer.h"
#include "hwc2.h"
#include "texture_draw.h"
#include "gfxstream/host/display.h"

namespace gfxstream {
namespace host {
namespace gl {

class DisplayGl : public Display {
  public:
    DisplayGl(TextureDraw* textureDraw): mTextureDraw(textureDraw) {}
    ~DisplayGl() {}

    struct PostLayer {
      ColorBuffer* colorBuffer = nullptr;

      std::optional<ComposeLayer> layerOptions;

      // TODO: This should probably be removed and TextureDraw should
      // only use drawLayer() but this is currently needed to support
      // existing draw paths without depending on FrameBuffer directly.
      struct OverlayOptions {
        float rotation;
        float dx;
        float dy;
      };
      std::optional<OverlayOptions> overlayOptions;
    };

    // TODO(b/233939967): move to generic Display.
    struct Post {
      uint32_t frameWidth = 0;
      uint32_t frameHeight = 0;
      std::vector<PostLayer> layers;
    };

    std::shared_future<void> post(const Post& request);

    void viewport(int width, int height);

    void clear();

    void exit();

    void setupContext();
  protected:
    void bindToSurfaceImpl(DisplaySurface* surface) override {}
    void surfaceUpdated(DisplaySurface* surface) override {}
    void unbindFromSurfaceImpl() override {}

  private:
    int mViewportWidth = 0;
    int mViewportHeight = 0;

    TextureDraw* mTextureDraw = nullptr;
};

}  // namespace gl
}  // namespace host
}  // namespace gfxstream
