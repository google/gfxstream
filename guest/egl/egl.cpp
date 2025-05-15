/*
* Copyright (C) 2011 The Android Open Source Project
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

#include <GLES3/gl31.h>
#include <assert.h>
#include <cutils/properties.h>
#include <cutils/trace.h>
#include <poll.h>
#include <qemu_pipe_bp.h>
#include <time.h>
#include <xf86drm.h>

#include <atomic>

#include "ClientAPIExts.h"
#include "EGLImage.h"
#include "GL2Encoder.h"
#include "GLEncoder.h"
#include "HostConnection.h"
#include "ProcessPipe.h"
#include "ThreadInfo.h"
#include "VirtGpu.h"
#include "aemu/base/Tracing.h"
#include "aemu/base/threads/AndroidThread.h"
#include "eglContext.h"
#include "eglDisplay.h"
#include "eglSync.h"
#include "egl_ftable.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/guest/GLClientState.h"
#include "gfxstream/guest/GLSharedGroup.h"
#include "gfxstream/guest/goldfish_sync.h"

using gfxstream::guest::GLClientState;
using gfxstream::guest::getCurrentThreadId;

#define DEBUG_EGL 0
#if DEBUG_EGL
#define DPRINT(fmt,...) GFXSTREAM_DEBUG(fmt, ##__VA_ARGS__);
#else
#define DPRINT(...)
#endif

template<typename T>
static T setErrorFunc(GLint error, T returnValue) {
    getEGLThreadInfo()->eglError = error;
    return returnValue;
}

const char *  eglStrError(EGLint err)
{
    switch (err){
        case EGL_SUCCESS:           return "EGL_SUCCESS";
        case EGL_NOT_INITIALIZED:   return "EGL_NOT_INITIALIZED";
        case EGL_BAD_ACCESS:        return "EGL_BAD_ACCESS";
        case EGL_BAD_ALLOC:         return "EGL_BAD_ALLOC";
        case EGL_BAD_ATTRIBUTE:     return "EGL_BAD_ATTRIBUTE";
        case EGL_BAD_CONFIG:        return "EGL_BAD_CONFIG";
        case EGL_BAD_CONTEXT:       return "EGL_BAD_CONTEXT";
        case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
        case EGL_BAD_DISPLAY:       return "EGL_BAD_DISPLAY";
        case EGL_BAD_MATCH:         return "EGL_BAD_MATCH";
        case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
        case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
        case EGL_BAD_PARAMETER:     return "EGL_BAD_PARAMETER";
        case EGL_BAD_SURFACE:       return "EGL_BAD_SURFACE";
        case EGL_CONTEXT_LOST:      return "EGL_CONTEXT_LOST";
        default: return "UNKNOWN";
    }
}

#define setErrorReturn(error, retVal)                                                                 \
    {                                                                                                 \
        GFXSTREAM_ERROR("tid %lu: error 0x%x (%s)", getCurrentThreadId(), error, eglStrError(error)); \
        return setErrorFunc(error, retVal);                                                           \
    }

#define RETURN_ERROR(ret, err)                                                                        \
    GFXSTREAM_ERROR("tid %lu: error 0x%x (%s)", getCurrentThreadId(), err, eglStrError(err));         \
    getEGLThreadInfo()->eglError = err;                                                               \
    return ret;


#define VALIDATE_CONFIG(cfg,ret) \
    if (!s_display.isValidConfig(cfg)) { \
        RETURN_ERROR(ret,EGL_BAD_CONFIG); \
    }

#define VALIDATE_DISPLAY(dpy,ret) \
    if ((dpy) != (EGLDisplay)&s_display) { \
        RETURN_ERROR(ret, EGL_BAD_DISPLAY);    \
    }

#define VALIDATE_DISPLAY_INIT(dpy,ret) \
    VALIDATE_DISPLAY(dpy, ret)    \
    if (!s_display.initialized()) {        \
        RETURN_ERROR(ret, EGL_NOT_INITIALIZED);    \
    }

#define DEFINE_HOST_CONNECTION \
    HostConnection *hostCon = HostConnection::get(); \
    ExtendedRCEncoderContext *rcEnc = (hostCon ? hostCon->rcEncoder() : NULL)

#define DEFINE_AND_VALIDATE_HOST_CONNECTION(ret)                     \
    HostConnection* hostCon = HostConnection::get();                 \
    if (!hostCon) {                                                  \
        GFXSTREAM_ERROR("egl: Failed to get host connection\n");               \
        return ret;                                                  \
    }                                                                \
    ExtendedRCEncoderContext* rcEnc = hostCon->rcEncoder();          \
    if (!rcEnc) {                                                    \
        GFXSTREAM_ERROR("egl: Failed to get renderControl encoder context\n"); \
        return ret;                                                  \
    }                                                                \
    auto* grallocHelper = hostCon->grallocHelper();                  \
    if (!grallocHelper) {                                            \
        GFXSTREAM_ERROR("egl: Failed to get grallocHelper\n");                 \
        return ret;                                                  \
    }                                                                \
    auto* anwHelper = hostCon->anwHelper();                          \
    if (!anwHelper) {                                                \
        GFXSTREAM_ERROR("egl: Failed to get anwHelper\n");                     \
        return ret;                                                  \
    }

#define DEFINE_AND_VALIDATE_HOST_CONNECTION_FOR_TLS(ret, tls)                      \
    HostConnection* hostCon = HostConnection::getWithThreadInfo(tls, kCapsetNone); \
    if (!hostCon) {                                                                \
        GFXSTREAM_ERROR("egl: Failed to get host connection\n");                             \
        return ret;                                                                \
    }                                                                              \
    ExtendedRCEncoderContext* rcEnc = hostCon->rcEncoder();                        \
    if (!rcEnc) {                                                                  \
        GFXSTREAM_ERROR("egl: Failed to get renderControl encoder context\n");               \
        return ret;                                                                \
    }                                                                              \
    auto const* grallocHelper = hostCon->grallocHelper();                          \
    if (!grallocHelper) {                                                          \
        GFXSTREAM_ERROR("egl: Failed to get grallocHelper\n");                               \
        return ret;                                                                \
    }                                                                              \
    auto* anwHelper = hostCon->anwHelper();                                        \
    if (!anwHelper) {                                                              \
        GFXSTREAM_ERROR("egl: Failed to get anwHelper\n");                                   \
        return ret;                                                                \
    }

#define VALIDATE_CONTEXT_RETURN(context,ret)  \
    if (!(context) || !s_display.isContext((context))) {                         \
        RETURN_ERROR(ret,EGL_BAD_CONTEXT);    \
    }

#define VALIDATE_SURFACE_RETURN(surface, ret)    \
    if ((surface) != EGL_NO_SURFACE) {    \
        if (!s_display.isSurface((surface))) \
            setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE); \
        egl_surface_t* s( static_cast<egl_surface_t*>(surface) );    \
        if (s->dpy != (EGLDisplay)&s_display)    \
            setErrorReturn(EGL_BAD_DISPLAY, EGL_FALSE);    \
    }

// The one and only supported display object.
static eglDisplay s_display;

EGLContext_t::EGLContext_t(EGLDisplay dpy, EGLConfig config, EGLContext_t* shareCtx, int maj, int min) :
    dpy(dpy),
    config(config),
    read(EGL_NO_SURFACE),
    draw(EGL_NO_SURFACE),
    dummy_surface(EGL_NO_SURFACE),
    shareCtx(shareCtx),
    rcContext(0),
    versionString(NULL),
    majorVersion(maj),
    minorVersion(min),
    vendorString(NULL) ,
    rendererString(NULL),
    shaderVersionString(NULL),
    extensionString(NULL),
    deletePending(0),
    goldfishSyncFd(-1)
{

    DEFINE_HOST_CONNECTION;
    switch (rcEnc->getGLESMaxVersion()) {
        case GLES_MAX_VERSION_3_0:
            deviceMajorVersion = 3;
            deviceMinorVersion = 0;
            break;
        case GLES_MAX_VERSION_3_1:
            deviceMajorVersion = 3;
            deviceMinorVersion = 1;
            break;
        case GLES_MAX_VERSION_3_2:
            deviceMajorVersion = 3;
            deviceMinorVersion = 2;
            break;
        default:
            deviceMajorVersion = 2;
            deviceMinorVersion = 0;
            break;
    }

    flags = 0;
    clientState = new GLClientState(majorVersion, minorVersion);
     if (shareCtx)
        sharedGroup = shareCtx->getSharedGroup();
    else
        sharedGroup = GLSharedGroupPtr(new GLSharedGroup());
    assert(dpy == (EGLDisplay)&s_display);
    s_display.onCreateContext((EGLContext)this);
};

int EGLContext_t::getGoldfishSyncFd() {
    if (goldfishSyncFd < 0) {
        goldfishSyncFd = goldfish_sync_open();
    }
    return goldfishSyncFd;
}

EGLContext_t::~EGLContext_t()
{
    if (goldfishSyncFd > 0) {
        goldfish_sync_close(goldfishSyncFd);
        goldfishSyncFd = -1;
    }
    assert(dpy == (EGLDisplay)&s_display);
    s_display.onDestroyContext((EGLContext)this);
    delete clientState;
    delete [] versionString;
    delete [] vendorString;
    delete [] rendererString;
    delete [] shaderVersionString;
    delete [] extensionString;
}

uint64_t currGuestTimeNs() {
    struct timespec ts;
#ifdef __APPLE__
    clock_gettime(CLOCK_REALTIME, &ts);
#else
    clock_gettime(CLOCK_BOOTTIME, &ts);
#endif
    uint64_t res = (uint64_t)(ts.tv_sec * 1000000000ULL + ts.tv_nsec);
    return res;
}

struct app_time_metric_t {
    uint64_t lastLogTime;
    uint64_t lastSwapBuffersReturnTime;
    unsigned int numSamples;
    uint64_t totalAppTime;
    uint64_t minAppTime;
    uint64_t maxAppTime;

    app_time_metric_t() :
        lastLogTime(0),
        lastSwapBuffersReturnTime(0),
        numSamples(0),
        totalAppTime(0),
        minAppTime(0),
        maxAppTime(0)
    {
    }

    void onSwapBuffersReturn() {
        lastSwapBuffersReturnTime = currGuestTimeNs();
    }

    static float ns2ms(uint64_t ns) {
        return (float)ns / 1000000.0;
    }

    void onQueueBufferReturn() {
        if(lastSwapBuffersReturnTime == 0) {
            // First swapBuffers call, or last call failed.
            return;
        }

        uint64_t now = currGuestTimeNs();
        uint64_t appTime = now - lastSwapBuffersReturnTime;
        if(numSamples == 0) {
          minAppTime = appTime;
          maxAppTime = appTime;
        }
        else {
          minAppTime = fmin(minAppTime, appTime);
          maxAppTime = fmax(maxAppTime, appTime);
        }
        totalAppTime += appTime;
        numSamples++;
        // Reset so we don't record a bad sample if swapBuffers fails
        lastSwapBuffersReturnTime = 0;

        if(lastLogTime == 0) {
            lastLogTime = now;
            return;
        }

        // Log/reset once every second
        if(now - lastLogTime > 1000000000) {
            float avgMs = ns2ms(totalAppTime) / numSamples;
            float minMs = ns2ms(minAppTime);
            float maxMs = ns2ms(maxAppTime);
            totalAppTime = 0;
            minAppTime = 0;
            maxAppTime = 0;
            numSamples = 0;
            lastLogTime = now;
        }
    }
};

// ----------------------------------------------------------------------------
//egl_surface_t

//we don't need to handle depth since it's handled when window created on the host

struct egl_surface_t {

    EGLDisplay          dpy;
    EGLConfig           config;


    egl_surface_t(EGLDisplay dpy, EGLConfig config, EGLint surfaceType);
    virtual     ~egl_surface_t();

    virtual     void        setSwapInterval(int interval) = 0;
    virtual     EGLBoolean  swapBuffers() = 0;

    EGLint      getSwapBehavior() const;
    uint32_t    getRcSurface()   { return rcSurface; }
    EGLint      getSurfaceType() { return surfaceType; }

    EGLint      getWidth(){ return width; }
    EGLint      getHeight(){ return height; }
    EGLint      getNativeWidth(){ return nativeWidth; }
    EGLint      getNativeHeight(){ return nativeHeight; }
    void        setTextureFormat(EGLint _texFormat) { texFormat = _texFormat; }
    EGLint      getTextureFormat() { return texFormat; }
    void        setTextureTarget(EGLint _texTarget) { texTarget = _texTarget; }
    EGLint      getTextureTarget() { return texTarget; }

    virtual     void setCollectingTimestamps(EGLint) { }
    virtual     EGLint isCollectingTimestamps() const { return EGL_FALSE; }
    EGLint      deletePending;
    void        setIsCurrent(bool isCurrent) { mIsCurrent = isCurrent; }
    bool        isCurrent() const { return mIsCurrent;}
private:
    //
    //Surface attributes
    //
    EGLint      width;
    EGLint      height;
    EGLint      texFormat;
    EGLint      texTarget;

    // Width of the actual window being presented (not the EGL texture)
    // Give it some default values.
    int nativeWidth;
    int nativeHeight;
    bool mIsCurrent;
protected:
    void        setWidth(EGLint w)  { width = w;  }
    void        setHeight(EGLint h) { height = h; }
    void        setNativeWidth(int w)  { nativeWidth = w;  }
    void        setNativeHeight(int h) { nativeHeight = h; }

    EGLint      surfaceType;
    uint32_t    rcSurface; //handle to surface created via remote control

    app_time_metric_t appTimeMetric;
};

egl_surface_t::egl_surface_t(EGLDisplay dpy, EGLConfig config, EGLint surfaceType)
    : dpy(dpy), config(config), deletePending(0), mIsCurrent(false),
      surfaceType(surfaceType), rcSurface(0)
{
    width = 0;
    height = 0;
    // prevent div by 0 in EGL_(HORIZONTAL|VERTICAL)_RESOLUTION queries.
    nativeWidth = 1;
    nativeHeight = 1;
    texFormat = EGL_NO_TEXTURE;
    texTarget = EGL_NO_TEXTURE;
    assert(dpy == (EGLDisplay)&s_display);
    s_display.onCreateSurface((EGLSurface)this);
}

EGLint egl_surface_t::getSwapBehavior() const {
    return EGL_BUFFER_PRESERVED;
}

egl_surface_t::~egl_surface_t()
{
    assert(dpy == (EGLDisplay)&s_display);
    s_display.onDestroySurface((EGLSurface)this);
}

// ----------------------------------------------------------------------------
// egl_window_surface_t

struct egl_window_surface_t : public egl_surface_t {
    static egl_window_surface_t* create(
            EGLDisplay dpy, EGLConfig config, EGLint surfType,
            EGLNativeWindowType window);

    virtual ~egl_window_surface_t();

    virtual void       setSwapInterval(int interval);
    virtual EGLBoolean swapBuffers();

    virtual     void        setCollectingTimestamps(EGLint collect)
        override { collectingTimestamps = (collect == EGL_TRUE) ? true : false; }
    virtual     EGLint isCollectingTimestamps() const override { return collectingTimestamps ? EGL_TRUE : EGL_FALSE; }


private:
    egl_window_surface_t(
            EGLDisplay dpy, EGLConfig config, EGLint surfType,
            EGLNativeWindowType window);
    EGLBoolean init();

    EGLNativeWindowType nativeWindow;
    EGLClientBuffer     buffer;
    bool collectingTimestamps;
};

egl_window_surface_t::egl_window_surface_t (
        EGLDisplay dpy, EGLConfig config, EGLint surfType,
        EGLNativeWindowType window)
:   egl_surface_t(dpy, config, surfType),
    nativeWindow(window),
    buffer(NULL),
    collectingTimestamps(false)
{
}

EGLBoolean egl_window_surface_t::init()
{
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    // keep a reference on the window
    anwHelper->acquire(nativeWindow);

    int consumerUsage = 0;
    if (anwHelper->getConsumerUsage(nativeWindow, &consumerUsage) != 0) {
        setErrorReturn(EGL_BAD_ALLOC, EGL_FALSE);
    } else {
        int producerUsage = GRALLOC_USAGE_HW_RENDER;
        anwHelper->setUsage(nativeWindow, consumerUsage | producerUsage);
    }

    int acquireFenceFd = -1;
    if (anwHelper->dequeueBuffer(nativeWindow, &buffer, &acquireFenceFd) != 0) {
        setErrorReturn(EGL_BAD_ALLOC, EGL_FALSE);
    }
    if (acquireFenceFd >= 0) {
        auto* syncHelper = hostCon->syncHelper();

        int waitRet = syncHelper->wait(acquireFenceFd, /* wait forever */-1);
        if (waitRet < 0) {
            GFXSTREAM_ERROR("Failed to wait for window surface's dequeued buffer.");
            anwHelper->cancelBuffer(nativeWindow, buffer);
        }

        syncHelper->close(acquireFenceFd);

        if (waitRet < 0) {
            setErrorReturn(EGL_BAD_ALLOC, EGL_FALSE);
        }
    }

    int bufferWidth = anwHelper->getWidth(buffer);
    int bufferHeight = anwHelper->getHeight(buffer);

    setWidth(bufferWidth);
    setHeight(bufferHeight);

    int nativeWidth = anwHelper->getWidth(nativeWindow);
    int nativeHeight = anwHelper->getHeight(nativeWindow);

    setNativeWidth(nativeWidth);
    setNativeHeight(nativeHeight);

    rcSurface = rcEnc->rcCreateWindowSurface(rcEnc, (uintptr_t)s_display.getIndexOfConfig(config),
            getWidth(), getHeight());

    if (!rcSurface) {
        GFXSTREAM_ERROR("rcCreateWindowSurface returned 0");
        return EGL_FALSE;
    }

    const int hostHandle = anwHelper->getHostHandle(buffer, grallocHelper);
    rcEnc->rcSetWindowColorBuffer(rcEnc, rcSurface, hostHandle);

    return EGL_TRUE;
}

