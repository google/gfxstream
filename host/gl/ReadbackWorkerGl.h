/*
* Copyright (C) 2017 The Android Open Source Project
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

#include <map>
#include <stdint.h>
#include <vector>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "gfxstream/Compiler.h"
#include "gfxstream/synchronization/Lock.h"
#include "DisplaySurfaceGl.h"
#include "ReadbackWorker.h"

namespace gfxstream {
class ColorBuffer;
}  // namespace gfxstream

namespace gfxstream {
namespace gl {

// This class implements async readback of emugl ColorBuffers.
// It is meant to run on both the emugl framebuffer posting thread
// and a separate GL thread, with two main points of interaction:
class ReadbackWorkerGl : public ReadbackWorker {
  public:
    ReadbackWorkerGl(std::unique_ptr<DisplaySurfaceGl> surface,
                     std::unique_ptr<DisplaySurfaceGl> flushSurface);

    ~ReadbackWorkerGl();

    // GL initialization (must be on the thread that
    // will run getPixels)
    void init() override;

    // doNextReadback(): Call this from the emugl FrameBuffer::post thread
    // or similar rendering thread.
    // This will trigger an async glReadPixels of the current framebuffer.
    // The post callback of Framebuffer will also be triggered, but
    // in async mode it should do minimal work that involves |fbImage|.
    // |repaint|: flag to prime async readback with multiple iterations
    // so that the consumer of readback doesn't lag behind.
    // |readbackBgra|: Whether to force the readback format as GL_BGRA_EXT,
    // so that we get (depending on driver quality, heh) a gpu conversion of the
    // readback image that is suitable for webrtc, which expects formats like that.
    DoNextReadbackResult doNextReadback(uint32_t displayId,
                                        ColorBuffer* cb,
                                        void* fbImage,
                                        bool repaint,
                                        bool readbackBgra) override;

    // getPixels(): Run this on a separate GL thread. This retrieves the
    // latest framebuffer that has been posted and read with doNextReadback.
    // This is meant for apps like video encoding to use as input; they will
    // need to do synchronized communication with the thread ReadbackWorker
    // is running on.
    void getPixels(uint32_t displayId, void* out, uint32_t bytes) override;

    // Duplicates the last frame and generates a post events if
    // there are no read events active.
    // This is usually called when there was no doNextReadback activity
    // for a few ms, to guarantee that end users see the final frame.
    FlushResult flushPipeline(uint32_t displayId) override;

    void initReadbackForDisplay(uint32_t displayId, uint32_t w, uint32_t h) override;

    void deinitReadbackForDisplay(uint32_t displayId) override;

    class TrackedDisplay {
      public:
        TrackedDisplay() = default;
        TrackedDisplay(uint32_t displayId, uint32_t w, uint32_t h);

        uint32_t mReadPixelsIndexEven = 0;
        uint32_t mReadPixelsIndexOdd = 1;
        uint32_t mPrevReadPixelsIndex = 1;
        uint32_t mMapCopyIndex = 0;
        bool mIsCopying = false;
        uint32_t mBufferSize = 0;
        std::vector<GLuint> mBuffers = {};
        uint32_t m_readbackCount = 0;
        uint32_t mDisplayId = 0;
    };

  private:
    gfxstream::base::Lock mLock;

    std::unique_ptr<DisplaySurfaceGl> mSurface;
    std::unique_ptr<DisplaySurfaceGl> mFlushSurface;

    std::map<uint32_t, TrackedDisplay> mTrackedDisplays;

    DISALLOW_COPY_AND_ASSIGN(ReadbackWorkerGl);
};

}  // namespace gl
}  // namespace gfxstream
