// Copyright 2022 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>
#include <string>

#include "Handle.h"
#include "StalePtrRegistry.h"
#include "aemu/base/files/Stream.h"
#include "gl/EmulatedEglContext.h"
#include "gl/EmulatedEglWindowSurface.h"
#include "gl/gles1_dec/GLESv1Decoder.h"
#include "gl/gles2_dec/GLESv2Decoder.h"

namespace gfxstream {
namespace gl {

struct RenderThreadInfoGl {
    // Create new instance. Only call this once per thread.
    // Future calls to get() will return this instance until
    // it is destroyed.
    RenderThreadInfoGl();

    // Destructor.
    ~RenderThreadInfoGl();

    // Return the current thread's instance, if any, or NULL.
    static RenderThreadInfoGl* get();

    // Functions to save / load a snapshot
    // They must be called after Framebuffer snapshot
    void onSave(android::base::Stream* stream);
    bool onLoad(android::base::Stream* stream);

    // Sometimes we can load render thread info before
    // FrameBuffer repopulates the contexts.
    void postLoadRefreshCurrentContextSurfacePtrs();

    // The unique id of owner guest process of this render thread
    uint64_t m_puid = 0;

    // All the contexts that are created by this render thread.
    // New emulator manages contexts in guest process level,
    // m_contextSet should be deprecated. It is only kept for
    // backward compatibility reason.
    ThreadContextSet                m_contextSet;
    // all the window surfaces that are created by this render thread
    WindowSurfaceSet                m_windowSet;

    // Current EGL context, draw surface and read surface.
    HandleType currContextHandleFromLoad;
    HandleType currDrawSurfHandleFromLoad;
    HandleType currReadSurfHandleFromLoad;

    EmulatedEglContextPtr currContext;
    EmulatedEglWindowSurfacePtr currDrawSurf;
    EmulatedEglWindowSurfacePtr currReadSurf;

    // Decoder states.
    GLESv1Decoder                   m_glDec;
    GLESv2Decoder                   m_gl2Dec;
};

}  // namespace gl
}  // namespace gfxstream