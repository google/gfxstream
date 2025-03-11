// Copyright (C) 2016 The Android Open Source Project
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
#include "RendererImpl.h"

#include <assert.h>

#include <algorithm>
#include <utility>
#include <variant>

#include "FrameBuffer.h"
#include "GraphicsDriverLock.h"
#include "RenderChannelImpl.h"
#include "RenderThread.h"
#include "aemu/base/system/System.h"
#include "aemu/base/threads/WorkerThread.h"
#include "gfxstream/host/logging.h"
#include "snapshot/common.h"

#if GFXSTREAM_ENABLE_HOST_GLES
#include "gl/EmulatedEglFenceSync.h"
#endif

namespace gfxstream {

// kUseSubwindowThread is used to determine whether the RenderWindow should use
// a separate thread to manage its subwindow GL/GLES context.
// For now, this feature is disabled entirely for the following
// reasons:
//
// - It must be disabled on Windows at all times, otherwise the main window
//   becomes unresponsive after a few seconds of user interaction (e.g. trying
//   to move it over the desktop). Probably due to the subtle issues around
//   input on this platform (input-queue is global, message-queue is
//   per-thread). Also, this messes considerably the display of the
//   main window when running the executable under Wine.
//
// - On Linux/XGL and OSX/Cocoa, this used to be necessary to avoid corruption
//   issues with the GL state of the main window when using the SDL UI.
//   After the switch to Qt, this is no longer necessary and may actually cause
//   undesired interactions between the UI thread and the RenderWindow thread:
//   for example, in a multi-monitor setup the context might be recreated when
//   dragging the window between monitors, triggering a Qt-specific callback
//   in the context of RenderWindow thread, which will become blocked on the UI
//   thread, which may in turn be blocked on something else.
static const bool kUseSubwindowThread = false;

// This object manages the cleanup of guest process resources when the process
// exits. It runs the cleanup in a separate thread to never block the main
// render thread for a low-priority task.
class RendererImpl::ProcessCleanupThread {
public:
    ProcessCleanupThread()
        : mCleanupWorker([](Cmd cmd) {
            using android::base::WorkerProcessingResult;
            struct {
                WorkerProcessingResult operator()(CleanProcessResources resources) {
                    FrameBuffer::getFB()->cleanupProcGLObjects(resources.puid);
                    // resources.resource are destroyed automatically when going out of the scope.
                    return WorkerProcessingResult::Continue;
                }
                WorkerProcessingResult operator()(Exit) {
                    return WorkerProcessingResult::Stop;
                }
            } visitor;
            return std::visit(visitor, std::move(cmd));
          }) {
        mCleanupWorker.start();
    }

    ~ProcessCleanupThread() {
        mCleanupWorker.enqueue(Exit{});
    }

    void cleanup(uint64_t processId, std::unique_ptr<ProcessResources> resource) {
        mCleanupWorker.enqueue(CleanProcessResources{
            .puid = processId,
            .resource = std::move(resource),
        });
    }

    void stop() {
        mCleanupWorker.enqueue(Exit{});
        mCleanupWorker.join();
    }

    void waitForCleanup() {
        mCleanupWorker.waitQueuedItems();
    }

private:
    struct CleanProcessResources {
        uint64_t puid;
        std::unique_ptr<ProcessResources> resource;
    };
    struct Exit {};
    using Cmd = std::variant<CleanProcessResources, Exit>;
    DISALLOW_COPY_AND_ASSIGN(ProcessCleanupThread);

