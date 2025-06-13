/*
* Copyright (C) 2011-2015 The Android Open Source Project
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

#include "FrameBuffer.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include <iomanip>

#if defined(__linux__)
#include <sys/resource.h>
#endif

#if GFXSTREAM_ENABLE_HOST_GLES
#include "GLESVersionDetector.h"
#include "OpenGLESDispatch/DispatchTables.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "PostWorkerGl.h"
#include "RenderControl.h"
#include "RenderThreadInfoGl.h"
#include "gl/YUVConverter.h"
#include "gl/gles2_dec/gles2_dec.h"
#include "gl/glestranslator/EGL/EglGlobalInfo.h"
#endif

#include "ContextHelper.h"
#include "Hwc2.h"
#include "NativeSubWindow.h"
#include "RenderThreadInfo.h"
#include "SyncThread.h"
#include "gfxstream/Metrics.h"
#include "gfxstream/SharedLibrary.h"
#include "gfxstream/Tracing.h"
#include "gfxstream/common/logging.h"
#include "gfxstream/containers/Lookup.h"
#include "gfxstream/host/Tracing.h"
#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/guest_operations.h"
#include "gfxstream/host/renderer_operations.h"
#include "gfxstream/host/stream_utils.h"
#include "gfxstream/host/vm_operations.h"
#include "gfxstream/host/window_operations.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/system/System.h"
#include "render-utils/MediaNative.h"
#include "vulkan/DisplayVk.h"
#include "vulkan/PostWorkerVk.h"
#include "vulkan/VkCommonOperations.h"
#include "vulkan/VkDecoderGlobalState.h"

namespace gfxstream {
namespace {

using gfxstream::Stream;
using gfxstream::base::AutoLock;
using gfxstream::base::CreateMetricsLogger;
using gfxstream::base::MetricEventVulkanOutOfMemory;
using gfxstream::base::SharedLibrary;
using gfxstream::base::WorkerProcessingResult;
using gfxstream::gl::GLESApi;
using gfxstream::gl::GLESApi_2;
using gfxstream::gl::GLESApi_CM;
using gfxstream::host::FeatureSet;
using gfxstream::host::GfxApiLogger;
using gfxstream::host::RepresentativeColorBufferMemoryTypeInfo;
#if GFXSTREAM_ENABLE_HOST_GLES
using gfxstream::gl::DisplaySurfaceGl;
using gfxstream::gl::EmulatedEglConfig;
using gfxstream::gl::EmulatedEglConfigList;
using gfxstream::gl::EmulatedEglContext;
using gfxstream::gl::EmulatedEglContextMap;
using gfxstream::gl::EmulatedEglContextPtr;
using gfxstream::gl::EmulatedEglFenceSync;
using gfxstream::gl::EmulatedEglWindowSurface;
using gfxstream::gl::EmulatedEglWindowSurfaceMap;
using gfxstream::gl::EmulatedEglWindowSurfacePtr;
using gfxstream::gl::EmulationGl;
using gfxstream::gl::GLES_DISPATCH_MAX_VERSION_2;
using gfxstream::gl::GLESDispatchMaxVersion;
using gfxstream::gl::RenderThreadInfoGl;
using gfxstream::gl::s_egl;
using gfxstream::gl::s_gles2;
using gfxstream::gl::TextureDraw;
using gfxstream::gl::YUVConverter;
using gfxstream::gl::YUVPlane;
#endif
using gfxstream::vk::AstcEmulationMode;
using gfxstream::vk::VkEmulation;

bool postOnlyOnMainThread() {
#if defined(__APPLE__) && !defined(QEMU_NEXT)
    return true;
#else
    return false;
#endif
}

static FrameBuffer* sFrameBuffer = NULL;

// A condition variable needed to wait for framebuffer initialization.
struct InitializedGlobals {
    gfxstream::base::Lock lock;
    gfxstream::base::ConditionVariable condVar;
};

// |sInitialized| caches the initialized framebuffer state - this way
// happy path doesn't need to lock the mutex.
static std::atomic<bool> sInitialized{false};

static InitializedGlobals* sGlobals() {
    static InitializedGlobals* g = new InitializedGlobals;
    return g;
}

}  // namespace

static HandleType sNextHandle = 0;

struct BufferRef {
    BufferPtr buffer;
};

#if GFXSTREAM_ENABLE_HOST_GLES
typedef std::unordered_map<uint64_t, gl::EmulatedEglWindowSurfaceSet>
    ProcOwnedEmulatedEglWindowSurfaces;
typedef std::unordered_map<uint64_t, gl::EmulatedEglContextSet> ProcOwnedEmulatedEglContexts;
typedef std::unordered_map<uint64_t, gl::EmulatedEglImageSet> ProcOwnedEmulatedEGLImages;
#endif  // GFXSTREAM_ENABLE_HOST_GLES

typedef std::unordered_map<HandleType, BufferRef> BufferMap;
typedef std::unordered_multiset<HandleType> BufferSet;
typedef std::unordered_map<uint64_t, BufferSet> ProcOwnedBuffers;
typedef std::unordered_map<uint64_t, ColorBufferSet> ProcOwnedColorBuffers;

typedef std::unordered_map<void*, std::function<void()>> CallbackMap;
typedef std::unordered_map<uint64_t, CallbackMap> ProcOwnedCleanupCallbacks;

class FrameBuffer::Impl : public gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent> {
   public:
    static std::unique_ptr<Impl> Create(FrameBuffer* framebuffer, uint32_t width, uint32_t height,
                                        const FeatureSet& features, bool useSubWindow,
                                        bool egl2egl);

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    ~Impl();

    bool setupSubWindow(FBNativeWindowType p_window, int wx, int wy, int ww, int wh, int fbw,
                        int fbh, float dpr, float zRot, bool deleteExisting, bool hideWindow);

    bool removeSubWindow();

    int getWidth() const { return m_framebufferWidth; }
    int getHeight() const { return m_framebufferHeight; }

    // Set a callback that will be called each time the emulated GPU content
    // is updated. This can be relatively slow with host-based GPU emulation,
    // so only do this when you need to.
    void setPostCallback(Renderer::OnPostCallback onPost, void* onPostContext, uint32_t displayId,
                         bool useBgraReadback = false);

    // Tests and reports if the host supports the format through the allocator
    bool isFormatSupported(GLenum format);

    // Create a new ColorBuffer instance from this display instance.
    // |p_width| and |p_height| are its dimensions in pixels.
    // |p_internalFormat| is the OpenGL format of this color buffer.
    // |p_frameworkFormat| describes the Android frameework format of this
    // color buffer, if differing from |p_internalFormat|.
    // See ColorBuffer::create() for
    // list of valid values. Note that ColorBuffer instances are reference-
    // counted. Use openColorBuffer / closeColorBuffer to operate on the
    // internal count.
    HandleType createColorBuffer(int p_width, int p_height, GLenum p_internalFormat,
                                 FrameworkFormat p_frameworkFormat);
    // Variant of createColorBuffer except with a particular
    // handle already assigned. This is for use with
    // virtio-gpu's RESOURCE_CREATE ioctl.
    void createColorBufferWithResourceHandle(int p_width, int p_height, GLenum p_internalFormat,
                                             FrameworkFormat p_frameworkFormat, HandleType handle);

    HandleType createBuffer(uint64_t size, uint32_t memoryProperty);

    void createBufferWithResourceHandle(uint64_t size, HandleType handle);

    int openColorBuffer(HandleType p_colorbuffer);

    void closeColorBuffer(HandleType p_colorbuffer);

    void closeBuffer(HandleType p_colorbuffer);

    void createGraphicsProcessResources(uint64_t puid);

    std::unique_ptr<ProcessResources> removeGraphicsProcessResources(uint64_t puid);

    void cleanupProcGLObjects(uint64_t puid);

    void readBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* bytes);

    void readColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                         GLenum format, GLenum type, void* pixels, uint64_t outPixelsSize);

    void readColorBufferYUV(HandleType p_colorbuffer, int x, int y, int width, int height,
                            void* pixels, uint32_t outPixelsSize);

    bool updateBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* pixels);

    bool updateColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                           GLenum format, GLenum type, void* pixels);
    bool updateColorBufferFromFrameworkFormat(HandleType p_colorbuffer, int x, int y, int width,
                                              int height, FrameworkFormat fwkFormat, GLenum format,
                                              GLenum type, void* pixels, void* metadata = nullptr);

    bool getColorBufferInfo(HandleType p_colorbuffer, int* width, int* height,
                            GLint* internalformat, FrameworkFormat* frameworkFormat = nullptr);
    bool getBufferInfo(HandleType p_buffer, int* size);

    bool post(HandleType p_colorbuffer, bool needLockAndBind = true);

    void postWithCallback(HandleType p_colorbuffer, Post::CompletionCallback callback,
                          bool needLockAndBind = true);
    bool hasGuestPostedAFrame() { return m_guestPostedAFrame; }
    void resetGuestPostedAFrame() { m_guestPostedAFrame = false; }

    void doPostCallback(void* pixels, uint32_t displayId);

    void getPixels(void* pixels, uint32_t bytes, uint32_t displayId);
    void flushReadPipeline(int displayId);
    void ensureReadbackWorker();

    bool asyncReadbackSupported();
    Renderer::ReadPixelsCallback getReadPixelsCallback();
    Renderer::FlushReadPixelPipeline getFlushReadPixelPipeline();

    // Re-post the last ColorBuffer that was displayed through post().
    // This is useful if you detect that the sub-window content needs to
    // be re-displayed for any reason.
    bool repost(bool needLockAndBind = true);

    // Change the rotation of the displayed GPU sub-window.
    void setDisplayRotation(float zRot) {
        if (zRot != m_zRot) {
            m_zRot = zRot;
            repost();
        }
    }

    void setDisplayTranslation(float px, float py) {
        // Sanity check the values to ensure they are between 0 and 1
        const float x = px > 1.f ? 1.f : (px < 0.f ? 0.f : px);
        const float y = py > 1.f ? 1.f : (py < 0.f ? 0.f : py);
        if (x != m_px || y != m_py) {
            m_px = x;
            m_py = y;
            repost();
        }
    }

    void lockContextStructureRead() { m_contextStructureLock.lockRead(); }
    void unlockContextStructureRead() { m_contextStructureLock.unlockRead(); }

    // For use with sync threads and otherwise, any time we need a GL context
    // not specifically for drawing, but to obtain certain things about
    // GL state.
    // It can be unsafe / leaky to change the structure of contexts
    // outside the facilities the FrameBuffer class provides.
    void createTrivialContext(HandleType shared, HandleType* contextOut, HandleType* surfOut);

    void setShuttingDown() { m_shuttingDown = true; }
    bool isShuttingDown() const { return m_shuttingDown; }
    bool compose(uint32_t bufferSize, void* buffer, bool post = true);
    // When false is returned, the callback won't be called. The callback will
    // be called on the PostWorker thread without blocking the current thread.
    AsyncResult composeWithCallback(uint32_t bufferSize, void* buffer,
                                    Post::CompletionCallback callback);

    void onSave(gfxstream::Stream* stream, const ITextureSaverPtr& textureSaver);
    bool onLoad(gfxstream::Stream* stream, const ITextureLoaderPtr& textureLoader);

    // lock and unlock handles (EmulatedEglContext, ColorBuffer, EmulatedEglWindowSurface)
    void lock() ACQUIRE(m_lock);
    void unlock() RELEASE(m_lock);

    float getDpr() const { return m_dpr; }
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }
    float getPx() const { return m_px; }
    float getPy() const { return m_py; }
    int getZrot() const { return m_zRot; }

    void setScreenMask(int width, int height, const unsigned char* rgbaData);

    void registerVulkanInstance(uint64_t id, const char* appName) const;
    void unregisterVulkanInstance(uint64_t id) const;

    bool isVulkanEnabled() const { return m_vulkanEnabled; }

    // Saves a screenshot of the previous frame.
    // nChannels should be 3 (RGB) or 4 (RGBA).
    // You must provide a pre-allocated buffer of sufficient
    // size. Returns 0 on success. In the case of failure and if *cPixels != 0
    // you can call this function again with a buffer of size *cPixels. cPixels
    // should usually be at at least desiredWidth * desiredHeight * nChannels.
    //
    // In practice the buffer should be > desiredWidth *
    // desiredHeight * nChannels.
    //
    // Note: Do not call this function again if it fails and *cPixels == 0
    //  swiftshader_indirect does not work with 3 channels
    //
    // This function supports rectangle snipping by
    // providing an |rect| parameter. The default value of {{0,0}, {0,0}}
    // indicates the users wants to snip the entire screen instead of a
    // partial screen.
    // - |rect|  represents a rectangle within the screen defined by
    // desiredWidth and desiredHeight.
    int getScreenshot(unsigned int nChannels, unsigned int* width, unsigned int* height,
                      uint8_t* pixels, size_t* cPixels, int displayId, int desiredWidth,
                      int desiredHeight, int desiredRotation, Rect rect = {{0, 0}, {0, 0}});

    void onLastColorBufferRef(uint32_t handle);
    ColorBufferPtr findColorBuffer(HandleType p_colorbuffer);
    BufferPtr findBuffer(HandleType p_buffer);

    void registerProcessCleanupCallback(void* key, uint64_t contextId,
                                        std::function<void()> callback);
    void unregisterProcessCleanupCallback(void* key);

    const ProcessResources* getProcessResources(uint64_t puid);

    int createDisplay(uint32_t* displayId);
    int createDisplay(uint32_t displayId);
    int destroyDisplay(uint32_t displayId);
    int setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer);
    int getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer);
    int getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId);
    int getDisplayPose(uint32_t displayId, int32_t* x, int32_t* y, uint32_t* w, uint32_t* h);
    int setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w, uint32_t h,
                       uint32_t dpi = 0);
    void getCombinedDisplaySize(int* w, int* h);

    HandleType getLastPostedColorBuffer() { return m_lastPostedColorBuffer; }
    void asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle,
                                     FenceCompletionCallback cb);
    void asyncWaitForGpuVulkanQsriWithCb(uint64_t image, FenceCompletionCallback cb);

    void setGuestManagedColorBufferLifetime(bool guestManaged);

    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForComposition(uint32_t colorBufferHandle,
                                                                       bool colorBufferIsTarget);
    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForDisplay(uint32_t colorBufferHandle);

    HealthMonitor<>* getHealthMonitor() { return m_healthMonitor.get(); }

    MetricsLogger& getMetricsLogger() { return *m_logger; }

    void logVulkanDeviceLost();
    void logVulkanOutOfMemory(VkResult result, const char* function, int line,
                              std::optional<uint64_t> allocationSize = std::nullopt);

    void setVsyncHz(int vsyncHz);
    void scheduleVsyncTask(VsyncThread::VsyncTask task);
    void setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY);
    void setDisplayActiveConfig(int configId);
    int getDisplayConfigsCount();
    int getDisplayConfigsParam(int configId, EGLint param);
    int getDisplayActiveConfig();

    bool flushColorBufferFromVk(HandleType colorBufferHandle);
    bool flushColorBufferFromVkBytes(HandleType colorBufferHandle, const void* bytes,
                                     size_t bytesSize);
    bool invalidateColorBufferForVk(HandleType colorBufferHandle);

    std::optional<BlobDescriptorInfo> exportColorBuffer(HandleType colorBufferHandle);
    std::optional<BlobDescriptorInfo> exportBuffer(HandleType bufferHandle);

#if GFXSTREAM_ENABLE_HOST_GLES
    // Retrieves the color buffer handle associated with |p_surface|.
    // Returns 0 if there is no such handle.
    HandleType getEmulatedEglWindowSurfaceColorBufferHandle(HandleType p_surface);

    // createTrivialContext(), but with a m_pbufContext
    // as shared, and not adding itself to the context map at all.
    void createSharedTrivialContext(EGLContext* contextOut, EGLSurface* surfOut);
    void destroySharedTrivialContext(EGLContext context, EGLSurface surf);

    // Attach a ColorBuffer to a EmulatedEglWindowSurface instance.
    // See the documentation for EmulatedEglWindowSurface::setColorBuffer().
    // |p_surface| is the target EmulatedEglWindowSurface's handle value.
    // |p_colorbuffer| is the ColorBuffer handle value.
    // Returns true on success, false otherwise.

    bool setEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface, HandleType p_colorbuffer);

    EGLint getEglVersion(EGLint* major, EGLint* minor);

    std::string getEglString(EGLenum name);
    std::string getGlString(EGLenum name);
    std::string getGlesExtensionsString() const;

    void getNumConfigs(int* outNumConfigs, int* outNumAttribs);
    EGLint getConfigs(uint32_t bufferSize, GLuint* buffer);
    EGLint chooseConfig(EGLint* attribs, EGLint* configs, EGLint configsSize);

    // Retrieve the GL strings of the underlying EGL/GLES implementation.
    // On return, |*vendor|, |*renderer| and |*version| will point to strings
    // that are owned by the instance (and must not be freed by the caller).
    void getGLStrings(const char** vendor, const char** renderer, const char** version) const {
        *vendor = m_graphicsAdapterVendor.c_str();
        *renderer = m_graphicsAdapterName.c_str();
        *version = m_graphicsApiVersion.c_str();
    }

    // Create a new EmulatedEglContext instance for this display instance.
    // |p_config| is the index of one of the configs returned by getConfigs().
    // |p_share| is either EGL_NO_CONTEXT or the handle of a shared context.
    // |version| specifies the GLES version as a GLESApi enum.
    // Return a new handle value, which will be 0 in case of error.
    HandleType createEmulatedEglContext(int p_config, HandleType p_share,
                                        GLESApi version = GLESApi_CM);

    // Destroy a given EmulatedEglContext instance. |p_context| is its handle
    // value as returned by createEmulatedEglContext().
    void destroyEmulatedEglContext(HandleType p_context);

    // Create a new EmulatedEglWindowSurface instance from this display instance.
    // |p_config| is the index of one of the configs returned by getConfigs().
    // |p_width| and |p_height| are the window dimensions in pixels.
    // Return a new handle value, or 0 in case of error.
    HandleType createEmulatedEglWindowSurface(int p_config, int p_width, int p_height);

    // Destroy a given EmulatedEglWindowSurface instance. |p_surcace| is its
    // handle value as returned by createEmulatedEglWindowSurface().
    void destroyEmulatedEglWindowSurface(HandleType p_surface);

    // Returns the set of ColorBuffers destroyed (for further cleanup)
    std::vector<HandleType> destroyEmulatedEglWindowSurfaceLocked(HandleType p_surface);

    void createEmulatedEglFenceSync(EGLenum type, int destroyWhenSignaled,
                                    uint64_t* outSync = nullptr, uint64_t* outSyncThread = nullptr);

    // Call this function when a render thread terminates to destroy all
    // resources it created. Necessary to avoid leaking host resources
    // when a guest application crashes, for example.
    void drainGlRenderThreadResources();

    // Call this function when a render thread terminates to destroy all
    // the remaining contexts it created. Necessary to avoid leaking host
    // contexts when a guest application crashes, for example.
    void drainGlRenderThreadContexts();

    // Call this function when a render thread terminates to destroy all
    // remaining window surface it created. Necessary to avoid leaking
    // host buffers when a guest application crashes, for example.
    void drainGlRenderThreadSurfaces();

    void postLoadRenderThreadContextSurfacePtrs();

    gl::EmulationGl& getEmulationGl();
    bool hasEmulationGl() const { return m_emulationGl != nullptr; }

    vk::VkEmulation& getEmulationVk();
    bool hasEmulationVk() const { return m_emulationVk != nullptr; }

    bool setColorBufferVulkanMode(HandleType colorBufferHandle, uint32_t mode);
    int32_t mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa, uint64_t size);

    // Return the host EGLDisplay used by this instance.
    EGLDisplay getDisplay() const;
    EGLSurface getWindowSurface() const;
    EGLContext getContext() const;
    EGLConfig getConfig() const;

    EGLContext getGlobalEGLContext() const;

    // Return a render context pointer from its handle
    gl::EmulatedEglContextPtr getContext_locked(HandleType p_context);

    // Return a color buffer pointer from its handle
    gl::EmulatedEglWindowSurfacePtr getWindowSurface_locked(HandleType p_windowsurface);

    // Return a TextureDraw instance that can be used with this surfaces
    // and windows created by this instance.
    gl::TextureDraw* getTextureDraw() const;

    bool isFastBlitSupported() const;
    void disableFastBlitForTesting();

    // Create an eglImage and return its handle.  Reference:
    // https://www.khronos.org/registry/egl/extensions/KHR/EGL_KHR_image_base.txt
    HandleType createEmulatedEglImage(HandleType context, EGLenum target, GLuint buffer);
    // Call the implementation of eglDestroyImageKHR, return if succeeds or
    // not. Reference:
    // https://www.khronos.org/registry/egl/extensions/KHR/EGL_KHR_image_base.txt
    EGLBoolean destroyEmulatedEglImage(HandleType image);
    // Copy the content of a EmulatedEglWindowSurface's Pbuffer to its attached
    // ColorBuffer. See the documentation for
    // EmulatedEglWindowSurface::flushColorBuffer().
    // |p_surface| is the target WindowSurface's handle value.
    // Returns true on success, false on failure.
    bool flushEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface);

    GLESDispatchMaxVersion getMaxGlesVersion();

    // Fill GLES usage protobuf
    void fillGLESUsages(android_studio::EmulatorGLESUsages*);

    void* platformCreateSharedEglContext(void);
    bool platformDestroySharedEglContext(void* context);

    bool flushColorBufferFromGl(HandleType colorBufferHandle);

    bool invalidateColorBufferForGl(HandleType colorBufferHandle);

    ContextHelper* getPbufferSurfaceContextHelper() const;

    // Bind the current context's EGL_TEXTURE_2D texture to a ColorBuffer
    // instance's EGLImage. This is intended to implement
    // glEGLImageTargetTexture2DOES() for all GLES versions.
    // |p_colorbuffer| is the ColorBuffer's handle value.
    // Returns true on success, false on failure.
    bool bindColorBufferToTexture(HandleType p_colorbuffer);
    bool bindColorBufferToTexture2(HandleType p_colorbuffer);

    // Bind the current context's EGL_RENDERBUFFER_OES render buffer to this
    // ColorBuffer's EGLImage. This is intended to implement
    // glEGLImageTargetRenderbufferStorageOES() for all GLES versions.
    // |p_colorbuffer| is the ColorBuffer's handle value.
    // Returns true on success, false on failure.
    bool bindColorBufferToRenderbuffer(HandleType p_colorbuffer);

    // Equivalent for eglMakeCurrent() for the current display.
    // |p_context|, |p_drawSurface| and |p_readSurface| are the handle values
    // of the context, the draw surface and the read surface, respectively.
    // Returns true on success, false on failure.
    // Note: if all handle values are 0, this is an unbind operation.
    bool bindContext(HandleType p_context, HandleType p_drawSurface, HandleType p_readSurface);

    // create a Y texture and a UV texture with width and height, the created
    // texture ids are stored in textures respectively
    void createYUVTextures(uint32_t type, uint32_t count, int width, int height, uint32_t* output);
    void destroyYUVTextures(uint32_t type, uint32_t count, uint32_t* textures);
    void updateYUVTextures(uint32_t type, uint32_t* textures, void* privData, void* func);
    void swapTexturesAndUpdateColorBuffer(uint32_t colorbufferhandle, int x, int y, int width,
                                          int height, uint32_t format, uint32_t type,
                                          uint32_t texture_type, uint32_t* textures);

    // Reads back the raw color buffer to |pixels|
    // if |pixels| is not null.
    // Always returns in |numBytes| how many bytes were
    // planned to be transmitted.
    // |numBytes| is not an input parameter;
    // fewer or more bytes cannot be specified.
    // If the framework format is YUV, it will read
    // back as raw YUV data.
    bool readColorBufferContents(HandleType p_colorbuffer, size_t* numBytes, void* pixels);

    void asyncWaitForGpuWithCb(uint64_t eglsync, FenceCompletionCallback cb);

    const gl::EGLDispatch* getEglDispatch();
    const gl::GLESv2Dispatch* getGles2Dispatch();
#endif

    const gfxstream::host::FeatureSet& getFeatures() const { return m_features; }

    RepresentativeColorBufferMemoryTypeInfo getRepresentativeColorBufferMemoryTypeInfo() const;

   private:
    Impl(int p_width, int p_height, const FeatureSet& features, bool useSubWindow);

    // Requires the caller to hold the m_colorBufferMapLock until the new handle is inserted into of
    // the object handle maps.
    HandleType genHandle_locked();

    bool removeSubWindow_locked();
    // Returns the set of ColorBuffers destroyed (for further cleanup)
    std::vector<HandleType> cleanupProcGLObjects_locked(uint64_t puid, bool forced = false);

    void markOpened(ColorBufferRef* cbRef);
    // Returns true if the color buffer was erased.
    bool closeColorBufferLocked(HandleType p_colorbuffer, bool forced = false);
    // Returns true if this was the last ref and we need to destroy stuff.
    bool decColorBufferRefCountLocked(HandleType p_colorbuffer);
    // Decrease refcount but not destroy the object.
    // Mainly used in post thread, when we need to destroy the object but cannot in post thread.
    void decColorBufferRefCountNoDestroy(HandleType p_colorbuffer);
    // Close all expired color buffers for real.
    // Treat all delayed color buffers as expired if forced=true
    void performDelayedColorBufferCloseLocked(bool forced = false);
    void eraseDelayedCloseColorBufferLocked(HandleType cb, uint64_t ts);

    AsyncResult postImpl(HandleType p_colorbuffer, Post::CompletionCallback callback,
                         bool needLockAndBind = true, bool repaint = false);
    bool postImplSync(HandleType p_colorbuffer, bool needLockAndBind = true, bool repaint = false);
    void setGuestPostedAFrame() {
        m_guestPostedAFrame = true;
        fireEvent({FrameBufferChange::FrameReady, mFrameNumber++});
    }
    HandleType createColorBufferWithResourceHandleLocked(int p_width, int p_height,
                                                         GLenum p_internalFormat,
                                                         FrameworkFormat p_frameworkFormat,
                                                         HandleType handle);
    HandleType createBufferWithResourceHandleLocked(int p_size, HandleType handle,
                                                    uint32_t memoryProperty);

    void recomputeLayout();
    void setDisplayPoseInSkinUI(int totalHeight);
    void sweepColorBuffersLocked();

    std::future<void> blockPostWorker(std::future<void> continueSignal);

    gfxstream::host::FeatureSet m_features;
    int m_x = 0;
    int m_y = 0;
    int m_framebufferWidth = 0;
    int m_framebufferHeight = 0;
    std::atomic_int m_windowWidth = 0;
    std::atomic_int m_windowHeight = 0;
    // When zoomed in, the size of the content is bigger than the window size, and we only
    // display / store a portion of it.
    int m_windowContentFullWidth = 0;
    int m_windowContentFullHeight = 0;
    float m_dpr = 0;

    bool m_useSubWindow = false;

    bool m_fpsStats = false;
    bool m_perfStats = false;
    int m_statsNumFrames = 0;
    long long m_statsStartTime = 0;

    gfxstream::base::Lock m_lock;
    gfxstream::base::ReadWriteLock m_contextStructureLock;
    gfxstream::base::Lock m_colorBufferMapLock;
    uint64_t mFrameNumber = 0;
    FBNativeWindowType m_nativeWindow = 0;

    ColorBufferMap m_colorbuffers;
    BufferMap m_buffers;

    // A collection of color buffers that were closed without any usages
    // (|opened| == false).
    //
    // If a buffer reached |refcount| == 0 while not being |opened|, instead of
    // deleting it we remember the timestamp when this happened. Later, we
    // check if the buffer stayed unopened long enough and if it did, we delete
    // it permanently. On the other hand, if the color buffer was used then
    // we don't care about timestamps anymore.
    //
    // Note: this collection is ordered by |ts| field.
    struct ColorBufferCloseInfo {
        uint64_t ts;          // when we got the close request.
        HandleType cbHandle;  // 0 == already closed, do nothing
    };
    using ColorBufferDelayedClose = std::vector<ColorBufferCloseInfo>;
    ColorBufferDelayedClose m_colorBufferDelayedCloseList;

    EGLNativeWindowType m_subWin = {};
    HandleType m_lastPostedColorBuffer = 0;
    float m_zRot = 0;
    float m_px = 0;
    float m_py = 0;

    // Async readback
    enum class ReadbackCmd {
        Init = 0,
        GetPixels = 1,
        AddRecordDisplay = 2,
        DelRecordDisplay = 3,
        Exit = 4,
    };
    struct Readback {
        ReadbackCmd cmd;
        uint32_t displayId;
        void* pixelsOut;
        uint32_t bytes;
        uint32_t width;
        uint32_t height;
    };
    gfxstream::base::WorkerProcessingResult sendReadbackWorkerCmd(const Readback& readback);
    bool m_guestPostedAFrame = false;

    struct onPost {
        Renderer::OnPostCallback cb;
        void* context;
        uint32_t displayId;
        uint32_t width;
        uint32_t height;
        unsigned char* img = nullptr;
        bool readBgra;
        ~onPost() {
            if (img) {
                delete[] img;
                img = nullptr;
            }
        }
    };
    std::map<uint32_t, onPost> m_onPost;
    ReadbackWorker* m_readbackWorker = nullptr;
    gfxstream::base::WorkerThread<Readback> m_readbackThread;
    std::atomic_bool m_readbackThreadStarted = false;

    std::string m_graphicsAdapterVendor;
    std::string m_graphicsAdapterName;
    std::string m_graphicsApiVersion;
    std::string m_graphicsApiExtensions;
    std::string m_graphicsDeviceExtensions;
    gfxstream::base::Lock m_procOwnedResourcesLock;
    std::unordered_map<uint64_t, std::unique_ptr<ProcessResources>> m_procOwnedResources;

    // Flag set when emulator is shutting down.
    bool m_shuttingDown = false;

    // When this feature is enabled, open/close operations from gralloc in guest
    // will no longer control the reference counting of color buffers on host.
    // Instead, it will be managed by a file descriptor in the guest kernel. In
    // case all the native handles in guest are destroyed, the pipe will be
    // automatically closed by the kernel. We only need to do reference counting
    // for color buffers attached in window surface.
    bool m_refCountPipeEnabled = false;

    // When this feature is enabled, and m_refCountPipeEnabled == false, color
    // buffer close operations will immediately close the color buffer if host
    // refcount hits 0. This is for use with guest kernels where the color
    // buffer is already tied to a file descriptor in the guest kernel.
    bool m_noDelayCloseColorBufferEnabled = false;

    std::unique_ptr<PostWorker> m_postWorker = {};
    std::atomic_bool m_postThreadStarted = false;
    gfxstream::base::WorkerThread<Post> m_postThread;
    gfxstream::base::WorkerProcessingResult postWorkerFunc(Post& post);
    std::future<void> sendPostWorkerCmd(Post post);

    bool m_vulkanEnabled = false;
    // Whether the guest manages ColorBuffer lifetime
    // so we don't need refcounting on the host side.
    bool m_guestManagedColorBufferLifetime = false;

    gfxstream::base::MessageChannel<HandleType, 1024> mOutstandingColorBufferDestroys;

    Compositor* m_compositor = nullptr;
    bool m_useVulkanComposition = false;

    std::unique_ptr<vk::VkEmulation> m_emulationVk;

    // The implementation for Vulkan native swapchain. Only initialized when useVulkan is set when
    // calling FrameBuffer::initialize(). DisplayVk is actually owned by VkEmulation.
    vk::DisplayVk* m_displayVk = nullptr;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    std::unique_ptr<gfxstream::host::RenderDoc> m_renderDoc = nullptr;

    // TODO(b/233939967): Refactor to create DisplayGl and DisplaySurfaceGl
    // and remove usage of non-generic DisplayVk.
    Display* m_display = nullptr;
    std::unique_ptr<DisplaySurface> m_displaySurface;

    // CompositorGl.
    // TODO: update RenderDoc to be a DisplaySurfaceUser.
    std::vector<DisplaySurfaceUser*> m_displaySurfaceUsers;

    // UUIDs of physical devices for Vulkan and GLES, respectively.  In most
    // cases, this determines whether we can support zero-copy interop.
    using VkUuid = std::array<uint8_t, VK_UUID_SIZE>;
    VkUuid m_vulkanUUID{};

    // Tracks platform EGL contexts that have been handed out to other users,
    // indexed by underlying native EGL context object.

    std::unique_ptr<MetricsLogger> m_logger;
    std::unique_ptr<HealthMonitor<>> m_healthMonitor;

    int m_vsyncHz = 60;

    // Vsync thread.
    std::unique_ptr<VsyncThread> m_vsyncThread = {};

    struct DisplayConfig {
        int w;
        int h;
        int dpiX;
        int dpiY;
        DisplayConfig() {}
        DisplayConfig(int w, int h, int x, int y) : w(w), h(h), dpiX(x), dpiY(y) {}
    };
    std::map<int, DisplayConfig> mDisplayConfigs;
    int mDisplayActiveConfigId = -1;

    std::unique_ptr<gl::EmulationGl> m_emulationGl;

    // The host associates color buffers with guest processes for memory
    // cleanup. Guest processes are identified with a host generated unique ID.
    // TODO(kaiyili): move all those resources to the ProcessResources struct.
    ProcOwnedColorBuffers m_procOwnedColorBuffers;
    ProcOwnedCleanupCallbacks m_procOwnedCleanupCallbacks;

#if GFXSTREAM_ENABLE_HOST_GLES
    gl::EmulatedEglContextMap m_contexts;
    gl::EmulatedEglImageMap m_images;
    gl::EmulatedEglWindowSurfaceMap m_windows;

    std::unordered_map<HandleType, HandleType> m_EmulatedEglWindowSurfaceToColorBuffer;

    ProcOwnedEmulatedEGLImages m_procOwnedEmulatedEglImages;
    ProcOwnedEmulatedEglContexts m_procOwnedEmulatedEglContexts;
    ProcOwnedEmulatedEglWindowSurfaces m_procOwnedEmulatedEglWindowSurfaces;
    gl::DisplayGl* m_displayGl = nullptr;

    struct PlatformEglContextInfo {
        EGLContext context;
        EGLSurface surface;
    };

    std::unordered_map<void*, PlatformEglContextInfo> m_platformEglContexts;
#endif
};

void MaybeIncreaseFileDescriptorSoftLimit() {
#if defined(__linux__)
    // Cuttlefish with Gfxstream on Nvidia and SwiftShader often hits the default nofile
    // soft limit (1024) when running large test suites.
    struct rlimit nofileLimits = {
        .rlim_cur = 0,
        .rlim_max = 0,
    };

    int ret = getrlimit(RLIMIT_NOFILE, &nofileLimits);
    if (ret) {
        GFXSTREAM_ERROR("Warning: failed to query nofile limits.");
        return;
    }

    const auto softLimit = nofileLimits.rlim_cur;
    const auto hardLimit = nofileLimits.rlim_max;

    constexpr const rlim_t kDesiredNofileSoftLimit = 4096;

    if (softLimit < kDesiredNofileSoftLimit) {
        if (softLimit == hardLimit) {
            GFXSTREAM_ERROR("Warning: unable to raise nofile soft limit - already at hard limit.");
            return;
        }

        if (kDesiredNofileSoftLimit > hardLimit) {
            GFXSTREAM_ERROR(
                "Warning: unable to raise nofile soft limit to desired %d - hard limit is %d.",
                static_cast<int>(kDesiredNofileSoftLimit), static_cast<int>(hardLimit));
        }

        const rlim_t requestedSoftLimit = std::min(kDesiredNofileSoftLimit, hardLimit);

        struct rlimit requestedNofileLimits = {
            .rlim_cur = requestedSoftLimit,
            .rlim_max = hardLimit,
        };

        ret = setrlimit(RLIMIT_NOFILE, &requestedNofileLimits);
        if (ret) {
            GFXSTREAM_ERROR("Warning: failed to raise nofile soft limit to %d: %s (%d)",
                            static_cast<int>(requestedSoftLimit), strerror(errno), errno);
            return;
        }

        GFXSTREAM_INFO("Raised nofile soft limit to %d.", static_cast<int>(requestedSoftLimit));
    } else {
        GFXSTREAM_INFO("Not raising nofile soft limit from %d.", static_cast<int>(softLimit));
    }
#endif
}

std::unique_ptr<FrameBuffer::Impl> FrameBuffer::Impl::Create(FrameBuffer* framebuffer,
                                                             uint32_t width, uint32_t height,
                                                             const FeatureSet& features,
                                                             bool useSubWindow, bool egl2egl) {
    GFXSTREAM_DEBUG("FrameBuffer::Impl::initialize");

    std::unique_ptr<Impl> impl(new Impl(width, height, features, useSubWindow));
    if (!impl) {
        GFXSTREAM_ERROR("Failed to allocate FrameBuffer::Impl.");
        return nullptr;
    }

    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "FrameBuffer::Impl::Init()");

    std::unique_ptr<gfxstream::host::RenderDocWithMultipleVkInstances> renderDocMultipleVkInstances = nullptr;
    if (!gfxstream::base::getEnvironmentVariable("ANDROID_EMU_RENDERDOC").empty()) {
        SharedLibrary* renderdocLib = nullptr;
#ifdef _WIN32
        renderdocLib = SharedLibrary::open(R"(C:\Program Files\RenderDoc\renderdoc.dll)");
#elif defined(__linux__)
        renderdocLib = SharedLibrary::open("librenderdoc.so");
#endif
        impl->m_renderDoc = gfxstream::host::RenderDoc::create(renderdocLib);
        if (impl->m_renderDoc) {
            GFXSTREAM_INFO("RenderDoc integration enabled.");
            renderDocMultipleVkInstances =
                std::make_unique<gfxstream::host::RenderDocWithMultipleVkInstances>(
                    *impl->m_renderDoc);
            if (!renderDocMultipleVkInstances) {
                GFXSTREAM_ERROR(
                    "Failed to initialize RenderDoc with multiple VkInstances. Can't capture any "
                    "information from guest VkInstances with RenderDoc.");
            }
        }
    }
    // Initialize Vulkan emulation state
    //
    // Note: This must happen before any use of s_egl,
    // or it's possible that the existing EGL display and contexts
    // used by underlying EGL driver might become invalid,
    // preventing new contexts from being created that share
    // against those contexts.
    vk::VulkanDispatch* vkDispatch = nullptr;
    if (impl->m_features.Vulkan.enabled) {
        vkDispatch = vk::vkDispatch(false /* not for testing */);

        gfxstream::host::BackendCallbacks callbacks{
            .registerProcessCleanupCallback =
                [impl = impl.get()](void* key, uint64_t contextId, std::function<void()> callback) {
                    impl->registerProcessCleanupCallback(key, contextId, callback);
                },
            .unregisterProcessCleanupCallback =
                [impl = impl.get()](void* key) { impl->unregisterProcessCleanupCallback(key); },
            .invalidateColorBuffer =
                [impl = impl.get()](uint32_t colorBufferHandle) {
                    impl->invalidateColorBufferForVk(colorBufferHandle);
                },
            .flushColorBuffer =
                [impl = impl.get()](uint32_t colorBufferHandle) {
                    impl->flushColorBufferFromVk(colorBufferHandle);
                },
            .flushColorBufferFromBytes =
                [impl = impl.get()](uint32_t colorBufferHandle, const void* bytes,
                                    size_t bytesSize) {
                    impl->flushColorBufferFromVkBytes(colorBufferHandle, bytes, bytesSize);
                },
            .scheduleAsyncWork =
                [impl = impl.get()](std::function<void()> work, std::string description) {
                    auto promise = std::make_shared<AutoCancelingPromise>();
                    auto future = promise->GetFuture();
                    SyncThread::get()->triggerGeneral(
                        [promise = std::move(promise), work = std::move(work)]() mutable {
                            work();
                            promise->MarkComplete();
                        },
                        description);
                    return future;
                },
#ifdef CONFIG_AEMU
            .registerVulkanInstance =
                [impl = impl.get()](uint64_t id, const char* appName) {
                    impl->registerVulkanInstance(id, appName);
                },
            .unregisterVulkanInstance =
                [impl = impl.get()](uint64_t id) { impl->unregisterVulkanInstance(id); },
#endif
        };
        impl->m_emulationVk = vk::VkEmulation::create(vkDispatch, callbacks, impl->m_features);
        if (impl->m_emulationVk) {
            vk::VkDecoderGlobalState::initialize(impl->m_emulationVk.get());
        } else {
            GFXSTREAM_ERROR(
                "Failed to initialize global Vulkan emulation. Disable the Vulkan support.");
        }
    }
    if (impl->m_emulationVk) {
        impl->m_vulkanEnabled = true;
        if (impl->m_features.VulkanNativeSwapchain.enabled) {
            impl->m_vkInstance = impl->m_emulationVk->getInstance();
        }

        auto vulkanUuidOpt = impl->m_emulationVk->getDeviceUuid();
        if (vulkanUuidOpt) {
            impl->m_vulkanUUID = *vulkanUuidOpt;
        } else {
            GFXSTREAM_WARNING("Doesn't support id properties, no vulkan device UUID");
        }
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    // Do not initialize GL emulation if the guest is using ANGLE.
    if (!impl->m_features.GuestVulkanOnly.enabled) {
        impl->m_emulationGl =
            EmulationGl::create(width, height, impl->m_features, useSubWindow, egl2egl);
        if (!impl->m_emulationGl) {
            GFXSTREAM_ERROR("Failed to initialize GL emulation.");
            return nullptr;
        }
    }
#endif

    impl->m_useVulkanComposition =
        impl->m_features.GuestVulkanOnly.enabled || impl->m_features.VulkanNativeSwapchain.enabled;

    vk::VkEmulation::Features vkEmulationFeatures = {
        .glInteropSupported = false,  // Set later.
        .deferredCommands =
            gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_DISABLE_DEFERRED_COMMANDS")
                .empty(),
        .createResourceWithRequirements =
            gfxstream::base::getEnvironmentVariable(
                "ANDROID_EMU_VK_DISABLE_USE_CREATE_RESOURCES_WITH_REQUIREMENTS")
                .empty(),
        .useVulkanComposition = impl->m_useVulkanComposition,
        .useVulkanNativeSwapchain = impl->m_features.VulkanNativeSwapchain.enabled,
        .guestRenderDoc = std::move(renderDocMultipleVkInstances),
        .astcLdrEmulationMode = AstcEmulationMode::Gpu,
        .enableEtc2Emulation = true,
        .enableYcbcrEmulation = false,
        .guestVulkanOnly = impl->m_features.GuestVulkanOnly.enabled,
        .useDedicatedAllocations = false,  // Set later.
    };

    //
    // Cache the GL strings so we don't have to think about threading or
    // current-context when asked for them.
    //
    bool useVulkanGraphicsDiagInfo = impl->m_emulationVk &&
                                     impl->m_features.VulkanNativeSwapchain.enabled &&
                                     impl->m_features.GuestVulkanOnly.enabled;

    if (useVulkanGraphicsDiagInfo) {
        impl->m_graphicsAdapterVendor = impl->m_emulationVk->getGpuVendor();
        impl->m_graphicsAdapterName = impl->m_emulationVk->getGpuName();
        impl->m_graphicsApiVersion = impl->m_emulationVk->getGpuVersionString();
        impl->m_graphicsApiExtensions = impl->m_emulationVk->getInstanceExtensionsString();
        impl->m_graphicsDeviceExtensions = impl->m_emulationVk->getDeviceExtensionsString();
    } else if (impl->m_emulationGl) {
#if GFXSTREAM_ENABLE_HOST_GLES
        impl->m_graphicsAdapterVendor = impl->m_emulationGl->getGlesVendor();
        impl->m_graphicsAdapterName = impl->m_emulationGl->getGlesRenderer();
        impl->m_graphicsApiVersion = impl->m_emulationGl->getGlesVersionString();
        impl->m_graphicsApiExtensions = impl->m_emulationGl->getGlesExtensionsString();
        impl->m_graphicsDeviceExtensions = "N/A";
#endif
    } else {
        impl->m_graphicsAdapterVendor = "N/A";
        impl->m_graphicsAdapterName = "N/A";
        impl->m_graphicsApiVersion = "N/A";
        impl->m_graphicsApiExtensions = "N/A";
        impl->m_graphicsDeviceExtensions = "N/A";
    }

    // Attempt to get the device UUID of the gles and match with Vulkan. If
    // they match, interop is possible. If they don't, then don't trust the
    // result of interop query to egl and fall back to CPU copy, as we might
    // have initialized Vulkan devices and GLES contexts from different
    // physical devices.

    bool vulkanInteropSupported = true;
    // First, if the VkEmulation instance doesn't support ext memory capabilities,
    // it won't support uuids.
    if (!impl->m_emulationVk || !impl->m_emulationVk->supportsPhysicalDeviceIDProperties()) {
        vulkanInteropSupported = false;
    }
    if (!impl->m_emulationGl) {
        vulkanInteropSupported = false;
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        if (!impl->m_emulationGl->isGlesVulkanInteropSupported()) {
            vulkanInteropSupported = false;
        }
        const auto& glesDeviceUuid = impl->m_emulationGl->getGlesDeviceUuid();
        if (!glesDeviceUuid || glesDeviceUuid != impl->m_vulkanUUID) {
            vulkanInteropSupported = false;
        }
#endif
    }

    if (gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD") == "lavapipe"
            || gfxstream::base::getEnvironmentVariable("ANDROID_EMU_VK_ICD") == "swiftshader") {
        vulkanInteropSupported = false;
        GFXSTREAM_DEBUG("vk icd software rendering, disable interop");
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (vulkanInteropSupported && impl->m_emulationGl && impl->m_emulationGl->isMesa()) {
        // Mesa currently expects dedicated allocations for external memory sharing
        // between GL and VK. See b/265186355.
        vkEmulationFeatures.useDedicatedAllocations = true;
    }
#endif

    GFXSTREAM_DEBUG("glvk interop final: %d", vulkanInteropSupported);
    vkEmulationFeatures.glInteropSupported = vulkanInteropSupported;
    if (impl->m_emulationVk && impl->m_features.Vulkan.enabled) {
        impl->m_emulationVk->initFeatures(std::move(vkEmulationFeatures));

        auto* display = impl->m_emulationVk->getDisplay();
        if (display) {
            impl->m_displayVk = display;
            impl->m_displaySurfaceUsers.push_back(impl->m_displayVk);
        }
    }

    if (impl->m_emulationVk && impl->m_useVulkanComposition) {
        impl->m_compositor = impl->m_emulationVk->getCompositor();
        if (!impl->m_compositor) {
            GFXSTREAM_ERROR("Failed to get CompositorVk from VkEmulation.");
            return nullptr;
        }
        GFXSTREAM_DEBUG("Performing composition using CompositorVk.");
    } else {
        GFXSTREAM_DEBUG("Performing composition using CompositorGl.");
#if GFXSTREAM_ENABLE_HOST_GLES
        auto compositorGl = impl->m_emulationGl->getCompositor();
        impl->m_compositor = compositorGl;
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (impl->m_emulationGl) {
        auto displayGl = impl->m_emulationGl->getDisplay();
        impl->m_displayGl = displayGl;
        impl->m_displaySurfaceUsers.push_back(displayGl);
    }
#endif

    GFXSTREAM_INFO("Graphics Adapter Vendor %s", impl->m_graphicsAdapterVendor.c_str());
    GFXSTREAM_INFO("Graphics Adapter %s", impl->m_graphicsAdapterName.c_str());
    GFXSTREAM_INFO("Graphics API Version %s", impl->m_graphicsApiVersion.c_str());
    GFXSTREAM_INFO("Graphics API Extensions %s", impl->m_graphicsApiExtensions.c_str());
    GFXSTREAM_INFO("Graphics Device Extensions %s", impl->m_graphicsDeviceExtensions.c_str());

    if (impl->m_useVulkanComposition) {
        impl->m_postWorker.reset(
            new PostWorkerVk(framebuffer, impl->m_compositor, impl->m_displayVk));
    } else {
        const bool shouldPostOnlyOnMainThread = postOnlyOnMainThread();

#if GFXSTREAM_ENABLE_HOST_GLES
        PostWorkerGl* postWorkerGl =
            new PostWorkerGl(shouldPostOnlyOnMainThread, framebuffer, impl->m_compositor,
                             impl->m_displayGl, impl->m_emulationGl.get());
        impl->m_postWorker.reset(postWorkerGl);
        impl->m_displaySurfaceUsers.push_back(postWorkerGl);
#endif
    }

    // Start up the single sync thread. If we are using Vulkan native
    // swapchain, then don't initialize SyncThread worker threads with EGL
    // contexts.
    SyncThread::initialize(
        /* hasGL */ impl->m_emulationGl != nullptr, impl->getHealthMonitor());

    // Start the vsync thread
    const uint64_t kOneSecondNs = 1000000000ULL;
    impl->m_vsyncThread.reset(new VsyncThread((uint64_t)kOneSecondNs / (uint64_t)impl->m_vsyncHz));

    // Nothing else to do - we're ready to rock!
    return impl;
}

FrameBuffer::Impl::Impl(int p_width, int p_height, const gfxstream::host::FeatureSet& features,
                        bool useSubWindow)
    : m_features(features),
      m_framebufferWidth(p_width),
      m_framebufferHeight(p_height),
      m_windowWidth(p_width),
      m_windowHeight(p_height),
      m_useSubWindow(useSubWindow),
      m_fpsStats(getenv("SHOW_FPS_STATS") != nullptr),
      m_readbackThread([this](FrameBuffer::Impl::Readback&& readback) {
          return sendReadbackWorkerCmd(readback);
      }),
      m_refCountPipeEnabled(features.RefCountPipe.enabled),
      m_noDelayCloseColorBufferEnabled(features.NoDelayCloseColorBuffer.enabled ||
                                       features.Minigbm.enabled),
      m_postThread([this](Post&& post) { return postWorkerFunc(post); }),
      m_logger(CreateMetricsLogger()),
      m_healthMonitor(CreateHealthMonitor(*m_logger)) {
    mDisplayActiveConfigId = 0;
    mDisplayConfigs[0] = {p_width, p_height, 160, 160};
    uint32_t displayId = 0;
    if (createDisplay(&displayId) < 0) {
        GFXSTREAM_ERROR( "Failed to create default display");
    }

    setDisplayPose(displayId, 0, 0, getWidth(), getHeight(), 0);
}

FrameBuffer::Impl::~Impl() {
    AutoLock fbLock(m_lock);

    m_postThread.enqueue({PostCmd::Exit});
    m_postThread.join();
    m_postWorker.reset();

    // Run other cleanup callbacks
    // Avoid deadlock by first storing a separate list of callbacks
    std::vector<std::function<void()>> callbacks;
    for (auto procIte : m_procOwnedCleanupCallbacks)
    {
        for (auto it : procIte.second) {
            callbacks.push_back(it.second);
        }
    }
    m_procOwnedCleanupCallbacks.clear();

    fbLock.unlock();

    for (auto cb : callbacks) {
        cb();
    }

    fbLock.lock();

    if (m_useSubWindow) {
        removeSubWindow_locked();
    }

    m_readbackThread.enqueue({ReadbackCmd::Exit});
    m_readbackThread.join();

    m_vsyncThread.reset();

    SyncThread::destroy();

    sweepColorBuffersLocked();

    m_buffers.clear();
    {
        AutoLock lock(m_colorBufferMapLock);
        m_colorbuffers.clear();
    }
    m_colorBufferDelayedCloseList.clear();

#if GFXSTREAM_ENABLE_HOST_GLES
    m_windows.clear();
    m_contexts.clear();

    for (auto it : m_platformEglContexts) {
        destroySharedTrivialContext(it.second.context, it.second.surface);
    }
#endif

    if (m_emulationGl) {
        m_emulationGl.reset();
    }
    if (m_emulationVk) {
        m_emulationVk.reset();
    }
}

WorkerProcessingResult FrameBuffer::Impl::sendReadbackWorkerCmd(const Readback& readback) {
    ensureReadbackWorker();
    switch (readback.cmd) {
    case ReadbackCmd::Init:
        m_readbackWorker->init();
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::GetPixels:
        m_readbackWorker->getPixels(readback.displayId, readback.pixelsOut, readback.bytes);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::AddRecordDisplay:
        m_readbackWorker->initReadbackForDisplay(readback.displayId, readback.width, readback.height);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::DelRecordDisplay:
        m_readbackWorker->deinitReadbackForDisplay(readback.displayId);
        return WorkerProcessingResult::Continue;
    case ReadbackCmd::Exit:
        return WorkerProcessingResult::Stop;
    }
    return WorkerProcessingResult::Stop;
}

WorkerProcessingResult FrameBuffer::Impl::postWorkerFunc(Post& post) {
    auto annotations = std::make_unique<EventHangMetadata::HangAnnotations>();
    if (m_healthMonitor)
        annotations->insert(
            {"Post command opcode", std::to_string(static_cast<uint64_t>(post.cmd))});
    auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "PostWorker main function")
                        .setAnnotations(std::move(annotations))
                        .build();
    switch (post.cmd) {
        case PostCmd::Post: {
            // We wrap the callback like this to workaround a bug in the MS STL implementation.
            auto packagePostCmdCallback =
                std::shared_ptr<Post::CompletionCallback>(std::move(post.completionCallback));
            std::unique_ptr<Post::CompletionCallback> postCallback =
                std::make_unique<Post::CompletionCallback>(
                    [packagePostCmdCallback](std::shared_future<void> waitForGpu) {
                        SyncThread::get()->triggerGeneral(
                            [composeCallback = std::move(packagePostCmdCallback), waitForGpu] {
                                (*composeCallback)(waitForGpu);
                            },
                            "Wait for post");
                    });
            m_postWorker->post(post.cb, std::move(postCallback));
            decColorBufferRefCountNoDestroy(post.cbHandle);
            break;
        }
        case PostCmd::Viewport:
            m_postWorker->viewport(post.viewport.width,
                                   post.viewport.height);
            break;
        case PostCmd::Compose: {
            std::unique_ptr<FlatComposeRequest> composeRequest;
            std::unique_ptr<Post::CompletionCallback> composeCallback;
            if (post.composeVersion <= 1) {
                composeCallback = std::move(post.completionCallback);
                composeRequest = ToFlatComposeRequest((ComposeDevice*)post.composeBuffer.data());
            } else {
                // std::shared_ptr(std::move(...)) is WA for MSFT STL implementation bug:
                // https://developercommunity.visualstudio.com/t/unable-to-move-stdpackaged-task-into-any-stl-conta/108672
                auto packageComposeCallback =
                    std::shared_ptr<Post::CompletionCallback>(std::move(post.completionCallback));
                composeCallback = std::make_unique<Post::CompletionCallback>(
                    [packageComposeCallback](
                        std::shared_future<void> waitForGpu) {
                        SyncThread::get()->triggerGeneral(
                            [composeCallback = std::move(packageComposeCallback), waitForGpu] {
                                (*composeCallback)(waitForGpu);
                            },
                            "Wait for host composition");
                    });
                composeRequest = ToFlatComposeRequest((ComposeDevice_v2*)post.composeBuffer.data());
            }
            m_postWorker->compose(std::move(composeRequest), std::move(composeCallback));
            break;
        }
        case PostCmd::Clear:
            m_postWorker->clear();
            break;
        case PostCmd::Screenshot:
            m_postWorker->screenshot(
                    post.screenshot.cb, post.screenshot.screenwidth,
                    post.screenshot.screenheight, post.screenshot.format,
                    post.screenshot.type, post.screenshot.rotation,
                    post.screenshot.pixels, post.screenshot.rect);
            decColorBufferRefCountNoDestroy(post.cbHandle);
            break;
        case PostCmd::Block:
            m_postWorker->block(std::move(post.block->scheduledSignal),
                                std::move(post.block->continueSignal));
            break;
        case PostCmd::Exit:
            m_postWorker->exit();
            return WorkerProcessingResult::Stop;
        default:
            break;
    }
    return WorkerProcessingResult::Continue;
}

std::future<void> FrameBuffer::Impl::sendPostWorkerCmd(Post post) {
    bool expectedPostThreadStarted = false;
    if (m_postThreadStarted.compare_exchange_strong(expectedPostThreadStarted, true)) {
        m_postThread.start();
    }

    bool shouldPostOnlyOnMainThread = postOnlyOnMainThread();
    // If we want to run only in the main thread and we are actually running
    // in the main thread already, don't use the PostWorker thread. Ideally,
    // PostWorker should handle this and dispatch directly, but we'll need to
    // transfer ownership of the thread to PostWorker.
    // TODO(lfy): do that refactor
    // For now, this fixes a screenshot issue on macOS.
    std::future<void> res = std::async(std::launch::deferred, [] {});
    res.wait();
    if (shouldPostOnlyOnMainThread && (PostCmd::Screenshot == post.cmd) &&
        get_gfxstream_window_operations().is_current_thread_ui_thread()) {
        post.cb->readToBytesScaled(post.screenshot.screenwidth, post.screenshot.screenheight,
                                   post.screenshot.format, post.screenshot.type,
                                   post.screenshot.rotation, post.screenshot.rect,
                                   post.screenshot.pixels);
    } else {
        std::future<void> completeFuture =
            m_postThread.enqueue(Post(std::move(post)));
        if (!shouldPostOnlyOnMainThread ||
            (PostCmd::Screenshot == post.cmd &&
             !get_gfxstream_window_operations().is_current_thread_ui_thread())) {
            res = std::move(completeFuture);
        }
    }
    return res;
}

void FrameBuffer::Impl::setPostCallback(Renderer::OnPostCallback onPost, void* onPostContext,
                                        uint32_t displayId, bool useBgraReadback) {
    AutoLock lock(m_lock);
    if (onPost) {
        uint32_t w, h;
        if (!get_gfxstream_multi_display_operations().get_display_info(
                displayId, nullptr, nullptr, &w, &h, nullptr, nullptr, nullptr)) {
            GFXSTREAM_ERROR("display %d not exist, cancelling OnPost callback", displayId);
            return;
        }
        if (m_onPost.find(displayId) != m_onPost.end()) {
            GFXSTREAM_ERROR("display %d already configured for recording", displayId);
            return;
        }
        m_onPost[displayId].cb = onPost;
        m_onPost[displayId].context = onPostContext;
        m_onPost[displayId].displayId = displayId;
        m_onPost[displayId].width = w;
        m_onPost[displayId].height = h;
        m_onPost[displayId].img = new unsigned char[4 * w * h];
        m_onPost[displayId].readBgra = useBgraReadback;
        bool expectedReadbackThreadStarted = false;
        if (m_readbackThreadStarted.compare_exchange_strong(expectedReadbackThreadStarted, true)) {
            m_readbackThread.start();
            m_readbackThread.enqueue({ ReadbackCmd::Init });
        }
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::AddRecordDisplay, displayId, nullptr, 0, w, h});
        completeFuture.wait();
    } else {
        std::future<void> completeFuture = m_readbackThread.enqueue(
            {ReadbackCmd::DelRecordDisplay, displayId});
        completeFuture.wait();
        m_onPost.erase(displayId);
    }
}

