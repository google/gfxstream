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

#include <cstdint>
#include <memory>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "ContextHelper.h"
#include "gfxstream/host/display_surface.h"
#include "render-utils/render_api_platform_types.h"

namespace gfxstream {
namespace gl {

class DisplaySurfaceGl : public gfxstream::DisplaySurfaceImpl {
  public:
    static std::unique_ptr<DisplaySurfaceGl> createPbufferSurface(EGLDisplay display,
                                                                  EGLConfig config,
                                                                  EGLContext shareContext,
                                                                  const EGLint* contextAttribs,
                                                                  EGLint width,
                                                                  EGLint height);

    static std::unique_ptr<DisplaySurfaceGl> createWindowSurface(EGLDisplay display,
                                                                 EGLConfig config,
                                                                 EGLContext shareContext,
                                                                 const GLint* contextAttribs,
                                                                 FBNativeWindowType window);

    ~DisplaySurfaceGl();

    ContextHelper* getContextHelper() const { return mContextHelper.get(); }

    EGLContext getContextForShareContext() const { return mContext; }

    EGLSurface getSurface() const { return mSurface; }

    bool bindContext() const;

   private:
    friend class DisplayGl;

    DisplaySurfaceGl(EGLDisplay display,
                     EGLSurface surface,
                     EGLContext context);

    EGLDisplay mDisplay = EGL_NO_DISPLAY;
    EGLSurface mSurface = EGL_NO_SURFACE;
    EGLContext mContext = EGL_NO_CONTEXT;

    std::unique_ptr<ContextHelper> mContextHelper;
};

}  // namespace gl
}  // namespace gfxstream