egl_window_surface_t* egl_window_surface_t::create(
        EGLDisplay dpy, EGLConfig config, EGLint surfType,
        EGLNativeWindowType window)
{
    egl_window_surface_t* wnd = new egl_window_surface_t(
            dpy, config, surfType, window);
    if (wnd && !wnd->init()) {
        delete wnd;
        wnd = NULL;
    }
    return wnd;
}

egl_window_surface_t::~egl_window_surface_t() {
    DEFINE_HOST_CONNECTION;
    if (rcSurface && rcEnc) {
        rcEnc->rcDestroyWindowSurface(rcEnc, rcSurface);
    }

    auto* anwHelper = hostCon->anwHelper();
    if (buffer) {
        anwHelper->cancelBuffer(nativeWindow, buffer);
    }
    anwHelper->release(nativeWindow);
}

void egl_window_surface_t::setSwapInterval(int interval)
{
    DEFINE_HOST_CONNECTION;
    hostCon->anwHelper()->setSwapInterval(nativeWindow, interval);
}

// createNativeSync() creates an OpenGL sync object on the host
// using rcCreateSyncKHR. If necessary, a native fence FD will
// also be created through the goldfish sync device.
// Returns a handle to the host-side FenceSync object.
static uint64_t createNativeSync(EGLenum type,
                                 const EGLint* attrib_list,
                                 int num_actual_attribs,
                                 bool destroy_when_signaled,
                                 int fd_in,
                                 int* fd_out) {
    DEFINE_HOST_CONNECTION;

    uint64_t sync_handle;
    uint64_t thread_handle;

    EGLint* actual_attribs =
        (EGLint*)(num_actual_attribs == 0 ? NULL : attrib_list);

    rcEnc->rcCreateSyncKHR(rcEnc, type,
                           actual_attribs,
                           num_actual_attribs * sizeof(EGLint),
                           destroy_when_signaled,
                           &sync_handle,
                           &thread_handle);

    if (type == EGL_SYNC_NATIVE_FENCE_ANDROID && fd_in < 0) {
        int queue_work_err =
            goldfish_sync_queue_work(
                    getEGLThreadInfo()->currentContext->getGoldfishSyncFd(),
                    sync_handle,
                    thread_handle,
                    fd_out);

        (void)queue_work_err;

        DPRINT("got native fence fd=%d queue_work_err=%d",
               *fd_out, queue_work_err);
    }

    return sync_handle;
}

// our cmd
#define VIRTIO_GPU_NATIVE_SYNC_CREATE_EXPORT_FD 0x9000
#define VIRTIO_GPU_NATIVE_SYNC_CREATE_IMPORT_FD 0x9001

// createNativeSync_virtioGpu()
// creates an OpenGL sync object on the host
// using rcCreateSyncKHR.
// If necessary, a native fence FD will be exported or imported.
// Returns a handle to the host-side FenceSync object.
static uint64_t createNativeSync_virtioGpu(
    EGLenum type,
    const EGLint* attrib_list,
    int num_actual_attribs,
    bool destroy_when_signaled,
    int fd_in,
    int* fd_out) {
    DEFINE_HOST_CONNECTION;

    uint64_t sync_handle;
    uint64_t thread_handle;

    EGLint* actual_attribs =
        (EGLint*)(num_actual_attribs == 0 ? NULL : attrib_list);

    // Create a normal sync obj
    rcEnc->rcCreateSyncKHR(rcEnc, type,
                           actual_attribs,
                           num_actual_attribs * sizeof(EGLint),
                           destroy_when_signaled,
                           &sync_handle,
                           &thread_handle);

    if (type == EGL_SYNC_NATIVE_FENCE_ANDROID && fd_in >= 0) {
        // Import fence fd; dup and close
        int importedFd = dup(fd_in);
        if (importedFd < 0) {
            GFXSTREAM_ERROR("Failed to dup imported fd. original: %d errno %d", fd_in, errno);
        }

        *fd_out = importedFd;

        if (close(fd_in)) {
            GFXSTREAM_ERROR("Failed to close imported fd. original: %d errno %d", fd_in, errno);
        }
    } else if (type == EGL_SYNC_NATIVE_FENCE_ANDROID && fd_in < 0) {
        // Export fence fd
        struct VirtGpuExecBuffer exec = { };
        struct gfxstreamCreateExportSync exportSync = { };
        exportSync.hdr.opCode = GFXSTREAM_CREATE_EXPORT_SYNC;
        exportSync.syncHandleLo = (uint32_t)sync_handle;
        exportSync.syncHandleHi = (uint32_t)(sync_handle >> 32);

        VirtGpuDevice* instance = VirtGpuDevice::getInstance();
        exec.command = static_cast<void*>(&exportSync);
        exec.command_size = sizeof(exportSync);
        exec.flags = kFenceOut;
        if (instance->execBuffer(exec, /*blob=*/nullptr)) {
            GFXSTREAM_ERROR("Failed to execbuffer to create sync.");
            return 0;
        }
        *fd_out = exec.handle.osHandle

        DPRINT("virtio-gpu: got native fence fd=%d queue_work_err=%d",
               *fd_out, queue_work_err);
    }

    return sync_handle;
}