static void subWindowRepaint(void* param) {
    GFXSTREAM_DEBUG("call repost from subWindowRepaint callback");
    auto fb = static_cast<FrameBuffer*>(param);
    fb->repost();
}

bool FrameBuffer::Impl::setupSubWindow(FBNativeWindowType p_window, int wx, int wy, int ww, int wh,
                                       int fbw, int fbh, float dpr, float zRot, bool deleteExisting,
                                       bool hideWindow) {
    GFXSTREAM_DEBUG("Begin setupSubWindow");
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("%s: Cannot create native sub-window in this configuration\n",
                        __FUNCTION__);
        return false;
    }

    // Do a quick check before even taking the lock - maybe we don't need to
    // do anything here.

    const bool shouldCreateSubWindow = !m_subWin || deleteExisting;

    // On Mac, since window coordinates are Y-up and not Y-down, the
    // subwindow may not change dimensions, but because the main window
    // did, the subwindow technically needs to be re-positioned. This
    // can happen on rotation, so a change in Z-rotation can be checked
    // for this case. However, this *should not* be done on Windows/Linux,
    // because the functions used to resize a native window on those hosts
    // will block if the shape doesn't actually change, freezing the
    // emulator.
    const bool shouldMoveSubWindow =
        !shouldCreateSubWindow &&
        !(m_x == wx && m_y == wy && m_windowWidth == ww && m_windowHeight == wh
#if defined(__APPLE__)
          && m_zRot == zRot
#endif
        );

    const bool redrawSubwindow =
        shouldCreateSubWindow || shouldMoveSubWindow || m_zRot != zRot || m_dpr != dpr ||
        m_windowContentFullWidth != fbw || m_windowContentFullHeight != fbh;
    if (!shouldCreateSubWindow && !shouldMoveSubWindow && !redrawSubwindow) {
        assert(sInitialized.load(std::memory_order_relaxed));
        GFXSTREAM_DEBUG("Exit setupSubWindow (nothing to do)");
        return true;
    }

    class ScopedPromise {
       public:
        ~ScopedPromise() { mPromise.set_value(); }
        std::future<void> getFuture() { return mPromise.get_future(); }
        DISALLOW_COPY_ASSIGN_AND_MOVE(ScopedPromise);
        static std::tuple<std::unique_ptr<ScopedPromise>, std::future<void>> create() {
            auto scopedPromise = std::unique_ptr<ScopedPromise>(new ScopedPromise());
            auto future = scopedPromise->mPromise.get_future();
            return std::make_tuple(std::move(scopedPromise), std::move(future));
        }

       private:
        ScopedPromise() = default;
        std::promise<void> mPromise;
    };
    std::unique_ptr<ScopedPromise> postWorkerContinueSignal;
    std::future<void> postWorkerContinueSignalFuture;
    std::tie(postWorkerContinueSignal, postWorkerContinueSignalFuture) = ScopedPromise::create();
    {
        auto watchdog =
            WATCHDOG_BUILDER(m_healthMonitor.get(), "Wait for other tasks on PostWorker")
                .setTimeoutMs(6000)
                .build();
        blockPostWorker(std::move(postWorkerContinueSignalFuture)).wait();
    }
    if (m_displayVk) {
        auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "Draining the VkQueue")
                            .setTimeoutMs(6000)
                            .build();
        m_displayVk->drainQueues();
    }
    auto lockWatchdog =
        WATCHDOG_BUILDER(m_healthMonitor.get(), "Wait for the FrameBuffer global lock").build();
    auto lockWatchdogId = lockWatchdog->release();
    AutoLock mutex(m_lock);
    if (lockWatchdogId.has_value()) {
        m_healthMonitor->stopMonitoringTask(lockWatchdogId.value());
    }

    if (deleteExisting) {
        removeSubWindow_locked();
    }

    bool success = false;

    // If the subwindow doesn't exist, create it with the appropriate dimensions
    if (!m_subWin) {
        // Create native subwindow for FB display output
        m_x = wx;
        m_y = wy;
        m_windowWidth = ww;
        m_windowHeight = wh;

        if (!hideWindow) {
            m_subWin = createSubWindow(p_window, m_x, m_y, m_windowWidth, m_windowHeight, dpr,
                                       subWindowRepaint, this, hideWindow);
        }
        if (m_subWin) {
            m_nativeWindow = p_window;

            if (m_displayVk) {
                m_displaySurface = m_emulationVk->createDisplaySurface(
                    m_subWin, m_windowWidth * dpr, m_windowHeight * dpr);
            } else if (m_emulationGl) {
#if GFXSTREAM_ENABLE_HOST_GLES
                m_displaySurface = m_emulationGl->createWindowSurface(m_windowWidth * dpr,
                                                                      m_windowHeight * dpr,
                                                                      m_subWin);
#endif
            } else {
                GFXSTREAM_FATAL("Unhandled window surface creation.");
            }

            if (m_displaySurface) {
                // Some backends use a default display surface. Unbind from that before
                // binding the new display surface. which potentially needs to be unbound.
                for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
                    displaySurfaceUser->unbindFromSurface();
                }

                // TODO: Make RenderDoc a DisplaySurfaceUser.
                if (m_displayVk) {
                    if (m_renderDoc) {
                        m_renderDoc->call(gfxstream::host::RenderDoc::kSetActiveWindow,
                                          RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(m_vkInstance),
                                          reinterpret_cast<RENDERDOC_WindowHandle>(m_subWin));
                    }
                }

                m_px = 0;
                m_py = 0;
                for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
                    displaySurfaceUser->bindToSurface(m_displaySurface.get());
                }
                success = true;
            } else {
                // Display surface creation failed.
                if (m_emulationGl) {
                    // NOTE: This can typically happen with software-only renderers like OSMesa.
                    destroySubWindow(m_subWin);
                    m_subWin = (EGLNativeWindowType)0;
                } else {
                    GFXSTREAM_FATAL("Failed to create DisplaySurface.");
                }
            }
        }
    }

    auto watchdog = WATCHDOG_BUILDER(m_healthMonitor.get(), "Updating subwindow state").build();
    // At this point, if the subwindow doesn't exist, it is because it either
    // couldn't be created
    // in the first place or the EGLSurface couldn't be created.
    if (m_subWin) {
        if (!shouldMoveSubWindow) {
            // Ensure that at least viewport parameters are properly updated.
            success = true;
        } else {
            // Only attempt to update window geometry if anything has actually
            // changed.
            m_x = wx;
            m_y = wy;
            m_windowWidth = ww;
            m_windowHeight = wh;

            {
                auto moveWatchdog =
                    WATCHDOG_BUILDER(m_healthMonitor.get(), "Moving subwindow").build();
                success = moveSubWindow(m_nativeWindow, m_subWin, m_x, m_y, m_windowWidth,
                                        m_windowHeight, dpr);
            }
            m_displaySurface->updateSize(m_windowWidth * dpr, m_windowHeight * dpr);
        }
        // We are safe to unblock the PostWorker thread now, because we have completed all the
        // operations that could modify the state of the m_subWin. We need to unblock the PostWorker
        // here because we may need to send and wait for other tasks dispatched to the PostWorker
        // later, e.g. the viewport command or the post command issued later.
        postWorkerContinueSignal.reset();

        if (success && redrawSubwindow) {
            // Subwin creation or movement was successful,
            // update viewport and z rotation and draw
            // the last posted color buffer.
            m_dpr = dpr;
            m_zRot = zRot;
            if (m_displayVk == nullptr) {
                Post postCmd;
                postCmd.cmd = PostCmd::Viewport;
                postCmd.viewport.width = fbw;
                postCmd.viewport.height = fbh;
                sendPostWorkerCmd(std::move(postCmd));

                if (m_lastPostedColorBuffer) {
                    GFXSTREAM_DEBUG("setupSubwindow: draw last posted cb");
                    postImpl(m_lastPostedColorBuffer,
                        [](std::shared_future<void> waitForGpu) {}, false);
                } else {
                    Post clearCmd;
                    clearCmd.cmd = PostCmd::Clear;
                    sendPostWorkerCmd(std::move(clearCmd));
                }
            }
            m_windowContentFullWidth = fbw;
            m_windowContentFullHeight = fbh;
        }
    }

    mutex.unlock();

    // Nobody ever checks for the return code, so there will be no retries or
    // even aborted run; if we don't mark the framebuffer as initialized here
    // its users will hang forever; if we do mark it, they will crash - which
    // is a better outcome (crash report == bug fixed).
    AutoLock lock(sGlobals()->lock);
    sInitialized.store(true, std::memory_order_relaxed);
    sGlobals()->condVar.broadcastAndUnlock(&lock);

    GFXSTREAM_DEBUG("Exit setupSubWindow (successful setup)");
    return success;
}

