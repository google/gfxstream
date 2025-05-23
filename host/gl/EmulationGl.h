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

#pragma once

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES3/gl3.h>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>

#include "BufferGl.h"
#include "ColorBufferGl.h"
#include "Compositor.h"
#include "CompositorGl.h"
#include "ContextHelper.h"
#include "gfxstream/host/display.h"
#include "DisplayGl.h"
#include "gfxstream/host/display_surface.h"
#include "EmulatedEglConfig.h"
#include "EmulatedEglContext.h"
#include "EmulatedEglFenceSync.h"
#include "EmulatedEglImage.h"
#include "EmulatedEglWindowSurface.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "ReadbackWorkerGl.h"
#include "TextureDraw.h"
#include "render-utils/stream.h"
#include "gfxstream/host/Features.h"

#define EGL_NO_CONFIG ((EGLConfig)0)

namespace gfxstream {
class FrameBuffer;
}  // namespace gfxstream

namespace gfxstream {
namespace gl {

class EmulationGl {
   public:
    static std::unique_ptr<EmulationGl> create(uint32_t width, uint32_t height,
                                               const gfxstream::host::FeatureSet& features,
                                               bool allowWindowSurface, bool egl2egl);

    ~EmulationGl();

    const EGLDispatch* getEglDispatch();
    const GLESv2Dispatch* getGles2Dispatch();

    GLESDispatchMaxVersion getGlesMaxDispatchVersion() const;

    static const GLint* getGlesMaxContextAttribs();

    bool hasEglExtension(const std::string& ext) const;
    void getEglVersion(EGLint* major, EGLint* minor) const;

    void getGlesVersion(GLint* major, GLint* minor) const;
    const std::string& getGlesVendor() const { return mGlesVendor; }
    const std::string& getGlesRenderer() const { return mGlesRenderer; }
    const std::string& getGlesVersionString() const { return mGlesVersion; }
    const std::string& getGlesExtensionsString() const { return mGlesExtensions; }
    bool isGlesVulkanInteropSupported() const { return mGlesVulkanInteropSupported; }

    bool isMesa() const;

    bool isFastBlitSupported() const;
    void disableFastBlitForTesting();

    bool isAsyncReadbackSupported() const;

    std::unique_ptr<DisplaySurface> createWindowSurface(uint32_t width,
                                                        uint32_t height,
                                                        EGLNativeWindowType window);

    const EmulatedEglConfigList& getEmulationEglConfigs() const { return *mEmulatedEglConfigs; }

    CompositorGl* getCompositor() { return mCompositorGl.get(); }

    DisplayGl* getDisplay() { return mDisplayGl.get(); }

    ReadbackWorkerGl* getReadbackWorker() { return mReadbackWorkerGl.get(); }

    using GlesUuid = std::array<uint8_t, GL_UUID_SIZE_EXT>;
    const std::optional<GlesUuid> getGlesDeviceUuid() const { return mGlesDeviceUuid; }

    std::unique_ptr<BufferGl> createBuffer(uint64_t size, HandleType handle);

    std::unique_ptr<BufferGl> loadBuffer(gfxstream::Stream* stream);

    bool isFormatSupported(GLenum format);

    std::unique_ptr<ColorBufferGl> createColorBuffer(uint32_t width, uint32_t height,
                                                     GLenum internalFormat,
                                                     FrameworkFormat frameworkFormat,
                                                     HandleType handle);

    std::unique_ptr<ColorBufferGl> loadColorBuffer(gfxstream::Stream* stream);

    std::unique_ptr<EmulatedEglContext> createEmulatedEglContext(
        uint32_t emulatedEglConfigIndex,
        const EmulatedEglContext* shareContext,
        GLESApi api,
        HandleType handle);

    std::unique_ptr<EmulatedEglContext> loadEmulatedEglContext(
        gfxstream::Stream* stream);

    std::unique_ptr<EmulatedEglFenceSync> createEmulatedEglFenceSync(
        EGLenum type,
        int destroyWhenSignaled);

    std::unique_ptr<EmulatedEglImage> createEmulatedEglImage(
        EmulatedEglContext* context,
        EGLenum target,
        EGLClientBuffer buffer);

    std::unique_ptr<EmulatedEglWindowSurface> createEmulatedEglWindowSurface(
        uint32_t emulatedConfigIndex,
        uint32_t width,
        uint32_t height,
        HandleType handle);

    std::unique_ptr<EmulatedEglWindowSurface> loadEmulatedEglWindowSurface(
        gfxstream::Stream* stream,
        const ColorBufferMap& colorBuffers,
        const EmulatedEglContextMap& contexts);

    std::unique_ptr<gfxstream::DisplaySurface> createFakeWindowSurface();

   private:
    // TODO(b/233939967): Remove this after fully transitioning to EmulationGl.
   friend class gfxstream::FrameBuffer;

   EmulationGl() = default;

   ContextHelper* getColorBufferContextHelper();

   gfxstream::host::FeatureSet mFeatures;

   EGLDisplay mEglDisplay = EGL_NO_DISPLAY;
   EGLint mEglVersionMajor = 0;
   EGLint mEglVersionMinor = 0;
   std::string mEglVendor;
   std::unordered_set<std::string> mEglExtensions;
   EGLConfig mEglConfig = EGL_NO_CONFIG;

   // The "global" context that all other contexts are shared with.
   EGLContext mEglContext = EGL_NO_CONTEXT;

   // Used for ColorBuffer ops.
   std::unique_ptr<gfxstream::DisplaySurface> mPbufferSurface;

   // Used for Composition and Display ops.
   std::unique_ptr<gfxstream::DisplaySurface> mWindowSurface;

   GLint mGlesVersionMajor = 0;
   GLint mGlesVersionMinor = 0;
   GLESDispatchMaxVersion mGlesDispatchMaxVersion = GLES_DISPATCH_MAX_VERSION_2;
   std::string mGlesVendor;
   std::string mGlesRenderer;
   std::string mGlesVersion;
   std::string mGlesExtensions;
   std::optional<GlesUuid> mGlesDeviceUuid;
   bool mGlesVulkanInteropSupported = false;

   std::unique_ptr<EmulatedEglConfigList> mEmulatedEglConfigs;

   bool mFastBlitSupported = false;

   std::unique_ptr<CompositorGl> mCompositorGl;
   std::unique_ptr<DisplayGl> mDisplayGl;
   std::unique_ptr<ReadbackWorkerGl> mReadbackWorkerGl;

   std::unique_ptr<TextureDraw> mTextureDraw;

   uint32_t mWidth = 0;
   uint32_t mHeight = 0;
};

}  // namespace gl
}  // namespace gfxstream