// createGoldfishOpenGLNativeSync() is for creating host-only sync objects
// that are needed by only this goldfish opengl driver,
// such as in swapBuffers().
// The guest will not see any of these, and these sync objects will be
// destroyed on the host when signaled.
// A native fence FD is possibly returned.
static void createGoldfishOpenGLNativeSync(int* fd_out) {
    createNativeSync(EGL_SYNC_NATIVE_FENCE_ANDROID,
                     NULL /* empty attrib list */,
                     0 /* 0 attrib count */,
                     true /* destroy when signaled. this is host-only
                             and there will only be one waiter */,
                     -1 /* we want a new fd */,
                     fd_out);
}

struct FrameTracingState {
    uint32_t frameNumber = 0;
    bool tracingEnabled = false;
    void onSwapBuffersSuccesful(ExtendedRCEncoderContext* rcEnc) {
        // edge trigger
        if (gfxstream::guest::isTracingEnabled() && !tracingEnabled) {
            if (rcEnc->hasHostSideTracing()) {
                rcEnc->rcSetTracingForPuid(rcEnc, getPuid(), 1, currGuestTimeNs());
            }
        }
        if (!gfxstream::guest::isTracingEnabled() && tracingEnabled) {
            if (rcEnc->hasHostSideTracing()) {
                rcEnc->rcSetTracingForPuid(rcEnc, getPuid(), 0, currGuestTimeNs());
            }
        }
        tracingEnabled = gfxstream::guest::isTracingEnabled();
        ++frameNumber;
    }
};

static FrameTracingState sFrameTracingState;

static void sFlushBufferAndCreateFence(
    HostConnection*, ExtendedRCEncoderContext* rcEnc, uint32_t rcSurface, uint32_t frameNumber, int* presentFenceFd) {
    atrace_int(ATRACE_TAG_GRAPHICS, "gfxstreamFrameNumber", (int32_t)frameNumber);

    if (rcEnc->hasHostSideTracing()) {
        rcEnc->rcFlushWindowColorBufferAsyncWithFrameNumber(rcEnc, rcSurface, frameNumber);
    } else {
        rcEnc->rcFlushWindowColorBufferAsync(rcEnc, rcSurface);
    }

    if (rcEnc->hasVirtioGpuNativeSync()) {
        createNativeSync_virtioGpu(EGL_SYNC_NATIVE_FENCE_ANDROID,
                     NULL /* empty attrib list */,
                     0 /* 0 attrib count */,
                     true /* destroy when signaled. this is host-only
                             and there will only be one waiter */,
                     -1 /* we want a new fd */,
                     presentFenceFd);
    } else if (rcEnc->hasNativeSync()) {
        createGoldfishOpenGLNativeSync(presentFenceFd);
    } else {
        // equivalent to glFinish if no native sync
        eglWaitClient();
    }
}

EGLBoolean egl_window_surface_t::swapBuffers()
{

    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    // Follow up flushWindowColorBuffer with a fence command.
    // When the fence command finishes,
    // we're sure that the buffer on the host
    // has been blitted.
    //
    // |presentFenceFd| guards the presentation of the
    // current frame with a goldfish sync fence fd.
    //
    // When |presentFenceFd| is signaled, the recipient
    // of the buffer that was sent through queueBuffer
    // can be sure that the buffer is current.
    //
    // If we don't take care of this synchronization,
    // an old frame can be processed by surfaceflinger,
    // resulting in out of order frames.

    int presentFenceFd = -1;

    if (buffer == NULL) {
        GFXSTREAM_ERROR("egl_window_surface_t::swapBuffers called with NULL buffer");
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }

    sFlushBufferAndCreateFence(
        hostCon, rcEnc, rcSurface,
        sFrameTracingState.frameNumber, &presentFenceFd);

    DPRINT("queueBuffer with fence %d", presentFenceFd);
    anwHelper->queueBuffer(nativeWindow, buffer, presentFenceFd);

    appTimeMetric.onQueueBufferReturn();

    DPRINT("calling dequeueBuffer...");

    int acquireFenceFd = -1;
    if (anwHelper->dequeueBuffer(nativeWindow, &buffer, &acquireFenceFd)) {
        buffer = NULL;
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }

    DPRINT("dequeueBuffer with fence %d", acquireFenceFd);

    if (acquireFenceFd > 0) {
        auto* syncHelper = hostCon->syncHelper();
        syncHelper->close(acquireFenceFd);
    }

    const int hostHandle = anwHelper->getHostHandle(buffer, grallocHelper);
    rcEnc->rcSetWindowColorBuffer(rcEnc, rcSurface, hostHandle);

    setWidth(anwHelper->getWidth(buffer));
    setHeight(anwHelper->getHeight(buffer));

    sFrameTracingState.onSwapBuffersSuccesful(rcEnc);
    appTimeMetric.onSwapBuffersReturn();

    return EGL_TRUE;
}

// ----------------------------------------------------------------------------
//egl_pbuffer_surface_t

struct egl_pbuffer_surface_t : public egl_surface_t {
    static egl_pbuffer_surface_t* create(EGLDisplay dpy, EGLConfig config,
            EGLint surfType, int32_t w, int32_t h, GLenum pixelFormat);

    virtual ~egl_pbuffer_surface_t();

    virtual void       setSwapInterval(int interval) { (void)interval; }
    virtual EGLBoolean swapBuffers() { return EGL_TRUE; }

    uint32_t getRcColorBuffer() { return rcColorBuffer; }

private:
    egl_pbuffer_surface_t(EGLDisplay dpy, EGLConfig config, EGLint surfType,
            int32_t w, int32_t h);
    EGLBoolean init(GLenum format);

    uint32_t rcColorBuffer;
    QEMU_PIPE_HANDLE refcountPipeFd;
};

egl_pbuffer_surface_t::egl_pbuffer_surface_t(EGLDisplay dpy, EGLConfig config,
        EGLint surfType, int32_t w, int32_t h)
:   egl_surface_t(dpy, config, surfType),
    rcColorBuffer(0), refcountPipeFd(QEMU_PIPE_INVALID_HANDLE)
{
    setWidth(w);
    setHeight(h);
}

egl_pbuffer_surface_t::~egl_pbuffer_surface_t()
{
    DEFINE_HOST_CONNECTION;
    if (rcEnc) {
        if (rcColorBuffer){
            if(qemu_pipe_valid(refcountPipeFd)) {
                qemu_pipe_close(refcountPipeFd);
            } else {
                rcEnc->rcCloseColorBuffer(rcEnc, rcColorBuffer);
            }
        }
        if (rcSurface)     rcEnc->rcDestroyWindowSurface(rcEnc, rcSurface);
    }
}

// Destroy a pending surface and set it to NULL.

static void s_destroyPendingSurfaceAndSetNull(EGLSurface* surface) {
    if (!surface)
        return;

    if (!s_display.isSurface(*surface)) {
        *surface = NULL;
        return;
    }

    egl_surface_t* surf = static_cast<egl_surface_t *>(*surface);
    if (surf && surf->deletePending) {
        delete surf;
        *surface = NULL;
    }
}

static void s_destroyPendingSurfacesInContext(EGLContext_t* context) {
    if (context->read == context->draw) {
        // If they are the same, delete it only once
        s_destroyPendingSurfaceAndSetNull(&context->draw);
        if (context->draw == NULL) {
            context->read = NULL;
        }
    } else {
        s_destroyPendingSurfaceAndSetNull(&context->draw);
        s_destroyPendingSurfaceAndSetNull(&context->read);
    }
}

EGLBoolean egl_pbuffer_surface_t::init(GLenum pixelFormat)
{
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    rcSurface = rcEnc->rcCreateWindowSurface(rcEnc, (uintptr_t)s_display.getIndexOfConfig(config),
            getWidth(), getHeight());
    if (!rcSurface) {
        GFXSTREAM_ERROR("rcCreateWindowSurface returned 0");
        return EGL_FALSE;
    }

    if (grallocHelper->getGrallocType() == gfxstream::GRALLOC_TYPE_GOLDFISH) {
        rcColorBuffer = rcEnc->rcCreateColorBuffer(rcEnc, getWidth(), getHeight(), pixelFormat);
    } else {
        rcColorBuffer = grallocHelper->createColorBuffer(getWidth(), getHeight(), pixelFormat);
    }

    if (!rcColorBuffer) {
        GFXSTREAM_ERROR("rcCreateColorBuffer returned 0");
        return EGL_FALSE;
    } else {
        refcountPipeFd = qemu_pipe_open("refcount");
        //Send color buffer handle in case RefCountPipe feature is turned on.
        if (qemu_pipe_valid(refcountPipeFd)) {
            qemu_pipe_write(refcountPipeFd, &rcColorBuffer, 4);
        }
    }

    rcEnc->rcSetWindowColorBuffer(rcEnc, rcSurface, rcColorBuffer);

    return EGL_TRUE;
}

egl_pbuffer_surface_t* egl_pbuffer_surface_t::create(EGLDisplay dpy,
        EGLConfig config, EGLint surfType, int32_t w, int32_t h,
        GLenum pixelFormat)
{
    egl_pbuffer_surface_t* pb = new egl_pbuffer_surface_t(dpy, config, surfType,
            w, h);
    if (pb && !pb->init(pixelFormat)) {
        delete pb;
        pb = NULL;
    }
    return pb;
}

// Required for Skia.
static const char kOESEGLImageExternalEssl3[] = "GL_OES_EGL_image_external_essl3";

static bool sWantES30OrAbove(const char* exts) {
    if (strstr(exts, kGLESMaxVersion_3_0) ||
        strstr(exts, kGLESMaxVersion_3_1) ||
        strstr(exts, kGLESMaxVersion_3_2)) {
        return true;
    }
    return false;
}

static std::vector<std::string> getExtStringArray() {
    std::vector<std::string> res;

    EGLThreadInfo *tInfo = getEGLThreadInfo();
    if (!tInfo || !tInfo->currentContext) {
        return res;
    }

    if (tInfo->currentContext->extensionStringArray.size() > 0) {
        return tInfo->currentContext->extensionStringArray;
    }

#define GL_EXTENSIONS                     0x1F03

    DEFINE_AND_VALIDATE_HOST_CONNECTION(res);

    char *hostStr = NULL;
    int n = rcEnc->rcGetGLString(rcEnc, GL_EXTENSIONS, NULL, 0);
    if (n < 0) {
        hostStr = new char[-n+1];
        n = rcEnc->rcGetGLString(rcEnc, GL_EXTENSIONS, hostStr, -n);
        if (n <= 0) {
            delete [] hostStr;
            hostStr = NULL;
        }
    }

    // push guest strings
    res.push_back("GL_EXT_robustness");

    if (!hostStr || !strlen(hostStr)) { return res; }

    // find the number of extensions
    int extStart = 0;
    int extEnd = 0;

    if (sWantES30OrAbove(hostStr) &&
        !strstr(hostStr, kOESEGLImageExternalEssl3)) {
        res.push_back(kOESEGLImageExternalEssl3);
    }

    const int hostStrLen = strlen(hostStr);
    while (extEnd < hostStrLen) {
        if (hostStr[extEnd] == ' ') {
            int extSz = extEnd - extStart;
            res.push_back(std::string(hostStr + extStart, extSz));
            extStart = extEnd + 1;
        }
        extEnd++;
    }

    tInfo->currentContext->extensionStringArray = res;

    delete [] hostStr;
    return res;
}