bool FrameBuffer::Impl::removeSubWindow() {
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("Cannot remove native sub-window in this configuration");
        return false;
    }

    AutoLock lock(sGlobals()->lock);
    sInitialized.store(false, std::memory_order_relaxed);
    sGlobals()->condVar.broadcastAndUnlock(&lock);

    AutoLock mutex(m_lock);
    return removeSubWindow_locked();
}

bool FrameBuffer::Impl::removeSubWindow_locked() {
    if (!m_useSubWindow) {
        GFXSTREAM_ERROR("Cannot remove native sub-window in this configuration");
        return false;
    }
    bool removed = false;
    if (m_subWin) {
        for (auto* displaySurfaceUser : m_displaySurfaceUsers) {
            displaySurfaceUser->unbindFromSurface();
        }
        m_displaySurface.reset();

        destroySubWindow(m_subWin);

        m_subWin = (EGLNativeWindowType)0;
        removed = true;
    }
    return removed;
}

HandleType FrameBuffer::Impl::genHandle_locked() {
    HandleType id;
    do {
        id = ++sNextHandle;
    } while (id == 0 ||
#if GFXSTREAM_ENABLE_HOST_GLES
             m_contexts.find(id) != m_contexts.end() || m_windows.find(id) != m_windows.end() ||
#endif
             m_colorbuffers.find(id) != m_colorbuffers.end() ||
             m_buffers.find(id) != m_buffers.end());

    return id;
}

