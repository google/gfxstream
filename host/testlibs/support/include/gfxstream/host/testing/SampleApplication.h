// Copyright (C) 2018 The Android Open Source Project
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

#include <cinttypes>
#include <functional>
#include <memory>

#include "FrameBuffer.h"
#include "Hwc2.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "RenderThreadInfo.h"
#include "gfxstream/Compiler.h"
#include "gfxstream/host/testing/OSWindow.h"
#include "gl/EmulatedEglContext.h"
#include "gl/EmulatedEglFenceSync.h"

namespace gfxstream {

// Determines whether the host GPU should be used.
bool shouldUseHostGpu();

// Determines whether the test will use a Window.
bool shouldUseWindow();

// Creates or adjusts a persistent test window.
// On some systems, test window creation can fail (such as when on a headless server).
// In that case, this function will return nullptr.
OSWindow* createOrGetTestWindow(int xoffset, int yoffset, int width, int height);


class ColorBufferQueue;
// Creates a window (or runs headless) to be used in a sample app.
class SampleApplication {
public:
 SampleApplication(int windowWidth = 256, int windowHeight = 256, int refreshRate = 60,
                   gl::GLESApi glVersion = gl::GLESApi_3_0, bool compose = false);
 virtual ~SampleApplication();

 // A basic draw loop that works similar to most simple
 // GL apps that run on desktop.
 //
 // Per frame:
 //
 // a single GL context for drawing,
 // a color buffer to blit,
 // and a call to post that color buffer.
 void rebind();
 void drawLoop();

 // A more complex loop that uses 3 separate contexts
 // to simulate what goes on in Android:
 //
 // Per frame
 //
 // a GL 'app' context for drawing,
 // a SurfaceFlinger context for rendering the "Layer",
 // and a HWC context for posting.
 void surfaceFlingerComposerLoop();

 // TODO:
 // void HWC2Loop();

 // Just initialize, draw, and swap buffers once.
 void drawOnce();

 bool isSwANGLE();
private:
    void drawWorkerWithCompose(ColorBufferQueue& app2sfQueue, ColorBufferQueue& sf2appQueue);
    void drawWorker(ColorBufferQueue& app2sfQueue, ColorBufferQueue& sf2appQueue,
                ColorBufferQueue& sf2hwcQueue, ColorBufferQueue& hwc2sfQueue);
    gl::EmulatedEglFenceSync* getFenceSync();

   protected:
    virtual void initialize() = 0;
    virtual void draw() = 0;

    virtual const gl::GLESv2Dispatch* getGlDispatch();

    int mWidth = 256;
    int mHeight = 256;
    int mRefreshRate = 60;

    bool mUseSubWindow = false;
    OSWindow* mWindow = nullptr;
    FrameBuffer* mFb = nullptr;
    std::unique_ptr<RenderThreadInfo> mRenderThreadInfo = {};

    int mXOffset= 400;
    int mYOffset= 400;

    unsigned int mColorBuffer = 0;
    unsigned int mSurface = 0;
    unsigned int mContext = 0;

    bool mIsCompose = false;
    uint32_t mTargetCb = 0;

    DISALLOW_COPY_ASSIGN_AND_MOVE(SampleApplication);
};

}  // namespace gfxstream
