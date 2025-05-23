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

namespace gfxstream {

class ColorBuffer;

// This class implements async readback of ColorBuffers on both the FrameBuffer
// posting thread and a separate worker thread.
class ReadbackWorker {
  public:
    virtual ~ReadbackWorker() = default;

    virtual void init() = 0;

    // Enables readback of the given display.
    virtual void initReadbackForDisplay(uint32_t displayId, uint32_t w, uint32_t h) = 0;

    // Enables readback of the given display.
    virtual void deinitReadbackForDisplay(uint32_t displayId) = 0;

    enum class DoNextReadbackResult {
      OK_NOT_READY_FOR_READ,

      OK_READY_FOR_READ,
    };

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
    virtual DoNextReadbackResult doNextReadback(uint32_t displayId,
                                                ColorBuffer* cb,
                                                void* fbImage,
                                                bool repaint,
                                                bool readbackBgra) = 0;

    // Retrieves the latest framebuffer that has been posted and read with
    // doNextReadback.  This is meant for apps like video encoding to use as
    // input; they will need to do synchronized communication with the thread
    // ReadbackWorker is running on.
    virtual void getPixels(uint32_t displayId, void* out, uint32_t bytes) = 0;

    // Duplicates the last frame and generates a post events if
    // there are no read events active.
    // This is usually called when there was no doNextReadback activity
    // for a few ms, to guarantee that end users see the final frame.
    enum class FlushResult {
      FAIL,

      OK_NOT_READY_FOR_READ,

      OK_READY_FOR_READ,
    };

    virtual FlushResult flushPipeline(uint32_t displayId) = 0;
};

}  // namespace gfxstream