bool FrameBuffer::Impl::isFormatSupported(GLenum format) {
    bool supported = true;
    if (m_emulationGl) {
        supported &= m_emulationGl->isFormatSupported(format);
    }
    if (m_emulationVk) {
        supported &= m_emulationVk->isFormatSupported(format);
    }
    return supported;
}

HandleType FrameBuffer::Impl::createColorBuffer(int p_width, int p_height, GLenum p_internalFormat,
                                                FrameworkFormat p_frameworkFormat) {
    AutoLock mutex(m_lock);
    sweepColorBuffersLocked();
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    return createColorBufferWithResourceHandleLocked(p_width, p_height, p_internalFormat,
                                                     p_frameworkFormat, genHandle_locked());
}

void FrameBuffer::Impl::createColorBufferWithResourceHandle(int p_width, int p_height,
                                                            GLenum p_internalFormat,
                                                            FrameworkFormat p_frameworkFormat,
                                                            HandleType handle) {
    {
        AutoLock mutex(m_lock);
        sweepColorBuffersLocked();

        AutoLock colorBufferMapLock(m_colorBufferMapLock);

        // Check for handle collision
        if (m_colorbuffers.count(handle) != 0) {
            GFXSTREAM_FATAL("ColorBuffer:%d already exists!", handle);
        }

        createColorBufferWithResourceHandleLocked(p_width, p_height, p_internalFormat,
                                                  p_frameworkFormat, handle);
    }
}

HandleType FrameBuffer::Impl::createColorBufferWithResourceHandleLocked(
    int p_width, int p_height, GLenum p_internalFormat, FrameworkFormat p_frameworkFormat,
    HandleType handle) {
    ColorBufferPtr cb =
        ColorBuffer::create(m_emulationGl.get(), m_emulationVk.get(), p_width, p_height,
                            p_internalFormat, p_frameworkFormat, handle, nullptr /*stream*/);
    if (cb.get() == nullptr) {
        GFXSTREAM_FATAL("Failed to create ColorBuffer:%d format:%d framework-format:%d with:%d height:%d",
                        handle, p_internalFormat, p_frameworkFormat, p_width, p_height);
    }

    assert(m_colorbuffers.count(handle) == 0);
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed. We only memoize the mapping from handle to ColorBuffer.
    // Explicitly set refcount to 1 to avoid the colorbuffer being added to
    // m_colorBufferDelayedCloseList in FrameBuffer::Impl::onLoad().
    if (m_refCountPipeEnabled) {
        m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 1, false, 0});
    } else {
        const int apiLevel = get_gfxstream_guest_android_api_level();
        // pre-O and post-O use different color buffer memory management
        // logic
        if (apiLevel > 0 && apiLevel < 26) {
            m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 1, false, 0});

            RenderThreadInfo* tInfo = RenderThreadInfo::get();
            uint64_t puid = tInfo->m_puid;
            if (puid) {
                m_procOwnedColorBuffers[puid].insert(handle);
            }

        } else {
            m_colorbuffers.try_emplace(handle, ColorBufferRef{std::move(cb), 0, false, 0});
        }
    }

    return handle;
}

HandleType FrameBuffer::Impl::createBuffer(uint64_t p_size, uint32_t memoryProperty) {
    AutoLock mutex(m_lock);
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    return createBufferWithResourceHandleLocked(p_size, genHandle_locked(), memoryProperty);
}

void FrameBuffer::Impl::createBufferWithResourceHandle(uint64_t size, HandleType handle) {
    AutoLock mutex(m_lock);
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    if (m_buffers.count(handle) != 0) {
        GFXSTREAM_FATAL("Buffer already exists with handle %d", handle);
    }

    createBufferWithResourceHandleLocked(size, handle, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
}

HandleType FrameBuffer::Impl::createBufferWithResourceHandleLocked(int p_size, HandleType handle,
                                                                   uint32_t memoryProperty) {
    if (m_buffers.count(handle) != 0) {
        GFXSTREAM_FATAL("Buffer already exists with handle %d", handle);
    }

    BufferPtr buffer(Buffer::create(m_emulationGl.get(), m_emulationVk.get(), p_size, handle));
    if (!buffer) {
        GFXSTREAM_ERROR("Create buffer failed.");
        return 0;
    }

    m_buffers[handle] = {std::move(buffer)};

    return handle;
}

int FrameBuffer::Impl::openColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) return 0;

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c;
    {
        AutoLock colorBuffermapLock(m_colorBufferMapLock);
        c = m_colorbuffers.find(p_colorbuffer);
        if (c == m_colorbuffers.end()) {
            // bad colorbuffer handle
            GFXSTREAM_ERROR("FB: openColorBuffer cb handle %d not found", p_colorbuffer);
            return -1;
        }
        c->second.refcount++;
        markOpened(&c->second);
    }

    uint64_t puid = tInfo ? tInfo->m_puid : 0;
    if (puid) {
        m_procOwnedColorBuffers[puid].insert(p_colorbuffer);
    }
    return 0;
}

void FrameBuffer::Impl::closeColorBuffer(HandleType p_colorbuffer) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) {
        return;
    }

    RenderThreadInfo* tInfo = RenderThreadInfo::get();

    std::vector<HandleType> toCleanup;

    AutoLock mutex(m_lock);
    uint64_t puid = tInfo ? tInfo->m_puid : 0;
    if (puid) {
        auto ite = m_procOwnedColorBuffers.find(puid);
        if (ite != m_procOwnedColorBuffers.end()) {
            const auto& cb = ite->second.find(p_colorbuffer);
            if (cb != ite->second.end()) {
                ite->second.erase(cb);
                if (closeColorBufferLocked(p_colorbuffer)) {
                    toCleanup.push_back(p_colorbuffer);
                }
            }
        }
    } else {
        if (closeColorBufferLocked(p_colorbuffer)) {
            toCleanup.push_back(p_colorbuffer);
        }
    }
}

void FrameBuffer::Impl::closeBuffer(HandleType p_buffer) {
    AutoLock mutex(m_lock);

    auto it = m_buffers.find(p_buffer);
    if (it == m_buffers.end()) {
        GFXSTREAM_ERROR("Failed to find Buffer:%d", p_buffer);
        return;
    }

    m_buffers.erase(it);
}

bool FrameBuffer::Impl::closeColorBufferLocked(HandleType p_colorbuffer, bool forced) {
    // When guest feature flag RefCountPipe is on, no reference counting is
    // needed.
    if (m_refCountPipeEnabled) {
        return false;
    }
    bool deleted = false;
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);

        if (m_noDelayCloseColorBufferEnabled) forced = true;

        ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
        if (c == m_colorbuffers.end()) {
            // This is harmless: it is normal for guest system to issue
            // closeColorBuffer command when the color buffer is already
            // garbage collected on the host. (we don't have a mechanism
            // to give guest a notice yet)
            return false;
        }

        // The guest can and will gralloc_alloc/gralloc_free and then
        // gralloc_register a buffer, due to API level (O+) or
        // timing issues.
        // So, we don't actually close the color buffer when refcount
        // reached zero, unless it has been opened at least once already.
        // Instead, put it on a 'delayed close' list to return to it later.
        if (--c->second.refcount == 0) {
            if (forced) {
                eraseDelayedCloseColorBufferLocked(c->first, c->second.closedTs);
                m_colorbuffers.erase(c);
                deleted = true;
            } else {
                c->second.closedTs = gfxstream::base::getUnixTimeUs();
                m_colorBufferDelayedCloseList.push_back({c->second.closedTs, p_colorbuffer});
            }
        }
    }

    performDelayedColorBufferCloseLocked(false);

    return deleted;
}

void FrameBuffer::Impl::decColorBufferRefCountNoDestroy(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        return;
    }

    if (--c->second.refcount == 0) {
        c->second.closedTs = gfxstream::base::getUnixTimeUs();
        m_colorBufferDelayedCloseList.push_back({c->second.closedTs, p_colorbuffer});
    }
}

void FrameBuffer::Impl::performDelayedColorBufferCloseLocked(bool forced) {
    // Let's wait just long enough to make sure it's not because of instant
    // timestamp change (end of previous second -> beginning of a next one),
    // but not for long - this is a workaround for race conditions, and they
    // are quick.
    static constexpr uint64_t kColorBufferClosingDelayUs = 1000000LL;

    const auto now = gfxstream::base::getUnixTimeUs();
    auto it = m_colorBufferDelayedCloseList.begin();
    while (it != m_colorBufferDelayedCloseList.end() &&
           (forced ||
           it->ts + kColorBufferClosingDelayUs <= now)) {
        if (it->cbHandle != 0) {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            const auto& cb = m_colorbuffers.find(it->cbHandle);
            if (cb != m_colorbuffers.end()) {
                m_colorbuffers.erase(cb);
            }
        }
        ++it;
    }
    m_colorBufferDelayedCloseList.erase(
                m_colorBufferDelayedCloseList.begin(), it);
}

void FrameBuffer::Impl::eraseDelayedCloseColorBufferLocked(HandleType cb, uint64_t ts) {
    // Find the first delayed buffer with a timestamp <= |ts|
    auto it = std::lower_bound(
                  m_colorBufferDelayedCloseList.begin(),
                  m_colorBufferDelayedCloseList.end(), ts,
                  [](const ColorBufferCloseInfo& ci, uint64_t ts) {
        return ci.ts < ts;
    });
    while (it != m_colorBufferDelayedCloseList.end() &&
           it->ts == ts) {
        // if this is the one we need - clear it out.
        if (it->cbHandle == cb) {
            it->cbHandle = 0;
            break;
        }
        ++it;
    }
}

void FrameBuffer::Impl::createGraphicsProcessResources(uint64_t puid) {
    bool inserted = false;
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        inserted = m_procOwnedResources.try_emplace(puid, ProcessResources::create()).second;
    }
    if (!inserted) {
        GFXSTREAM_WARNING("Failed to create process resource for puid %" PRIu64 ".", puid);
    }
}

std::unique_ptr<ProcessResources> FrameBuffer::Impl::removeGraphicsProcessResources(uint64_t puid) {
    std::unordered_map<uint64_t, std::unique_ptr<ProcessResources>>::node_type node;
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        node = m_procOwnedResources.extract(puid);
    }
    if (node.empty()) {
        GFXSTREAM_WARNING("Failed to find process resource for puid %" PRIu64 ".", puid);
        return nullptr;
    }
    std::unique_ptr<ProcessResources> res = std::move(node.mapped());
    return res;
}

void FrameBuffer::Impl::cleanupProcGLObjects(uint64_t puid) {
    bool renderThreadWithThisPuidExists = false;

    do {
        renderThreadWithThisPuidExists = false;
        RenderThreadInfo::forAllRenderThreadInfos(
            [puid, &renderThreadWithThisPuidExists](RenderThreadInfo* i) {
            if (i->m_puid == puid) {
                renderThreadWithThisPuidExists = true;

                bool shouldExit = false;
                i->m_shouldExit.compare_exchange_strong(shouldExit, true);
            }
        });
        gfxstream::base::sleepUs(10000);
    } while (renderThreadWithThisPuidExists);


    AutoLock mutex(m_lock);

    cleanupProcGLObjects_locked(puid);

    // Run other cleanup callbacks
    // Avoid deadlock by first storing a separate list of callbacks
    std::vector<std::function<void()>> callbacks;

    {
        auto procIte = m_procOwnedCleanupCallbacks.find(puid);
        if (procIte != m_procOwnedCleanupCallbacks.end()) {
            for (auto it : procIte->second) {
                callbacks.push_back(it.second);
            }
            m_procOwnedCleanupCallbacks.erase(procIte);
        }
    }

    mutex.unlock();

    for (auto cb : callbacks) {
        cb();
    }
}

std::vector<HandleType> FrameBuffer::Impl::cleanupProcGLObjects_locked(uint64_t puid, bool forced) {
    std::vector<HandleType> colorBuffersToCleanup;
    {
        std::unique_ptr<RecursiveScopedContextBind> bind = nullptr;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
        }
        // Clean up window surfaces
        if (m_emulationGl) {
            auto procIte = m_procOwnedEmulatedEglWindowSurfaces.find(puid);
            if (procIte != m_procOwnedEmulatedEglWindowSurfaces.end()) {
                for (auto whndl : procIte->second) {
                    auto w = m_windows.find(whndl);
                    // TODO(b/265186226): figure out if we are leaking?
                    if (w == m_windows.end()) {
                        continue;
                    }
                    if (!m_guestManagedColorBufferLifetime) {
                        if (m_refCountPipeEnabled) {
                            if (decColorBufferRefCountLocked(w->second.second)) {
                                colorBuffersToCleanup.push_back(w->second.second);
                            }
                        } else {
                            if (closeColorBufferLocked(w->second.second, forced)) {
                                colorBuffersToCleanup.push_back(w->second.second);
                            }
                        }
                    }
                    m_windows.erase(w);
                }
                m_procOwnedEmulatedEglWindowSurfaces.erase(procIte);
            }
        }
#endif

        // Clean up color buffers.
        // A color buffer needs to be closed as many times as it is opened by
        // the guest process, to give the correct reference count.
        // (Note that a color buffer can be shared across guest processes.)
        {
            if (!m_guestManagedColorBufferLifetime) {
                auto procIte = m_procOwnedColorBuffers.find(puid);
                if (procIte != m_procOwnedColorBuffers.end()) {
                    for (auto cb : procIte->second) {
                        if (closeColorBufferLocked(cb, forced)) {
                            colorBuffersToCleanup.push_back(cb);
                        }
                    }
                    m_procOwnedColorBuffers.erase(procIte);
                }
            }
        }

#if GFXSTREAM_ENABLE_HOST_GLES
        // Clean up EGLImage handles
        if (m_emulationGl) {
            auto procImagesIt = m_procOwnedEmulatedEglImages.find(puid);
            if (procImagesIt != m_procOwnedEmulatedEglImages.end()) {
                for (auto image : procImagesIt->second) {
                    m_images.erase(image);
                }
                m_procOwnedEmulatedEglImages.erase(procImagesIt);
            }
        }
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    // Unbind before cleaning up contexts
    // Cleanup render contexts
    if (m_emulationGl) {
        auto procIte = m_procOwnedEmulatedEglContexts.find(puid);
        if (procIte != m_procOwnedEmulatedEglContexts.end()) {
            for (auto ctx : procIte->second) {
                m_contexts.erase(ctx);
            }
            m_procOwnedEmulatedEglContexts.erase(procIte);
        }
    }
#endif

    return colorBuffersToCleanup;
}

void FrameBuffer::Impl::markOpened(ColorBufferRef* cbRef) {
    cbRef->opened = true;
    eraseDelayedCloseColorBufferLocked(cbRef->cb->getHndl(), cbRef->closedTs);
    cbRef->closedTs = 0;
}

void FrameBuffer::Impl::readBuffer(HandleType handle, uint64_t offset, uint64_t size, void* bytes) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(handle);
    if (!buffer) {
        GFXSTREAM_ERROR("Failed to read buffer: buffer %d not found.", handle);
        return;
    }

    buffer->readToBytes(offset, size, bytes);
}

void FrameBuffer::Impl::readColorBuffer(HandleType p_colorbuffer, int x, int y, int width,
                                        int height, GLenum format, GLenum type, void* outPixels,
                                        uint64_t outPixelsSize) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY, "FrameBuffer::Impl::readColorBuffer()",
                          "ColorBuffer", p_colorbuffer);

    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return;
    }

    colorBuffer->readToBytes(x, y, width, height, format, type, outPixels, outPixelsSize);
}

void FrameBuffer::Impl::readColorBufferYUV(HandleType p_colorbuffer, int x, int y, int width,
                                           int height, void* outPixels, uint32_t outPixelsSize) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return;
    }

    colorBuffer->readYuvToBytes(x, y, width, height, outPixels, outPixelsSize);
}

bool FrameBuffer::Impl::updateBuffer(HandleType p_buffer, uint64_t offset, uint64_t size,
                                     void* bytes) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(p_buffer);
    if (!buffer) {
        GFXSTREAM_ERROR("Failed to update buffer: buffer %d not found.", p_buffer);
        return false;
    }

    return buffer->updateFromBytes(offset, size, bytes);
}

bool FrameBuffer::Impl::updateColorBuffer(HandleType p_colorbuffer, int x, int y, int width,
                                          int height, GLenum format, GLenum type, void* pixels) {
    GFXSTREAM_TRACE_EVENT(GFXSTREAM_TRACE_DEFAULT_CATEGORY,
                          "FrameBuffer::Impl::updateColorBuffer()", "ColorBuffer", p_colorbuffer);

    if (width == 0 || height == 0) {
        return false;
    }

    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    colorBuffer->updateFromBytes(x, y, width, height, format, type, pixels);

    return true;
}

bool FrameBuffer::Impl::updateColorBufferFromFrameworkFormat(HandleType p_colorbuffer, int x, int y,
                                                             int width, int height,
                                                             FrameworkFormat fwkFormat,
                                                             GLenum format, GLenum type,
                                                             void* pixels, void* metadata) {
    if (width == 0 || height == 0) {
        return false;
    }

    AutoLock mutex(m_lock);

    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        // bad colorbuffer handle
        return false;
    }

    (*c).second.cb->updateFromBytes(x, y, width, height, fwkFormat, format, type, pixels, metadata);
    return true;
}

bool FrameBuffer::Impl::getColorBufferInfo(HandleType p_colorbuffer, int* width, int* height,
                                           GLint* internalformat,
                                           FrameworkFormat* frameworkFormat) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    *width = colorBuffer->getWidth();
    *height = colorBuffer->getHeight();
    *internalformat = colorBuffer->getFormat();
    if (frameworkFormat) {
        *frameworkFormat = colorBuffer->getFrameworkFormat();
    }

    return true;
}

bool FrameBuffer::Impl::getBufferInfo(HandleType p_buffer, int* size) {
    AutoLock mutex(m_lock);

    BufferMap::iterator c(m_buffers.find(p_buffer));
    if (c == m_buffers.end()) {
        // Bad buffer handle.
        return false;
    }

    auto buf = (*c).second.buffer;
    *size = buf->getSize();
    return true;
}

bool FrameBuffer::Impl::post(HandleType p_colorbuffer, bool needLockAndBind) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_features.GuestVulkanOnly.enabled) {
        flushColorBufferFromGl(p_colorbuffer);
    }
#endif

    auto res = postImplSync(p_colorbuffer, needLockAndBind);
    if (res) setGuestPostedAFrame();
    return res;
}

void FrameBuffer::Impl::postWithCallback(HandleType p_colorbuffer,
                                         Post::CompletionCallback callback, bool needLockAndBind) {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_features.GuestVulkanOnly.enabled) {
        flushColorBufferFromGl(p_colorbuffer);
    }
#endif

    AsyncResult res = postImpl(p_colorbuffer, callback, needLockAndBind);
    if (res.Succeeded()) {
        setGuestPostedAFrame();
    }

    if (!res.CallbackScheduledOrFired()) {
        // If postImpl fails, we have not fired the callback. postWithCallback
        // should always ensure the callback fires.
        std::shared_future<void> callbackRes = std::async(std::launch::deferred, [] {});
        callback(callbackRes);
    }
}

bool FrameBuffer::Impl::postImplSync(HandleType p_colorbuffer, bool needLockAndBind, bool repaint) {
    std::promise<void> promise;
    std::future<void> completeFuture = promise.get_future();
    auto posted = postImpl(
        p_colorbuffer,
        [&](std::shared_future<void> waitForGpu) {
            waitForGpu.wait();
            promise.set_value();
        },
        needLockAndBind, repaint);
    if (posted.CallbackScheduledOrFired()) {
        completeFuture.wait();
    }

    return posted.Succeeded();
}

AsyncResult FrameBuffer::Impl::postImpl(HandleType p_colorbuffer, Post::CompletionCallback callback,
                                        bool needLockAndBind, bool repaint) {
    ColorBufferPtr colorBuffer = nullptr;
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        ColorBufferMap::iterator c = m_colorbuffers.find(p_colorbuffer);
        if (c != m_colorbuffers.end()) {
            colorBuffer = c->second.cb;
            c->second.refcount++;
            markOpened(&c->second);
        }
    }
    if (!colorBuffer) {
        return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }

    std::optional<AutoLock> lock;
#if GFXSTREAM_ENABLE_HOST_GLES
    std::optional<RecursiveScopedContextBind> bind;
#endif
    if (needLockAndBind) {
        lock.emplace(m_lock);
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            bind.emplace(getPbufferSurfaceContextHelper());
        }
#endif
    }
    AsyncResult ret = AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;

    m_lastPostedColorBuffer = p_colorbuffer;

    colorBuffer->touch();
    if (m_subWin) {
        Post postCmd;
        postCmd.cmd = PostCmd::Post;
        postCmd.cb = colorBuffer.get();
        postCmd.cbHandle = p_colorbuffer;
        postCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
        sendPostWorkerCmd(std::move(postCmd));
        ret = AsyncResult::OK_AND_CALLBACK_SCHEDULED;
    } else {
        // If there is no sub-window, don't display anything, the client will
        // rely on m_onPost to get the pixels instead.
        ret = AsyncResult::OK_AND_CALLBACK_NOT_SCHEDULED;
    }

    //
    // output FPS and performance usage statistics
    //
    if (m_fpsStats) {
        long long currTime = gfxstream::base::getHighResTimeUs() / 1000;
        m_statsNumFrames++;
        if (currTime - m_statsStartTime >= 1000) {
            if (m_fpsStats) {
                float dt = (float)(currTime - m_statsStartTime) / 1000.0f;
                printf("FPS: %5.3f \n", (float)m_statsNumFrames / dt);
                m_statsNumFrames = 0;
            }
            m_statsStartTime = currTime;
        }
    }

    //
    // Send framebuffer (without FPS overlay) to callback
    //
    if (!m_onPost.empty()) {
        for (auto& iter : m_onPost) {
            ColorBufferPtr cb;
            if (iter.first == 0) {
                cb = colorBuffer;
            } else {
                uint32_t displayColorBufferHandle = 0;
                if (getDisplayColorBuffer(iter.first, &displayColorBufferHandle) < 0) {
                    GFXSTREAM_ERROR("Failed to get color buffer for display %d, skip onPost",
                                    iter.first);
                    continue;
                }

                cb = findColorBuffer(displayColorBufferHandle);
                if (!cb) {
                    GFXSTREAM_ERROR("Failed to find ColorBuffer %d, skip onPost",
                                    displayColorBufferHandle);
                    continue;
                }
            }

            if (asyncReadbackSupported()) {
                ensureReadbackWorker();
                const auto status = m_readbackWorker->doNextReadback(
                    iter.first, cb.get(), iter.second.img, repaint, iter.second.readBgra);
                if (status == ReadbackWorker::DoNextReadbackResult::OK_READY_FOR_READ) {
                    doPostCallback(iter.second.img, iter.first);
                }
            } else {
    #if GFXSTREAM_ENABLE_HOST_GLES
                cb->glOpReadback(iter.second.img, iter.second.readBgra);
    #endif
                doPostCallback(iter.second.img, iter.first);
            }
        }
    }

    if (!m_subWin) {  // m_subWin is supposed to be false
        decColorBufferRefCountLocked(p_colorbuffer);
    }

    return ret;
}

