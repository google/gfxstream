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

#include "display_surface_gl.h"

#include <stdio.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace host {
namespace gl {
namespace {

struct PreviousContextInfo {
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface readSurface = EGL_NO_SURFACE;
    EGLSurface drawSurface = EGL_NO_SURFACE;
};

// Permanently wraps a single (mDisplay, mSurface, mContext) triple and manages
// temporary scoped binds via setupContext() / teardownContext().
//
// This addresses two requirements (b/320319509, b/561516389):
// 1. Cross-thread safety: EGL only allows mContext/mSurface to be current on
//    ONE thread at a time (concurrent eglMakeCurrent on another thread fails
//    with EGL_BAD_ACCESS). mMutex serializes threads so a second thread waits
//    until the owning thread finishes its outermost teardownContext() and
//    unbinds mContext.
// 2. Same-thread recursion: Because mMutex is a std::recursive_mutex, the
//    owning thread can nest multiple setupContext() calls without deadlocking.
//    Each setupContext() pushes the active context onto this helper's
//    mPreviousContexts stack, and the original context is restored when the
//    outermost teardownContext() pops the last entry.
class DisplaySurfaceGlContextHelper : public ContextHelper {
  public:
    DisplaySurfaceGlContextHelper(EGLDisplay display,
                                  EGLSurface surface,
                                  EGLContext context)
        : mDisplay(display),
          mSurface(surface),
          mContext(context) {
        if (mDisplay == EGL_NO_DISPLAY) {
            GFXSTREAM_FATAL("DisplaySurfaceGlContextHelper created with no display?");
        }
        if (mSurface == EGL_NO_SURFACE) {
            GFXSTREAM_FATAL("DisplaySurfaceGlContextHelper created with no surface?");
        }
        if (mContext == EGL_NO_CONTEXT) {
            GFXSTREAM_FATAL("DisplaySurfaceGlContextHelper created with no context?");
        }
    }

    ~DisplaySurfaceGlContextHelper() override {
        const std::thread::id owner = mOwnerThread.load(std::memory_order_relaxed);
        if (owner != std::thread::id()) {
            GFXSTREAM_ERROR("DisplaySurfaceGlContextHelper context=%p destroyed while still bound.",
                            mContext);
            if (owner == std::this_thread::get_id()) {
                while (!mPreviousContexts.empty()) {
                    mPreviousContexts.pop_back();
                    mMutex.unlock();
                }
                releaseOwnership();
            }
        }
    }

    bool setupContext() override {
        mMutex.lock();
        mOwnerThread.store(std::this_thread::get_id(), std::memory_order_relaxed);

        EGLContext currentContext = s_egl.eglGetCurrentContext();
        EGLSurface currentDrawSurface = s_egl.eglGetCurrentSurface(EGL_DRAW);
        EGLSurface currentReadSurface = s_egl.eglGetCurrentSurface(EGL_READ);

        bool needsUpdate = (currentContext != mContext ||
                            currentDrawSurface != mSurface ||
                            currentReadSurface != mSurface);

        if (needsUpdate) {
            // If mPreviousContexts is non-empty, an outer setupContext() on this
            // thread already bound mContext, so currentContext != mContext means
            // some code in between switched the thread's EGL context behind this
            // helper's back. Log and re-bind mContext to recover.
            if (!mPreviousContexts.empty()) {
                GFXSTREAM_ERROR(
                    "DisplaySurfaceGlContextHelper context was preempted by others, "
                    "current=%p, needed=%p",
                    currentContext, mContext);
            }

            if (!s_egl.eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
                // b/284523053
                // Legacy swiftshader logspam on exit with this line.
                GFXSTREAM_DEBUG("Failed to make display surface context current: %d",
                                s_egl.eglGetError());
                if (mPreviousContexts.empty()) {
                    releaseOwnership();
                }
                mMutex.unlock();
                return false;
            }
        }

        mPreviousContexts.push_back(
            {.context = currentContext,
             .readSurface = currentReadSurface,
             .drawSurface = currentDrawSurface});
        return true;
    }