    android::base::WorkerThread<Cmd> mCleanupWorker;
};

RendererImpl::RendererImpl() {
    mCleanupThread.reset(new ProcessCleanupThread());
}

RendererImpl::~RendererImpl() {
    stop(true);
    // We can't finish until the loader render thread has
    // completed else can get a crash at the end of the destructor.
    if (mLoaderRenderThread) {
        mLoaderRenderThread->wait();
    }
    mRenderWindow.reset();
}

bool RendererImpl::initialize(int width, int height, const gfxstream::host::FeatureSet& features,
                              bool useSubWindow, bool egl2egl) {
#ifdef CONFIG_AEMU
    if (android::base::getEnvironmentVariable("ANDROID_EMUGL_VERBOSE") == "1") {
        set_gfxstream_enable_verbose_logs();
    }
    if (android::base::getEnvironmentVariable("ANDROID_EMUGL_LOG_COLORS") == "1") {
        set_gfxstream_enable_log_colors();
    }
#endif

    if (mRenderWindow) {
        return false;
    }

    std::unique_ptr<RenderWindow> renderWindow(new RenderWindow(
            width, height, features, kUseSubwindowThread, useSubWindow, egl2egl));
    if (!renderWindow) {
        GFXSTREAM_ERROR("Could not create rendering window class\n");
        GFXSTREAM_DEBUG("Could not create rendering window class");
        return false;
    }
    if (!renderWindow->isValid()) {
        GFXSTREAM_ERROR("Could not initialize emulated framebuffer\n");
        return false;
    }

    mRenderWindow = std::move(renderWindow);
    GFXSTREAM_DEBUG("OpenGL renderer initialized successfully");

    // This render thread won't do anything but will only preload resources
    // for the real threads to start faster.
    mLoaderRenderThread.reset(new RenderThread(nullptr));
    mLoaderRenderThread->start();

    return true;
}

void RendererImpl::stop(bool wait) {
    android::base::AutoLock lock(mChannelsLock);
    mStopped = true;
    auto channels = std::move(mChannels);
    lock.unlock();

    if (const auto fb = FrameBuffer::getFB()) {
        fb->setShuttingDown();
    }
    for (const auto& c : channels) {
        c->stopFromHost();
    }
    // We're stopping the renderer, so there's no need to clean up resources
    // of some pending processes: we'll destroy everything soon.
    mCleanupThread->stop();

    mStoppedChannels.insert(mStoppedChannels.end(),
                            std::make_move_iterator(channels.begin()),
                            std::make_move_iterator(channels.end()));

    // Each render channel is referenced in the corresponing pipe object, so
    // even if we clear the |channels| vector they could still be alive
    // for a while. This means we need to make sure to wait for render thread
    // exit explicitly.
    for (const auto& c : mStoppedChannels) {
        c->renderThread()->waitForFinished();
        {
            android::base::AutoLock driverLock(*graphicsDriverLock());
            c->renderThread()->sendExitSignal();
            c->renderThread()->wait();
        }
    }

    if (!wait) {
        return;
    }

    mCleanupThread->waitForCleanup();
    mStoppedChannels.clear();
}

void RendererImpl::finish() {
    {
        android::base::AutoLock lock(mChannelsLock);
        mRenderWindow->setPaused(true);
    }
    cleanupRenderThreads();
    {
        android::base::AutoLock lock(mChannelsLock);
        mRenderWindow->setPaused(false);
    }
}

void RendererImpl::cleanupRenderThreads() {
    android::base::AutoLock lock(mChannelsLock);
    const auto channels = std::move(mChannels);
    assert(mChannels.empty());
    lock.unlock();
    for (const auto& c : channels) {
        // Please DO NOT notify the guest about this event (DO NOT call
        // stopFromHost() ), because this is used to kill old threads when
        // loading from a snapshot, and the newly loaded guest should not
        // be notified for those behavior.
        c->stop();
    }
    for (const auto& c : channels) {
        c->renderThread()->waitForFinished();
        {
            android::base::AutoLock driverLock(*graphicsDriverLock());
            c->renderThread()->sendExitSignal();
            c->renderThread()->wait();
        }
    }
}

void RendererImpl::waitForProcessCleanup() {
    mCleanupThread->waitForCleanup();
    // Recreate it to make sure we've started from scratch and that we've
    // finished all in-progress cleanups as well.
    mCleanupThread.reset(new ProcessCleanupThread());
}

RenderChannelPtr RendererImpl::createRenderChannel(
        android::base::Stream* loadStream, uint32_t virtioGpuContextId) {
    const auto channel =
        std::make_shared<RenderChannelImpl>(loadStream, virtioGpuContextId);
    {
        android::base::AutoLock lock(mChannelsLock);

        if (mStopped) {
            return nullptr;
        }

        // Clean up the stopped channels.
        mChannels.erase(
                std::remove_if(mChannels.begin(), mChannels.end(),
                               [](const std::shared_ptr<RenderChannelImpl>& c) {
                                   return c->renderThread()->isFinished();
                               }),
                mChannels.end());
        mChannels.emplace_back(channel);

        // Take the time to check if our loader thread is done as well.
        if (mLoaderRenderThread && mLoaderRenderThread->isFinished()) {
            mLoaderRenderThread->wait();
            mLoaderRenderThread.reset();
        }

        GFXSTREAM_DEBUG("Started new RenderThread (total %" PRIu64 ") @%p",
                        static_cast<uint64_t>(mChannels.size()), channel->renderThread());
    }

    return channel;
}

void RendererImpl::addListener(FrameBufferChangeEventListener* listener) {
    mRenderWindow->addListener(listener);
}

void RendererImpl::removeListener(FrameBufferChangeEventListener* listener) {
    mRenderWindow->removeListener(listener);
}

void* RendererImpl::addressSpaceGraphicsConsumerCreate(
    struct asg_context context,
    android::base::Stream* loadStream,
    android::emulation::asg::ConsumerCallbacks callbacks,
    uint32_t contextId, uint32_t capsetId,
    std::optional<std::string> nameOpt) {
    auto thread = new RenderThread(context, loadStream, callbacks, contextId,
                                   capsetId, std::move(nameOpt));
    thread->start();
    android::base::AutoLock lock(mAddressSpaceRenderThreadLock);
    mAddressSpaceRenderThreads.emplace(thread);
    return (void*)thread;
}

void RendererImpl::addressSpaceGraphicsConsumerDestroy(void* consumer) {
    RenderThread* thread = (RenderThread*)consumer;
    {
        android::base::AutoLock lock(mAddressSpaceRenderThreadLock);
        mAddressSpaceRenderThreads.erase(thread);
    }

    thread->waitForFinished();
    {
        android::base::AutoLock driverLock(*graphicsDriverLock());
        thread->sendExitSignal();
        thread->wait();
    }
    delete thread;
}

void RendererImpl::addressSpaceGraphicsConsumerPreSave(void* consumer) {
    RenderThread* thread = (RenderThread*)consumer;
    thread->pausePreSnapshot();
}

void RendererImpl::addressSpaceGraphicsConsumerSave(void* consumer, android::base::Stream* stream) {
    RenderThread* thread = (RenderThread*)consumer;
    thread->save(stream);
}

void RendererImpl::addressSpaceGraphicsConsumerPostSave(void* consumer) {
    RenderThread* thread = (RenderThread*)consumer;
    thread->resume();
}

void RendererImpl::addressSpaceGraphicsConsumerRegisterPostLoadRenderThread(void* consumer) {
    RenderThread* thread = (RenderThread*)consumer;
    mAdditionalPostLoadRenderThreads.push_back(thread);
}

void RendererImpl::pauseAllPreSave() {
    {
        android::base::AutoLock lock(mChannelsLock);
        if (mStopped) {
            return;
        }
        for (const auto& c : mChannels) {
            c->renderThread()->pausePreSnapshot();
        }
    }
    {
        android::base::AutoLock lock(mAddressSpaceRenderThreadLock);
        for (const auto& thread : mAddressSpaceRenderThreads) {
            thread->pausePreSnapshot();
        }
    }
    waitForProcessCleanup();
}

void RendererImpl::resumeAll() {
    {
        android::base::AutoLock lock(mAddressSpaceRenderThreadLock);
        for (const auto t : mAdditionalPostLoadRenderThreads) {
            t->resume();
        }
    }
    {
        android::base::AutoLock lock(mChannelsLock);
        if (mStopped) {
            return;
        }
        for (const auto& c : mChannels) {
            c->renderThread()->resume();
        }
        for (const auto& thread : mAddressSpaceRenderThreads) {
            thread->resume();
        }
        mAdditionalPostLoadRenderThreads.clear();
    }

    repaintOpenGLDisplay();
}

void RendererImpl::save(android::base::Stream* stream,
                        const android::snapshot::ITextureSaverPtr& textureSaver) {
    stream->putByte(mStopped);
    if (mStopped) {
        return;
    }
    auto fb = FrameBuffer::getFB();
    assert(fb);
    fb->onSave(stream, textureSaver);
}

bool RendererImpl::load(android::base::Stream* stream,
                        const android::snapshot::ITextureLoaderPtr& textureLoader) {

#ifdef SNAPSHOT_PROFILE
    android::base::System::Duration startTime =
            android::base::System::get()->getUnixTimeUs();
#endif
    waitForProcessCleanup();
#ifdef SNAPSHOT_PROFILE
    printf("Previous session cleanup time: %lld ms\n",
           (long long)(android::base::System::get()
                               ->getUnixTimeUs() -
                       startTime) /
                   1000);
#endif

    mStopped = stream->getByte();
    if (mStopped) {
        return true;
    }
    auto fb = FrameBuffer::getFB();
    assert(fb);
    return fb->onLoad(stream, textureLoader);
}

void RendererImpl::fillGLESUsages(android_studio::EmulatorGLESUsages* usages) {
    auto fb = FrameBuffer::getFB();
#if GFXSTREAM_ENABLE_HOST_GLES
    if (fb) fb->fillGLESUsages(usages);
#endif
}

int RendererImpl::getScreenshot(unsigned int nChannels, unsigned int* width, unsigned int* height,
                                uint8_t* pixels, size_t* cPixels, int displayId = 0,
                                int desiredWidth = 0, int desiredHeight = 0,
                                int desiredRotation = 0, Rect rect = {{0, 0}, {0, 0}}) {
    auto fb = FrameBuffer::getFB();
    if (fb) {
        return fb->getScreenshot(nChannels, width, height, pixels, cPixels,
                                 displayId, desiredWidth, desiredHeight,
                                 desiredRotation, rect);
    }
    *cPixels = 0;
    return -1;
}

void RendererImpl::setMultiDisplay(uint32_t id,
                                   int32_t x,
                                   int32_t y,
                                   uint32_t w,
                                   uint32_t h,
                                   uint32_t dpi,
                                   bool add) {
    auto fb = FrameBuffer::getFB();
    if (fb) {
        if (add) {
            fb->createDisplay(&id);
            fb->setDisplayPose(id, x, y, w, h, dpi);
        } else {
            fb->destroyDisplay(id);
        }
    }
}

void RendererImpl::setMultiDisplayColorBuffer(uint32_t id, uint32_t cb) {
    auto fb = FrameBuffer::getFB();
    if (fb) {
        fb->setDisplayColorBuffer(id, cb);
    }
}

RendererImpl::HardwareStrings RendererImpl::getHardwareStrings() {
    assert(mRenderWindow);

    const char* vendor = nullptr;
    const char* renderer = nullptr;
    const char* version = nullptr;
    if (!mRenderWindow->getHardwareStrings(&vendor, &renderer, &version)) {
        return {};
    }
    HardwareStrings res;
    res.vendor = vendor ? vendor : "";
    res.renderer = renderer ? renderer : "";
    res.version = version ? version : "";
    return res;
}

void RendererImpl::setPostCallback(RendererImpl::OnPostCallback onPost,
                                   void* context,
                                   bool useBgraReadback,
                                   uint32_t displayId) {
    assert(mRenderWindow);
    mRenderWindow->setPostCallback(onPost, context, displayId, useBgraReadback);
}

bool RendererImpl::asyncReadbackSupported() {
    assert(mRenderWindow);
    return mRenderWindow->asyncReadbackSupported();
}

RendererImpl::ReadPixelsCallback
RendererImpl::getReadPixelsCallback() {
    assert(mRenderWindow);
    return mRenderWindow->getReadPixelsCallback();
}

RendererImpl::FlushReadPixelPipeline
RendererImpl::getFlushReadPixelPipeline() {
    assert(mRenderWindow);
    return mRenderWindow->getFlushReadPixelPipeline();
}

bool RendererImpl::showOpenGLSubwindow(FBNativeWindowType window,
                                       int wx,
                                       int wy,
                                       int ww,
                                       int wh,
                                       int fbw,
                                       int fbh,
                                       float dpr,
                                       float zRot,
                                       bool deleteExisting,
                                       bool hideWindow) {
    assert(mRenderWindow);
    return mRenderWindow->setupSubWindow(window, wx, wy, ww, wh, fbw, fbh, dpr,
                                         zRot, deleteExisting, hideWindow);
}

bool RendererImpl::destroyOpenGLSubwindow() {
    assert(mRenderWindow);
    return mRenderWindow->removeSubWindow();
}

void RendererImpl::setOpenGLDisplayRotation(float zRot) {
    assert(mRenderWindow);
    mRenderWindow->setRotation(zRot);
}

void RendererImpl::setOpenGLDisplayTranslation(float px, float py) {
    assert(mRenderWindow);
    mRenderWindow->setTranslation(px, py);
}

void RendererImpl::repaintOpenGLDisplay() {
    assert(mRenderWindow);
    mRenderWindow->repaint();
}

bool RendererImpl::hasGuestPostedAFrame() {
    if (mRenderWindow) {
        return mRenderWindow->hasGuestPostedAFrame();
    }
    return false;
}

void RendererImpl::resetGuestPostedAFrame() {
    if (mRenderWindow) {
        mRenderWindow->resetGuestPostedAFrame();
    }
}

void RendererImpl::setScreenMask(int width, int height, const unsigned char* rgbaData) {
    assert(mRenderWindow);
    mRenderWindow->setScreenMask(width, height, rgbaData);
}

void RendererImpl::onGuestGraphicsProcessCreate(uint64_t puid) {
    FrameBuffer::getFB()->createGraphicsProcessResources(puid);
}

void RendererImpl::cleanupProcGLObjects(uint64_t puid) {
    std::unique_ptr<ProcessResources> resource =
        FrameBuffer::getFB()->removeGraphicsProcessResources(puid);
    mCleanupThread->cleanup(puid, std::move(resource));
}

static struct AndroidVirtioGpuOps sVirtioGpuOps = {
    .create_buffer_with_handle =
        [](uint64_t size, uint32_t handle) {
            FrameBuffer::getFB()->createBufferWithResourceHandle(size, handle);
        },
    .create_color_buffer_with_handle =
        [](uint32_t width, uint32_t height, uint32_t format, uint32_t fwkFormat, uint32_t handle) {
            FrameBuffer::getFB()->createColorBufferWithResourceHandle(
                width, height, (GLenum)format, (FrameworkFormat)fwkFormat, handle);
        },
    .open_color_buffer = [](uint32_t handle) { FrameBuffer::getFB()->openColorBuffer(handle); },
    .close_buffer = [](uint32_t handle) { FrameBuffer::getFB()->closeBuffer(handle); },
    .close_color_buffer = [](uint32_t handle) { FrameBuffer::getFB()->closeColorBuffer(handle); },
    .update_buffer =
        [](uint32_t handle, uint64_t offset, uint64_t size, void* bytes) {
            FrameBuffer::getFB()->updateBuffer(handle, offset, size, bytes);
        },
    .update_color_buffer =
        [](uint32_t handle, int x, int y, int width, int height, uint32_t format, uint32_t type,
           void* pixels) {
            FrameBuffer::getFB()->updateColorBuffer(handle, x, y, width, height, format, type,
                                                    pixels);
        },
    .read_buffer =
        [](uint32_t handle, uint64_t offset, uint64_t size, void* bytes) {
            FrameBuffer::getFB()->readBuffer(handle, offset, size, bytes);
        },
    .read_color_buffer =
        [](uint32_t handle, int x, int y, int width, int height, uint32_t format, uint32_t type,
           void* pixels) {
            FrameBuffer::getFB()->readColorBuffer(handle, x, y, width, height, format, type,
                                                  pixels);
        },
    .read_color_buffer2 =
        [](uint32_t handle, int x, int y, int width, int height, uint32_t format, uint32_t type,
           void* pixels, uint64_t pixels_size) {
            FrameBuffer::getFB()->readColorBuffer(handle, x, y, width, height, format, type,
                                                  pixels, pixels_size);
        },
    .read_color_buffer_yuv =
        [](uint32_t handle, int x, int y, int width, int height, void* pixels,
           uint32_t pixels_size) {
            FrameBuffer::getFB()->readColorBufferYUV(handle, x, y, width, height, pixels,
                                                     pixels_size);
        },
    .post_color_buffer = [](uint32_t handle) { FrameBuffer::getFB()->post(handle); },
    .async_post_color_buffer =
        [](uint32_t handle, CpuCompletionCallback cb) {
            FrameBuffer::getFB()->postWithCallback(handle, cb);
        },
    .repost = []() { FrameBuffer::getFB()->repost(); },
#if GFXSTREAM_ENABLE_HOST_GLES
    .create_yuv_textures =
        [](uint32_t type, uint32_t count, int width, int height, uint32_t* output) {
            FrameBuffer::getFB()->createYUVTextures(type, count, width, height, output);
        },
    .destroy_yuv_textures =
        [](uint32_t type, uint32_t count, uint32_t* textures) {
            FrameBuffer::getFB()->destroyYUVTextures(type, count, textures);
        },
    .update_yuv_textures =
        [](uint32_t type, uint32_t* textures, void* privData, void* func) {
            FrameBuffer::getFB()->updateYUVTextures(type, textures, privData, func);
        },
    .swap_textures_and_update_color_buffer =
        [](uint32_t colorbufferhandle, int x, int y, int width, int height, uint32_t format,
           uint32_t type, uint32_t texture_type, uint32_t* textures, void* metadata) {
            FrameBuffer::getFB()->swapTexturesAndUpdateColorBuffer(
                colorbufferhandle, x, y, width, height, format, type, texture_type, textures);
        },
#endif
    .get_last_posted_color_buffer =
        []() { return FrameBuffer::getFB()->getLastPostedColorBuffer(); },
#if GFXSTREAM_ENABLE_HOST_GLES
    .bind_color_buffer_to_texture =
        [](uint32_t handle) { FrameBuffer::getFB()->bindColorBufferToTexture2(handle); },
    .get_global_egl_context = []() { return FrameBuffer::getFB()->getGlobalEGLContext(); },
#endif
    .set_guest_managed_color_buffer_lifetime =
        [](bool guestManaged) {
            FrameBuffer::getFB()->setGuestManagedColorBufferLifetime(guestManaged);
        },
#if GFXSTREAM_ENABLE_HOST_GLES
    .async_wait_for_gpu_with_cb =
        [](uint64_t eglsync, FenceCompletionCallback cb) {
            FrameBuffer::getFB()->asyncWaitForGpuWithCb(eglsync, cb);
        },
#endif
    .async_wait_for_gpu_vulkan_with_cb =
        [](uint64_t device, uint64_t fence, FenceCompletionCallback cb) {
            FrameBuffer::getFB()->asyncWaitForGpuVulkanWithCb(device, fence, cb);
        },
    .async_wait_for_gpu_vulkan_qsri_with_cb =
        [](uint64_t image, FenceCompletionCallback cb) {
            FrameBuffer::getFB()->asyncWaitForGpuVulkanQsriWithCb(image, cb);
        },
    .update_color_buffer_from_framework_format =
        [](uint32_t handle, int x, int y, int width, int height, uint32_t fwkFormat,
           uint32_t format, uint32_t type, void* pixels, void* pMetadata) {
            FrameBuffer::getFB()->updateColorBufferFromFrameworkFormat(
                handle, x, y, width, height, (FrameworkFormat)fwkFormat, format, type, pixels,
                pMetadata);
        },
};

struct AndroidVirtioGpuOps* RendererImpl::getVirtioGpuOps() {
    return &sVirtioGpuOps;
}

void RendererImpl::snapshotOperationCallback(int op, int stage) {
    using namespace android::snapshot;
    switch (op) {
        case SNAPSHOTTER_OPERATION_LOAD:
            if (stage == SNAPSHOTTER_STAGE_START) {
#ifdef SNAPSHOT_PROFILE
             android::base::System::Duration startTime =
                     android::base::System::get()->getUnixTimeUs();
#endif
                mRenderWindow->setPaused(true);
                cleanupRenderThreads();
#ifdef SNAPSHOT_PROFILE
                printf("Previous session suspend time: %lld ms\n",
                       (long long)(android::base::System::get()
                                           ->getUnixTimeUs() -
                                   startTime) /
                               1000);
#endif
            }
            if (stage == SNAPSHOTTER_STAGE_END) {
                mRenderWindow->setPaused(false);
            }
            break;
        default:
            break;
    }
}

void RendererImpl::setVsyncHz(int vsyncHz) {
    if (mRenderWindow) {
        mRenderWindow->setVsyncHz(vsyncHz);
    }
}

void RendererImpl::setDisplayConfigs(int configId, int w, int h,
                                     int dpiX, int dpiY) {
    if (mRenderWindow) {
        mRenderWindow->setDisplayConfigs(configId, w, h, dpiX, dpiY);
    }
}

void RendererImpl::setDisplayActiveConfig(int configId) {
    if (mRenderWindow) {
        mRenderWindow->setDisplayActiveConfig(configId);
    }
}

const void* RendererImpl::getEglDispatch() {
#if GFXSTREAM_ENABLE_HOST_GLES
    return FrameBuffer::getFB()->getEglDispatch();
#else
    return nullptr;
#endif
}

const void* RendererImpl::getGles2Dispatch() {
#if GFXSTREAM_ENABLE_HOST_GLES
    return FrameBuffer::getFB()->getGles2Dispatch();
#else
    return nullptr;
#endif
}

}  // namespace gfxstream