void FrameBuffer::Impl::doPostCallback(void* pixels, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Cannot find post callback function for display %d", displayId);
        return;
    }
    iter->second.cb(iter->second.context, displayId, iter->second.width, iter->second.height, -1,
                    GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char*)pixels);
}

void FrameBuffer::Impl::getPixels(void* pixels, uint32_t bytes, uint32_t displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Display %d not configured for recording yet", displayId);
        return;
    }
    std::future<void> completeFuture =
        m_readbackThread.enqueue({ReadbackCmd::GetPixels, displayId, pixels, bytes});
    completeFuture.wait();
}

void FrameBuffer::Impl::flushReadPipeline(int displayId) {
    const auto& iter = m_onPost.find(displayId);
    if (iter == m_onPost.end()) {
        GFXSTREAM_ERROR("Cannot find onPost pixels for display %d", displayId);
        return;
    }

    ensureReadbackWorker();

    const auto status = m_readbackWorker->flushPipeline(displayId);
    if (status == ReadbackWorker::FlushResult::OK_READY_FOR_READ) {
        doPostCallback(nullptr, displayId);
    }
}

void FrameBuffer::Impl::ensureReadbackWorker() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (!m_readbackWorker) {
        if (!m_emulationGl) {
            GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        }
        m_readbackWorker = m_emulationGl->getReadbackWorker();
    }
#endif
}

static void sFrameBuffer_ReadPixelsCallback(void* pixels, uint32_t bytes, uint32_t displayId) {
    FrameBuffer::getFB()->getPixels(pixels, bytes, displayId);
}

static void sFrameBuffer_FlushReadPixelPipeline(int displayId) {
    FrameBuffer::getFB()->flushReadPipeline(displayId);
}

bool FrameBuffer::Impl::asyncReadbackSupported() {
#if GFXSTREAM_ENABLE_HOST_GLES
    return m_emulationGl && m_emulationGl->isAsyncReadbackSupported();
#else
    return false;
#endif
}

Renderer::ReadPixelsCallback FrameBuffer::Impl::getReadPixelsCallback() {
    return sFrameBuffer_ReadPixelsCallback;
}

Renderer::FlushReadPixelPipeline FrameBuffer::Impl::getFlushReadPixelPipeline() {
    return sFrameBuffer_FlushReadPixelPipeline;
}

bool FrameBuffer::Impl::repost(bool needLockAndBind) {
    GFXSTREAM_DEBUG("Reposting framebuffer.");
    if (m_displayVk) {
        setGuestPostedAFrame();
        return true;
    }
    if (m_lastPostedColorBuffer && sInitialized.load(std::memory_order_relaxed)) {
        GFXSTREAM_DEBUG("Has last posted colorbuffer and is initialized; post.");
        auto res = postImplSync(m_lastPostedColorBuffer, needLockAndBind, true);
        if (res) setGuestPostedAFrame();
        return res;
    } else {
        GFXSTREAM_DEBUG("No repost: no last posted color buffer");
        if (!sInitialized.load(std::memory_order_relaxed)) {
            GFXSTREAM_DEBUG("No repost: initialization is not finished.");
        }
    }
    return false;
}

template <class Collection>
static void saveProcOwnedCollection(Stream* stream, const Collection& c) {
    // Exclude empty handle lists from saving as they add no value but only
    // increase the snapshot size; keep the format compatible with
    // gfxstream::saveCollection() though.
    const int count = std::count_if(
        c.begin(), c.end(),
        [](const typename Collection::value_type& pair) { return !pair.second.empty(); });
    stream->putBe32(count);
    for (const auto& pair : c) {
        if (pair.second.empty()) {
            continue;
        }
        stream->putBe64(pair.first);
        saveCollection(stream, pair.second, [](Stream* s, HandleType h) { s->putBe32(h); });
    }
}

template <class Collection>
static void loadProcOwnedCollection(Stream* stream, Collection* c) {
    loadCollection(stream, c, [](Stream* stream) -> typename Collection::value_type {
        const int processId = stream->getBe64();
        typename Collection::mapped_type handles;
        loadCollection(stream, &handles, [](Stream* s) { return s->getBe32(); });
        return {processId, std::move(handles)};
    });
}

int FrameBuffer::Impl::getScreenshot(unsigned int nChannels, unsigned int* width,
                                     unsigned int* height, uint8_t* pixels, size_t* cPixels,
                                     int displayId, int desiredWidth, int desiredHeight,
                                     int desiredRotation, Rect rect) {
#ifdef CONFIG_AEMU
   if (get_gfxstream_should_skip_draw()) {
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
#else
    return 0;
#endif

    AutoLock mutex(m_lock);
    uint32_t w, h, cb, screenWidth, screenHeight;
    if (!get_gfxstream_multi_display_operations().get_display_info(displayId, nullptr, nullptr, &w,
                                                                   &h, nullptr, nullptr, nullptr)) {
        GFXSTREAM_ERROR("Screenshot of invalid display %d", displayId);
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
    if (nChannels != 3 && nChannels != 4) {
        GFXSTREAM_ERROR("Screenshot only support 3(RGB) or 4(RGBA) channels");
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }
    get_gfxstream_multi_display_operations().get_display_color_buffer(displayId, &cb);
    if (displayId == 0) {
        cb = m_lastPostedColorBuffer;
    }
    ColorBufferPtr colorBuffer = findColorBuffer(cb);
    if (!colorBuffer) {
        *width = 0;
        *height = 0;
        *cPixels = 0;
        return -1;
    }

    screenWidth = (desiredWidth == 0) ? w : desiredWidth;
    screenHeight = (desiredHeight == 0) ? h : desiredHeight;

    bool useSnipping = (rect.size.w != 0 && rect.size.h != 0);
    if (useSnipping) {
        if (desiredWidth == 0 || desiredHeight == 0) {
            GFXSTREAM_ERROR(
                "Must provide non-zero desiredWidth and desireRectanlge "
                "when using rectangle snipping");
            *width = 0;
            *height = 0;
            *cPixels = 0;
            return -1;
        }
        if ((rect.pos.x < 0 || rect.pos.y < 0) ||
            (desiredWidth < rect.pos.x + rect.size.w || desiredHeight < rect.pos.y + rect.size.h)) {
            return -1;
        }
    }

    if (useSnipping) {
        *width = rect.size.w;
        *height = rect.size.h;
    } else {
        *width = screenWidth;
        *height = screenHeight;
    }

    int needed =
        useSnipping ? (nChannels * rect.size.w * rect.size.h) : (nChannels * (*width) * (*height));

    if (*cPixels < (size_t)needed) {
        *cPixels = needed;
        return -2;
    }
    *cPixels = needed;
    if (desiredRotation == GFXSTREAM_ROTATION_90 || desiredRotation == GFXSTREAM_ROTATION_270) {
        std::swap(*width, *height);
        std::swap(screenWidth, screenHeight);
        std::swap(rect.size.w, rect.size.h);
    }
    // Transform the x, y coordinates given the rotation.
    // Assume (0, 0) represents the top left corner of the screen.
    if (useSnipping) {
        int x = 0, y = 0;
        switch (desiredRotation) {
            case GFXSTREAM_ROTATION_0:
                x = rect.pos.x;
                y = rect.pos.y;
                break;
            case GFXSTREAM_ROTATION_90:
                x = rect.pos.y;
                y = rect.pos.x;
                break;
            case GFXSTREAM_ROTATION_180:
                x = screenWidth - rect.pos.x - rect.size.w;
                y = rect.pos.y;
                break;
            case GFXSTREAM_ROTATION_270:
                x = rect.pos.y;
                y = screenHeight - rect.pos.x - rect.size.h;
                break;
        }
        rect.pos.x = x;
        rect.pos.y = y;
    }

    GLenum format = nChannels == 3 ? GL_RGB : GL_RGBA;
    Post scrCmd;
    scrCmd.cmd = PostCmd::Screenshot;
    scrCmd.screenshot.cb = colorBuffer.get();
    scrCmd.screenshot.screenwidth = screenWidth;
    scrCmd.screenshot.screenheight = screenHeight;
    scrCmd.screenshot.format = format;
    scrCmd.screenshot.type = GL_UNSIGNED_BYTE;
    scrCmd.screenshot.rotation = desiredRotation;
    scrCmd.screenshot.pixels = pixels;
    scrCmd.screenshot.rect = rect;

    std::future<void> completeFuture = sendPostWorkerCmd(std::move(scrCmd));

    mutex.unlock();
    completeFuture.wait();
    return 0;
}

void FrameBuffer::Impl::onLastColorBufferRef(uint32_t handle) {
    if (!mOutstandingColorBufferDestroys.trySend((HandleType)handle)) {
        GFXSTREAM_ERROR(
            "warning: too many outstanding "
            "color buffer destroys. leaking handle 0x%x",
            handle);
    }
}

bool FrameBuffer::Impl::decColorBufferRefCountLocked(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    const auto& it = m_colorbuffers.find(p_colorbuffer);
    if (it != m_colorbuffers.end()) {
        it->second.refcount -= 1;
        if (it->second.refcount == 0) {
            m_colorbuffers.erase(p_colorbuffer);
            return true;
        }
    }
    return false;
}

bool FrameBuffer::Impl::compose(uint32_t bufferSize, void* buffer, bool needPost) {
    std::promise<void> promise;
    std::future<void> completeFuture = promise.get_future();
    auto composeRes =
        composeWithCallback(bufferSize, buffer, [&](std::shared_future<void> waitForGpu) {
            waitForGpu.wait();
            promise.set_value();
        });
    if (!composeRes.Succeeded()) {
        return false;
    }

    if (composeRes.CallbackScheduledOrFired()) {
        completeFuture.wait();
    }

#ifdef CONFIG_AEMU
    const auto& multiDisplay = get_gfxstream_multi_display_operations();
    const bool is_pixel_fold = multiDisplay.is_pixel_fold();
    if (needPost) {
        // AEMU with -no-window mode uses this code path.
        ComposeDevice* composeDevice = (ComposeDevice*)buffer;

        switch (composeDevice->version) {
            case 1: {
                post(composeDevice->targetHandle, true);
                break;
            }
            case 2: {
                ComposeDevice_v2* composeDeviceV2 = (ComposeDevice_v2*)buffer;
                if (is_pixel_fold || composeDeviceV2->displayId == 0) {
                    post(composeDeviceV2->targetHandle, true);
                }
                break;
            }
            default: {
                return false;
            }
        }
    }
#endif

    return true;
}

AsyncResult FrameBuffer::Impl::composeWithCallback(uint32_t bufferSize, void* buffer,
                                                   Post::CompletionCallback callback) {
    ComposeDevice* p = (ComposeDevice*)buffer;
    AutoLock mutex(m_lock);

    switch (p->version) {
        case 1: {
            Post composeCmd;
            composeCmd.composeVersion = 1;
            composeCmd.composeBuffer.resize(bufferSize);
            memcpy(composeCmd.composeBuffer.data(), buffer, bufferSize);
            composeCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
            composeCmd.cmd = PostCmd::Compose;
            sendPostWorkerCmd(std::move(composeCmd));
            return AsyncResult::OK_AND_CALLBACK_SCHEDULED;
        }

        case 2: {
            // support for multi-display
            ComposeDevice_v2* p2 = (ComposeDevice_v2*)buffer;
            if (p2->displayId != 0) {
                mutex.unlock();
                setDisplayColorBuffer(p2->displayId, p2->targetHandle);
                mutex.lock();
            }
            Post composeCmd;
            composeCmd.composeVersion = 2;
            composeCmd.composeBuffer.resize(bufferSize);
            memcpy(composeCmd.composeBuffer.data(), buffer, bufferSize);
            composeCmd.completionCallback = std::make_unique<Post::CompletionCallback>(callback);
            composeCmd.cmd = PostCmd::Compose;
            sendPostWorkerCmd(std::move(composeCmd));
            return AsyncResult::OK_AND_CALLBACK_SCHEDULED;
        }

        default:
            GFXSTREAM_ERROR("yet to handle composition device version: %d", p->version);
            return AsyncResult::FAIL_AND_CALLBACK_NOT_SCHEDULED;
    }
}

void FrameBuffer::Impl::onSave(Stream* stream, const ITextureSaverPtr& textureSaver) {
    // Things we do not need to snapshot:
    //     m_eglSurface
    //     m_eglContext
    //     m_pbufSurface
    //     m_pbufContext
    //     m_prevContext
    //     m_prevReadSurf
    //     m_prevDrawSurf
    AutoLock mutex(m_lock);

    std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        // Some snapshot commands try using GL.
        bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
        if (!bind->isOk()) {
            GFXSTREAM_ERROR("Failed to make context current for saving snapshot.");
        }

        // eglPreSaveContext labels all guest context textures to be saved
        // (textures created by the host are not saved!)
        // eglSaveAllImages labels all EGLImages (both host and guest) to be saved
        // and save all labeled textures and EGLImages.
        if (s_egl.eglPreSaveContext && s_egl.eglSaveAllImages) {
            for (const auto& ctx : m_contexts) {
                s_egl.eglPreSaveContext(getDisplay(), ctx.second->getEGLContext(), stream);
            }
            s_egl.eglSaveAllImages(getDisplay(), stream, &textureSaver);
        }
    }
#endif

    // Don't save subWindow's x/y/w/h here - those are related to the current
    // emulator UI state, not guest state that we're saving.
    stream->putBe32(m_framebufferWidth);
    stream->putBe32(m_framebufferHeight);
    stream->putFloat(m_dpr);
    stream->putBe32(mDisplayActiveConfigId);
    saveCollection(stream, mDisplayConfigs,
                   [](Stream* s, const std::map<int, DisplayConfig>::value_type& pair) {
                       s->putBe32(pair.first);
                       s->putBe32(pair.second.w);
                       s->putBe32(pair.second.h);
                       s->putBe32(pair.second.dpiX);
                       s->putBe32(pair.second.dpiY);
                   });

    stream->putBe32(m_useSubWindow);
    stream->putBe32(/*Obsolete m_eglContextInitialized =*/1);

    stream->putBe32(m_fpsStats);
    stream->putBe32(m_statsNumFrames);
    stream->putBe64(m_statsStartTime);

    // Save all contexts.
    // Note: some of the contexts might not be restored yet. In such situation
    // we skip reading from GPU (for non-texture objects) or force a restore in
    // previous eglPreSaveContext and eglSaveAllImages calls (for texture
    // objects).
    // TODO: skip reading from GPU even for texture objects.
#if GFXSTREAM_ENABLE_HOST_GLES
    saveCollection(
        stream, m_contexts,
        [](Stream* s, const EmulatedEglContextMap::value_type& pair) { pair.second->onSave(s); });
#endif

    // We don't need to save |m_colorBufferCloseTsMap| here - there's enough
    // information to reconstruct it when loading.
    uint64_t now = gfxstream::base::getUnixTimeUs();

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        stream->putByte(m_guestManagedColorBufferLifetime);
        saveCollection(stream, m_colorbuffers,
                       [now](Stream* s, const ColorBufferMap::value_type& pair) {
                           pair.second.cb->onSave(s);
                           s->putBe32(pair.second.refcount);
                           s->putByte(pair.second.opened);
                           s->putBe32(std::max<uint64_t>(0, now - pair.second.closedTs));
                       });
    }
    stream->putBe32(m_lastPostedColorBuffer);
#if GFXSTREAM_ENABLE_HOST_GLES
    saveCollection(stream, m_windows,
                   [](Stream* s, const EmulatedEglWindowSurfaceMap::value_type& pair) {
                       pair.second.first->onSave(s);
                       s->putBe32(pair.second.second);  // Color buffer handle.
                   });
#endif

#if GFXSTREAM_ENABLE_HOST_GLES
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglWindowSurfaces);
#endif
    saveProcOwnedCollection(stream, m_procOwnedColorBuffers);
#if GFXSTREAM_ENABLE_HOST_GLES
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglImages);
    saveProcOwnedCollection(stream, m_procOwnedEmulatedEglContexts);
#endif

    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (m_features.VulkanSnapshots.enabled) {
        AutoLock procResourceLock(m_procOwnedResourcesLock);
        stream->putBe64(m_procOwnedResources.size());
        for (const auto& element : m_procOwnedResources) {
            stream->putBe64(element.first);
            stream->putBe32(element.second->getSequenceNumberPtr()->load());
        }
    }

    // Save Vulkan state
    if (m_features.VulkanSnapshots.enabled && vk::VkDecoderGlobalState::get()) {
        vk::VkDecoderGlobalState::get()->save(stream);
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        if (s_egl.eglPostSaveContext) {
            for (const auto& ctx : m_contexts) {
                s_egl.eglPostSaveContext(getDisplay(), ctx.second->getEGLContext(), stream);
            }
            // We need to run the post save step for m_eglContext
            // to mark their texture handles dirty
            if (getContext() != EGL_NO_CONTEXT) {
                s_egl.eglPostSaveContext(getDisplay(), getContext(), stream);
            }
        }

        EmulatedEglFenceSync::onSave(stream);
    }
#endif
}

bool FrameBuffer::Impl::onLoad(Stream* stream, const ITextureLoaderPtr& textureLoader) {
    AutoLock lock(m_lock);
    // cleanups
    {
        sweepColorBuffersLocked();

        std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            // Some snapshot commands try using GL.
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
            if (!bind->isOk()) {
                GFXSTREAM_ERROR("Failed to make context current for loading snapshot.");
            }
        }
#endif

        bool cleanupComplete = false;
        {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            if (m_procOwnedCleanupCallbacks.empty() && m_procOwnedColorBuffers.empty() &&
#if GFXSTREAM_ENABLE_HOST_GLES
                m_procOwnedEmulatedEglContexts.empty() && m_procOwnedEmulatedEglImages.empty() &&
                m_procOwnedEmulatedEglWindowSurfaces.empty() &&
#endif
                (
#if GFXSTREAM_ENABLE_HOST_GLES
                    !m_contexts.empty() || !m_windows.empty() ||
#endif
                    m_colorbuffers.size() > m_colorBufferDelayedCloseList.size())) {
                // we are likely on a legacy system image, which does not have
                // process owned objects. We need to force cleanup everything
#if GFXSTREAM_ENABLE_HOST_GLES
                m_contexts.clear();
                m_windows.clear();
#endif
                m_colorbuffers.clear();
                cleanupComplete = true;
            }
        }
        if (!cleanupComplete) {
            std::vector<HandleType> colorBuffersToCleanup;

#if GFXSTREAM_ENABLE_HOST_GLES
            while (m_procOwnedEmulatedEglWindowSurfaces.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                    m_procOwnedEmulatedEglWindowSurfaces.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#endif
            while (m_procOwnedColorBuffers.size()) {
                auto cleanupHandles =
                    cleanupProcGLObjects_locked(m_procOwnedColorBuffers.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#if GFXSTREAM_ENABLE_HOST_GLES
            while (m_procOwnedEmulatedEglImages.size()) {
                auto cleanupHandles =
                    cleanupProcGLObjects_locked(m_procOwnedEmulatedEglImages.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
            while (m_procOwnedEmulatedEglContexts.size()) {
                auto cleanupHandles = cleanupProcGLObjects_locked(
                    m_procOwnedEmulatedEglContexts.begin()->first, true);
                colorBuffersToCleanup.insert(colorBuffersToCleanup.end(), cleanupHandles.begin(),
                                             cleanupHandles.end());
            }
#endif

            std::vector<std::function<void()>> cleanupCallbacks;

            while (m_procOwnedCleanupCallbacks.size()) {
                auto it = m_procOwnedCleanupCallbacks.begin();
                while (it != m_procOwnedCleanupCallbacks.end()) {
                    for (auto it2 : it->second) {
                        cleanupCallbacks.push_back(it2.second);
                    }
                    it = m_procOwnedCleanupCallbacks.erase(it);
                }
            }

            {
                AutoLock mutex(m_procOwnedResourcesLock);
                m_procOwnedResources.clear();
            }

            performDelayedColorBufferCloseLocked(true);

            lock.unlock();

            for (auto cb : cleanupCallbacks) {
                cb();
            }

            lock.lock();
            cleanupComplete = true;
        }
        m_colorBufferDelayedCloseList.clear();
#if GFXSTREAM_ENABLE_HOST_GLES
        assert(m_contexts.empty());
        assert(m_windows.empty());
#endif
        {
            AutoLock colorBufferMapLock(m_colorBufferMapLock);
            if (!m_colorbuffers.empty()) {
                GFXSTREAM_ERROR("warning: on load, stale colorbuffers: %zu", m_colorbuffers.size());
                m_colorbuffers.clear();
            }
            assert(m_colorbuffers.empty());
        }
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            if (s_egl.eglLoadAllImages) {
                s_egl.eglLoadAllImages(getDisplay(), stream, &textureLoader);
            }
        }
#endif
    }
    // See comment about subwindow position in onSave().
    m_framebufferWidth = stream->getBe32();
    m_framebufferHeight = stream->getBe32();
    m_dpr = stream->getFloat();
    mDisplayActiveConfigId = stream->getBe32();
    loadCollection(stream, &mDisplayConfigs,
                   [](Stream* s) -> std::map<int, DisplayConfig>::value_type {
                       int idx = static_cast<int>(s->getBe32());
                       int w = static_cast<int>(s->getBe32());
                       int h = static_cast<int>(s->getBe32());
                       int dpiX = static_cast<int>(s->getBe32());
                       int dpiY = static_cast<int>(s->getBe32());
                       return {idx, {w, h, dpiX, dpiY}};
                   });

    // TODO: resize the window
    //
    m_useSubWindow = stream->getBe32();
    /*Obsolete m_eglContextInitialized =*/stream->getBe32();

    m_fpsStats = stream->getBe32();
    m_statsNumFrames = stream->getBe32();
    m_statsStartTime = stream->getBe64();

#if GFXSTREAM_ENABLE_HOST_GLES
    loadCollection(
        stream, &m_contexts, [this](Stream* stream) -> EmulatedEglContextMap::value_type {
            if (!m_emulationGl) {
                GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
            }

            auto context = m_emulationGl->loadEmulatedEglContext(stream);
            auto contextHandle = context ? context->getHndl() : 0;
            return {contextHandle, std::move(context)};
        });
    assert(!gfxstream::base::find(m_contexts, 0));
#endif

    auto now = gfxstream::base::getUnixTimeUs();
    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        m_guestManagedColorBufferLifetime = stream->getByte();
        loadCollection(
            stream, &m_colorbuffers, [this, now](Stream* stream) -> ColorBufferMap::value_type {
                ColorBufferPtr cb =
                    ColorBuffer::onLoad(m_emulationGl.get(), m_emulationVk.get(), stream);
                const HandleType handle = cb->getHndl();
                const unsigned refCount = stream->getBe32();
                const bool opened = stream->getByte();
                const uint64_t closedTs = now - stream->getBe32();
                if (refCount == 0) {
                    m_colorBufferDelayedCloseList.push_back({closedTs, handle});
                }
                return {handle, ColorBufferRef{std::move(cb), refCount, opened, closedTs}};
            });
    }
    m_lastPostedColorBuffer = static_cast<HandleType>(stream->getBe32());
    GFXSTREAM_DEBUG("Got lasted posted color buffer from snapshot");

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
#if GFXSTREAM_ENABLE_HOST_GLES
        loadCollection(
            stream, &m_windows, [this](Stream* stream) -> EmulatedEglWindowSurfaceMap::value_type {
                if (!m_emulationGl) {
                    GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
                }

                auto window =
                    m_emulationGl->loadEmulatedEglWindowSurface(stream, m_colorbuffers, m_contexts);

                HandleType handle = window->getHndl();
                HandleType colorBufferHandle = stream->getBe32();
                return {handle, {std::move(window), colorBufferHandle}};
            });
#endif
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglWindowSurfaces);
#endif
    loadProcOwnedCollection(stream, &m_procOwnedColorBuffers);
#if GFXSTREAM_ENABLE_HOST_GLES
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglImages);
    loadProcOwnedCollection(stream, &m_procOwnedEmulatedEglContexts);
#endif
    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (m_features.VulkanSnapshots.enabled) {
        size_t resourceCount = stream->getBe64();
        for (size_t i = 0; i < resourceCount; i++) {
            uint64_t puid = stream->getBe64();
            uint32_t sequenceNumber = stream->getBe32();
            std::unique_ptr<ProcessResources> processResources = ProcessResources::create();
            processResources->getSequenceNumberPtr()->store(sequenceNumber);
            {
                AutoLock mutex(m_procOwnedResourcesLock);
                m_procOwnedResources.emplace(puid, std::move(processResources));
            }
        }
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        if (s_egl.eglPostLoadAllImages) {
            s_egl.eglPostLoadAllImages(getDisplay(), stream);
        }
    }

    registerTriggerWait();
#endif

    {
        std::unique_ptr<RecursiveScopedContextBind> bind;
#if GFXSTREAM_ENABLE_HOST_GLES
        if (m_emulationGl) {
            // Some snapshot commands try using GL.
            bind = std::make_unique<RecursiveScopedContextBind>(getPbufferSurfaceContextHelper());
            if (!bind->isOk()) {
                GFXSTREAM_ERROR("Failed to make context current for loading snapshot.");
            }
        }
#endif

        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        for (auto& it : m_colorbuffers) {
            if (it.second.cb) {
                it.second.cb->touch();
            }
        }
    }

    // Restore Vulkan state
    if (m_features.VulkanSnapshots.enabled && vk::VkDecoderGlobalState::get()) {
        lock.unlock();
        GfxApiLogger gfxLogger;
        vk::VkDecoderGlobalState::get()->load(stream, gfxLogger, m_healthMonitor.get());
        lock.lock();
    }

    repost(false);

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_emulationGl) {
        EmulatedEglFenceSync::onLoad(stream);
    }