    void teardownContext() override {
        if (mOwnerThread.load(std::memory_order_relaxed) != std::this_thread::get_id()) {
            GFXSTREAM_ERROR(
                "DisplaySurfaceGlContextHelper context=%p teardown from a thread that did not "
                "bind it, ignoring.",
                mContext);
            return;
        }
        if (mPreviousContexts.empty()) {
            GFXSTREAM_ERROR(
                "DisplaySurfaceGlContextHelper context=%p teardown without a matching setup.",
                mContext);
            releaseOwnership();
            return;
        }

        const PreviousContextInfo previous = mPreviousContexts.back();
        mPreviousContexts.pop_back();
        // True only when unwinding the very first (outermost) setupContext()
        // call on this thread; false when unwinding a nested inner bind.
        const bool outermost = mPreviousContexts.empty();

        EGLContext currentContext = s_egl.eglGetCurrentContext();
        EGLSurface currentDrawSurface = s_egl.eglGetCurrentSurface(EGL_DRAW);
        EGLSurface currentReadSurface = s_egl.eglGetCurrentSurface(EGL_READ);

        bool needsUpdate = (currentContext != previous.context ||
                            currentDrawSurface != previous.drawSurface ||
                            currentReadSurface != previous.readSurface);

        if (needsUpdate) {
            if (!s_egl.eglMakeCurrent(mDisplay,
                                      previous.drawSurface,
                                      previous.readSurface,
                                      previous.context)) {
                GFXSTREAM_ERROR("Failed to restore previous context: %d", s_egl.eglGetError());
                if (outermost) {
                    // Ensure mContext/mSurface are not left stranded on this thread,
                    // otherwise subsequent threads will fail with EGL_BAD_ACCESS (12290).
                    s_egl.eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                }
            }
        }

        // Release ownership and unlock only after the previous context is restored
        // so another waiting thread can safely bind mContext.
        if (outermost) {
            releaseOwnership();
        }
        mMutex.unlock();
    }

    // Whether this helper is currently bound on the calling thread.
    bool isBound() const override {
        return mOwnerThread.load(std::memory_order_relaxed) == std::this_thread::get_id();
    }

  private:
    void releaseOwnership() { mOwnerThread.store(std::thread::id(), std::memory_order_relaxed); }

    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;

    // Serializes cross-thread access to mContext/mSurface while permitting
    // nested setupContext() calls on the owning thread.
    std::recursive_mutex mMutex;
    std::atomic<std::thread::id> mOwnerThread{};
    // Per-helper LIFO stack of saved contexts for nested binds on mOwnerThread.
    std::vector<PreviousContextInfo> mPreviousContexts;
};

}  // namespace

/*static*/
std::unique_ptr<DisplaySurfaceGl> DisplaySurfaceGl::createPbufferSurface(
        EGLDisplay display,
        EGLConfig config,
        EGLContext shareContext,
        const EGLint* contextAttribs,
        EGLint width,
        EGLint height) {
    EGLContext context = s_egl.eglCreateContext(display, config, shareContext, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        GFXSTREAM_ERROR("Failed to create context for DisplaySurfaceGl.");
        return nullptr;
    }

    const EGLint surfaceAttribs[] = {
        EGL_WIDTH, width,   //
        EGL_HEIGHT, height, //
        EGL_NONE,           //
    };
    EGLSurface surface = s_egl.eglCreatePbufferSurface(display, config, surfaceAttribs);
    if (surface == EGL_NO_SURFACE) {
        GFXSTREAM_ERROR("Failed to create pbuffer surface for DisplaySurfaceGl.");
        return nullptr;
    }

    return std::unique_ptr<DisplaySurfaceGl>(new DisplaySurfaceGl(display, surface, context));
}

/*static*/
std::unique_ptr<DisplaySurfaceGl> DisplaySurfaceGl::createWindowSurface(
        EGLDisplay display,
        EGLConfig config,
        EGLContext shareContext,
        const GLint* contextAttribs,
        FBNativeWindowType window) {
    EGLContext context = s_egl.eglCreateContext(display, config, shareContext, contextAttribs);
    if (context == EGL_NO_CONTEXT) {
        GFXSTREAM_ERROR("Failed to create context for DisplaySurfaceGl.");
        return nullptr;
    }

    EGLSurface surface = s_egl.eglCreateWindowSurface(display, config, window, nullptr);
    if (surface == EGL_NO_SURFACE) {
        GFXSTREAM_ERROR("Failed to create window surface for DisplaySurfaceGl.");
        return nullptr;
    }

    return std::unique_ptr<DisplaySurfaceGl>(new DisplaySurfaceGl(display, surface, context));
}

bool DisplaySurfaceGl::bindContext() const {
    if (!s_egl.eglMakeCurrent(mDisplay, mSurface, mSurface, mContext)) {
        GFXSTREAM_ERROR("Failed to make display surface context current: %d", s_egl.eglGetError());
        return false;
    }
    return true;
}

void DisplaySurfaceGl::unbindContext() const {
    if (mDisplay != EGL_NO_DISPLAY) {
        s_egl.eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
}

DisplaySurfaceGl::DisplaySurfaceGl(EGLDisplay display,
                                   EGLSurface surface,
                                   EGLContext context)
    : mDisplay(display),
      mSurface(surface),
      mContext(context),
      mContextHelper(new DisplaySurfaceGlContextHelper(display, surface, context)) {}

DisplaySurfaceGl::~DisplaySurfaceGl() {
    if (mDisplay != EGL_NO_DISPLAY) {
        if (mSurface) {
            s_egl.eglDestroySurface(mDisplay, mSurface);
        }
        if (mContext) {
            s_egl.eglDestroyContext(mDisplay, mContext);
        }
    }
}

}  // namespace gl
}  // namespace host
}  // namespace gfxstream