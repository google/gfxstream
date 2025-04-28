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
#include "ReadbackWorkerGl.h"

#include <string.h>

#include "ColorBuffer.h"
#include "ContextHelper.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "gfxstream/host/logging.h"
#include "gl/ColorBufferGl.h"

namespace gfxstream {
namespace gl {

ReadbackWorkerGl::TrackedDisplay::TrackedDisplay(uint32_t displayId, uint32_t w, uint32_t h)
    : mBufferSize(4 * w * h /* RGBA8 (4 bpp) */),
      mBuffers(4 /* mailbox */,
               0),  // Note, last index is used for duplicating buffer on flush
      mDisplayId(displayId) {}

ReadbackWorkerGl::ReadbackWorkerGl(std::unique_ptr<DisplaySurfaceGl> surface,
                                   std::unique_ptr<DisplaySurfaceGl> flushSurface)
    : mSurface(std::move(surface)),
      mFlushSurface(std::move(flushSurface)) {}

void ReadbackWorkerGl::init() {
    if (!mFlushSurface->getContextHelper()->setupContext()) {
        GFXSTREAM_ERROR("Failed to make ReadbackWorkerGl flush surface current.");
    }
}

ReadbackWorkerGl::~ReadbackWorkerGl() {
    // Context not available on exit
}

void ReadbackWorkerGl::initReadbackForDisplay(uint32_t displayId, uint32_t w, uint32_t h) {
    android::base::AutoLock lock(mLock);

    auto [it, inserted] =  mTrackedDisplays.emplace(displayId, TrackedDisplay(displayId, w, h));
    if (!inserted) {
        GFXSTREAM_ERROR("Double init of TrackeDisplay for display:%d", displayId);
        return;
    }

    TrackedDisplay& display = it->second;

    s_gles2.glGenBuffers(display.mBuffers.size(), &display.mBuffers[0]);
    for (auto buffer : display.mBuffers) {
        s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, buffer);
        s_gles2.glBufferData(GL_PIXEL_PACK_BUFFER, display.mBufferSize, nullptr, GL_STREAM_READ);
    }
    s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

void ReadbackWorkerGl::deinitReadbackForDisplay(uint32_t displayId) {
    android::base::AutoLock lock(mLock);

    auto it = mTrackedDisplays.find(displayId);
    if (it == mTrackedDisplays.end()) {
        GFXSTREAM_ERROR("Double deinit of TrackedDisplay for display:%d", displayId);
        return;
    }

    TrackedDisplay& display = it->second;

    s_gles2.glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    s_gles2.glBindBuffer(GL_COPY_READ_BUFFER, 0);
    s_gles2.glDeleteBuffers(display.mBuffers.size(), &display.mBuffers[0]);

    mTrackedDisplays.erase(it);
}

ReadbackWorkerGl::DoNextReadbackResult
ReadbackWorkerGl::doNextReadback(uint32_t displayId,
                                 ColorBuffer* cb,
                                 void* fbImage,
                                 bool repaint,
                                 bool readbackBgra) {
    // if |repaint|, make sure that the current frame is immediately sent down
    // the pipeline and made available to the consumer by priming async
    // readback; doing 4 consecutive reads in a row, which should be enough to
    // fill the 3 buffers in the triple buffering setup and on the 4th, trigger
    // a post callback.
    int numIter = repaint ? 4 : 1;

    DoNextReadbackResult ret = DoNextReadbackResult::OK_NOT_READY_FOR_READ;

    // Mailbox-style triple buffering setup:
    // We want to avoid glReadPixels while in the middle of doing
    // memcpy to the consumer, but also want to avoid latency while
    // that is going on.
    //
    // There are 3 buffer ids, A, B, and C.
    // If we are not in the middle of copying out a frame,
    // set glReadPixels to write to buffer A and copy from buffer B in
    // alternation, so the consumer always gets the latest frame
    // +1 frame of lag in order to not cause blocking on
    // glReadPixels / glMapBufferRange.
    // If we are in the middle of copying out a frame, reset A and B to
    // not be the buffer being copied out, and continue glReadPixels to
    // buffer A and B as before.
    //
    // The resulting invariants are:
    // - glReadPixels is called on a different buffer every time
    //   so we avoid introducing sync points there.
    // - At no time are we mapping/copying a buffer and also doing
    //   glReadPixels on it (avoid simultaneous map + glReadPixels)
    // - glReadPixels and then immediately map/copy the same buffer
    //   doesn't happen either (avoid sync point in glMapBufferRange)
    for (int i = 0; i < numIter; i++) {
        android::base::AutoLock lock(mLock);
        TrackedDisplay& r = mTrackedDisplays[displayId];
        if (r.mIsCopying) {
            switch (r.mMapCopyIndex) {
                // To keep double buffering effect on
                // glReadPixels, need to keep even/oddness of
                // mReadPixelsIndexEven and mReadPixelsIndexOdd.
                case 0:
                    r.mReadPixelsIndexEven = 2;
                    r.mReadPixelsIndexOdd = 1;
                    break;
                case 1:
                    r.mReadPixelsIndexEven = 0;
                    r.mReadPixelsIndexOdd = 2;
                    break;
                case 2:
                    r.mReadPixelsIndexEven = 0;
                    r.mReadPixelsIndexOdd = 1;
                    break;
            }
        } else {
            r.mReadPixelsIndexEven = 0;
            r.mReadPixelsIndexOdd = 1;
            r.mMapCopyIndex = r.mPrevReadPixelsIndex;
        }

        // Double buffering on buffer A / B part
        uint32_t readAt;
        if (r.m_readbackCount % 2 == 0) {
            readAt = r.mReadPixelsIndexEven;
        } else {
            readAt = r.mReadPixelsIndexOdd;
        }
        r.m_readbackCount++;
        r.mPrevReadPixelsIndex = readAt;

        cb->glOpReadbackAsync(r.mBuffers[readAt], readbackBgra);

        // It's possible to post callback before any of the async readbacks
        // have written any data yet, which results in a black frame.  Safer
        // option to avoid this glitch is to wait until all 3 potential
        // buffers in our triple buffering setup have had chances to readback.
        lock.unlock();
        if (r.m_readbackCount > 3) {
            ret = DoNextReadbackResult::OK_READY_FOR_READ;
        }
    }

    return ret;
}

ReadbackWorkerGl::FlushResult ReadbackWorkerGl::flushPipeline(uint32_t displayId) {
    android::base::AutoLock lock(mLock);

    auto it = mTrackedDisplays.find(displayId);
    if (it == mTrackedDisplays.end()) {
        GFXSTREAM_ERROR("Failed to find TrackedDisplay for display:%d", displayId);
        return FlushResult::FAIL;
    }
    TrackedDisplay& display = it->second;

    if (display.mIsCopying) {
        // No need to make the last frame available,
        // we are currently being read.
        return FlushResult::OK_NOT_READY_FOR_READ;
    }

    auto src = display.mBuffers[display.mPrevReadPixelsIndex];
    auto srcSize = display.mBufferSize;
    auto dst = display.mBuffers.back();

    // This is not called from a renderthread, so let's activate the context.
    {
        RecursiveScopedContextBind contextBind(mSurface->getContextHelper());
        if (!contextBind.isOk()) {
            GFXSTREAM_ERROR("Failed to make ReadbackWorkerGl surface current, skipping flush.");
            return FlushResult::FAIL;
        }

        // We now copy the last frame into slot 4, where no other thread
        // ever writes.
        s_gles2.glBindBuffer(GL_COPY_READ_BUFFER, src);
        s_gles2.glBindBuffer(GL_COPY_WRITE_BUFFER, dst);
        s_gles2.glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, srcSize);
    }

    display.mMapCopyIndex = display.mBuffers.size() - 1;
    return FlushResult::OK_READY_FOR_READ;
}

void ReadbackWorkerGl::getPixels(uint32_t displayId, void* buf, uint32_t bytes) {
    android::base::AutoLock lock(mLock);

    auto it = mTrackedDisplays.find(displayId);
    if (it == mTrackedDisplays.end()) {
        GFXSTREAM_ERROR("Failed to find TrackedDisplay for display:%d", displayId);
        return;
    }
    TrackedDisplay& display = it->second;
    display.mIsCopying = true;
    lock.unlock();

    auto buffer = display.mBuffers[display.mMapCopyIndex];
    s_gles2.glBindBuffer(GL_COPY_READ_BUFFER, buffer);
    void* pixels = s_gles2.glMapBufferRange(GL_COPY_READ_BUFFER, 0, bytes, GL_MAP_READ_BIT);
    memcpy(buf, pixels, bytes);
    s_gles2.glUnmapBuffer(GL_COPY_READ_BUFFER);

    lock.lock();
    display.mIsCopying = false;
    lock.unlock();
}

}  // namespace gl
}  // namespace gfxstream