#endif

    return true;
    // TODO: restore memory management
}

void FrameBuffer::Impl::lock() { m_lock.lock(); }

void FrameBuffer::Impl::unlock() { m_lock.unlock(); }

ColorBufferPtr FrameBuffer::Impl::findColorBuffer(HandleType p_colorbuffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
    if (c == m_colorbuffers.end()) {
        return nullptr;
    } else {
        return c->second.cb;
    }
}

BufferPtr FrameBuffer::Impl::findBuffer(HandleType p_buffer) {
    AutoLock colorBufferMapLock(m_colorBufferMapLock);
    BufferMap::iterator b(m_buffers.find(p_buffer));
    if (b == m_buffers.end()) {
        return nullptr;
    } else {
        return b->second.buffer;
    }
}

void FrameBuffer::Impl::registerProcessCleanupCallback(void* key, uint64_t contextId,
                                                       std::function<void()> cb) {
    AutoLock mutex(m_lock);

    auto& callbackMap = m_procOwnedCleanupCallbacks[contextId];
    if (!callbackMap.insert({key, std::move(cb)}).second) {
        GFXSTREAM_ERROR("%s: tried to override existing key %p ", __func__, key);
    }
}

void FrameBuffer::Impl::unregisterProcessCleanupCallback(void* key) {
    AutoLock mutex(m_lock);
    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    if (!tInfo) return;

    auto& callbackMap = m_procOwnedCleanupCallbacks[tInfo->m_puid];
    auto erasedCount = callbackMap.erase(key);
    if (erasedCount == 0) {
        GFXSTREAM_ERROR(
            "%s: tried to erase nonexistent key %p "
            "associated with process %llu",
            __func__, key, (unsigned long long)(tInfo->m_puid));
    }
}

const ProcessResources* FrameBuffer::Impl::getProcessResources(uint64_t puid) {
    {
        AutoLock mutex(m_procOwnedResourcesLock);
        auto i = m_procOwnedResources.find(puid);
        if (i != m_procOwnedResources.end()) {
            return i->second.get();
        }
    }
    GFXSTREAM_ERROR("Failed to find process owned resources for puid %" PRIu64 ".", puid);
    return nullptr;
}

int FrameBuffer::Impl::createDisplay(uint32_t* displayId) {
    return get_gfxstream_multi_display_operations().create_display(displayId);
}

int FrameBuffer::Impl::createDisplay(uint32_t displayId) {
    return get_gfxstream_multi_display_operations().create_display(&displayId);
}

int FrameBuffer::Impl::destroyDisplay(uint32_t displayId) {
    return get_gfxstream_multi_display_operations().destroy_display(displayId);
}

int FrameBuffer::Impl::setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    return get_gfxstream_multi_display_operations().set_display_color_buffer(displayId,
                                                                             colorBuffer);
}

int FrameBuffer::Impl::getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    return get_gfxstream_multi_display_operations().get_display_color_buffer(displayId,
                                                                             colorBuffer);
}

int FrameBuffer::Impl::getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    return get_gfxstream_multi_display_operations().get_color_buffer_display(colorBuffer,
                                                                             displayId);
}

int FrameBuffer::Impl::getDisplayPose(uint32_t displayId, int32_t* x, int32_t* y, uint32_t* w,
                                      uint32_t* h) {
    return get_gfxstream_multi_display_operations().get_display_pose(displayId, x, y, w, h);
}

int FrameBuffer::Impl::setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w,
                                      uint32_t h, uint32_t dpi) {
    return get_gfxstream_multi_display_operations().set_display_pose(displayId, x, y, w, h, dpi);
}

void FrameBuffer::Impl::sweepColorBuffersLocked() {
    HandleType handleToDestroy = 0;
    while (mOutstandingColorBufferDestroys.tryReceive(&handleToDestroy)) {
        decColorBufferRefCountLocked(handleToDestroy);
    }
}

std::future<void> FrameBuffer::Impl::blockPostWorker(std::future<void> continueSignal) {
    std::promise<void> scheduled;
    std::future<void> scheduledFuture = scheduled.get_future();
    Post postCmd = {
        .cmd = PostCmd::Block,
        .block = std::make_unique<Post::Block>(Post::Block{
            .scheduledSignal = std::move(scheduled),
            .continueSignal = std::move(continueSignal),
        }),
    };
    sendPostWorkerCmd(std::move(postCmd));
    return scheduledFuture;
}

void FrameBuffer::Impl::asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle,
                                                    FenceCompletionCallback cb) {
    (void)deviceHandle;
    SyncThread::get()->triggerWaitVkWithCompletionCallback((VkFence)fenceHandle, std::move(cb));
}

void FrameBuffer::Impl::asyncWaitForGpuVulkanQsriWithCb(uint64_t image,
                                                        FenceCompletionCallback cb) {
    SyncThread::get()->triggerWaitVkQsriWithCompletionCallback((VkImage)image, std::move(cb));
}

void FrameBuffer::Impl::setGuestManagedColorBufferLifetime(bool guestManaged) {
    m_guestManagedColorBufferLifetime = guestManaged;
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::Impl::borrowColorBufferForComposition(
    uint32_t colorBufferHandle, bool colorBufferIsTarget) {
    ColorBufferPtr colorBufferPtr = findColorBuffer(colorBufferHandle);
    if (!colorBufferPtr) {
        GFXSTREAM_ERROR("Failed to get borrowed image info for ColorBuffer:%d", colorBufferHandle);
        return nullptr;
    }

    if (m_useVulkanComposition) {
        invalidateColorBufferForVk(colorBufferHandle);
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        invalidateColorBufferForGl(colorBufferHandle);
#endif
    }

    const auto api = m_useVulkanComposition ? ColorBuffer::UsedApi::kVk : ColorBuffer::UsedApi::kGl;
    return colorBufferPtr->borrowForComposition(api, colorBufferIsTarget);
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::Impl::borrowColorBufferForDisplay(
    uint32_t colorBufferHandle) {
    ColorBufferPtr colorBufferPtr = findColorBuffer(colorBufferHandle);
    if (!colorBufferPtr) {
        GFXSTREAM_ERROR("Failed to get borrowed image info for ColorBuffer:%d", colorBufferHandle);
        return nullptr;
    }

    if (m_useVulkanComposition) {
        invalidateColorBufferForVk(colorBufferHandle);
    } else {
#if GFXSTREAM_ENABLE_HOST_GLES
        invalidateColorBufferForGl(colorBufferHandle);
#else
        GFXSTREAM_ERROR("Failed to invalidate ColorBuffer:%d", colorBufferHandle);
#endif
    }

    const auto api = m_useVulkanComposition ? ColorBuffer::UsedApi::kVk : ColorBuffer::UsedApi::kGl;
    return colorBufferPtr->borrowForDisplay(api);
}

void FrameBuffer::Impl::logVulkanDeviceLost() {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("Device lost without VkEmulation?");
    }
    m_emulationVk->onVkDeviceLost();
}

void FrameBuffer::Impl::logVulkanOutOfMemory(VkResult result, const char* function, int line,
                                             std::optional<uint64_t> allocationSize) {
    m_logger->logMetricEvent(MetricEventVulkanOutOfMemory{
        .vkResultCode = result,
        .function = function,
        .line = std::make_optional(line),
        .allocationSize = allocationSize,
    });
}

void FrameBuffer::Impl::setVsyncHz(int vsyncHz) {
    const uint64_t kOneSecondNs = 1000000000ULL;
    m_vsyncHz = vsyncHz;
    if (m_vsyncThread) {
        m_vsyncThread->setPeriod(kOneSecondNs / (uint64_t)m_vsyncHz);
    }
}

void FrameBuffer::Impl::scheduleVsyncTask(VsyncThread::VsyncTask task) {
    if (!m_vsyncThread) {
        GFXSTREAM_ERROR("%s: warning: no vsync thread exists", __func__);
        task(0);
        return;
    }

    m_vsyncThread->schedule(task);
}

void FrameBuffer::Impl::setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY) {
    AutoLock mutex(m_lock);
    mDisplayConfigs[configId] = {w, h, dpiX, dpiY};
    GFXSTREAM_INFO("Setting display: %d configuration to: %dx%d, dpi: %dx%d ", configId, w, h, dpiX,
                   dpiY);
}

void FrameBuffer::Impl::setDisplayActiveConfig(int configId) {
    AutoLock mutex(m_lock);
    if (mDisplayConfigs.find(configId) == mDisplayConfigs.end()) {
        GFXSTREAM_ERROR("config %d not set", configId);
        return;
    }
    mDisplayActiveConfigId = configId;
    m_framebufferWidth = mDisplayConfigs[configId].w;
    m_framebufferHeight = mDisplayConfigs[configId].h;
    setDisplayPose(0, 0, 0, getWidth(), getHeight(), 0);
    GFXSTREAM_INFO("setDisplayActiveConfig %d", configId);
}

int FrameBuffer::Impl::getDisplayConfigsCount() {
    AutoLock mutex(m_lock);
    return mDisplayConfigs.size();
}

int FrameBuffer::Impl::getDisplayConfigsParam(int configId, EGLint param) {
    AutoLock mutex(m_lock);
    if (mDisplayConfigs.find(configId) == mDisplayConfigs.end()) {
        return -1;
    }
    switch (param) {
        case FB_WIDTH:
            return mDisplayConfigs[configId].w;
        case FB_HEIGHT:
            return mDisplayConfigs[configId].h;
        case FB_XDPI:
            return mDisplayConfigs[configId].dpiX;
        case FB_YDPI:
            return mDisplayConfigs[configId].dpiY;
        case FB_FPS:
            return 60;
        case FB_MIN_SWAP_INTERVAL:
            return -1;
        case FB_MAX_SWAP_INTERVAL:
            return -1;
        default:
            return -1;
    }
}

int FrameBuffer::Impl::getDisplayActiveConfig() {
    AutoLock mutex(m_lock);
    return mDisplayActiveConfigId >= 0 ? mDisplayActiveConfigId : -1;
}

bool FrameBuffer::Impl::flushColorBufferFromVk(HandleType colorBufferHandle) {
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromVk();
}

bool FrameBuffer::Impl::flushColorBufferFromVkBytes(HandleType colorBufferHandle, const void* bytes,
                                                    size_t bytesSize) {
    AutoLock mutex(m_lock);

    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromVkBytes(bytes, bytesSize);
}

bool FrameBuffer::Impl::invalidateColorBufferForVk(HandleType colorBufferHandle) {
    // It reads contents from GL, which requires a context lock.
    // Also we should not do this in PostWorkerGl, otherwise it will deadlock.
    //
    // b/283524158
    // b/273986739
    AutoLock mutex(m_lock);
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_DEBUG("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForVk();
}

std::optional<BlobDescriptorInfo> FrameBuffer::Impl::exportColorBuffer(
    HandleType colorBufferHandle) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        return std::nullopt;
    }

    return colorBuffer->exportBlob();
}

std::optional<BlobDescriptorInfo> FrameBuffer::Impl::exportBuffer(HandleType bufferHandle) {
    AutoLock mutex(m_lock);

    BufferPtr buffer = findBuffer(bufferHandle);
    if (!buffer) {
        return std::nullopt;
    }

    return buffer->exportBlob();
}

bool FrameBuffer::Impl::setColorBufferVulkanMode(HandleType colorBufferHandle, uint32_t mode) {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("VK emulation not enabled.");
        return false;
    }

    return m_emulationVk->setColorBufferVulkanMode(colorBufferHandle, mode);
}

int32_t FrameBuffer::Impl::mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa,
                                                uint64_t size) {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("VK emulation not enabled.");
        return false;
    }

    return m_emulationVk->mapGpaToBufferHandle(bufferHandle, gpa, size);
}

#if GFXSTREAM_ENABLE_HOST_GLES
HandleType FrameBuffer::Impl::getEmulatedEglWindowSurfaceColorBufferHandle(HandleType p_surface) {
    AutoLock mutex(m_lock);

    auto it = m_EmulatedEglWindowSurfaceToColorBuffer.find(p_surface);
    if (it == m_EmulatedEglWindowSurfaceToColorBuffer.end()) {
        return 0;
    }

    return it->second;
}

void FrameBuffer::Impl::setScreenMask(int width, int height, const unsigned char* rgbaData) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        return;
    }
    m_emulationGl->mTextureDraw->setScreenMask(width, height, rgbaData);
}

#ifdef CONFIG_AEMU
void FrameBuffer::Impl::registerVulkanInstance(uint64_t id, const char* appName) const {
    auto* tInfo = RenderThreadInfo::get();
    std::string process_name;
    if (tInfo && tInfo->m_processName.has_value()) {
        process_name = tInfo->m_processName.value();
        // for deqp: com.drawelements.deqp:testercore
        // remove the ":testercore" for deqp
        auto position = process_name.find(":");
        if (position != std::string::npos) {
            process_name = process_name.substr(0, position);
        }
    } else if(appName) {
        process_name = std::string(appName);
    }
    get_gfxstream_vm_operations().register_vulkan_instance(id, process_name.c_str());
}

void FrameBuffer::Impl::unregisterVulkanInstance(uint64_t id) const {
    get_gfxstream_vm_operations().unregister_vulkan_instance(id);
}
#endif

void FrameBuffer::Impl::createTrivialContext(HandleType shared, HandleType* contextOut,
                                             HandleType* surfOut) {
    assert(contextOut);
    assert(surfOut);

    *contextOut = createEmulatedEglContext(0, shared, GLESApi_2);
    // Zero size is formally allowed here, but SwiftShader doesn't like it and
    // fails.
    *surfOut = createEmulatedEglWindowSurface(0, 1, 1);
}

void FrameBuffer::Impl::createSharedTrivialContext(EGLContext* contextOut, EGLSurface* surfOut) {
    assert(contextOut);
    assert(surfOut);

    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    const EmulatedEglConfig* config = m_emulationGl->getEmulationEglConfigs().get(0 /* p_config */);
    if (!config) return;

    int maj, min;
    get_gfxstream_gles_version(&maj, &min);

    const EGLint contextAttribs[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, maj,
                                     EGL_CONTEXT_MINOR_VERSION_KHR, min, EGL_NONE};

    *contextOut = s_egl.eglCreateContext(getDisplay(), config->getHostEglConfig(),
                                         getGlobalEGLContext(), contextAttribs);

    const EGLint pbufAttribs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};

    *surfOut = s_egl.eglCreatePbufferSurface(getDisplay(), config->getHostEglConfig(), pbufAttribs);
}

void FrameBuffer::Impl::destroySharedTrivialContext(EGLContext context, EGLSurface surface) {
    if (getDisplay() != EGL_NO_DISPLAY) {
        s_egl.eglDestroyContext(getDisplay(), context);
        s_egl.eglDestroySurface(getDisplay(), surface);
    }
}

bool FrameBuffer::Impl::setEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface,
                                                               HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    EmulatedEglWindowSurfaceMap::iterator w(m_windows.find(p_surface));
    if (w == m_windows.end()) {
        // bad surface handle
        GFXSTREAM_ERROR("bad window surface handle %#x", p_surface);
        return false;
    }

    {
        AutoLock colorBufferMapLock(m_colorBufferMapLock);
        ColorBufferMap::iterator c(m_colorbuffers.find(p_colorbuffer));
        if (c == m_colorbuffers.end()) {
            GFXSTREAM_ERROR("bad color buffer handle %d", p_colorbuffer);
            // bad colorbuffer handle
            return false;
        }

        (*w).second.first->setColorBuffer((*c).second.cb);
        markOpened(&c->second);
        if (!m_guestManagedColorBufferLifetime) {
            c->second.refcount++;
        }
    }
    if (w->second.second) {
        if (!m_guestManagedColorBufferLifetime) {
            if (m_refCountPipeEnabled) {
                decColorBufferRefCountLocked(w->second.second);
            } else {
                closeColorBufferLocked(w->second.second);
            }
        }
    }

    (*w).second.second = p_colorbuffer;

    m_EmulatedEglWindowSurfaceToColorBuffer[p_surface] = p_colorbuffer;

    return true;
}

std::string FrameBuffer::Impl::getEglString(EGLenum name) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        return "";
    }
    return m_emulationGl->getEglString(name);
}

std::string FrameBuffer::Impl::getGlString(EGLenum name) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        return "";
    }
    return m_emulationGl->getGlString(name);
}

GLESDispatchMaxVersion FrameBuffer::Impl::getMaxGlesVersion() {
    if (!m_emulationGl) {
        return GLES_DISPATCH_MAX_VERSION_2;
    }
    return m_emulationGl->getGlesMaxDispatchVersion();
}

std::string FrameBuffer::Impl::getGlesExtensionsString() const {
    if (!m_emulationGl) {
        return "<no GL emulation>";
    }
    return m_emulationGl->getGlesExtensionsString();
}

EGLint FrameBuffer::Impl::getEglVersion(EGLint* major, EGLint* minor) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
        return EGL_FALSE;
    }

    m_emulationGl->getEglVersion(major, minor);
    return EGL_TRUE;
}

void FrameBuffer::Impl::getNumConfigs(int* outNumConfigs, int* outNumAttribs) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    m_emulationGl->getEmulationEglConfigs().getPackInfo(outNumConfigs, outNumAttribs);
}

EGLint FrameBuffer::Impl::getConfigs(uint32_t bufferSize, GLuint* buffer) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getEmulationEglConfigs().packConfigs(bufferSize, buffer);
}

EGLint FrameBuffer::Impl::chooseConfig(EGLint* attribs, EGLint* configs, EGLint configsSize) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getEmulationEglConfigs().chooseConfig(attribs, configs, configsSize);
}

HandleType FrameBuffer::Impl::createEmulatedEglContext(int config, HandleType shareContextHandle,
                                                       GLESApi version) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);
    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    // Hold the ColorBuffer map lock so that the new handle won't collide with a ColorBuffer handle.
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    EmulatedEglContextPtr shareContext = nullptr;
    if (shareContextHandle != 0) {
        auto shareContextIt = m_contexts.find(shareContextHandle);
        if (shareContextIt == m_contexts.end()) {
            GFXSTREAM_ERROR("Failed to find share EmulatedEglContext:%d", shareContextHandle);
            return 0;
        }
        shareContext = shareContextIt->second;
    }

    HandleType contextHandle = genHandle_locked();
    auto context =
        m_emulationGl->createEmulatedEglContext(config, shareContext.get(), version, contextHandle);
    if (!context) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglContext.");
        return 0;
    }

    m_contexts[contextHandle] = std::move(context);

    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        m_procOwnedEmulatedEglContexts[puid].insert(contextHandle);
    } else {  // legacy path to manage context lifetime by threads
        if (!tinfo->m_glInfo) {
            GFXSTREAM_FATAL("RenderThreadGL not available.");
        }
        tinfo->m_glInfo->m_contextSet.insert(contextHandle);
    }

    return contextHandle;
}

void FrameBuffer::Impl::destroyEmulatedEglContext(HandleType contextHandle) {
    AutoLock mutex(m_lock);
    sweepColorBuffersLocked();

    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    m_contexts.erase(contextHandle);
    RenderThreadInfo* tinfo = RenderThreadInfo::get();
    uint64_t puid = tinfo->m_puid;
    // The new emulator manages render contexts per guest process.
    // Fall back to per-thread management if the system image does not
    // support it.
    if (puid) {
        auto it = m_procOwnedEmulatedEglContexts.find(puid);
        if (it != m_procOwnedEmulatedEglContexts.end()) {
            it->second.erase(contextHandle);
        }
    } else {
        if (!tinfo->m_glInfo) {
            GFXSTREAM_FATAL("RenderThreadGL not available.");
        }
        tinfo->m_glInfo->m_contextSet.erase(contextHandle);
    }
}

HandleType FrameBuffer::Impl::createEmulatedEglWindowSurface(int p_config, int p_width,
                                                             int p_height) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);
    // Hold the ColorBuffer map lock so that the new handle won't collide with a ColorBuffer handle.
    AutoLock colorBufferMapLock(m_colorBufferMapLock);

    HandleType handle = genHandle_locked();

    auto window =
        m_emulationGl->createEmulatedEglWindowSurface(p_config, p_width, p_height, handle);
    if (!window) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglWindowSurface.");
        return 0;
    }

    m_windows[handle] = {std::move(window), 0};

    RenderThreadInfo* info = RenderThreadInfo::get();
    if (!info->m_glInfo) {
        GFXSTREAM_FATAL("RRenderThreadInfoGl not available.");
    }

    uint64_t puid = info->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglWindowSurfaces[puid].insert(handle);
    } else {  // legacy path to manage window surface lifetime by threads
        info->m_glInfo->m_windowSet.insert(handle);
    }

    return handle;
}

