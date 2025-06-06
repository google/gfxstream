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

#pragma once

#include <stdint.h>

#include <memory>

#if GFXSTREAM_ENABLE_HOST_GLES
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#else
#include "GlesCompat.h"
#endif  // GFXSTREAM_ENABLE_HOST_GLES
#include <vulkan/vulkan.h>

#include "Buffer.h"
#include "ColorBuffer.h"
#include "FrameworkFormats.h"
#include "Handle.h"
#include "PostCommands.h"
#include "VsyncThread.h"
#include "gfxstream/AsyncResult.h"
#include "gfxstream/EventNotificationSupport.h"
#include "gfxstream/HealthMonitor.h"
#include "gfxstream/Metrics.h"
#include "gfxstream/host/ProcessResources.h"
#include "gfxstream/host/borrowed_image.h"
#include "gfxstream/host/external_object_manager.h"
#include "gfxstream/host/gl_enums.h"
#include "gfxstream/host/vk_enums.h"
#include "render-utils/Renderer.h"
#include "render-utils/render_api.h"
#include "render-utils/stream.h"
#include "render-utils/virtio_gpu_ops.h"

// values for 'param' argument of rcGetFBParam
#define FB_WIDTH 1
#define FB_HEIGHT 2
#define FB_XDPI 3
#define FB_YDPI 4
#define FB_FPS 5
#define FB_MIN_SWAP_INTERVAL 6
#define FB_MAX_SWAP_INTERVAL 7

namespace gfxstream {

// The FrameBuffer class holds the global state of the emulation library on
// top of the underlying EGL/GLES implementation. It should probably be
// named "Display" instead of "FrameBuffer".
//
// There is only one global instance, that can be retrieved with getFB(),
// and which must be previously setup by calling initialize().
//
class FrameBuffer : public gfxstream::base::EventNotificationSupport<FrameBufferChangeEvent> {
   public:
    ~FrameBuffer();

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
    static FrameBuffer* getFB();

    // Wait for a FrameBuffer instance to be initialized and ready to use.
    // This function blocks the caller until there is a valid initialized
    // object in getFB() and
    static void waitUntilInitialized();

    // Return the emulated GPU display width in pixels.
    int getWidth() const;

    // Return the emulated GPU display height in pixels.
    int getHeight() const;

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
    bool hasGuestPostedAFrame();
    void resetGuestPostedAFrame();

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
    void setDisplayRotation(float zRot);

    // Changes what coordinate of this framebuffer will be displayed at the
    // corner of the GPU sub-window. Specifically, |px| and |py| = 0 means
    // align the bottom-left of the framebuffer with the bottom-left of the
    // sub-window, and |px| and |py| = 1 means align the top right of the
    // framebuffer with the top right of the sub-window. Intermediate values
    // interpolate between these states.
    void setDisplayTranslation(float px, float py);

    void lockContextStructureRead();
    void unlockContextStructureRead();

    // For use with sync threads and otherwise, any time we need a GL context
    // not specifically for drawing, but to obtain certain things about
    // GL state.
    // It can be unsafe / leaky to change the structure of contexts
    // outside the facilities the FrameBuffer class provides.
    void createTrivialContext(HandleType shared, HandleType* contextOut, HandleType* surfOut);

    void setShuttingDown();
    bool isShuttingDown();
    bool compose(uint32_t bufferSize, void* buffer, bool post = true);
    // When false is returned, the callback won't be called. The callback will
    // be called on the PostWorker thread without blocking the current thread.
    AsyncResult composeWithCallback(uint32_t bufferSize, void* buffer,
                             Post::CompletionCallback callback);

    void onSave(gfxstream::Stream* stream,
                const ITextureSaverPtr& textureSaver);
    bool onLoad(gfxstream::Stream* stream,
                const ITextureLoaderPtr& textureLoader);

    // lock and unlock handles (EmulatedEglContext, ColorBuffer, EmulatedEglWindowSurface)
    void lock();
    void unlock();

    float getDpr() const;
    int windowWidth() const;
    int windowHeight() const;
    float getPx() const;
    float getPy() const;
    int getZrot() const;

    void setScreenMask(int width, int height, const unsigned char* rgbaData);

    void registerVulkanInstance(uint64_t id, const char* appName) const;
    void unregisterVulkanInstance(uint64_t id) const;

    bool isVulkanEnabled() const;

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
    int setDisplayPose(uint32_t displayId, int32_t x, int32_t y, uint32_t w, uint32_t h,
                       uint32_t dpi = 0);
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

    HandleType getLastPostedColorBuffer();
    void asyncWaitForGpuVulkanWithCb(uint64_t deviceHandle, uint64_t fenceHandle, FenceCompletionCallback cb);
    void asyncWaitForGpuVulkanQsriWithCb(uint64_t image, FenceCompletionCallback cb);

    void setGuestManagedColorBufferLifetime(bool guestManaged);

    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForComposition(uint32_t colorBufferHandle,
                                                                       bool colorBufferIsTarget);
    std::unique_ptr<BorrowedImageInfo> borrowColorBufferForDisplay(uint32_t colorBufferHandle);

    HealthMonitor<>* getHealthMonitor();
    MetricsLogger& getMetricsLogger();

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

    bool hasEmulationGl() const;
    bool hasEmulationVk() const;

    bool setColorBufferVulkanMode(HandleType colorBufferHandle, uint32_t mode);
    int32_t mapGpaToBufferHandle(uint32_t bufferHandle, uint64_t gpa, uint64_t size = 0);

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

    gl::GLESDispatchMaxVersion getMaxGlesVersion();
    std::string getGlesExtensionsString() const;

    void getNumConfigs(int* outNumConfigs, int* outNumAttribs);
    EGLint getConfigs(uint32_t bufferSize, GLuint* buffer);
    EGLint chooseConfig(EGLint* attribs, EGLint* configs, EGLint configsSize);

    // Retrieve the GL strings of the underlying EGL/GLES implementation.
    // On return, |*vendor|, |*renderer| and |*version| will point to strings
    // that are owned by the instance (and must not be freed by the caller).
    void getGLStrings(const char** vendor, const char** renderer, const char** version) const;

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

    void postLoadRenderThreadContextSurfacePtrs();

    // Return the host EGLDisplay used by this instance.
    EGLDisplay getDisplay() const;
    EGLSurface getWindowSurface() const;
    EGLContext getContext() const;
    EGLConfig getConfig() const;

    EGLContext getGlobalEGLContext() const;

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

    // Fill GLES usage protobuf
    void fillGLESUsages(android_studio::EmulatorGLESUsages*);

    void* platformCreateSharedEglContext(void);
    bool platformDestroySharedEglContext(void* context);

    bool flushColorBufferFromGl(HandleType colorBufferHandle);

    bool invalidateColorBufferForGl(HandleType colorBufferHandle);

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

    const void* getEglDispatch();
    const void* getGles2Dispatch();
#endif  // GFXSTREAM_ENABLE_HOST_GLES

    const gfxstream::host::FeatureSet& getFeatures() const;

    host::RepresentativeColorBufferMemoryTypeInfo getRepresentativeColorBufferMemoryTypeInfo()
        const;

   private:
    FrameBuffer() = default;

    class Impl;
    std::unique_ptr<Impl> mImpl;
};

}  // namespace gfxstream
