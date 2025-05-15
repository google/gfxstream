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

#include "SyncThread.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "OpenGLESDispatch/OpenGLDispatchLoader.h"
#endif

#include "gfxstream/Metrics.h"
#include "gfxstream/system/System.h"
#include "gfxstream/threads/Thread.h"
#include "gfxstream/host/Tracing.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/host/sync_device.h"

#ifndef _MSC_VER
#include <sys/time.h>
#endif
#include <memory>

namespace gfxstream {

using gfxstream::base::EventHangMetadata;

#if GFXSTREAM_ENABLE_HOST_GLES
using gl::EGLDispatch;
using gl::EmulatedEglFenceSync;
#endif

#define DEBUG 0

#if DEBUG

static uint64_t curr_ns() {
    auto now = std::chrono::system_clock::now();
    auto now_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(now);
    return now_ns.time_since_epoch().count();
}

#define DPRINT(fmt, ...)                                                             \
    do {                                                                             \
        GFXSTREAM_INFO("@ time=%llu, %s: " fmt, curr_ns(), __func__, ##__VA_ARGS__); \
    } while (0)

#else

#define DPRINT(...)

#endif

#define SYNC_THREAD_CHECK(condition)                                        \
    do {                                                                    \
        if (!(condition)) {                                                 \
            GFXSTREAM_FATAL(#condition " was false.");                      \
        }                                                                   \
    } while (0)

// The single global sync thread instance.
class GlobalSyncThread {
public:
    GlobalSyncThread() = default;

    void initialize(bool hasGl, HealthMonitor<>* healthMonitor) {
        AutoLock mutex(mLock);
        SYNC_THREAD_CHECK(!mSyncThread);
        mSyncThread = std::make_unique<SyncThread>(hasGl, healthMonitor);
    }
    SyncThread* syncThreadPtr() {
        AutoLock mutex(mLock);
        return mSyncThread.get();
    }

    void destroy() {
        AutoLock mutex(mLock);
        mSyncThread = nullptr;
    }

private:
    std::unique_ptr<SyncThread> mSyncThread = nullptr;
    // lock for the access to this object
    gfxstream::base::Lock mLock;
    using AutoLock = gfxstream::base::AutoLock;
};

static GlobalSyncThread* sGlobalSyncThread() {
    static GlobalSyncThread* t = new GlobalSyncThread;
    return t;
}

static const uint32_t kTimelineInterval = 1;
static const uint64_t kDefaultTimeoutNsecs = 5ULL * 1000ULL * 1000ULL * 1000ULL;

SyncThread::SyncThread(bool hasGl, HealthMonitor<>* healthMonitor)
    : gfxstream::base::Thread(gfxstream::base::ThreadFlags::MaskSignals, 512 * 1024),
      mWorkerThreadPool(kNumWorkerThreads,
                        [this](Command&& command, ThreadPool::WorkerId id) {
                            doSyncThreadCmd(std::move(command), id);
                        }),
      mHasGl(hasGl),
      mHealthMonitor(healthMonitor) {
    this->start();
    mWorkerThreadPool.start();
#if GFXSTREAM_ENABLE_HOST_GLES
    if (hasGl) {
        initSyncEGLContext();
    }
#endif
}

SyncThread::~SyncThread() {
    cleanup();
}

#if GFXSTREAM_ENABLE_HOST_GLES
void SyncThread::triggerWait(EmulatedEglFenceSync* fenceSync,
                             uint64_t timeline) {
    std::stringstream ss;
    ss << "triggerWait fenceSyncInfo=0x" << std::hex << reinterpret_cast<uintptr_t>(fenceSync)
       << " timeline=0x" << std::hex << timeline;
    sendAsync(
        [fenceSync, timeline, this](WorkerId) {
            doSyncWait(fenceSync, [timeline] {
                DPRINT("wait done (with fence), use goldfish sync timeline inc");
                gfxstream_sync_timeline_inc(timeline, kTimelineInterval);
            });
        },
        ss.str());
}

void SyncThread::triggerWaitWithCompletionCallback(EmulatedEglFenceSync* fenceSync, FenceCompletionCallback cb) {
    std::stringstream ss;
    ss << "triggerWaitWithCompletionCallback fenceSyncInfo=0x" << std::hex
       << reinterpret_cast<uintptr_t>(fenceSync);
    sendAsync(
        [fenceSync, cb = std::move(cb), this](WorkerId) { doSyncWait(fenceSync, std::move(cb)); },
        ss.str());
}

void SyncThread::initSyncEGLContext() {
    // b/383543476: force sequential init to work around a deadlock that is
    // believed to be a driver issue.
    std::mutex initMutex;
    mWorkerThreadPool.broadcast([&, this] {
        return Command{
            .mTask = std::packaged_task<int(WorkerId)>([&, this](WorkerId workerId) {
                DPRINT("for worker id: %d", workerId);
                std::lock_guard<std::mutex> initLock(initMutex);
                // We shouldn't initialize EGL context, when SyncThread is initialized
                // without GL enabled.
                SYNC_THREAD_CHECK(mHasGl);

                const EGLDispatch* egl = gl::LazyLoadedEGLDispatch::get();

                mDisplay = egl->eglGetDisplay(EGL_DEFAULT_DISPLAY);
                int eglMaj, eglMin;
                egl->eglInitialize(mDisplay, &eglMaj, &eglMin);

                const EGLint configAttribs[] = {
                    EGL_SURFACE_TYPE,
                    EGL_PBUFFER_BIT,
                    EGL_RENDERABLE_TYPE,
                    EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE,
                    8,
                    EGL_GREEN_SIZE,
                    8,
                    EGL_BLUE_SIZE,
                    8,
                    EGL_NONE,
                };

                EGLint nConfigs;
                EGLConfig config;

                egl->eglChooseConfig(mDisplay, configAttribs, &config, 1, &nConfigs);

                const EGLint pbufferAttribs[] = {
                    EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE,
                };

                mSurface[workerId] = egl->eglCreatePbufferSurface(mDisplay, config, pbufferAttribs);

                const EGLint contextAttribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
                mContext[workerId] =
                    egl->eglCreateContext(mDisplay, config, EGL_NO_CONTEXT, contextAttribs);

                egl->eglMakeCurrent(mDisplay, mSurface[workerId], mSurface[workerId],
                                    mContext[workerId]);
                return 0;
            }),
            .mDescription = "init sync EGL context",
        };
    });
    mWorkerThreadPool.waitAllItems();
}

void SyncThread::doSyncWait(EmulatedEglFenceSync* fenceSync, std::function<void()> onComplete) {
    DPRINT("enter");

    if (!EmulatedEglFenceSync::getFromHandle((uint64_t)(uintptr_t)fenceSync)) {
        if (onComplete) {
            onComplete();
        }
        return;
    }
    // We shouldn't use EmulatedEglFenceSync to wait, when SyncThread is initialized
    // without GL enabled, because EmulatedEglFenceSync uses EGL/GLES.
    SYNC_THREAD_CHECK(mHasGl);

    EGLint wait_result = 0x0;

    DPRINT("wait on sync obj: %p", fenceSync);
    wait_result = fenceSync->wait(kDefaultTimeoutNsecs);

    DPRINT(
        "done waiting, with wait result=0x%x. "
        "increment timeline (and signal fence)",
        wait_result);

    if (wait_result != EGL_CONDITION_SATISFIED_KHR) {
        EGLint error = gl::s_egl.eglGetError();
        DPRINT("error: eglClientWaitSync abnormal exit 0x%x. sync handle 0x%llx. egl error = %#x\n",
               wait_result, (unsigned long long)fenceSync, error);
        (void)error;
    }

    DPRINT("issue timeline increment");

    // We always unconditionally increment timeline at this point, even
    // if the call to eglClientWaitSync returned abnormally.
    // There are three cases to consider:
    // - EGL_CONDITION_SATISFIED_KHR: either the sync object is already
    //   signaled and we need to increment this timeline immediately, or
    //   we have waited until the object is signaled, and then
    //   we increment the timeline.
    // - EGL_TIMEOUT_EXPIRED_KHR: the fence command we put in earlier
    //   in the OpenGL stream is not actually ever signaled, and we
    //   end up blocking in the above eglClientWaitSyncKHR call until
    //   our timeout runs out. In this case, provided we have waited
    //   for |kDefaultTimeoutNsecs|, the guest will have received all
    //   relevant error messages about fence fd's not being signaled
    //   in time, so we are properly emulating bad behavior even if
    //   we now increment the timeline.
    // - EGL_FALSE (error): chances are, the underlying EGL implementation
    //   on the host doesn't actually support fence objects. In this case,
    //   we should fail safe: 1) It must be only very old or faulty
    //   graphics drivers / GPU's that don't support fence objects.
    //   2) The consequences of signaling too early are generally, out of
    //   order frames and scrambled textures in some apps. But, not
    //   incrementing the timeline means that the app's rendering freezes.
    //   So, despite the faulty GPU driver, not incrementing is too heavyweight a response.

    if (onComplete) {
        onComplete();
    }
    EmulatedEglFenceSync::incrementTimelineAndDeleteOldFences();

    DPRINT("done timeline increment");

    DPRINT("exit");
}

#endif

void SyncThread::triggerWaitVk(VkFence vkFence, uint64_t timeline) {
    std::stringstream ss;
    ss << "triggerWaitVk vkFence=0x" << std::hex << reinterpret_cast<uintptr_t>(vkFence)
       << " timeline=0x" << std::hex << timeline;
    sendAsync(
        [vkFence, timeline](WorkerId) {
            doSyncWaitVk(vkFence, [timeline] {
                DPRINT("vk wait done, use goldfish sync timeline inc");
                gfxstream_sync_timeline_inc(timeline, kTimelineInterval);
            });
        },
        ss.str());
}

void SyncThread::triggerWaitVkWithCompletionCallback(VkFence vkFence, FenceCompletionCallback cb) {
    std::stringstream ss;
    ss << "triggerWaitVkWithCompletionCallback vkFence=0x" << std::hex
       << reinterpret_cast<uintptr_t>(vkFence);
    sendAsync([vkFence, cb = std::move(cb)](WorkerId) { doSyncWaitVk(vkFence, std::move(cb)); },
              ss.str());
}

void SyncThread::triggerWaitVkQsriWithCompletionCallback(VkImage vkImage, FenceCompletionCallback cb) {
    std::stringstream ss;
    ss << "triggerWaitVkQsriWithCompletionCallback vkImage=0x"
       << reinterpret_cast<uintptr_t>(vkImage);
    sendAsync(
        [vkImage, cb = std::move(cb)](WorkerId) {
            auto decoder = vk::VkDecoderGlobalState::get();
            auto res = decoder->registerQsriCallback(vkImage, cb);
            // If registerQsriCallback does not schedule the callback, we still need to complete
            // the task, otherwise we may hit deadlocks on tasks on the same ring.
            if (!res.CallbackScheduledOrFired()) {
                cb();
            }
        },
        ss.str());
}

void SyncThread::triggerWaitVkQsri(VkImage vkImage, uint64_t timeline) {
     std::stringstream ss;
    ss << "triggerWaitVkQsri vkImage=0x" << std::hex << vkImage
       << " timeline=0x" << std::hex << timeline;
    sendAsync(
        [vkImage, timeline](WorkerId) {
            auto decoder = vk::VkDecoderGlobalState::get();
            auto res = decoder->registerQsriCallback(vkImage, [timeline](){
                 gfxstream_sync_timeline_inc(timeline, kTimelineInterval);
            });
            // If registerQsriCallback does not schedule the callback, we still need to complete
            // the task, otherwise we may hit deadlocks on tasks on the same ring.
            if (!res.CallbackScheduledOrFired()) {
                gfxstream_sync_timeline_inc(timeline, kTimelineInterval);
            }
        },
        ss.str());
}

void SyncThread::triggerGeneral(FenceCompletionCallback cb, std::string description) {
    std::stringstream ss;
    ss << "triggerGeneral: " << description;
    sendAsync(std::bind(std::move(cb)), ss.str());
}

void SyncThread::cleanup() {
    sendAndWaitForResult(
        [this](WorkerId workerId) {
#if GFXSTREAM_ENABLE_HOST_GLES
            if (mHasGl) {
                const EGLDispatch* egl = gl::LazyLoadedEGLDispatch::get();

                egl->eglMakeCurrent(mDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);

                egl->eglDestroyContext(mDisplay, mContext[workerId]);
                egl->eglDestroySurface(mDisplay, mSurface[workerId]);
                mContext[workerId] = EGL_NO_CONTEXT;
                mSurface[workerId] = EGL_NO_SURFACE;
            }
#endif
            return 0;
        },
        "cleanup");
    DPRINT("signal");
    mLock.lock();
    mExiting = true;
    mCv.signalAndUnlock(&mLock);
    DPRINT("exit");
    // Wait for the control thread to exit. We can't destroy the SyncThread
    // before we wait the control thread.
    if (!wait(nullptr)) {
        GFXSTREAM_ERROR("Fail to wait the control thread of the SyncThread to exit.");
    }
}

// Private methods below////////////////////////////////////////////////////////

intptr_t SyncThread::main() {
    DPRINT("in sync thread");
    mLock.lock();
    mCv.wait(&mLock, [this] { return mExiting; });
    mLock.unlock();

    mWorkerThreadPool.done();
    mWorkerThreadPool.join();
    DPRINT("exited sync thread");
    return 0;
}

int SyncThread::sendAndWaitForResult(std::function<int(WorkerId)> job, std::string description) {
    DPRINT("sendAndWaitForResult task(%s)", description.c_str());
    std::packaged_task<int(WorkerId)> task(std::move(job));
    std::future<int> resFuture = task.get_future();
    Command command = {
        .mTask = std::move(task),
        .mDescription = std::move(description),
    };

    mWorkerThreadPool.enqueue(std::move(command));
    auto res = resFuture.get();
    DPRINT("exit");
    return res;
}

void SyncThread::sendAsync(std::function<void(WorkerId)> job, std::string description) {
    DPRINT("send task(%s)", description.c_str());
    mWorkerThreadPool.enqueue(Command{
        .mTask =
            std::packaged_task<int(WorkerId)>([job = std::move(job)](WorkerId workerId) mutable {
                job(workerId);
                return 0;
            }),
        .mDescription = std::move(description),
    });
    DPRINT("exit");
}

void SyncThread::doSyncThreadCmd(Command&& command, WorkerId workerId) {
    static thread_local std::once_flag sOnceFlag;
    std::call_once(sOnceFlag, [&] {
        GFXSTREAM_TRACE_NAME_TRACK(GFXSTREAM_TRACE_TRACK_FOR_CURRENT_THREAD(), "SyncThread");
    });

    std::unique_ptr<std::unordered_map<std::string, std::string>> syncThreadData =
        std::make_unique<std::unordered_map<std::string, std::string>>();
    syncThreadData->insert({{"syncthread_cmd_desc", command.mDescription}});
    auto watchdog = WATCHDOG_BUILDER(mHealthMonitor, "SyncThread task execution")
                        .setHangType(EventHangMetadata::HangType::kSyncThread)
                        .setAnnotations(std::move(syncThreadData))
                        .build();
    command.mTask(workerId);
}

int SyncThread::doSyncWaitVk(VkFence vkFence, std::function<void()> onComplete) {
    DPRINT("enter");

    auto decoder = vk::VkDecoderGlobalState::get();
    auto result = decoder->waitForFence(vkFence, kDefaultTimeoutNsecs);
    if (result == VK_TIMEOUT) {
        DPRINT("SYNC_WAIT_VK timeout: vkFence=%p", vkFence);
    } else if (result != VK_SUCCESS) {
        DPRINT("SYNC_WAIT_VK error: %d vkFence=%p", result, vkFence);
    }

    DPRINT("issue timeline increment");

    // We always unconditionally increment timeline at this point, even
    // if the call to vkWaitForFences returned abnormally.
    // See comments in |doSyncWait| about the rationale.
    if (onComplete) {
        onComplete();
    }

    DPRINT("done timeline increment");

    DPRINT("exit");
    return result;
}

/* static */
SyncThread* SyncThread::get() {
    auto res = sGlobalSyncThread()->syncThreadPtr();
    SYNC_THREAD_CHECK(res);
    return res;
}

void SyncThread::initialize(bool hasGl, HealthMonitor<>* healthMonitor) {
    sGlobalSyncThread()->initialize(hasGl, healthMonitor);
}

void SyncThread::destroy() { sGlobalSyncThread()->destroy(); }

}  // namespace gfxstream