static const char *getGLString(int glEnum)
{
    EGLThreadInfo *tInfo = getEGLThreadInfo();
    if (!tInfo || !tInfo->currentContext) {
        return NULL;
    }

    const char** strPtr = NULL;

#define GL_VENDOR                         0x1F00
#define GL_RENDERER                       0x1F01
#define GL_VERSION                        0x1F02
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
#define GL_EXTENSIONS                     0x1F03

    switch(glEnum) {
        case GL_VERSION:
            strPtr = &tInfo->currentContext->versionString;
            break;
        case GL_VENDOR:
            strPtr = &tInfo->currentContext->vendorString;
            break;
        case GL_RENDERER:
            strPtr = &tInfo->currentContext->rendererString;
            break;
        case GL_SHADING_LANGUAGE_VERSION:
            strPtr = &tInfo->currentContext->shaderVersionString;
            break;
        case GL_EXTENSIONS:
            strPtr = &tInfo->currentContext->extensionString;
            break;
    }

    if (!strPtr) {
        return NULL;
    }

    if (*strPtr) {
        return *strPtr;
    }

    char* hostStr = NULL;

    if (glEnum == GL_EXTENSIONS) {

        std::vector<std::string> exts = getExtStringArray();

        int totalSz = 1; // null terminator
        for (unsigned int i = 0; i < exts.size(); i++) {
            totalSz += exts[i].size() + 1; // for space
        }

        if (totalSz == 1) return NULL;

        hostStr = new char[totalSz];
        memset(hostStr, 0, totalSz);

        char* current = hostStr;
        for (unsigned int i = 0; i < exts.size(); i++) {
            memcpy(current, exts[i].c_str(), exts[i].size());
            current += exts[i].size();
            *current = ' ';
            ++current;
        }
    } else {
        //
        // first query of that string - need to query host
        //
        DEFINE_AND_VALIDATE_HOST_CONNECTION(NULL);
        int n = rcEnc->rcGetGLString(rcEnc, glEnum, NULL, 0);
        if (n < 0) {
            hostStr = new char[-n+1];
            n = rcEnc->rcGetGLString(rcEnc, glEnum, hostStr, -n);
            if (n <= 0) {
                delete [] hostStr;
                hostStr = NULL;
            }
        }
    }

    //
    // keep the string in the context and return its value
    //
    *strPtr = hostStr;
    return hostStr;
}

// ----------------------------------------------------------------------------

// Note: C99 syntax was tried here but does not work for all compilers.
static EGLClient_eglInterface s_eglIface = {
    getThreadInfo: getEGLThreadInfo,
    getGLString: getGLString,
};

#define DBG_FUNC DBG("%s\n", __FUNCTION__)
EGLDisplay eglGetDisplay(EGLNativeDisplayType display_id)
{
    //
    // we support only EGL_DEFAULT_DISPLAY.
    //
    if (display_id != EGL_DEFAULT_DISPLAY) {
        return EGL_NO_DISPLAY;
    }

    return (EGLDisplay)&s_display;
}

EGLBoolean eglInitialize(EGLDisplay dpy, EGLint *major, EGLint *minor)
{
    VALIDATE_DISPLAY(dpy,EGL_FALSE);

    if (!s_display.initialize(&s_eglIface)) {
        return EGL_FALSE;
    }
    if (major!=NULL)
        *major = s_display.getVersionMajor();
    if (minor!=NULL)
        *minor = s_display.getVersionMinor();
    return EGL_TRUE;
}

EGLBoolean eglTerminate(EGLDisplay dpy)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);

    s_display.terminate();
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
    rcEnc->rcGetRendererVersion(rcEnc);
    return EGL_TRUE;
}

EGLint eglGetError()
{
    EGLint error = getEGLThreadInfo()->eglError;
    getEGLThreadInfo()->eglError = EGL_SUCCESS;
    return error;
}

__eglMustCastToProperFunctionPointerType eglGetProcAddress(const char *procname)
{
    // search in EGL function table
    for (int i=0; i<egl_num_funcs; i++) {
        if (!strcmp(egl_funcs_by_name[i].name, procname)) {
            return (__eglMustCastToProperFunctionPointerType)egl_funcs_by_name[i].proc;
        }
    }

    // look in gles client api's extensions table
    return (__eglMustCastToProperFunctionPointerType)ClientAPIExts::getProcAddress(procname);

    // Fail - function not found.
    return NULL;
}

const char* eglQueryString(EGLDisplay dpy, EGLint name)
{
    // EGL_BAD_DISPLAY is generated if display is not an EGL display connection, unless display is
    // EGL_NO_DISPLAY and name is EGL_EXTENSIONS.
    if (dpy || name != EGL_EXTENSIONS) {
        VALIDATE_DISPLAY_INIT(dpy, NULL);
    }

    return s_display.queryString(name);
}

EGLBoolean eglGetConfigs(EGLDisplay dpy, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);

    if(!num_config) {
        RETURN_ERROR(EGL_FALSE,EGL_BAD_PARAMETER);
    }

    GLint numConfigs = s_display.getNumConfigs();
    if (!configs) {
        *num_config = numConfigs;
        return EGL_TRUE;
    }

    EGLint i;
    for (i = 0 ; i < numConfigs && i < config_size ; i++) {
        *configs++ = (EGLConfig)(uintptr_t)s_display.getConfigAtIndex(i);
    }
    *num_config = i;
    return EGL_TRUE;
}

EGLBoolean eglChooseConfig(EGLDisplay dpy, const EGLint *attrib_list, EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);

    if (!num_config) {
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    int attribs_size = 0;
    EGLint backup_attribs[1];
    if (attrib_list) {
        const EGLint * attrib_p = attrib_list;
        while (attrib_p[0] != EGL_NONE) {
            attribs_size += 2;
            attrib_p += 2;
        }
        attribs_size++; //for the terminating EGL_NONE
    } else {
        attribs_size = 1;
        backup_attribs[0] = EGL_NONE;
        attrib_list = backup_attribs;
    }

    uint32_t* tempConfigs[config_size];
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
    *num_config = rcEnc->rcChooseConfig(rcEnc, (EGLint*)attrib_list,
            attribs_size * sizeof(EGLint), (uint32_t*)tempConfigs, config_size);

    if (*num_config < 0) {
        EGLint err = -(*num_config);
        *num_config = 0;
        switch (err) {
            case EGL_BAD_ATTRIBUTE:
                setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_FALSE);
            default:
                return EGL_FALSE;
        }
    }

    if (configs!=NULL) {
        EGLint i=0;
        for (i=0;i<(*num_config);i++) {
            EGLConfig guestConfig = s_display.getConfigAtIndex(*((uint32_t*)tempConfigs+i));
            configs[i] = guestConfig;
        }
    }

    return EGL_TRUE;
}

EGLBoolean eglGetConfigAttrib(EGLDisplay dpy, EGLConfig config, EGLint attribute, EGLint *value)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_CONFIG(config, EGL_FALSE);

    if (s_display.getConfigAttrib(config, attribute, value))
    {
        return EGL_TRUE;
    }
    else
    {
        DPRINT("%s: bad attrib 0x%x", __FUNCTION__, attribute);
        RETURN_ERROR(EGL_FALSE, EGL_BAD_ATTRIBUTE);
    }
}

EGLSurface eglCreateWindowSurface(EGLDisplay dpy, EGLConfig config, EGLNativeWindowType win, const EGLint *attrib_list)
{
    (void)attrib_list;

    VALIDATE_DISPLAY_INIT(dpy, NULL);
    VALIDATE_CONFIG(config, EGL_FALSE);
    if (win == 0) {
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SURFACE);
    }

    EGLint surfaceType;
    if (s_display.getConfigAttrib(config, EGL_SURFACE_TYPE, &surfaceType) == EGL_FALSE)    return EGL_FALSE;

    if (!(surfaceType & EGL_WINDOW_BIT)) {
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SURFACE);
    }

    DEFINE_HOST_CONNECTION;
    if (!hostCon->anwHelper()->isValid(win)) {
        setErrorReturn(EGL_BAD_NATIVE_WINDOW, EGL_NO_SURFACE);
    }

    egl_surface_t* surface = egl_window_surface_t::create(&s_display, config, EGL_WINDOW_BIT, win);
    if (!surface) {
        setErrorReturn(EGL_BAD_ALLOC, EGL_NO_SURFACE);
    }

    return surface;
}

EGLSurface eglCreatePbufferSurface(EGLDisplay dpy, EGLConfig config, const EGLint *attrib_list)
{
    VALIDATE_DISPLAY_INIT(dpy, NULL);
    VALIDATE_CONFIG(config, EGL_FALSE);

    EGLint surfaceType;
    if (s_display.getConfigAttrib(config, EGL_SURFACE_TYPE, &surfaceType) == EGL_FALSE)    return EGL_FALSE;

    if (!(surfaceType & EGL_PBUFFER_BIT)) {
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SURFACE);
    }

    int32_t w = 0;
    int32_t h = 0;
    EGLint texFormat = EGL_NO_TEXTURE;
    EGLint texTarget = EGL_NO_TEXTURE;
    while (attrib_list[0] != EGL_NONE) {
        switch (attrib_list[0]) {
            case EGL_WIDTH:
                w = attrib_list[1];
                if (w < 0) setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_SURFACE);
                break;
            case EGL_HEIGHT:
                h = attrib_list[1];
                if (h < 0) setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_SURFACE);
                break;
            case EGL_TEXTURE_FORMAT:
                texFormat = attrib_list[1];
                break;
            case EGL_TEXTURE_TARGET:
                texTarget = attrib_list[1];
                break;
            // the followings are not supported
            case EGL_LARGEST_PBUFFER:
            case EGL_MIPMAP_TEXTURE:
            case EGL_VG_ALPHA_FORMAT:
            case EGL_VG_COLORSPACE:
                break;
            default:
                GFXSTREAM_ERROR("Unknown attribute: 0x%x", attrib_list[0]);
                setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_NO_SURFACE);
        };
        attrib_list+=2;
    }
    if (((texFormat == EGL_NO_TEXTURE)&&(texTarget != EGL_NO_TEXTURE)) ||
        ((texFormat != EGL_NO_TEXTURE)&&(texTarget == EGL_NO_TEXTURE))) {
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SURFACE);
    }
    // TODO: check EGL_TEXTURE_FORMAT - need to support eglBindTexImage

    GLenum pixelFormat;
    if (s_display.getConfigGLPixelFormat(config, &pixelFormat) == EGL_FALSE)
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SURFACE);

    egl_surface_t* surface = egl_pbuffer_surface_t::create(dpy, config,
            EGL_PBUFFER_BIT, w, h, pixelFormat);
    if (!surface) {
        setErrorReturn(EGL_BAD_ALLOC, EGL_NO_SURFACE);
    }

    //setup attributes
    surface->setTextureFormat(texFormat);
    surface->setTextureTarget(texTarget);

    return surface;
}

