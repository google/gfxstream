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
#ifndef _LIBRENDER_FRAMEBUFFER_H
#define _LIBRENDER_FRAMEBUFFER_H

#include <stdint.h>

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "Buffer.h"
#include "ColorBuffer.h"
#include "Compositor.h"
#include "gfxstream/host/display.h"
#include "gfxstream/host/display_surface.h"
#include "ExternalObjectManager.h"
#include "Hwc2.h"
#include "PostCommands.h"
#include "PostWorker.h"
#include "ProcessResources.h"
#include "ReadbackWorker.h"
#include "VsyncThread.h"
#include "aemu/base/AsyncResult.h"
#include "aemu/base/EventNotificationSupport.h"
#include "aemu/base/HealthMonitor.h"
#include "aemu/base/Metrics.h"
#include "aemu/base/ThreadAnnotations.h"
#include "aemu/base/files/Stream.h"
#include "aemu/base/synchronization/Lock.h"
#include "aemu/base/synchronization/MessageChannel.h"
#include "aemu/base/threads/Thread.h"
#include "aemu/base/threads/WorkerThread.h"
#include "gfxstream/host/Features.h"

#if GFXSTREAM_ENABLE_HOST_GLES

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "gl/BufferGl.h"
#include "gl/ColorBufferGl.h"
#include "gl/CompositorGl.h"
#include "gl/DisplaySurfaceGl.h"
#include "gl/EmulatedEglConfig.h"
#include "gl/EmulatedEglContext.h"
#include "gl/EmulatedEglImage.h"
#include "gl/EmulatedEglWindowSurface.h"
#include "gl/EmulationGl.h"
#include "gl/GLESVersionDetector.h"
#include "gl/TextureDraw.h"
#else
#include "GlesCompat.h"
#endif

// values for 'param' argument of rcGetFBParam
#define FB_WIDTH 1
#define FB_HEIGHT 2
#define FB_XDPI 3
#define FB_YDPI 4
#define FB_FPS 5
#define FB_MIN_SWAP_INTERVAL 6
#define FB_MAX_SWAP_INTERVAL 7

#include "render-utils/Renderer.h"
#include "render-utils/virtio_gpu_ops.h"
#include "render-utils/render_api.h"
#include "utils/RenderDoc.h"

namespace gfxstream {
namespace vk {
class DisplayVk;
}  // namespace vk
}  // namespace gfxstream

namespace gfxstream {

using android::base::CreateMetricsLogger;
using emugl::HealthMonitor;
using emugl::MetricsLogger;

struct BufferRef {
    BufferPtr buffer;
};

#if GFXSTREAM_ENABLE_HOST_GLES
typedef std::unordered_map<uint64_t, gl::EmulatedEglWindowSurfaceSet>
    ProcOwnedEmulatedEglWindowSurfaces;

typedef std::unordered_map<uint64_t, gl::EmulatedEglContextSet> ProcOwnedEmulatedEglContexts;
typedef std::unordered_map<uint64_t, gl::EmulatedEglImageSet> ProcOwnedEmulatedEGLImages;
#endif

typedef std::unordered_map<uint64_t, ColorBufferSet> ProcOwnedColorBuffers;

typedef std::unordered_map<HandleType, BufferRef> BufferMap;
typedef std::unordered_multiset<HandleType> BufferSet;
typedef std::unordered_map<uint64_t, BufferSet> ProcOwnedBuffers;

typedef std::unordered_map<void*, std::function<void()>> CallbackMap;
typedef std::unordered_map<uint64_t, CallbackMap> ProcOwnedCleanupCallbacks;

// The FrameBuffer class holds the global state of the emulation library on
// top of the underlying EGL/GLES implementation. It should probably be
// named "Display" instead of "FrameBuffer".
//
// There is only one global instance, that can be retrieved with getFB(),
// and which must be previously setup by calling initialize().
//
class FrameBuffer : public android::base::EventNotificationSupport<FrameBufferChangeEvent> {
   public:
    // Initialize the global instance.
    // |width| and |height| are the dimensions of the emulator GPU display
    // in pixels. |useSubWindow| is true to indicate that the caller
    // will use setupSubWindow() to let EmuGL display the GPU content in its
    // own sub-windows. If false, this means the caller will use
    // setPostCallback() instead to retrieve the content.
    // Returns true on success, false otherwise.
    static bool initialize(int width, int height, const gfxstream::host::FeatureSet& features,
                           bool useSubWindow, bool egl2egl);