void FrameBuffer::Impl::destroyEmulatedEglWindowSurface(HandleType p_surface) {
    if (m_shuttingDown) {
        return;
    }
    AutoLock mutex(m_lock);
    destroyEmulatedEglWindowSurfaceLocked(p_surface);
}

std::vector<HandleType> FrameBuffer::Impl::destroyEmulatedEglWindowSurfaceLocked(
    HandleType p_surface) {
    std::vector<HandleType> colorBuffersToCleanUp;
    const auto w = m_windows.find(p_surface);
    if (w != m_windows.end()) {
        RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
        if (!m_guestManagedColorBufferLifetime) {
            if (m_refCountPipeEnabled) {
                if (decColorBufferRefCountLocked(w->second.second)) {
                    colorBuffersToCleanUp.push_back(w->second.second);
                }
            } else {
                if (closeColorBufferLocked(w->second.second)) {
                    colorBuffersToCleanUp.push_back(w->second.second);
                }
            }
        }
        m_windows.erase(w);
        RenderThreadInfo* tinfo = RenderThreadInfo::get();
        uint64_t puid = tinfo->m_puid;
        if (puid) {
            auto ite = m_procOwnedEmulatedEglWindowSurfaces.find(puid);
            if (ite != m_procOwnedEmulatedEglWindowSurfaces.end()) {
                ite->second.erase(p_surface);
            }
        } else {
            if (!tinfo->m_glInfo) {
                GFXSTREAM_FATAL("RenderThreadGL not available.");
            }
            tinfo->m_glInfo->m_windowSet.erase(p_surface);
        }
    }
    return colorBuffersToCleanUp;
}

void FrameBuffer::Impl::createEmulatedEglFenceSync(EGLenum type, int destroyWhenSignaled,
                                                   uint64_t* outSync, uint64_t* outSyncThread) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    // TODO(b/233939967): move RenderThreadInfoGl usage to EmulationGl.
    RenderThreadInfoGl* const info = RenderThreadInfoGl::get();
    if (!info) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }
    if (!info->currContext) {
        uint32_t syncContext;
        uint32_t syncSurface;
        createTrivialContext(0,  // There is no context to share.
                             &syncContext, &syncSurface);
        bindContext(syncContext, syncSurface, syncSurface);
        // This context is then cleaned up when the render thread exits.
    }

    auto sync = m_emulationGl->createEmulatedEglFenceSync(type, destroyWhenSignaled);
    if (!sync) {
        return;
    }

    if (outSync) {
        *outSync = (uint64_t)(uintptr_t)sync.release();
    }
    if (outSyncThread) {
        *outSyncThread = reinterpret_cast<uint64_t>(SyncThread::get());
    }
}

void FrameBuffer::Impl::postLoadRenderThreadContextSurfacePtrs() {
    RenderThreadInfoGl* const info = RenderThreadInfoGl::get();
    if (!info) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }

    AutoLock lock(m_lock);
    info->currContext = getContext_locked(info->currContextHandleFromLoad);
    info->currDrawSurf = getWindowSurface_locked(info->currDrawSurfHandleFromLoad);
    info->currReadSurf = getWindowSurface_locked(info->currReadSurfHandleFromLoad);
}

void FrameBuffer::Impl::drainGlRenderThreadResources() {
    // If we're already exiting then snapshot should not contain
    // this thread information at all.
    if (isShuttingDown()) {
        return;
    }

    // Release references to the current thread's context/surfaces if any
    bindContext(0, 0, 0);

    drainGlRenderThreadSurfaces();
    drainGlRenderThreadContexts();

    if (!s_egl.eglReleaseThread()) {
        GFXSTREAM_ERROR("Error: RenderThread @%p failed to eglReleaseThread()", this);
    }
}

void FrameBuffer::Impl::drainGlRenderThreadContexts() {
    if (isShuttingDown()) {
        return;
    }

    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }

    if (tinfo->m_contextSet.empty()) {
        return;
    }

    AutoLock mutex(m_lock);
    gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);
    for (const HandleType contextHandle : tinfo->m_contextSet) {
        m_contexts.erase(contextHandle);
    }
    tinfo->m_contextSet.clear();
}

void FrameBuffer::Impl::drainGlRenderThreadSurfaces() {
    if (isShuttingDown()) {
        return;
    }

    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGL not available.");
    }

    if (tinfo->m_windowSet.empty()) {
        return;
    }

    std::vector<HandleType> colorBuffersToCleanup;

    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    for (const HandleType winHandle : tinfo->m_windowSet) {
        const auto winIt = m_windows.find(winHandle);
        if (winIt != m_windows.end()) {
            if (const HandleType oldColorBufferHandle = winIt->second.second) {
                if (!m_guestManagedColorBufferLifetime) {
                    if (m_refCountPipeEnabled) {
                        if (decColorBufferRefCountLocked(oldColorBufferHandle)) {
                            colorBuffersToCleanup.push_back(oldColorBufferHandle);
                        }
                    } else {
                        if (closeColorBufferLocked(oldColorBufferHandle)) {
                            colorBuffersToCleanup.push_back(oldColorBufferHandle);
                        }
                    }
                }
                m_windows.erase(winIt);
            }
        }
    }
    tinfo->m_windowSet.clear();
}

EmulationGl& FrameBuffer::Impl::getEmulationGl() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return *m_emulationGl;
}

VkEmulation& FrameBuffer::Impl::getEmulationVk() {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return *m_emulationVk;
}

EGLDisplay FrameBuffer::Impl::getDisplay() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglDisplay;
}

EGLSurface FrameBuffer::Impl::getWindowSurface() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    if (!m_emulationGl->mWindowSurface) {
        return EGL_NO_SURFACE;
    }

    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mWindowSurface->getImpl());

    return displaySurfaceGl->getSurface();
}

EGLContext FrameBuffer::Impl::getContext() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglContext;
}

EGLContext FrameBuffer::Impl::getConfig() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    return m_emulationGl->mEglConfig;
}

EGLContext FrameBuffer::Impl::getGlobalEGLContext() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    if (!m_emulationGl->mPbufferSurface) {
        GFXSTREAM_FATAL("FrameBuffer pbuffer surface not available.");
    }

    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mPbufferSurface->getImpl());

    return displaySurfaceGl->getContextForShareContext();
}

EmulatedEglContextPtr FrameBuffer::Impl::getContext_locked(HandleType p_context) {
    return gfxstream::base::findOrDefault(m_contexts, p_context);
}

EmulatedEglWindowSurfacePtr FrameBuffer::Impl::getWindowSurface_locked(HandleType p_windowsurface) {
    return gfxstream::base::findOrDefault(m_windows, p_windowsurface).first;
}

TextureDraw* FrameBuffer::Impl::getTextureDraw() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->mTextureDraw.get();
}

bool FrameBuffer::Impl::isFastBlitSupported() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->isFastBlitSupported();
}

void FrameBuffer::Impl::disableFastBlitForTesting() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    m_emulationGl->disableFastBlitForTesting();
}

HandleType FrameBuffer::Impl::createEmulatedEglImage(HandleType contextHandle, EGLenum target,
                                                     GLuint buffer) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);

    EmulatedEglContext* context = nullptr;
    if (contextHandle) {
        gfxstream::base::AutoWriteLock contextLock(m_contextStructureLock);

        auto it = m_contexts.find(contextHandle);
        if (it == m_contexts.end()) {
            GFXSTREAM_ERROR("Failed to find EmulatedEglContext:%d", contextHandle);
            return false;
        }

        context = it->second.get();
    }

    auto image = m_emulationGl->createEmulatedEglImage(context, target,
                                                       reinterpret_cast<EGLClientBuffer>(buffer));
    if (!image) {
        GFXSTREAM_ERROR("Failed to create EmulatedEglImage");
        return false;
    }

    HandleType imageHandle = image->getHandle();

    m_images[imageHandle] = std::move(image);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglImages[puid].insert(imageHandle);
    }
    return imageHandle;
}

EGLBoolean FrameBuffer::Impl::destroyEmulatedEglImage(HandleType imageHandle) {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    AutoLock mutex(m_lock);

    auto imageIt = m_images.find(imageHandle);
    if (imageIt == m_images.end()) {
        GFXSTREAM_ERROR("Failed to find EmulatedEglImage:%d", imageHandle);
        return false;
    }
    auto& image = imageIt->second;

    EGLBoolean success = image->destroy();
    m_images.erase(imageIt);

    RenderThreadInfo* tInfo = RenderThreadInfo::get();
    uint64_t puid = tInfo->m_puid;
    if (puid) {
        m_procOwnedEmulatedEglImages[puid].erase(imageHandle);
        // We don't explicitly call m_procOwnedEmulatedEglImages.erase(puid) when the
        // size reaches 0, since it could go between zero and one many times in
        // the lifetime of a process. It will be cleaned up by
        // cleanupProcGLObjects(puid) when the process is dead.
    }
    return success;
}

bool FrameBuffer::Impl::flushEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface) {
    AutoLock mutex(m_lock);

    auto it = m_windows.find(p_surface);
    if (it == m_windows.end()) {
        GFXSTREAM_ERROR("FB::flushEmulatedEglWindowSurfaceColorBuffer: window handle %#x not found",
                        p_surface);
        // bad surface handle
        return false;
    }

    EmulatedEglWindowSurface* surface = it->second.first.get();
    surface->flushColorBuffer();

    return true;
}

void FrameBuffer::Impl::fillGLESUsages(android_studio::EmulatorGLESUsages* usages) {
    if (s_egl.eglFillUsages) {
        s_egl.eglFillUsages(usages);
    }
}

void* FrameBuffer::Impl::platformCreateSharedEglContext(void) {
    AutoLock lock(m_lock);

    EGLContext context = 0;
    EGLSurface surface = 0;
    createSharedTrivialContext(&context, &surface);

    void* underlyingContext = s_egl.eglGetNativeContextANDROID(getDisplay(), context);
    if (!underlyingContext) {
        GFXSTREAM_ERROR("Error: Underlying egl backend could not produce a native EGL context.");
        return nullptr;
    }

    m_platformEglContexts[underlyingContext] = {context, surface};

#if defined(__QNX__)
    EGLDisplay currDisplay = eglGetCurrentDisplay();
    EGLSurface currRead = eglGetCurrentSurface(EGL_READ);
    EGLSurface currDraw = eglGetCurrentSurface(EGL_DRAW);
    EGLSurface currContext = eglGetCurrentContext();
    // Make this context current to ensure thread-state is initialized
    s_egl.eglMakeCurrent(getDisplay(), surface, surface, context);
    // Revert back to original state
    s_egl.eglMakeCurrent(currDisplay, currRead, currDraw, currContext);
#endif

    return underlyingContext;
}

bool FrameBuffer::Impl::platformDestroySharedEglContext(void* underlyingContext) {
    AutoLock lock(m_lock);

    auto it = m_platformEglContexts.find(underlyingContext);
    if (it == m_platformEglContexts.end()) {
        GFXSTREAM_ERROR(
            "Error: Could not find underlying egl context %p (perhaps already destroyed?)",
            underlyingContext);
        return false;
    }

    destroySharedTrivialContext(it->second.context, it->second.surface);

    m_platformEglContexts.erase(it);

    return true;
}

bool FrameBuffer::Impl::flushColorBufferFromGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_ERROR("%s: Failed to find ColorBuffer:%d", __func__, colorBufferHandle);
        return false;
    }
    return colorBuffer->flushFromGl();
}

bool FrameBuffer::Impl::invalidateColorBufferForGl(HandleType colorBufferHandle) {
    auto colorBuffer = findColorBuffer(colorBufferHandle);
    if (!colorBuffer) {
        GFXSTREAM_DEBUG("Failed to find ColorBuffer: %d", colorBufferHandle);
        return false;
    }
    return colorBuffer->invalidateForGl();
}

ContextHelper* FrameBuffer::Impl::getPbufferSurfaceContextHelper() const {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }
    if (!m_emulationGl->mPbufferSurface) {
        GFXSTREAM_FATAL("EGL emulation pbuffer surface not available.");
    }
    const auto* displaySurfaceGl =
        reinterpret_cast<const DisplaySurfaceGl*>(m_emulationGl->mPbufferSurface->getImpl());

    return displaySurfaceGl->getContextHelper();
}

bool FrameBuffer::Impl::bindColorBufferToTexture(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToTexture();
}

bool FrameBuffer::Impl::bindColorBufferToTexture2(HandleType p_colorbuffer) {
    // This is only called when using multi window display
    // It will deadlock when posting from main thread.
    std::unique_ptr<AutoLock> mutex;
    if (!postOnlyOnMainThread()) {
        mutex = std::make_unique<AutoLock>(m_lock);
    }

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToTexture2();
}

bool FrameBuffer::Impl::bindColorBufferToRenderbuffer(HandleType p_colorbuffer) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpBindToRenderbuffer();
}

bool FrameBuffer::Impl::bindContext(HandleType p_context, HandleType p_drawSurface,
                                    HandleType p_readSurface) {
    if (m_shuttingDown) {
        return false;
    }

    AutoLock mutex(m_lock);

    EmulatedEglWindowSurfacePtr draw, read;
    EmulatedEglContextPtr ctx;

    //
    // if this is not an unbind operation - make sure all handles are good
    //
    if (p_context || p_drawSurface || p_readSurface) {
        ctx = getContext_locked(p_context);
        if (!ctx) return false;
        auto drawWindowIt = m_windows.find(p_drawSurface);
        if (drawWindowIt == m_windows.end()) {
            // bad surface handle
            return false;
        }
        draw = (*drawWindowIt).second.first;

        if (p_readSurface != p_drawSurface) {
            auto readWindowIt = m_windows.find(p_readSurface);
            if (readWindowIt == m_windows.end()) {
                // bad surface handle
                return false;
            }
            read = (*readWindowIt).second.first;
        } else {
            read = draw;
        }
    } else {
        // if unbind operation, sweep color buffers
        sweepColorBuffersLocked();
    }

    if (!s_egl.eglMakeCurrent(getDisplay(), draw ? draw->getEGLSurface() : EGL_NO_SURFACE,
                              read ? read->getEGLSurface() : EGL_NO_SURFACE,
                              ctx ? ctx->getEGLContext() : EGL_NO_CONTEXT)) {
        GFXSTREAM_ERROR("eglMakeCurrent failed");
        return false;
    }

    //
    // Bind the surface(s) to the context
    //
    RenderThreadInfoGl* const tinfo = RenderThreadInfoGl::get();
    if (!tinfo) {
        GFXSTREAM_FATAL("RenderThreadGl not available.");
    }

    EmulatedEglWindowSurfacePtr bindDraw, bindRead;
    if (draw.get() == NULL && read.get() == NULL) {
        // Unbind the current read and draw surfaces from the context
        bindDraw = tinfo->currDrawSurf;
        bindRead = tinfo->currReadSurf;
    } else {
        bindDraw = draw;
        bindRead = read;
    }

    if (bindDraw.get() != NULL && bindRead.get() != NULL) {
        if (bindDraw.get() != bindRead.get()) {
            bindDraw->bind(ctx, EmulatedEglWindowSurface::BIND_DRAW);
            bindRead->bind(ctx, EmulatedEglWindowSurface::BIND_READ);
        } else {
            bindDraw->bind(ctx, EmulatedEglWindowSurface::BIND_READDRAW);
        }
    }

    //
    // update thread info with current bound context
    //
    tinfo->currContext = ctx;
    tinfo->currDrawSurf = draw;
    tinfo->currReadSurf = read;
    if (ctx) {
        if (ctx->clientVersion() > GLESApi_CM)
            tinfo->m_gl2Dec.setContextData(&ctx->decoderContextData());
        else
            tinfo->m_glDec.setContextData(&ctx->decoderContextData());
    } else {
        tinfo->m_glDec.setContextData(NULL);
        tinfo->m_gl2Dec.setContextData(NULL);
    }
    return true;
}

void FrameBuffer::Impl::createYUVTextures(uint32_t type, uint32_t count, int width, int height,
                                          uint32_t* output) {
    FrameworkFormat format = static_cast<FrameworkFormat>(type);
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    for (uint32_t i = 0; i < count; ++i) {
        if (format == FRAMEWORK_FORMAT_NV12) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, m_features.Yuv420888ToNv21.enabled,
                                         YUVPlane::Y, &output[2 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::UV,
                                         &output[2 * i + 1]);
        } else if (format == FRAMEWORK_FORMAT_YUV_420_888) {
            YUVConverter::createYUVGLTex(GL_TEXTURE0, width, height, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::Y,
                                         &output[3 * i]);
            YUVConverter::createYUVGLTex(GL_TEXTURE1, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::U,
                                         &output[3 * i + 1]);
            YUVConverter::createYUVGLTex(GL_TEXTURE2, width / 2, height / 2, format, m_features.Yuv420888ToNv21.enabled, YUVPlane::V,
                                         &output[3 * i + 2]);
        }
    }
}

void FrameBuffer::Impl::destroyYUVTextures(uint32_t type, uint32_t count, uint32_t* textures) {
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());
    if (type == FRAMEWORK_FORMAT_NV12) {
        s_gles2.glDeleteTextures(2 * count, textures);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        s_gles2.glDeleteTextures(3 * count, textures);
    }
}

void FrameBuffer::Impl::updateYUVTextures(uint32_t type, uint32_t* textures, void* privData,
                                          void* func) {
    AutoLock mutex(m_lock);
    RecursiveScopedContextBind bind(getPbufferSurfaceContextHelper());

    yuv_updater_t updater = (yuv_updater_t)func;
    uint32_t gtextures[3] = {0, 0, 0};

    if (type == FRAMEWORK_FORMAT_NV12) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
    } else if (type == FRAMEWORK_FORMAT_YUV_420_888) {
        gtextures[0] = s_gles2.glGetGlobalTexName(textures[0]);
        gtextures[1] = s_gles2.glGetGlobalTexName(textures[1]);
        gtextures[2] = s_gles2.glGetGlobalTexName(textures[2]);
    }

#ifdef __APPLE__
    EGLContext prevContext = s_egl.eglGetCurrentContext();
    auto mydisp = EglGlobalInfo::getInstance()->getDisplayFromDisplayType(EGL_DEFAULT_DISPLAY);
    void* nativecontext = mydisp->getLowLevelContext(prevContext);
    struct MediaNativeCallerData callerdata;
    callerdata.ctx = nativecontext;
    callerdata.converter = nsConvertVideoFrameToNV12Textures;
    void* pcallerdata = &callerdata;
#else
    void* pcallerdata = nullptr;
#endif

    updater(privData, type, gtextures, pcallerdata);
}

void FrameBuffer::Impl::swapTexturesAndUpdateColorBuffer(uint32_t p_colorbuffer, int x, int y,
                                                         int width, int height, uint32_t format,
                                                         uint32_t type, uint32_t texture_type,
                                                         uint32_t* textures) {
    {
        AutoLock mutex(m_lock);
        ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
        if (!colorBuffer) {
            // bad colorbuffer handle
            return;
        }
        colorBuffer->glOpSwapYuvTexturesAndUpdate(
            format, type, static_cast<FrameworkFormat>(texture_type), textures);
    }
}

bool FrameBuffer::Impl::readColorBufferContents(HandleType p_colorbuffer, size_t* numBytes,
                                                void* pixels) {
    AutoLock mutex(m_lock);

    ColorBufferPtr colorBuffer = findColorBuffer(p_colorbuffer);
    if (!colorBuffer) {
        // bad colorbuffer handle
        return false;
    }

    return colorBuffer->glOpReadContents(numBytes, pixels);
}

void FrameBuffer::Impl::asyncWaitForGpuWithCb(uint64_t eglsync, FenceCompletionCallback cb) {
    EmulatedEglFenceSync* fenceSync = EmulatedEglFenceSync::getFromHandle(eglsync);

    if (!fenceSync) {
        GFXSTREAM_ERROR("err: fence sync 0x%llx not found", (unsigned long long)eglsync);
        return;
    }

    SyncThread::get()->triggerWaitWithCompletionCallback(fenceSync, std::move(cb));
}

const gl::GLESv2Dispatch* FrameBuffer::Impl::getGles2Dispatch() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getGles2Dispatch();
}

const gl::EGLDispatch* FrameBuffer::Impl::getEglDispatch() {
    if (!m_emulationGl) {
        GFXSTREAM_FATAL("GL/EGL emulation not enabled.");
    }

    return m_emulationGl->getEglDispatch();
}

#endif  // GFXSTREAM_ENABLE_HOST_GLES

RepresentativeColorBufferMemoryTypeInfo
FrameBuffer::Impl::getRepresentativeColorBufferMemoryTypeInfo() const {
    if (!m_emulationVk) {
        GFXSTREAM_FATAL("VK emulation not enabled.");
    }

    return m_emulationVk->getRepresentativeColorBufferMemoryTypeInfo();
}

FrameBuffer::~FrameBuffer() = default;

/*static*/
bool FrameBuffer::initialize(int width, int height, const FeatureSet& features, bool useSubWindow,
                             bool egl2egl) {
    GFXSTREAM_DEBUG("FrameBuffer::initialize()");

    if (sFrameBuffer) {
        return true;
    }

    std::unique_ptr<FrameBuffer> framebuffer(new FrameBuffer());

    framebuffer->mImpl = FrameBuffer::Impl::Create(framebuffer.get(), width, height, features,
                                                   useSubWindow, egl2egl);
    if (!framebuffer->mImpl) {
        GFXSTREAM_ERROR("Failed to initialize FrameBuffer().");
        return false;
    }

    sFrameBuffer = framebuffer.release();

    {
        AutoLock lock(sGlobals()->lock);
        sInitialized.store(true, std::memory_order_release);
        sGlobals()->condVar.broadcastAndUnlock(&lock);
    }

    return true;
}

/*static*/
void FrameBuffer::waitUntilInitialized() {
    if (sInitialized.load(std::memory_order_relaxed)) {
        return;
    }

    {
        AutoLock l(sGlobals()->lock);
        sGlobals()->condVar.wait(&l, [] { return sInitialized.load(std::memory_order_acquire); });
    }
}

/*static*/
void FrameBuffer::finalize() {
    FrameBuffer* framebuffer = sFrameBuffer;
    sFrameBuffer = nullptr;
    if (framebuffer) {
        delete framebuffer;
    }

    sInitialized.store(false, std::memory_order_relaxed);
}

/*static*/
FrameBuffer* FrameBuffer::getFB() { return sFrameBuffer; }

bool FrameBuffer::setupSubWindow(FBNativeWindowType p_window, int wx, int wy, int ww, int wh,
                                 int fbw, int fbh, float dpr, float zRot, bool deleteExisting,
                                 bool hideWindow) {
    return mImpl->setupSubWindow(p_window, wx, wy, ww, wh, fbw, fbh, dpr, zRot, deleteExisting,
                                 hideWindow);
}

bool FrameBuffer::removeSubWindow() { return mImpl->removeSubWindow(); }

int FrameBuffer::getWidth() const { return mImpl->getWidth(); }

int FrameBuffer::getHeight() const { return mImpl->getHeight(); }