EGLSurface eglCreatePixmapSurface(EGLDisplay dpy, EGLConfig config, EGLNativePixmapType pixmap, const EGLint *attrib_list)
{
    //XXX: Pixmap not supported. The host cannot render to a pixmap resource
    //     located on host. In order to support Pixmaps we should either punt
    //     to s/w rendering -or- let the host render to a buffer that will be
    //     copied back to guest at some sync point. None of those methods not
    //     implemented and pixmaps are not used with OpenGL anyway ...
    VALIDATE_CONFIG(config, EGL_FALSE);
    (void)dpy;
    (void)pixmap;
    (void)attrib_list;
    return EGL_NO_SURFACE;
}

EGLBoolean eglDestroySurface(EGLDisplay dpy, EGLSurface eglSurface)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(eglSurface, EGL_FALSE);

    egl_surface_t* surface(static_cast<egl_surface_t*>(eglSurface));
    if (surface->isCurrent()) {
        surface->deletePending = 1;
    } else {
        delete surface;
    }

    return EGL_TRUE;
}

static float s_getNativeDpi() {
    float nativeDPI = 560.0f;
    const char* dpiPropName = "qemu.sf.lcd_density";
    char dpiProp[PROPERTY_VALUE_MAX];
    if (property_get(dpiPropName, dpiProp, NULL) > 0) {
        nativeDPI = atof(dpiProp);
    }
    return nativeDPI;
}

EGLBoolean eglQuerySurface(EGLDisplay dpy, EGLSurface eglSurface, EGLint attribute, EGLint *value)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(eglSurface, EGL_FALSE);

    egl_surface_t* surface( static_cast<egl_surface_t*>(eglSurface) );

    // Parameters involved in queries of EGL_(HORIZONTAL|VERTICAL)_RESOLUTION
    float currWidth, currHeight, scaledResolution, effectiveSurfaceDPI;
    EGLBoolean ret = EGL_TRUE;
    switch (attribute) {
        case EGL_CONFIG_ID:
            ret = s_display.getConfigAttrib(surface->config, EGL_CONFIG_ID, value);
            break;
        case EGL_WIDTH:
            *value = surface->getWidth();
            break;
        case EGL_HEIGHT:
            *value = surface->getHeight();
            break;
        case EGL_TEXTURE_FORMAT:
            if (surface->getSurfaceType() & EGL_PBUFFER_BIT) {
                *value = surface->getTextureFormat();
            }
            break;
        case EGL_TEXTURE_TARGET:
            if (surface->getSurfaceType() & EGL_PBUFFER_BIT) {
                *value = surface->getTextureTarget();
            }
            break;
        case EGL_SWAP_BEHAVIOR:
        {
            EGLint surfaceType;
            ret = s_display.getConfigAttrib(surface->config, EGL_SURFACE_TYPE,
                    &surfaceType);
            if (ret == EGL_TRUE) {
                if (surfaceType & EGL_SWAP_BEHAVIOR_PRESERVED_BIT) {
                    *value = EGL_BUFFER_PRESERVED;
                } else {
                    *value = EGL_BUFFER_DESTROYED;
                }
            }
            break;
        }
        case EGL_LARGEST_PBUFFER:
            // not modified for a window or pixmap surface
            // and we ignore it when creating a PBuffer surface (default is EGL_FALSE)
            if (surface->getSurfaceType() & EGL_PBUFFER_BIT) *value = EGL_FALSE;
            break;
        case EGL_MIPMAP_TEXTURE:
            // not modified for a window or pixmap surface
            // and we ignore it when creating a PBuffer surface (default is 0)
            if (surface->getSurfaceType() & EGL_PBUFFER_BIT) *value = false;
            break;
        case EGL_MIPMAP_LEVEL:
            // not modified for a window or pixmap surface
            // and we ignore it when creating a PBuffer surface (default is 0)
            if (surface->getSurfaceType() & EGL_PBUFFER_BIT) *value = 0;
            break;
        case EGL_MULTISAMPLE_RESOLVE:
            // ignored when creating the surface, return default
            *value = EGL_MULTISAMPLE_RESOLVE_DEFAULT;
            break;
        case EGL_HORIZONTAL_RESOLUTION:
            // pixel/mm * EGL_DISPLAY_SCALING
            // TODO: get the DPI from avd config
            currWidth = surface->getWidth();
            scaledResolution = currWidth / surface->getNativeWidth();
            effectiveSurfaceDPI =
                scaledResolution * s_getNativeDpi() * EGL_DISPLAY_SCALING;
            *value = (EGLint)(effectiveSurfaceDPI);
            break;
        case EGL_VERTICAL_RESOLUTION:
            // pixel/mm * EGL_DISPLAY_SCALING
            // TODO: get the real DPI from avd config
            currHeight = surface->getHeight();
            scaledResolution = currHeight / surface->getNativeHeight();
            effectiveSurfaceDPI =
                scaledResolution * s_getNativeDpi() * EGL_DISPLAY_SCALING;
            *value = (EGLint)(effectiveSurfaceDPI);
            break;
        case EGL_PIXEL_ASPECT_RATIO:
            // w / h * EGL_DISPLAY_SCALING
            // Please don't ask why * EGL_DISPLAY_SCALING, the document says it
            *value = 1 * EGL_DISPLAY_SCALING;
            break;
        case EGL_RENDER_BUFFER:
            switch (surface->getSurfaceType()) {
                case EGL_PBUFFER_BIT:
                    *value = EGL_BACK_BUFFER;
                    break;
                case EGL_PIXMAP_BIT:
                    *value = EGL_SINGLE_BUFFER;
                    break;
                case EGL_WINDOW_BIT:
                    // ignored when creating the surface, return default
                    *value = EGL_BACK_BUFFER;
                    break;
                default:
                    GFXSTREAM_ERROR("eglQuerySurface %x unknown surface type %x",
                            attribute, surface->getSurfaceType());
                    ret = setErrorFunc(EGL_BAD_ATTRIBUTE, EGL_FALSE);
                    break;
            }
            break;
        case EGL_VG_COLORSPACE:
            // ignored when creating the surface, return default
            *value = EGL_VG_COLORSPACE_sRGB;
            break;
        case EGL_VG_ALPHA_FORMAT:
            // ignored when creating the surface, return default
            *value = EGL_VG_ALPHA_FORMAT_NONPRE;
            break;
        case EGL_TIMESTAMPS_ANDROID:
            *value = surface->isCollectingTimestamps();
            break;
        //TODO: complete other attributes
        default:
            GFXSTREAM_ERROR("eglQuerySurface %x  EGL_BAD_ATTRIBUTE", attribute);
            ret = setErrorFunc(EGL_BAD_ATTRIBUTE, EGL_FALSE);
            break;
    }

    return ret;
}

EGLBoolean eglBindAPI(EGLenum api)
{
    if (api != EGL_OPENGL_ES_API)
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    return EGL_TRUE;
}

EGLenum eglQueryAPI()
{
    return EGL_OPENGL_ES_API;
}

EGLBoolean eglWaitClient()
{
    return eglWaitGL();
}

// We may need to trigger this directly from the TLS destructor.
static EGLBoolean s_eglReleaseThreadImpl(EGLThreadInfo* tInfo) {
    if (!tInfo) return EGL_TRUE;

    tInfo->eglError = EGL_SUCCESS;
    EGLContext_t* context = tInfo->currentContext;

    if (!context || !s_display.isContext(context)) {
        HostConnection::exit();
        return EGL_TRUE;
    }

    // The following code is doing pretty much the same thing as
    // eglMakeCurrent(&s_display, EGL_NO_CONTEXT, EGL_NO_SURFACE, EGL_NO_SURFACE)
    // with the only issue that we do not require a valid display here.
    DEFINE_AND_VALIDATE_HOST_CONNECTION_FOR_TLS(EGL_FALSE, tInfo);
    // We are going to call makeCurrent on the null context and surface
    // anyway once we are on the host, so skip rcMakeCurrent here.
    // rcEnc->rcMakeCurrent(rcEnc, 0, 0, 0);
    context->flags &= ~EGLContext_t::IS_CURRENT;

    s_destroyPendingSurfacesInContext(context);

    if (context->deletePending) {
        if (context->rcContext) {
            rcEnc->rcDestroyContext(rcEnc, context->rcContext);
            context->rcContext = 0;
        }
        delete context;
    }
    tInfo->currentContext = 0;

    HostConnection::exit();

    return EGL_TRUE;
}

EGLBoolean eglReleaseThread()
{
    return s_eglReleaseThreadImpl(getEGLThreadInfo());
}

EGLSurface eglCreatePbufferFromClientBuffer(EGLDisplay dpy, EGLenum buftype, EGLClientBuffer buffer, EGLConfig config, const EGLint *attrib_list)
{
    //TODO
    (void)dpy;
    (void)buftype;
    (void)buffer;
    (void)config;
    (void)attrib_list;
    GFXSTREAM_WARNING("Not implemented");
    return 0;
}

EGLBoolean eglSurfaceAttrib(EGLDisplay dpy, EGLSurface surface, EGLint attribute, EGLint value)
{
    // Right now we don't do anything when using host GPU.
    // This is purely just to pass the data through
    // without issuing a warning. We may benefit from validating the
    // display and surface for debug purposes.
    // TODO: Find cases where we actually need to do something.
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(surface, EGL_FALSE);
    if (surface == EGL_NO_SURFACE) {
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }

    (void)value;

    egl_surface_t* p_surface( static_cast<egl_surface_t*>(surface) );
    switch (attribute) {
    case EGL_MIPMAP_LEVEL:
        return true;
    case EGL_MULTISAMPLE_RESOLVE:
    {
        if (value == EGL_MULTISAMPLE_RESOLVE_BOX) {
            EGLint surface_type;
            s_display.getConfigAttrib(p_surface->config, EGL_SURFACE_TYPE, &surface_type);
            if (0 == (surface_type & EGL_MULTISAMPLE_RESOLVE_BOX_BIT)) {
                setErrorReturn(EGL_BAD_MATCH, EGL_FALSE);
            }
        }
        return true;
    }
    case EGL_SWAP_BEHAVIOR:
        if (value == EGL_BUFFER_PRESERVED) {
            EGLint surface_type;
            s_display.getConfigAttrib(p_surface->config, EGL_SURFACE_TYPE, &surface_type);
            if (0 == (surface_type & EGL_SWAP_BEHAVIOR_PRESERVED_BIT)) {
                setErrorReturn(EGL_BAD_MATCH, EGL_FALSE);
            }
        }
        return true;
    case EGL_TIMESTAMPS_ANDROID:
        DPRINT("%s: set frame timestamps collecting %d\n", __func__, value);
        p_surface->setCollectingTimestamps(value);
        return true;
    default:
        GFXSTREAM_WARNING("attr=0x%x not implemented", attribute);
        setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    }
    return false;
}