    // Finalize the instance.
    static void finalize();

    // Setup a sub-window to display the content of the emulated GPU
    // on-top of an existing UI window. |p_window| is the platform-specific
    // parent window handle. |wx|, |wy|, |ww| and |wh| are the
    // dimensions in pixels of the sub-window, relative to the parent window's
    // coordinate. |fbw| and |fbh| are the dimensions used to initialize
    // the framebuffer, which may be different from the dimensions of the
    // sub-window (in which case scaling will be applied automatically).
    // |dpr| is the device pixel ratio of the monitor, which is needed for
    // proper panning on high-density displays (like retina)
    // |zRot| is a rotation angle in degrees, (clockwise in the Y-upwards GL
    // coordinate space).
    //
    // If a sub-window already exists, this function updates the subwindow
    // and framebuffer properties to match the given values.
    //
    // Return true on success, false otherwise.
    //
    // NOTE: This can return false for software-only EGL engines like OSMesa.
    bool setupSubWindow(FBNativeWindowType p_window, int wx, int wy, int ww,
                        int wh, int fbw, int fbh, float dpr, float zRot,
                        bool deleteExisting, bool hideWindow);

    // Remove the sub-window created by setupSubWindow(), if any.
    // Return true on success, false otherwise.
    bool removeSubWindow();

    // Return a pointer to the global instance. initialize() must be called
    // previously, or this will return NULL.
    static FrameBuffer* getFB() { return s_theFrameBuffer; }

    // Wait for a FrameBuffer instance to be initialized and ready to use.
    // This function blocks the caller until there is a valid initialized
    // object in getFB() and
    static void waitUntilInitialized();

    // Return the emulated GPU display width in pixels.
    int getWidth() const { return m_framebufferWidth; }

    // Return the emulated GPU display height in pixels.
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
    HandleType createColorBuffer(int p_width, int p_height,
                                 GLenum p_internalFormat,
                                 FrameworkFormat p_frameworkFormat);
    // Variant of createColorBuffer except with a particular
    // handle already assigned. This is for use with
    // virtio-gpu's RESOURCE_CREATE ioctl.
    void createColorBufferWithResourceHandle(int p_width, int p_height, GLenum p_internalFormat,
                                             FrameworkFormat p_frameworkFormat, HandleType handle);

    // Create a new data Buffer instance from this display instance.
    // The buffer will be backed by a VkBuffer and VkDeviceMemory (if Vulkan
    // is available).
    // |size| is the requested size of Buffer in bytes.
    // |memoryProperty| is the requested memory property bits of the device
    // memory.
    HandleType createBuffer(uint64_t size, uint32_t memoryProperty);

    // Variant of createBuffer except with a particular handle already
    // assigned and using device local memory. This is for use with
    // virtio-gpu's RESOURCE_CREATE ioctl for BLOB resources.
    void createBufferWithResourceHandle(uint64_t size, HandleType handle);

    // Increment the reference count associated with a given ColorBuffer
    // instance. |p_colorbuffer| is its handle value as returned by
    // createColorBuffer().
    int openColorBuffer(HandleType p_colorbuffer);

    // Decrement the reference count associated with a given ColorBuffer
    // instance. |p_colorbuffer| is its handle value as returned by
    // createColorBuffer(). Note that if the reference count reaches 0,
    // the instance is destroyed automatically.
    void closeColorBuffer(HandleType p_colorbuffer);

