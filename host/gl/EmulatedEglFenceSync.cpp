/*
* Copyright (C) 2016 The Android Open Source Project
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
#include "EmulatedEglFenceSync.h"

#include <unordered_set>

#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "RenderThreadInfoGl.h"
#include "StalePtrRegistry.h"
#include "gfxstream/containers/Lookup.h"
#include "gfxstream/containers/StaticMap.h"
#include "aemu/base/files/StreamSerializing.h"
#include "gfxstream/synchronization/Lock.h"

namespace gfxstream {
namespace gl {
namespace {

using gfxstream::base::AutoLock;
using gfxstream::base::Lock;
using gfxstream::base::StaticMap;

// Timeline class is meant to delete native fences after the
// sync device has incremented the timeline.  We assume a
// maximum number of outstanding timelines in the guest (16) in
// order to derive when a native fence is definitely safe to
// delete. After at least that many timeline increments have
// happened, we sweep away the remaining native fences.
// The function that performs the deleting,
// incrementTimelineAndDeleteOldFences(), happens on the SyncThread.

class Timeline {
  public:
    Timeline() = default;

    static constexpr int kMaxGuestTimelines = 16;
    void addFence(EmulatedEglFenceSync* fence) {
        mFences.set(fence, mTime.load() + kMaxGuestTimelines);
    }

    void incrementTimelineAndDeleteOldFences() {
        ++mTime;
        sweep();
    }

    void sweep() {
        mFences.eraseIf([time = mTime.load()](EmulatedEglFenceSync* fence, int fenceTime) {
            EmulatedEglFenceSync* actual = EmulatedEglFenceSync::getFromHandle((uint64_t)(uintptr_t)fence);
            if (!actual) return true;

            bool shouldErase = fenceTime <= time;
            if (shouldErase) {
                if (!actual->decRef() &&
                    actual->shouldDestroyWhenSignaled()) {
                    actual->decRef();
                }
            }
            return shouldErase;
        });
    }

  private:
    std::atomic<int> mTime {0};
    StaticMap<EmulatedEglFenceSync*, int> mFences;
};

static Timeline* sTimeline() {
    static Timeline* t = new Timeline;
    return t;
}

}  // namespace

// static
void EmulatedEglFenceSync::incrementTimelineAndDeleteOldFences() {
    sTimeline()->incrementTimelineAndDeleteOldFences();
}

// static
std::unique_ptr<EmulatedEglFenceSync> EmulatedEglFenceSync::create(
        EGLDisplay display,
        bool hasNativeFence,
        bool destroyWhenSignaled) {
    auto sync = s_egl.eglCreateSyncKHR(display, EGL_SYNC_FENCE_KHR, nullptr);
    if (sync == EGL_NO_SYNC_KHR) {
        GFXSTREAM_ERROR("Failed to create EGL fence sync: %d", s_egl.eglGetError());
        return nullptr;
    }

    // This MUST be present, or we get a deadlock effect.
    s_gles2.glFlush();

    return std::unique_ptr<EmulatedEglFenceSync>(
        new EmulatedEglFenceSync(display,
                                 sync,
                                 hasNativeFence,
                                 destroyWhenSignaled));
}

EmulatedEglFenceSync::EmulatedEglFenceSync(EGLDisplay display,
                                           EGLSyncKHR sync,
                                           bool hasNativeFence,
                                           bool destroyWhenSignaled)
    : mDestroyWhenSignaled(destroyWhenSignaled),
      mDisplay(display),
      mSync(sync) {

    addToRegistry();

    assert(mCount == 1);
    if (hasNativeFence) {
        incRef();
        sTimeline()->addFence(this);
    }

    // Assumes that there is a valid + current OpenGL context
    assert(RenderThreadInfoGl::get());
}

EmulatedEglFenceSync::~EmulatedEglFenceSync() {
    removeFromRegistry();
}

EGLint EmulatedEglFenceSync::wait(uint64_t timeout) {
    incRef();
    EGLint wait_res =
        s_egl.eglClientWaitSyncKHR(mDisplay, mSync,
                                   EGL_SYNC_FLUSH_COMMANDS_BIT_KHR,
                                   timeout);
    decRef();
    return wait_res;
}

void EmulatedEglFenceSync::waitAsync() {
    s_egl.eglWaitSyncKHR(mDisplay, mSync, 0);
}

bool EmulatedEglFenceSync::isSignaled() {
    EGLint val;
    if (EGL_TRUE ==
            s_egl.eglGetSyncAttribKHR(
                mDisplay, mSync, EGL_SYNC_STATUS_KHR, &val))
        return val == EGL_SIGNALED_KHR;

    return true; // if invalid, treat as signaled
}

void EmulatedEglFenceSync::destroy() {
    s_egl.eglDestroySyncKHR(mDisplay, mSync);
}

// Snapshots for EmulatedEglFenceSync//////////////////////////////////////////////////////
// It's possible, though it does not happen often, that a fence
// can be created but not yet waited on by the guest, which
// needs careful handling:
//
// 1. Avoid manipulating garbage memory on snapshot restore;
// rcCreateSyncKHR *creates new fence in valid memory*
// --snapshot--
// rcClientWaitSyncKHR *refers to uninitialized memory*
// rcDestroySyncKHR *refers to uninitialized memory*
// 2. Make rcCreateSyncKHR/rcDestroySyncKHR implementations return
// the "signaled" status if referring to previous snapshot fences. It's
// assumed that the GPU is long done with them.
// 3. Avoid name collisions where a new EmulatedEglFenceSync object is created
// that has the same uint64_t casting as a EmulatedEglFenceSync object from a previous
// snapshot.

// Maintain a StalePtrRegistry<EmulatedEglFenceSync>:
static StalePtrRegistry<EmulatedEglFenceSync>* sFenceRegistry() {
    static StalePtrRegistry<EmulatedEglFenceSync>* s = new StalePtrRegistry<EmulatedEglFenceSync>;
    return s;
}

// static
void EmulatedEglFenceSync::addToRegistry() {
    sFenceRegistry()->addPtr(this);
}

// static
void EmulatedEglFenceSync::removeFromRegistry() {
    sFenceRegistry()->removePtr(this);
}

// static
void EmulatedEglFenceSync::onSave(android::base::Stream* stream) {
    sFenceRegistry()->makeCurrentPtrsStale();
    sFenceRegistry()->onSave(stream);
}

// static
void EmulatedEglFenceSync::onLoad(android::base::Stream* stream) {
    sFenceRegistry()->onLoad(stream);
}

// static
EmulatedEglFenceSync* EmulatedEglFenceSync::getFromHandle(uint64_t handle) {
    return sFenceRegistry()->getPtr(handle);
}

}  // namespace gl
}  // namespace gfxstream