EGLBoolean eglBindTexImage(EGLDisplay dpy, EGLSurface eglSurface, EGLint buffer)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(eglSurface, EGL_FALSE);
    if (eglSurface == EGL_NO_SURFACE) {
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }

    if (buffer != EGL_BACK_BUFFER) {
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    egl_surface_t* surface( static_cast<egl_surface_t*>(eglSurface) );

    if (surface->getTextureFormat() == EGL_NO_TEXTURE) {
        setErrorReturn(EGL_BAD_MATCH, EGL_FALSE);
    }

    if (!(surface->getSurfaceType() & EGL_PBUFFER_BIT)) {
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }

    //It's now safe to cast to pbuffer surface
    egl_pbuffer_surface_t* pbSurface = (egl_pbuffer_surface_t*)surface;

    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
    rcEnc->rcBindTexture(rcEnc, pbSurface->getRcColorBuffer());

    return GL_TRUE;
}

EGLBoolean eglReleaseTexImage(EGLDisplay dpy, EGLSurface surface, EGLint buffer)
{
    //TODO
    (void)dpy;
    (void)surface;
    (void)buffer;
    GFXSTREAM_WARNING("Not implemented");
    return 0;
}

EGLBoolean eglSwapInterval(EGLDisplay dpy, EGLint interval)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    EGLContext_t* ctx = getEGLThreadInfo()->currentContext;
    if (!ctx) {
        setErrorReturn(EGL_BAD_CONTEXT, EGL_FALSE);
    }
    if (!ctx->draw) {
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);
    }
    egl_surface_t* draw(static_cast<egl_surface_t*>(ctx->draw));
    draw->setSwapInterval(interval);

    rcEnc->rcFBSetSwapInterval(rcEnc, interval); //TODO: implement on the host

    return EGL_TRUE;
}

static EGLConfig chooseDefaultEglConfig(const EGLDisplay& display) {
    const EGLint attribs[] = {
        EGL_RED_SIZE,   8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,
        EGL_DEPTH_SIZE, 0,
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };
    EGLint numConfigs;
    EGLConfig config;
    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        GFXSTREAM_ERROR("eglChooseConfig failed to select a default config");
        return EGL_NO_CONFIG_KHR;
    }
    return config;
}

EGLContext eglCreateContext(EGLDisplay dpy, EGLConfig config, EGLContext share_context, const EGLint *attrib_list)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_NO_CONTEXT);

    if (config == EGL_NO_CONFIG_KHR) {
        config = chooseDefaultEglConfig(dpy);
    }

    VALIDATE_CONFIG(config, EGL_NO_CONTEXT);

    EGLint majorVersion = 1; //default
    EGLint minorVersion = 0;

    bool wantedMajorVersion = false;
    bool wantedMinorVersion = false;

    while (attrib_list && attrib_list[0] != EGL_NONE) {
           EGLint attrib_val = attrib_list[1];
        switch(attrib_list[0]) {
        case EGL_CONTEXT_MAJOR_VERSION_KHR:
            majorVersion = attrib_val;
            wantedMajorVersion = true;
            break;
        case EGL_CONTEXT_MINOR_VERSION_KHR:
            minorVersion = attrib_val;
            wantedMinorVersion = true;
            break;
        case EGL_CONTEXT_FLAGS_KHR:
            if ((attrib_val & EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR) ||
                (attrib_val & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)  ||
                (attrib_val & EGL_CONTEXT_OPENGL_ROBUST_ACCESS_BIT_KHR)) {
                // Valid.
            } else {
                RETURN_ERROR(EGL_NO_CONTEXT,EGL_BAD_ATTRIBUTE);
            }
            break;
        case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
            if ((attrib_val | EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR) ||
                (attrib_val | EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR)) {
                // Valid
            } else {
                RETURN_ERROR(EGL_NO_CONTEXT,EGL_BAD_ATTRIBUTE);
            }
            break;
        case EGL_CONTEXT_PRIORITY_LEVEL_IMG:
            // According to the spec, we are allowed not to honor this hint.
            // https://www.khronos.org/registry/EGL/extensions/IMG/EGL_IMG_context_priority.txt
            break;
        default:
            GFXSTREAM_VERBOSE("eglCreateContext unsupported attrib 0x%x", attrib_list[0]);
            setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_NO_CONTEXT);
        }
        attrib_list+=2;
    }

    // Support up to GLES 3.2 depending on advertised version from the host system.
    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_NO_CONTEXT);
    if (rcEnc->getGLESMaxVersion() >= GLES_MAX_VERSION_3_0) {
        if (!wantedMajorVersion) {
            majorVersion = 1;
            wantedMinorVersion = false;
        }

        if (wantedMajorVersion &&
            majorVersion == 2) {
            majorVersion = 3;
            wantedMinorVersion = false;
        }

        if (majorVersion == 3 && !wantedMinorVersion) {
            switch (rcEnc->getGLESMaxVersion()) {
                case GLES_MAX_VERSION_3_0:
                    minorVersion = 0;
                    break;
                case GLES_MAX_VERSION_3_1:
                    minorVersion = 1;
                    break;
                case GLES_MAX_VERSION_3_2:
                    minorVersion = 2;
                    break;
                default:
                    minorVersion = 0;
                    break;
            }
        }
    } else {
        if (!wantedMajorVersion) {
            majorVersion = 1;
        }
    }

    switch (majorVersion) {
    case 1:
    case 2:
        break;
    case 3:
        if (rcEnc->getGLESMaxVersion() < GLES_MAX_VERSION_3_0) {
            GFXSTREAM_ERROR("EGL_BAD_CONFIG: no ES 3 support");
            setErrorReturn(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
        }
        switch (minorVersion) {
            case 0:
                break;
            case 1:
                if (rcEnc->getGLESMaxVersion() < GLES_MAX_VERSION_3_1) {
                    GFXSTREAM_ERROR("EGL_BAD_CONFIG: no ES 3.1 support");
                    setErrorReturn(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
                }
                break;
            case 2:
                if (rcEnc->getGLESMaxVersion() < GLES_MAX_VERSION_3_2) {
                    GFXSTREAM_ERROR("EGL_BAD_CONFIG: no ES 3.2 support");
                    setErrorReturn(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
                }
                break;
            default:
                GFXSTREAM_ERROR("EGL_BAD_CONFIG: Unknown ES version %d.%d",
                                majorVersion, minorVersion);
                setErrorReturn(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
        }
        break;
    default:
        GFXSTREAM_ERROR("%s:%d EGL_BAD_CONFIG: invalid major GLES version: %d", majorVersion);
        setErrorReturn(EGL_BAD_CONFIG, EGL_NO_CONTEXT);
    }

    uint32_t rcShareCtx = 0;
    EGLContext_t * shareCtx = NULL;
    if (share_context) {
        shareCtx = static_cast<EGLContext_t*>(share_context);
        rcShareCtx = shareCtx->rcContext;
        if (shareCtx->dpy != dpy)
            setErrorReturn(EGL_BAD_MATCH, EGL_NO_CONTEXT);
    }

    int rcMajorVersion = majorVersion;
    if (majorVersion == 3 && minorVersion == 1) {
        rcMajorVersion = 4;
    }
    if (majorVersion == 3 && minorVersion == 2) {
        rcMajorVersion = 4;
    }
    uint32_t rcContext = rcEnc->rcCreateContext(rcEnc, (uintptr_t)s_display.getIndexOfConfig(config), rcShareCtx, rcMajorVersion);
    if (!rcContext) {
        GFXSTREAM_ERROR("rcCreateContext returned 0");
        setErrorReturn(EGL_BAD_ALLOC, EGL_NO_CONTEXT);
    }

    EGLContext_t * context = new EGLContext_t(dpy, config, shareCtx, majorVersion, minorVersion);
    DPRINT("%s: %p: maj %d min %d rcv %d", __FUNCTION__, context, majorVersion, minorVersion, rcMajorVersion);
    if (!context) {
        GFXSTREAM_ERROR("could not alloc egl context!");
        setErrorReturn(EGL_BAD_ALLOC, EGL_NO_CONTEXT);
    }

    context->rcContext = rcContext;
    return context;
}

EGLBoolean eglDestroyContext(EGLDisplay dpy, EGLContext ctx)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_CONTEXT_RETURN(ctx, EGL_FALSE);

    EGLContext_t * context = static_cast<EGLContext_t*>(ctx);

    if (context->flags & EGLContext_t::IS_CURRENT) {
        context->deletePending = 1;
        return EGL_TRUE;
    }

    if (context->rcContext) {
        DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
        rcEnc->rcDestroyContext(rcEnc, context->rcContext);
        context->rcContext = 0;
    }

    if (context->dummy_surface != EGL_NO_SURFACE) {
        eglDestroySurface(context->dpy, context->dummy_surface);
        context->dummy_surface = EGL_NO_SURFACE;
    }

    delete context;
    return EGL_TRUE;
}

static EGLSurface getOrCreateDummySurface(EGLContext_t* context) {
    if (context->dummy_surface != EGL_NO_SURFACE) {
        return context->dummy_surface;
    }

    EGLint attribs[] = {
        EGL_WIDTH, 16,
        EGL_HEIGHT, 16,
        EGL_NONE};

    context->dummy_surface = eglCreatePbufferSurface(context->dpy, context->config, attribs);
    if (context->dummy_surface == EGL_NO_SURFACE) {
        GFXSTREAM_ERROR("Unable to create a dummy PBuffer EGL surface");
    }
    return context->dummy_surface;
}

EGLBoolean eglMakeCurrent(EGLDisplay dpy, EGLSurface draw, EGLSurface read, EGLContext ctx)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(draw, EGL_FALSE);
    VALIDATE_SURFACE_RETURN(read, EGL_FALSE);

    // Only place to initialize the TLS destructor; any
    // thread can suddenly jump in any eglMakeCurrent
    setTlsDestructor((tlsDtorCallback)s_eglReleaseThreadImpl);

    EGLContext_t * context = static_cast<EGLContext_t*>(ctx);

    if (ctx != EGL_NO_CONTEXT && read == EGL_NO_SURFACE) {
        read = getOrCreateDummySurface(context);
    }
    if (ctx != EGL_NO_CONTEXT && draw == EGL_NO_SURFACE) {
        draw = getOrCreateDummySurface(context);
    }

    if ((read == EGL_NO_SURFACE && draw == EGL_NO_SURFACE) && (ctx != EGL_NO_CONTEXT))
        setErrorReturn(EGL_BAD_MATCH, EGL_FALSE);
    if ((read != EGL_NO_SURFACE || draw != EGL_NO_SURFACE) && (ctx == EGL_NO_CONTEXT))
        setErrorReturn(EGL_BAD_MATCH, EGL_FALSE);

    uint32_t ctxHandle = (context) ? context->rcContext : 0;
    egl_surface_t * drawSurf = static_cast<egl_surface_t *>(draw);
    uint32_t drawHandle = (drawSurf) ? drawSurf->getRcSurface() : 0;
    egl_surface_t * readSurf = static_cast<egl_surface_t *>(read);
    uint32_t readHandle = (readSurf) ? readSurf->getRcSurface() : 0;

    //
    // Nothing to do if no binding change has made
    //
    EGLThreadInfo *tInfo = getEGLThreadInfo();

    if (tInfo->currentContext == context &&
        (context == NULL ||
        (context && (context->draw == draw) && (context->read == read)))) {
        return EGL_TRUE;
    }

    // Destroy surfaces while the previous context is still current.
    EGLContext_t* prevCtx = tInfo->currentContext;
    if (tInfo->currentContext) {
        if (prevCtx->draw) {
            static_cast<egl_surface_t *>(prevCtx->draw)->setIsCurrent(false);
        }
        if (prevCtx->read) {
            static_cast<egl_surface_t *>(prevCtx->read)->setIsCurrent(false);
        }
        s_destroyPendingSurfacesInContext(tInfo->currentContext);
    }

    if (context && (context->flags & EGLContext_t::IS_CURRENT) && (context != tInfo->currentContext)) {
        // context is current to another thread
        GFXSTREAM_ERROR("EGL_BAD_ACCESS: context %p current to another thread!", context);
        setErrorReturn(EGL_BAD_ACCESS, EGL_FALSE);
    }

    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
    if (rcEnc->hasAsyncFrameCommands()) {
        rcEnc->rcMakeCurrentAsync(rcEnc, ctxHandle, drawHandle, readHandle);
    } else {
        rcEnc->rcMakeCurrent(rcEnc, ctxHandle, drawHandle, readHandle);
    }

    //Now make the local bind
    if (context) {
        context->draw = draw;
        context->read = read;
        if (drawSurf) {
            drawSurf->setIsCurrent(true);
        }
        if (readSurf) {
            readSurf->setIsCurrent(true);
        }
        context->flags |= EGLContext_t::IS_CURRENT;
        GLClientState* contextState =
            context->getClientState();

        if (!hostCon->gl2Encoder()->isInitialized()) {
            DPRINT("%s: %p: ver %d %d (tinfo %p) (first time)",
                  __FUNCTION__,
                  context, context->majorVersion, context->minorVersion, tInfo);
            s_display.gles2_iface()->init();
            hostCon->gl2Encoder()->setInitialized();
            ClientAPIExts::initClientFuncs(s_display.gles2_iface(), 1);
        }
        if (contextState->needsInitFromCaps()) {
            // Need to set the version first if
            // querying caps, or validation will trip incorrectly.
            hostCon->gl2Encoder()->setVersion(
                context->majorVersion,
                context->minorVersion,
                context->deviceMajorVersion,
                context->deviceMinorVersion);
            hostCon->gl2Encoder()->setClientState(contextState);
            if (context->majorVersion > 1) {
                HostDriverCaps caps = s_display.getHostDriverCaps(
                    context->majorVersion,
                    context->minorVersion);
                contextState->initFromCaps(caps);
            } else {
                // Just put some stuff here to make gles1 happy
                HostDriverCaps gles1Caps = {
                    .max_vertex_attribs = 16,
                    .max_combined_texture_image_units = 8,
                    .max_color_attachments = 8,

                    .max_texture_size = 4096,
                    .max_texture_size_cube_map = 2048,
                    .max_renderbuffer_size = 4096,
                };
                contextState->initFromCaps(gles1Caps);
            }
        }

        // update the client state, share group, and version
        if (context->majorVersion > 1) {
            hostCon->gl2Encoder()->setClientStateMakeCurrent(
                    contextState,
                    context->majorVersion,
                    context->minorVersion,
                    context->deviceMajorVersion,
                    context->deviceMinorVersion);
            hostCon->gl2Encoder()->setSharedGroup(context->getSharedGroup());
        }
        else {
            hostCon->glEncoder()->setClientState(context->getClientState());
            hostCon->glEncoder()->setSharedGroup(context->getSharedGroup());
        }
    } else if (tInfo->currentContext) {
        //release ClientState & SharedGroup
        if (tInfo->currentContext->majorVersion > 1) {
            hostCon->gl2Encoder()->setClientState(NULL);
            hostCon->gl2Encoder()->setSharedGroup(GLSharedGroupPtr(NULL));
        }
        else {
            hostCon->glEncoder()->setClientState(NULL);
            hostCon->glEncoder()->setSharedGroup(GLSharedGroupPtr(NULL));
        }
    }

    // Delete the previous context here
    if (tInfo->currentContext && (tInfo->currentContext != context)) {
        tInfo->currentContext->flags &= ~EGLContext_t::IS_CURRENT;
        if (tInfo->currentContext->deletePending && tInfo->currentContext != context) {
            eglDestroyContext(dpy, tInfo->currentContext);
        }
    }

    // Now the new context is current in tInfo
    tInfo->currentContext = context;

    //Check maybe we need to init the encoder, if it's first eglMakeCurrent
    if (tInfo->currentContext) {
        if (tInfo->currentContext->majorVersion > 1) {
            if (!hostCon->gl2Encoder()->isInitialized()) {
                s_display.gles2_iface()->init();
                hostCon->gl2Encoder()->setInitialized();
                ClientAPIExts::initClientFuncs(s_display.gles2_iface(), 1);
            }
            const char* exts = getGLString(GL_EXTENSIONS);
            if (exts) {
                hostCon->gl2Encoder()->setExtensions(exts, getExtStringArray());
            }
        }
        else {
            if (!hostCon->glEncoder()->isInitialized()) {
                DPRINT("%s: %p: ver %d %d (tinfo %p) (first time)",
                      __FUNCTION__,
                      context, context->majorVersion, context->minorVersion, tInfo);
                s_display.gles_iface()->init();
                hostCon->glEncoder()->setInitialized();
                ClientAPIExts::initClientFuncs(s_display.gles_iface(), 0);
            }
        }
    }

    return EGL_TRUE;
}

EGLContext eglGetCurrentContext()
{
    return getEGLThreadInfo()->currentContext;
}

EGLSurface eglGetCurrentSurface(EGLint readdraw)
{
    EGLContext_t * context = getEGLThreadInfo()->currentContext;
    if (!context)
        return EGL_NO_SURFACE; //not an error

    switch (readdraw) {
        case EGL_READ:
            return context->read;
        case EGL_DRAW:
            return context->draw;
        default:
            GFXSTREAM_ERROR("Unknown parameter: 0x%x\n", readdraw);
            setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_SURFACE);
    }
}