    // Destroy a Buffer created previously. |p_buffer| is its handle value as
    // returned by createBuffer().
    void closeBuffer(HandleType p_colorbuffer);

    // The caller mustn't refer to this puid before this function returns, i.e. the creation of the
    // host process pipe must be blocked until this function returns.
    void createGraphicsProcessResources(uint64_t puid);
    // The process resource is returned so that we can destroy it on a separate thread.
    std::unique_ptr<ProcessResources> removeGraphicsProcessResources(uint64_t puid);
    // TODO(kaiyili): retire cleanupProcGLObjects in favor of removeGraphicsProcessResources.
    void cleanupProcGLObjects(uint64_t puid);

    // Read the content of a given Buffer into client memory.
    // |p_buffer| is the Buffer's handle value.
    // |offset| and |size| are the position and size of a slice of the buffer
    // that will be read.
    // |bytes| is the address of a caller-provided buffer that will be filled
    // with the buffer data.
    void readBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* bytes);

    // Read the content of a given ColorBuffer into client memory.
    // |p_colorbuffer| is the ColorBuffer's handle value. Similar
    // to glReadPixels(), this can be a slow operation.
    // |x|, |y|, |width| and |height| are the position and dimensions of
    // a rectangle whose pixel values will be transfered to the host.
    // |format| indicates the format of the pixel data, e.g. GL_RGB or GL_RGBA.
    // |type| is the type of pixel data, e.g. GL_UNSIGNED_BYTE.
    // |pixels| is the address of a caller-provided buffer that will be filled
    // with the pixel data.
    // |outPixelsSize| is the size of buffer
    void readColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                         GLenum format, GLenum type, void* pixels, uint64_t outPixelsSize);

    // Old, unsafe version for backwards compatibility
    void readColorBuffer(HandleType p_colorbuffer, int x, int y, int width, int height,
                         GLenum format, GLenum type, void* pixels) {
        return readColorBuffer(p_colorbuffer, x, y, width, height, format, type, pixels,
                               std::numeric_limits<uint64_t>::max());
    }

    // Read the content of a given YUV420_888 ColorBuffer into client memory.
    // |p_colorbuffer| is the ColorBuffer's handle value. Similar
    // to glReadPixels(), this can be a slow operation.
    // |x|, |y|, |width| and |height| are the position and dimensions of
    // a rectangle whose pixel values will be transfered to the host.
    // |pixels| is the address of a caller-provided buffer that will be filled
    // with the pixel data.
    // |outPixelsSize| is the size of buffer
    void readColorBufferYUV(HandleType p_colorbuffer, int x, int y, int width,
                            int height, void* pixels, uint32_t outPixelsSize);

    // Update the content of a given Buffer from client data.
    // |p_buffer| is the Buffer's handle value.
    // |offset| and |size| are the position and size of a slice of the buffer
    // that will be updated.
    // |bytes| is the address of a caller-provided buffer containing the new
    // buffer data.
    bool updateBuffer(HandleType p_buffer, uint64_t offset, uint64_t size, void* pixels);

    // Update the content of a given ColorBuffer from client data.
    // |p_colorbuffer| is the ColorBuffer's handle value. Similar
    // to glReadPixels(), this can be a slow operation.
    // |x|, |y|, |width| and |height| are the position and dimensions of
    // a rectangle whose pixel values will be transfered to the GPU
    // |format| indicates the format of the OpenGL buffer, e.g. GL_RGB or
    // GL_RGBA. |frameworkFormat| indicates the format of the pixel data; if
    // FRAMEWORK_FORMAT_GL_COMPATIBLE, |format| (OpenGL format) is used.
    // Otherwise, explicit conversion to |format| is needed.
    // |type| is the type of pixel data, e.g. GL_UNSIGNED_BYTE.
    // |pixels| is the address of a buffer containing the new pixel data.
    // Returns true on success, false otherwise.
    bool updateColorBuffer(HandleType p_colorbuffer, int x, int y, int width,
                           int height, GLenum format, GLenum type,
                           void* pixels);
    bool updateColorBufferFromFrameworkFormat(HandleType p_colorbuffer, int x, int y, int width,
                                              int height, FrameworkFormat fwkFormat, GLenum format,
                                              GLenum type, void* pixels, void* metadata = nullptr);

    bool getColorBufferInfo(HandleType p_colorbuffer, int* width, int* height,
                            GLint* internalformat,
                            FrameworkFormat* frameworkFormat = nullptr);
    bool getBufferInfo(HandleType p_buffer, int* size);

    // Display the content of a given ColorBuffer into the framebuffer's
    // sub-window. |p_colorbuffer| is a handle value.
    // |needLockAndBind| is used to indicate whether the operation requires
    // acquiring/releasing the FrameBuffer instance's lock and binding the
    // contexts. It should be |false| only when called internally.
    bool post(HandleType p_colorbuffer, bool needLockAndBind = true);
    // The callback will always be called; however, the callback may not be called
    // until after this function has returned. If the callback is deferred, then it
    // will be dispatched to run on SyncThread.
    void postWithCallback(HandleType p_colorbuffer, Post::CompletionCallback callback, bool needLockAndBind = true);
    bool hasGuestPostedAFrame() { return m_guestPostedAFrame; }
    void resetGuestPostedAFrame() { m_guestPostedAFrame = false; }

    // Runs the post callback with |pixels| (good for when the readback
    // happens in a separate place)
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

    // Changes what coordinate of this framebuffer will be displayed at the
    // corner of the GPU sub-window. Specifically, |px| and |py| = 0 means
    // align the bottom-left of the framebuffer with the bottom-left of the
    // sub-window, and |px| and |py| = 1 means align the top right of the
    // framebuffer with the top right of the sub-window. Intermediate values
    // interpolate between these states.
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

    ~FrameBuffer();

    void onSave(android::base::Stream* stream,
                const ITextureSaverPtr& textureSaver);
    bool onLoad(android::base::Stream* stream,
                const ITextureLoaderPtr& textureLoader);

    // lock and unlock handles (EmulatedEglContext, ColorBuffer, EmulatedEglWindowSurface)
    void lock() ACQUIRE(m_lock);
    void unlock() RELEASE(m_lock);

    float getDpr() const { return m_dpr; }
    int windowWidth() const { return m_windowWidth; }
    int windowHeight() const { return m_windowHeight; }
    float getPx() const { return m_px; }
    float getPy() const { return m_py; }
    int getZrot() const { return m_zRot; }

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

    int createDisplay(uint32_t *displayId);
    int createDisplay(uint32_t displayId);
    int destroyDisplay(uint32_t displayId);
    int setDisplayColorBuffer(uint32_t displayId, uint32_t colorBuffer);
    int getDisplayColorBuffer(uint32_t displayId, uint32_t* colorBuffer);
    int getColorBufferDisplay(uint32_t colorBuffer, uint32_t* displayId);
    int getDisplayPose(uint32_t displayId, int32_t* x, int32_t* y, uint32_t* w,
                       uint32_t* h);
    int setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w,
                       uint32_t h, uint32_t dpi = 0);
    void getCombinedDisplaySize(int* w, int* h);
    struct DisplayInfo {
        uint32_t cb;
        int32_t pos_x;
        int32_t pos_y;
        uint32_t width;
        uint32_t height;
        uint32_t dpi;
        DisplayInfo()
            : cb(0), pos_x(0), pos_y(0), width(0), height(0), dpi(0){};
        DisplayInfo(uint32_t cb, int32_t x, int32_t y, uint32_t w, uint32_t h,
                    uint32_t d)
            : cb(cb), pos_x(x), pos_y(y), width(w), height(h), dpi(d) {}
    };
    // Inline with MultiDisplay::s_invalidIdMultiDisplay
    static const uint32_t s_invalidIdMultiDisplay = 0xFFFFFFAB;
    static const uint32_t s_maxNumMultiDisplay = 11;

    HandleType getLastPostedColorBuffer() { return m_lastPostedColorBuffer; }
    void asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle, FenceCompletionCallback cb);
    void asyncWaitForGpuVulkanQsriWithCb(uint64_t image, FenceCompletionCallback cb);

    void setGuestManagedColorBufferLifetime(bool guestManaged);

    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForComposition(uint32_t colorBufferHandle,
                                                                       bool colorBufferIsTarget);
    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForDisplay(uint32_t colorBufferHandle);

    HealthMonitor<>* getHealthMonitor() { return m_healthMonitor.get(); }

    emugl::MetricsLogger& getMetricsLogger() {
        return *m_logger;
    }

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
    // Return the list of configs available from this display.
    const gl::EmulatedEglConfigList* getConfigs() const;

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
                                        gl::GLESApi version = gl::GLESApi_CM);

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

    gl::EmulationGl& getEmulationGl();
    bool hasEmulationGl() const { return m_emulationGl != nullptr; }

    vk::VkEmulation& getEmulationVk();
    bool hasEmulationVk() const { return m_emulationVk != nullptr; }

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

    gl::GLESDispatchMaxVersion getMaxGLESVersion();

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

   private:
    FrameBuffer(int p_width, int p_height, const gfxstream::host::FeatureSet& features, bool useSubWindow);
    // Requires the caller to hold the m_colorBufferMapLock until the new handle is inserted into of
    // the object handle maps.
    HandleType genHandle_locked();

    bool removeSubWindow_locked();
    // Returns the set of ColorBuffers destroyed (for further cleanup)
    std::vector<HandleType> cleanupProcGLObjects_locked(uint64_t puid,
                                                        bool forced = false);

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

   private:

    static FrameBuffer* s_theFrameBuffer;
    static HandleType s_nextHandle;

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

    android::base::Lock m_lock;
    android::base::ReadWriteLock m_contextStructureLock;
    android::base::Lock m_colorBufferMapLock;
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
    android::base::WorkerProcessingResult sendReadbackWorkerCmd(
        const Readback& readback);
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
    android::base::WorkerThread<Readback> m_readbackThread;
    std::atomic_bool m_readbackThreadStarted = false;

    std::string m_graphicsAdapterVendor;
    std::string m_graphicsAdapterName;
    std::string m_graphicsApiVersion;
    std::string m_graphicsApiExtensions;
    std::string m_graphicsDeviceExtensions;
    android::base::Lock m_procOwnedResourcesLock;
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
    android::base::WorkerThread<Post> m_postThread;
    android::base::WorkerProcessingResult postWorkerFunc(Post& post);
    std::future<void> sendPostWorkerCmd(Post post);

    bool m_vulkanEnabled = false;
    // Whether the guest manages ColorBuffer lifetime
    // so we don't need refcounting on the host side.
    bool m_guestManagedColorBufferLifetime = false;

    android::base::MessageChannel<HandleType, 1024>
        mOutstandingColorBufferDestroys;

    Compositor* m_compositor = nullptr;
    bool m_useVulkanComposition = false;

    std::unique_ptr<vk::VkEmulation> m_emulationVk;

    // The implementation for Vulkan native swapchain. Only initialized when useVulkan is set when
    // calling FrameBuffer::initialize(). DisplayVk is actually owned by VkEmulation.
    vk::DisplayVk* m_displayVk = nullptr;
    VkInstance m_vkInstance = VK_NULL_HANDLE;
    std::unique_ptr<emugl::RenderDoc> m_renderDoc = nullptr;

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

    struct DisplayConfig{
        int w;
        int h;
        int dpiX;
        int dpiY;
        DisplayConfig() {}
        DisplayConfig(int w, int h, int x, int y)
        : w(w), h(h), dpiX(x), dpiY(y) {}
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

}  // namespace gfxstream

#endif