void FrameBuffer::setPostCallback(Renderer::OnPostCallback onPost, void* onPostContext,
                                  uint32_t displayId, bool useBgraReadback) {
    mImpl->setPostCallback(onPost, onPostContext, displayId, useBgraReadback);
}

bool FrameBuffer::isFormatSupported(GLenum format) { return mImpl->isFormatSupported(format); }

HandleType FrameBuffer::createColorBuffer(int p_width, int p_height, GLenum p_internalFormat,
                                          FrameworkFormat p_frameworkFormat) {
    return mImpl->createColorBuffer(p_width, p_height, p_internalFormat, p_frameworkFormat);
}

void FrameBuffer::createColorBufferWithResourceHandle(int p_width, int p_height,
                                                      GLenum p_internalFormat,
                                                      FrameworkFormat p_frameworkFormat,
                                                      HandleType handle) {
    mImpl->createColorBufferWithResourceHandle(p_width, p_height, p_internalFormat,
                                               p_frameworkFormat, handle);
}

HandleType FrameBuffer::createBuffer(uint64_t size, uint32_t memoryProperty) {
    return mImpl->createBuffer(size, memoryProperty);
}

void FrameBuffer::createBufferWithResourceHandle(uint64_t size, HandleType handle) {
    mImpl->createBufferWithResourceHandle(size, handle);
}

int FrameBuffer::openColorBuffer(HandleType p_colorbuffer) {
    return mImpl->openColorBuffer(p_colorbuffer);
}

void FrameBuffer::closeColorBuffer(HandleType p_colorbuffer) {
    mImpl->closeColorBuffer(p_colorbuffer);
}

void FrameBuffer::closeBuffer(HandleType p_buffer) { mImpl->closeBuffer(p_buffer); }

void FrameBuffer::createGraphicsProcessResources(uint64_t puid) {
    mImpl->createGraphicsProcessResources(puid);
}

std::unique_ptr<ProcessResources> FrameBuffer::removeGraphicsProcessResources(uint64_t puid) {
    return mImpl->removeGraphicsProcessResources(puid);
}

void FrameBuffer::cleanupProcGLObjects(uint64_t puid) { mImpl->cleanupProcGLObjects(puid); }

void FrameBuffer::readBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* bytes) {
    mImpl->readBuffer(p_buffer, offset, size, bytes);
}

void FrameBuffer::readColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                                  GLenum format, GLenum type, void* pixels,
                                  uint64_t outPixelsSize) {
    mImpl->readColorBuffer(p_colorbuffer, x, y, width, height, format, type, pixels, outPixelsSize);
}

void FrameBuffer::readColorBufferYUV(HandleType p_colorbuffer, int x, int y, int width, int height,
                                     void* pixels, uint32_t outPixelsSize) {
    mImpl->readColorBufferYUV(p_colorbuffer, x, y, width, height, pixels, outPixelsSize);
}

bool FrameBuffer::updateBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* pixels) {
    return mImpl->updateBuffer(p_buffer, offset, size, pixels);
}

bool FrameBuffer::updateColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                                    GLenum format, GLenum type, void* pixels) {
    return mImpl->updateColorBuffer(p_colorbuffer, x, y, width, height, format, type, pixels);
}

bool FrameBuffer::updateColorBufferFromFrameworkFormat(HandleType p_colorbuffer, int x, int y,
                                                       int width, int height,
                                                       FrameworkFormat fwkFormat, GLenum format,
                                                       GLenum type, void* pixels, void* metadata) {
    return mImpl->updateColorBufferFromFrameworkFormat(p_colorbuffer, x, y, width, height,
                                                       fwkFormat, format, type, pixels, metadata);
}

bool FrameBuffer::getColorBufferInfo(HandleType p_colorbuffer, int* width, int* height,
                                     GLint* internalformat, FrameworkFormat* frameworkFormat) {
    return mImpl->getColorBufferInfo(p_colorbuffer, width, height, internalformat, frameworkFormat);
}

bool FrameBuffer::getBufferInfo(HandleType p_buffer, int* size) {
    return mImpl->getBufferInfo(p_buffer, size);
}

bool FrameBuffer::post(HandleType p_colorbuffer, bool needLockAndBind) {
    return mImpl->post(p_colorbuffer, needLockAndBind);
}

void FrameBuffer::postWithCallback(HandleType p_colorbuffer, Post::CompletionCallback callback,
                                   bool needLockAndBind) {
    mImpl->postWithCallback(p_colorbuffer, callback, needLockAndBind);
}

bool FrameBuffer::hasGuestPostedAFrame() { return mImpl->hasGuestPostedAFrame(); }

void FrameBuffer::resetGuestPostedAFrame() { mImpl->resetGuestPostedAFrame(); }

void FrameBuffer::doPostCallback(void* pixels, uint32_t displayId) {
    mImpl->doPostCallback(pixels, displayId);
}

void FrameBuffer::getPixels(void* pixels, uint32_t bytes, uint32_t displayId) {
    mImpl->getPixels(pixels, bytes, displayId);
}

void FrameBuffer::flushReadPipeline(int displayId) { mImpl->flushReadPipeline(displayId); }

void FrameBuffer::ensureReadbackWorker() { mImpl->ensureReadbackWorker(); }

bool FrameBuffer::asyncReadbackSupported() { return mImpl->asyncReadbackSupported(); }

Renderer::ReadPixelsCallback FrameBuffer::getReadPixelsCallback() {
    return mImpl->getReadPixelsCallback();
}

Renderer::FlushReadPixelPipeline FrameBuffer::getFlushReadPixelPipeline() {
    return mImpl->getFlushReadPixelPipeline();
}

bool FrameBuffer::repost(bool needLockAndBind) { return mImpl->repost(needLockAndBind); }

void FrameBuffer::setDisplayRotation(float zRot) { mImpl->setDisplayRotation(zRot); }

void FrameBuffer::setDisplayTranslation(float px, float py) {
    mImpl->setDisplayTranslation(px, py);
}

void FrameBuffer::lockContextStructureRead() { mImpl->lockContextStructureRead(); }

void FrameBuffer::unlockContextStructureRead() { mImpl->unlockContextStructureRead(); }

void FrameBuffer::createTrivialContext(HandleType shared, HandleType* contextOut,
                                       HandleType* surfOut) {
    mImpl->createTrivialContext(shared, contextOut, surfOut);
}

void FrameBuffer::setShuttingDown() { mImpl->setShuttingDown(); }

bool FrameBuffer::isShuttingDown() { return mImpl->isShuttingDown(); }

bool FrameBuffer::compose(uint32_t bufferSize, void* buffer, bool post) {
    return mImpl->compose(bufferSize, buffer, post);
}

AsyncResult FrameBuffer::composeWithCallback(uint32_t bufferSize, void* buffer,
                                             Post::CompletionCallback callback) {
    return mImpl->composeWithCallback(bufferSize, buffer, callback);
}

void FrameBuffer::onSave(gfxstream::Stream* stream, const ITextureSaverPtr& textureSaver) {
    mImpl->onSave(stream, textureSaver);
}

bool FrameBuffer::onLoad(gfxstream::Stream* stream, const ITextureLoaderPtr& textureLoader) {
    return mImpl->onLoad(stream, textureLoader);
}

void FrameBuffer::lock() NO_THREAD_SAFETY_ANALYSIS { mImpl->lock(); }

void FrameBuffer::unlock() NO_THREAD_SAFETY_ANALYSIS { mImpl->unlock(); }

float FrameBuffer::getDpr() const { return mImpl->getDpr(); }

int FrameBuffer::windowWidth() const { return mImpl->windowWidth(); }

int FrameBuffer::windowHeight() const { return mImpl->windowHeight(); }

float FrameBuffer::getPx() const { return mImpl->getPx(); }

float FrameBuffer::getPy() const { return mImpl->getPy(); }

int FrameBuffer::getZrot() const { return mImpl->getZrot(); }

void FrameBuffer::setScreenMask(int width, int height, const unsigned char* rgbaData) {
    mImpl->setScreenMask(width, height, rgbaData);
}

#ifdef CONFIG_AEMU
void FrameBuffer::registerVulkanInstance(uint64_t id, const char* appName) const {
    mImpl->registerVulkanInstance(id, appName);
}

void FrameBuffer::unregisterVulkanInstance(uint64_t id) const {
    mImpl->unregisterVulkanInstance(id);
}
#endif  // ifdef CONFIG_AEMU

bool FrameBuffer::isVulkanEnabled() const { return mImpl->isVulkanEnabled(); }

int FrameBuffer::getScreenshot(unsigned int nChannels, unsigned int* width, unsigned int* height,
                               uint8_t* pixels, size_t* cPixels, int displayId, int desiredWidth,
                               int desiredHeight, int desiredRotation, Rect rect) {
    return mImpl->getScreenshot(nChannels, width, height, pixels, cPixels, displayId, desiredWidth,
                                desiredHeight, desiredRotation, rect);
}

void FrameBuffer::onLastColorBufferRef(uint32_t handle) { mImpl->onLastColorBufferRef(handle); }

ColorBufferPtr FrameBuffer::findColorBuffer(HandleType p_colorbuffer) {
    return mImpl->findColorBuffer(p_colorbuffer);
}

BufferPtr FrameBuffer::findBuffer(HandleType p_buffer) { return mImpl->findBuffer(p_buffer); }

void FrameBuffer::registerProcessCleanupCallback(void* key, uint64_t contextId,
                                                 std::function<void()> callback) {
    mImpl->registerProcessCleanupCallback(key, contextId, callback);
}

void FrameBuffer::unregisterProcessCleanupCallback(void* key) {
    mImpl->unregisterProcessCleanupCallback(key);
}

const ProcessResources* FrameBuffer::getProcessResources(uint64_t puid) {
    return mImpl->getProcessResources(puid);
}

int FrameBuffer::createDisplay(uint32_t* displayId) { return mImpl->createDisplay(displayId); }

int FrameBuffer::createDisplay(uint32_t displayId) { return mImpl->createDisplay(displayId); }

int FrameBuffer::destroyDisplay(uint32_t displayId) { return mImpl->destroyDisplay(displayId); }

int FrameBuffer::setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer) {
    return mImpl->setDisplayColorBuffer(displayId, colorBuffer);
}

int FrameBuffer::getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer) {
    return mImpl->getDisplayColorBuffer(displayId, colorBuffer);
}

int FrameBuffer::getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId) {
    return mImpl->getColorBufferDisplay(colorBuffer, displayId);
}

int FrameBuffer::getDisplayPose(uint32_t displayId, int32_t* x, int32_t* y, uint32_t* w,
                                uint32_t* h) {
    return mImpl->getDisplayPose(displayId, x, y, w, h);
}

int FrameBuffer::setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w, uint32_t h,
                                uint32_t dpi) {
    return mImpl->setDisplayPose(displayId, x, y, w, h, dpi);
}

HandleType FrameBuffer::getLastPostedColorBuffer() { return mImpl->getLastPostedColorBuffer(); }

void FrameBuffer::asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle,
                                              FenceCompletionCallback cb) {
    mImpl->asyncWaitForGpuVulkanWithCb(deviceHandle, fenceHandle, cb);
}

void FrameBuffer::asyncWaitForGpuVulkanQsriWithCb(uint64_t image, FenceCompletionCallback cb) {
    mImpl->asyncWaitForGpuVulkanQsriWithCb(image, cb);
}

void FrameBuffer::setGuestManagedColorBufferLifetime(bool guestManaged) {
    mImpl->setGuestManagedColorBufferLifetime(guestManaged);
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::borrowColorBufferForComposition(
    uint32_t colorBufferHandle, bool colorBufferIsTarget) {
    return mImpl->borrowColorBufferForComposition(colorBufferHandle, colorBufferIsTarget);
}

std::unique_ptr<BorrowedImageInfo> FrameBuffer::borrowColorBufferForDisplay(
    uint32_t colorBufferHandle) {
    return mImpl->borrowColorBufferForDisplay(colorBufferHandle);
}

HealthMonitor<>* FrameBuffer::getHealthMonitor() { return mImpl->getHealthMonitor(); }

MetricsLogger& FrameBuffer::getMetricsLogger() { return mImpl->getMetricsLogger(); }

void FrameBuffer::logVulkanDeviceLost() { mImpl->logVulkanDeviceLost(); }

void FrameBuffer::logVulkanOutOfMemory(VkResult result, const char* function, int line,
                                       std::optional<uint64_t> allocationSize) {
    mImpl->logVulkanOutOfMemory(result, function, line, allocationSize);
}

void FrameBuffer::setVsyncHz(int vsyncHz) { mImpl->setVsyncHz(vsyncHz); }

void FrameBuffer::scheduleVsyncTask(VsyncThread::VsyncTask task) { mImpl->scheduleVsyncTask(task); }

void FrameBuffer::setDisplayConfigs(int configId, int w, int h, int dpiX, int dpiY) {
    mImpl->setDisplayConfigs(configId, w, h, dpiX, dpiY);
}

void FrameBuffer::setDisplayActiveConfig(int configId) { mImpl->setDisplayActiveConfig(configId); }

int FrameBuffer::getDisplayConfigsCount() { return mImpl->getDisplayConfigsCount(); }

int FrameBuffer::getDisplayConfigsParam(int configId, EGLint param) {
    return mImpl->getDisplayConfigsParam(configId, param);
}

int FrameBuffer::getDisplayActiveConfig() { return mImpl->getDisplayActiveConfig(); }

bool FrameBuffer::flushColorBufferFromVk(HandleType colorBufferHandle) {
    return mImpl->flushColorBufferFromVk(colorBufferHandle);
}

bool FrameBuffer::flushColorBufferFromVkBytes(HandleType colorBufferHandle, const void* bytes,
                                              size_t bytesSize) {
    return mImpl->flushColorBufferFromVkBytes(colorBufferHandle, bytes, bytesSize);
}

bool FrameBuffer::invalidateColorBufferForVk(HandleType colorBufferHandle) {
    return mImpl->invalidateColorBufferForVk(colorBufferHandle);
}

std::optional<BlobDescriptorInfo> FrameBuffer::exportColorBuffer(HandleType colorBufferHandle) {
    return mImpl->exportColorBuffer(colorBufferHandle);
}

std::optional<BlobDescriptorInfo> FrameBuffer::exportBuffer(HandleType bufferHandle) {
    return mImpl->exportBuffer(bufferHandle);
}

bool FrameBuffer::hasEmulationGl() const { return mImpl->hasEmulationGl(); }

bool FrameBuffer::hasEmulationVk() const { return mImpl->hasEmulationVk(); }

bool FrameBuffer::setColorBufferVulkanMode(HandleType colorBufferHandle, uint32_t mode) {
    return mImpl->setColorBufferVulkanMode(colorBufferHandle, mode);
}

int32_t FrameBuffer::mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa, uint64_t size) {
    return mImpl->mapGpaToBufferHandle(bufferHandle, gpa, size);
}

#if GFXSTREAM_ENABLE_HOST_GLES

HandleType FrameBuffer::getEmulatedEglWindowSurfaceColorBufferHandle(HandleType p_surface) {
    return mImpl->getEmulatedEglWindowSurfaceColorBufferHandle(p_surface);
}

void FrameBuffer::createSharedTrivialContext(EGLContext* contextOut, EGLSurface* surfOut) {
    mImpl->createSharedTrivialContext(contextOut, surfOut);
}

void FrameBuffer::destroySharedTrivialContext(EGLContext context, EGLSurface surf) {
    mImpl->destroySharedTrivialContext(context, surf);
}

bool FrameBuffer::setEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface,
                                                         HandleType p_colorbuffer) {
    return mImpl->setEmulatedEglWindowSurfaceColorBuffer(p_surface, p_colorbuffer);
}

std::string FrameBuffer::getEglString(EGLenum name) { return mImpl->getEglString(name); }

std::string FrameBuffer::getGlString(EGLenum name) { return mImpl->getGlString(name); }

GLESDispatchMaxVersion FrameBuffer::getMaxGlesVersion() { return mImpl->getMaxGlesVersion(); }

std::string FrameBuffer::getGlesExtensionsString() const {
    return mImpl->getGlesExtensionsString();
}

EGLint FrameBuffer::getEglVersion(EGLint* major, EGLint* minor) {
    return mImpl->getEglVersion(major, minor);
}

void FrameBuffer::getNumConfigs(int* outNumConfigs, int* outNumAttribs) {
    mImpl->getNumConfigs(outNumConfigs, outNumAttribs);
}

EGLint FrameBuffer::getConfigs(uint32_t bufferSize, GLuint* buffer) {
    return mImpl->getConfigs(bufferSize, buffer);
}

EGLint FrameBuffer::chooseConfig(EGLint* attribs, EGLint* configs, EGLint configsSize) {
    return mImpl->chooseConfig(attribs, configs, configsSize);
}

void FrameBuffer::getGLStrings(const char** vendor, const char** renderer,
                               const char** version) const {
    mImpl->getGLStrings(vendor, renderer, version);
}

HandleType FrameBuffer::createEmulatedEglContext(int p_config, HandleType p_share,
                                                 GLESApi version) {
    return mImpl->createEmulatedEglContext(p_config, p_share, version);
}

void FrameBuffer::destroyEmulatedEglContext(HandleType p_context) {
    mImpl->destroyEmulatedEglContext(p_context);
}

HandleType FrameBuffer::createEmulatedEglWindowSurface(int p_config, int p_width, int p_height) {
    return mImpl->createEmulatedEglWindowSurface(p_config, p_width, p_height);
}

void FrameBuffer::destroyEmulatedEglWindowSurface(HandleType p_surface) {
    mImpl->destroyEmulatedEglWindowSurface(p_surface);
}

std::vector<HandleType> FrameBuffer::destroyEmulatedEglWindowSurfaceLocked(HandleType p_surface) {
    return mImpl->destroyEmulatedEglWindowSurfaceLocked(p_surface);
}

void FrameBuffer::createEmulatedEglFenceSync(EGLenum type, int destroyWhenSignaled,
                                             uint64_t* outSync, uint64_t* outSyncThread) {
    mImpl->createEmulatedEglFenceSync(type, destroyWhenSignaled, outSync, outSyncThread);
}

void FrameBuffer::drainGlRenderThreadResources() { mImpl->drainGlRenderThreadResources(); }

void FrameBuffer::drainGlRenderThreadContexts() { mImpl->drainGlRenderThreadContexts(); }

void FrameBuffer::drainGlRenderThreadSurfaces() { mImpl->drainGlRenderThreadSurfaces(); }

void FrameBuffer::postLoadRenderThreadContextSurfacePtrs() {
    mImpl->postLoadRenderThreadContextSurfacePtrs();
}

EGLDisplay FrameBuffer::getDisplay() const { return mImpl->getDisplay(); }

EGLSurface FrameBuffer::getWindowSurface() const { return mImpl->getWindowSurface(); }

EGLContext FrameBuffer::getContext() const { return mImpl->getContext(); }

EGLConfig FrameBuffer::getConfig() const { return mImpl->getConfig(); }

EGLContext FrameBuffer::getGlobalEGLContext() const { return mImpl->getGlobalEGLContext(); }

bool FrameBuffer::isFastBlitSupported() const { return mImpl->isFastBlitSupported(); }

void FrameBuffer::disableFastBlitForTesting() { mImpl->disableFastBlitForTesting(); }

HandleType FrameBuffer::createEmulatedEglImage(HandleType context, EGLenum target, GLuint buffer) {
    return mImpl->createEmulatedEglImage(context, target, buffer);
}

EGLBoolean FrameBuffer::destroyEmulatedEglImage(HandleType image) {
    return mImpl->destroyEmulatedEglImage(image);
}

bool FrameBuffer::flushEmulatedEglWindowSurfaceColorBuffer(HandleType p_surface) {
    return mImpl->flushEmulatedEglWindowSurfaceColorBuffer(p_surface);
}

void FrameBuffer::fillGLESUsages(android_studio::EmulatorGLESUsages* usages) {
    mImpl->fillGLESUsages(usages);
}

void* FrameBuffer::platformCreateSharedEglContext(void) {
    return mImpl->platformCreateSharedEglContext();
}

bool FrameBuffer::platformDestroySharedEglContext(void* context) {
    return mImpl->platformDestroySharedEglContext(context);
}

bool FrameBuffer::flushColorBufferFromGl(HandleType colorBufferHandle) {
    return mImpl->flushColorBufferFromGl(colorBufferHandle);
}

bool FrameBuffer::invalidateColorBufferForGl(HandleType colorBufferHandle) {
    return mImpl->invalidateColorBufferForGl(colorBufferHandle);
}

bool FrameBuffer::bindColorBufferToTexture(HandleType p_colorbuffer) {
    return mImpl->bindColorBufferToTexture(p_colorbuffer);
}

bool FrameBuffer::bindColorBufferToTexture2(HandleType p_colorbuffer) {
    return mImpl->bindColorBufferToTexture2(p_colorbuffer);
}

bool FrameBuffer::bindColorBufferToRenderbuffer(HandleType p_colorbuffer) {
    return mImpl->bindColorBufferToRenderbuffer(p_colorbuffer);
}

bool FrameBuffer::bindContext(HandleType p_context, HandleType p_drawSurface,
                              HandleType p_readSurface) {
    return mImpl->bindContext(p_context, p_drawSurface, p_readSurface);
}

void FrameBuffer::createYUVTextures(uint32_t type, uint32_t count, int width, int height,
                                    uint32_t* output) {
    mImpl->createYUVTextures(type, count, width, height, output);
}

void FrameBuffer::destroyYUVTextures(uint32_t type, uint32_t count, uint32_t* textures) {
    mImpl->destroyYUVTextures(type, count, textures);
}

void FrameBuffer::updateYUVTextures(uint32_t type, uint32_t* textures, void* privData, void* func) {
    mImpl->updateYUVTextures(type, textures, privData, func);
}

void FrameBuffer::swapTexturesAndUpdateColorBuffer(uint32_t colorBufferHandle, int x, int y,
                                                   int width, int height, uint32_t format,
                                                   uint32_t type, uint32_t texture_type,
                                                   uint32_t* textures) {
    mImpl->swapTexturesAndUpdateColorBuffer(colorBufferHandle, x, y, width, height, format, type,
                                            texture_type, textures);
}

bool FrameBuffer::readColorBufferContents(HandleType p_colorbuffer, size_t* numBytes,
                                          void* pixels) {
    return mImpl->readColorBufferContents(p_colorbuffer, numBytes, pixels);
}

void FrameBuffer::asyncWaitForGpuWithCb(uint64_t eglsync, FenceCompletionCallback cb) {
    mImpl->asyncWaitForGpuWithCb(eglsync, cb);
}

const void* FrameBuffer::getEglDispatch() { return mImpl->getEglDispatch(); }

const void* FrameBuffer::getGles2Dispatch() { return mImpl->getGles2Dispatch(); }

#endif

const FeatureSet& FrameBuffer::getFeatures() const { return mImpl->getFeatures(); }

RepresentativeColorBufferMemoryTypeInfo FrameBuffer::getRepresentativeColorBufferMemoryTypeInfo()
    const {
    return mImpl->getRepresentativeColorBufferMemoryTypeInfo();
}

}  // namespace gfxstream