EGLDisplay eglGetCurrentDisplay()
{
    EGLContext_t * context = getEGLThreadInfo()->currentContext;
    if (!context)
        return EGL_NO_DISPLAY; //not an error

    return context->dpy;
}

EGLBoolean eglQueryContext(EGLDisplay dpy, EGLContext ctx, EGLint attribute, EGLint *value)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    VALIDATE_CONTEXT_RETURN(ctx, EGL_FALSE);

    EGLContext_t * context = static_cast<EGLContext_t*>(ctx);

    EGLBoolean ret = EGL_TRUE;
    switch (attribute) {
        case EGL_CONFIG_ID:
            ret = s_display.getConfigAttrib(context->config, EGL_CONFIG_ID, value);
            break;
        case EGL_CONTEXT_CLIENT_TYPE:
            *value = EGL_OPENGL_ES_API;
            break;
        case EGL_CONTEXT_CLIENT_VERSION:
            *value = context->majorVersion;
            break;
        case EGL_RENDER_BUFFER:
            if (!context->draw)
                *value = EGL_NONE;
            else
                *value = EGL_BACK_BUFFER; //single buffer not supported
            break;
        default:
            GFXSTREAM_ERROR("eglQueryContext %x  EGL_BAD_ATTRIBUTE", attribute);
            setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_FALSE);
            break;
    }

    return ret;
}

EGLBoolean eglWaitGL()
{
    EGLThreadInfo *tInfo = getEGLThreadInfo();
    if (!tInfo || !tInfo->currentContext) {
        return EGL_FALSE;
    }

    if (tInfo->currentContext->majorVersion > 1) {
        s_display.gles2_iface()->finish();
    }
    else {
        s_display.gles_iface()->finish();
    }

    return EGL_TRUE;
}

EGLBoolean eglWaitNative(EGLint engine)
{
    (void)engine;
    return EGL_TRUE;
}

EGLBoolean eglSwapBuffers(EGLDisplay dpy, EGLSurface eglSurface)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    if (eglSurface == EGL_NO_SURFACE)
        setErrorReturn(EGL_BAD_SURFACE, EGL_FALSE);

    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    egl_surface_t* d = static_cast<egl_surface_t*>(eglSurface);
    if (d->dpy != dpy)
        setErrorReturn(EGL_BAD_DISPLAY, EGL_FALSE);

    // post the surface
    EGLBoolean ret = d->swapBuffers();

    hostCon->flush();
    return ret;
}

EGLBoolean eglCopyBuffers(EGLDisplay dpy, EGLSurface surface, EGLNativePixmapType target)
{
    //TODO :later
    (void)dpy;
    (void)surface;
    (void)target;
    return 0;
}

EGLBoolean eglLockSurfaceKHR(EGLDisplay display, EGLSurface surface, const EGLint *attrib_list)
{
    //TODO later
    (void)display;
    (void)surface;
    (void)attrib_list;
    return 0;
}

EGLBoolean eglUnlockSurfaceKHR(EGLDisplay display, EGLSurface surface)
{
    //TODO later
    (void)display;
    (void)surface;
    return 0;
}

/* Define to match AIDL PixelFormat::R_8. */
#define HAL_PIXEL_FORMAT_R8 0x38

EGLImageKHR eglCreateImageKHR(EGLDisplay dpy, EGLContext ctx, EGLenum target, EGLClientBuffer buffer, const EGLint *attrib_list)
{
    (void)attrib_list;

    VALIDATE_DISPLAY_INIT(dpy, EGL_NO_IMAGE_KHR);

    if (target == EGL_NATIVE_BUFFER_ANDROID) {
        if (ctx != EGL_NO_CONTEXT) {
            setErrorReturn(EGL_BAD_CONTEXT, EGL_NO_IMAGE_KHR);
        }

        DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);
        if (!anwHelper->isValid(buffer)) {
            setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_IMAGE_KHR);
        }

        int format = anwHelper->getFormat(buffer, grallocHelper);
        switch (format) {
            case HAL_PIXEL_FORMAT_R8:
            case HAL_PIXEL_FORMAT_RGBA_8888:
            case HAL_PIXEL_FORMAT_RGBX_8888:
            case HAL_PIXEL_FORMAT_RGB_888:
            case HAL_PIXEL_FORMAT_RGB_565:
            case HAL_PIXEL_FORMAT_YV12:
            case HAL_PIXEL_FORMAT_BGRA_8888:
            case HAL_PIXEL_FORMAT_RGBA_FP16:
            case HAL_PIXEL_FORMAT_RGBA_1010102:
            case HAL_PIXEL_FORMAT_YCBCR_420_888:
            case HAL_PIXEL_FORMAT_YCBCR_P010:
            case HAL_PIXEL_FORMAT_DEPTH_16:
            case HAL_PIXEL_FORMAT_DEPTH_24:
            case HAL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
            case HAL_PIXEL_FORMAT_DEPTH_32F:
            case HAL_PIXEL_FORMAT_DEPTH_32F_STENCIL_8:
                break;
            case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
                GFXSTREAM_WARNING("Using HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED");
                break;
            default:
                GFXSTREAM_ERROR("Unknown parameter: 0x%x", format);
                setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_IMAGE_KHR);
        }

        anwHelper->acquire(buffer);

        EGLImage_t *image = new EGLImage_t();
        image->dpy = dpy;
        image->target = target;
        image->buffer = buffer;
        image->width = anwHelper->getWidth(buffer);
        image->height = anwHelper->getHeight(buffer);

        return (EGLImageKHR)image;
    }
    else if (target == EGL_GL_TEXTURE_2D_KHR) {
        VALIDATE_CONTEXT_RETURN(ctx, EGL_NO_IMAGE_KHR);

        EGLContext_t *context = static_cast<EGLContext_t*>(ctx);
        DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_NO_IMAGE_KHR);

        uint32_t ctxHandle = (context) ? context->rcContext : 0;
        GLuint texture = (GLuint)reinterpret_cast<uintptr_t>(buffer);
        uint32_t img = rcEnc->rcCreateClientImage(rcEnc, ctxHandle, target, texture);
        EGLImage_t *image = new EGLImage_t();
        image->dpy = dpy;
        image->target = target;
        image->host_egl_image = img;
        image->width = context->getClientState()->queryTexWidth(0, texture);
        image->height = context->getClientState()->queryTexHeight(0, texture);

        return (EGLImageKHR)image;
    }

    setErrorReturn(EGL_BAD_PARAMETER, EGL_NO_IMAGE_KHR);
}

EGLBoolean eglDestroyImageKHR(EGLDisplay dpy, EGLImageKHR img)
{
    VALIDATE_DISPLAY_INIT(dpy, EGL_FALSE);
    EGLImage_t *image = (EGLImage_t*)img;

    if (!image || image->dpy != dpy) {
        RETURN_ERROR(EGL_FALSE, EGL_BAD_PARAMETER);
    }

    DEFINE_AND_VALIDATE_HOST_CONNECTION(EGL_FALSE);

    if (image->target == EGL_NATIVE_BUFFER_ANDROID) {
        EGLClientBuffer buffer = image->buffer;
        if (!anwHelper->isValid(buffer)) {
            setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
        }

        anwHelper->release(buffer);
        delete image;

        return EGL_TRUE;
    } else if (image->target == EGL_GL_TEXTURE_2D_KHR) {
        uint32_t host_egl_image = image->host_egl_image;
        delete image;

        return rcEnc->rcDestroyClientImage(rcEnc, host_egl_image);
    }

    setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
}

#define FENCE_SYNC_HANDLE (EGLSyncKHR)0xFE4CE
#define MAX_EGL_SYNC_ATTRIBS 10

EGLSyncKHR eglCreateSyncKHR(EGLDisplay dpy, EGLenum type,
        const EGLint *attrib_list)
{
    VALIDATE_DISPLAY(dpy, EGL_NO_SYNC_KHR);
    DPRINT("type for eglCreateSyncKHR: 0x%x", type);

    DEFINE_HOST_CONNECTION;

    if ((type != EGL_SYNC_FENCE_KHR &&
         type != EGL_SYNC_NATIVE_FENCE_ANDROID) ||
        (type != EGL_SYNC_FENCE_KHR &&
         !rcEnc->hasNativeSync() &&
         !rcEnc->hasVirtioGpuNativeSync())) {
        setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_NO_SYNC_KHR);
    }

    EGLThreadInfo *tInfo = getEGLThreadInfo();
    if (!tInfo || !tInfo->currentContext) {
        setErrorReturn(EGL_BAD_MATCH, EGL_NO_SYNC_KHR);
    }

    int num_actual_attribs = 0;

    // If attrib_list is not NULL,
    // ensure attrib_list contains (key, value) pairs
    // followed by a single EGL_NONE.
    // Also validate attribs.
    int inputFenceFd = -1;
    if (attrib_list) {
        for (int i = 0; i < MAX_EGL_SYNC_ATTRIBS; i += 2) {
            if (attrib_list[i] == EGL_NONE) {
                num_actual_attribs = i;
                break;
            }
            if (i + 1 == MAX_EGL_SYNC_ATTRIBS) {
                DPRINT("ERROR: attrib list without EGL_NONE");
                setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_NO_SYNC_KHR);
            }
        }

        // Validate and input attribs
        for (int i = 0; i < num_actual_attribs; i += 2) {
            EGLint attrib_key = attrib_list[i];
            EGLint attrib_val = attrib_list[i + 1];
            switch (attrib_key) {
                case EGL_SYNC_TYPE_KHR:
                case EGL_SYNC_STATUS_KHR:
                case EGL_SYNC_CONDITION_KHR:
                case EGL_SYNC_NATIVE_FENCE_FD_ANDROID:
                    break;
                default:
                    setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_NO_SYNC_KHR);
            }
            if (attrib_key == EGL_SYNC_NATIVE_FENCE_FD_ANDROID) {
                if (attrib_val != EGL_NO_NATIVE_FENCE_FD_ANDROID) {
                    inputFenceFd = attrib_val;
                }
            }
            DPRINT("attrib: 0x%x : 0x%x", attrib_key, attrib_val);
        }
    }

    uint64_t sync_handle = 0;
    int newFenceFd = -1;

    if (rcEnc->hasVirtioGpuNativeSync()) {
        sync_handle =
            createNativeSync_virtioGpu(
                type, attrib_list, num_actual_attribs,
                false /* don't destroy when signaled on the host;
                         let the guest clean this up,
                         because the guest called eglCreateSyncKHR. */,
                inputFenceFd, &newFenceFd);
    } else if (rcEnc->hasNativeSync()) {
        sync_handle =
            createNativeSync(
                type, attrib_list, num_actual_attribs,
                false /* don't destroy when signaled on the host;
                         let the guest clean this up,
                         because the guest called eglCreateSyncKHR. */,
                inputFenceFd,
                &newFenceFd);

    } else {
        // Just trigger a glFinish if the native sync on host
        // is unavailable.
        eglWaitClient();
    }

    EGLSync_t* syncRes = new EGLSync_t(sync_handle);

    if (type == EGL_SYNC_NATIVE_FENCE_ANDROID) {
        syncRes->type = EGL_SYNC_NATIVE_FENCE_ANDROID;

        if (rcEnc->hasVirtioGpuNativeSync()) {
            syncRes->android_native_fence_fd = newFenceFd;
        } else {
            if (inputFenceFd < 0) {
                syncRes->android_native_fence_fd = newFenceFd;
            } else {
                DPRINT("has input fence fd %d",
                        inputFenceFd);
                syncRes->android_native_fence_fd = inputFenceFd;
            }
        }
    } else {
        syncRes->type = EGL_SYNC_FENCE_KHR;
        syncRes->android_native_fence_fd = -1;
        if (!rcEnc->hasNativeSync() && !rcEnc->hasVirtioGpuNativeSync()) {
            syncRes->status = EGL_SIGNALED_KHR;
        }
    }

    return (EGLSyncKHR)syncRes;
}

EGLBoolean eglDestroySyncKHR(EGLDisplay dpy, EGLSyncKHR eglsync)
{
    (void)dpy;

    if (!eglsync) {
        GFXSTREAM_ERROR("Null sync object!");
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    EGLSync_t* sync = static_cast<EGLSync_t*>(eglsync);

    if (sync && sync->android_native_fence_fd > 0) {
        close(sync->android_native_fence_fd);
        sync->android_native_fence_fd = -1;
    }

    if (sync) {
        DEFINE_HOST_CONNECTION;
        if (rcEnc->hasVirtioGpuNativeSync() || rcEnc->hasNativeSync()) {
            if (rcEnc->hasAsyncFrameCommands()) {
                rcEnc->rcDestroySyncKHRAsync(rcEnc, sync->handle);
            } else {
                rcEnc->rcDestroySyncKHR(rcEnc, sync->handle);
            }
        }
        delete sync;
    }

    return EGL_TRUE;
}

EGLint eglClientWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR eglsync, EGLint flags,
        EGLTimeKHR timeout)
{
    (void)dpy;

    if (!eglsync) {
        GFXSTREAM_ERROR("Null sync object!");
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    EGLSync_t* sync = (EGLSync_t*)eglsync;

    DPRINT("sync=0x%lx (handle=0x%lx) flags=0x%x timeout=0x%llx",
           sync, sync->handle, flags, timeout);

    DEFINE_HOST_CONNECTION;

    EGLint retval;
    if (rcEnc->hasVirtioGpuNativeSync() || rcEnc->hasNativeSync()) {
        retval = rcEnc->rcClientWaitSyncKHR
            (rcEnc, sync->handle, flags, timeout);
    } else {
        retval = EGL_CONDITION_SATISFIED_KHR;
    }
    EGLint res_status;
    switch (sync->type) {
        case EGL_SYNC_FENCE_KHR:
            res_status = EGL_SIGNALED_KHR;
            break;
        case EGL_SYNC_NATIVE_FENCE_ANDROID:
            res_status = EGL_SYNC_NATIVE_FENCE_SIGNALED_ANDROID;
            break;
        default:
            res_status = EGL_SIGNALED_KHR;
    }
    sync->status = res_status;
    return retval;
}

EGLBoolean eglGetSyncAttribKHR(EGLDisplay dpy, EGLSyncKHR eglsync,
        EGLint attribute, EGLint *value)
{
    (void)dpy;

    EGLSync_t* sync = (EGLSync_t*)eglsync;

    if (!sync) {
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    if (!value) {
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    switch (attribute) {
    case EGL_SYNC_TYPE_KHR:
        *value = sync->type;
        return EGL_TRUE;
    case EGL_SYNC_STATUS_KHR: {
        if (sync->status == EGL_SIGNALED_KHR) {
            *value = sync->status;
            return EGL_TRUE;
        } else {
            // ask the host again
            DEFINE_HOST_CONNECTION;
            if (rcEnc->hasVirtioGpuNativeSync() || rcEnc->hasNativeSyncV4()) {
                if (rcEnc->rcIsSyncSignaled(rcEnc, sync->handle)) {
                    sync->status = EGL_SIGNALED_KHR;
                }
            }
            *value = sync->status;
            return EGL_TRUE;
        }
    }
    case EGL_SYNC_CONDITION_KHR:
        *value = EGL_SYNC_PRIOR_COMMANDS_COMPLETE_KHR;
        return EGL_TRUE;
    default:
        setErrorReturn(EGL_BAD_ATTRIBUTE, EGL_FALSE);
    }
}

int eglDupNativeFenceFDANDROID(EGLDisplay dpy, EGLSyncKHR eglsync) {
    (void)dpy;

    DPRINT("call");

    EGLSync_t* sync = (EGLSync_t*)eglsync;
    if (sync && sync->android_native_fence_fd > 0) {
        int res = dup(sync->android_native_fence_fd);
        return res;
    } else {
        return -1;
    }
}

EGLint eglWaitSyncKHR(EGLDisplay dpy, EGLSyncKHR eglsync, EGLint flags) {
    (void)dpy;

    if (!eglsync) {
        GFXSTREAM_ERROR("Null sync object!");
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    if (flags) {
        GFXSTREAM_ERROR("Flags must be 0, got 0x%x", flags);
        setErrorReturn(EGL_BAD_PARAMETER, EGL_FALSE);
    }

    DEFINE_HOST_CONNECTION;
    if (rcEnc->hasVirtioGpuNativeSync() || rcEnc->hasNativeSyncV3()) {
        EGLSync_t* sync = (EGLSync_t*)eglsync;
        rcEnc->rcWaitSyncKHR(rcEnc, sync->handle, flags);
    }

    return EGL_TRUE;
}
