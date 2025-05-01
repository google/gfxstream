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

#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include <EGL/egl.h>

#include "GLDecoderContextData.h"
#include "Handle.h"
#include "render-utils/stream.h"

namespace gfxstream {
namespace gl {

// Tracks all the possible OpenGL ES API versions.
enum GLESApi {
    GLESApi_CM = 1,
    GLESApi_2 = 2,
    GLESApi_3_0 = 3,
    GLESApi_3_1 = 4,
    GLESApi_3_2 = 5,
};

// A class used to model a guest EGLContext. This simply wraps a host
// EGLContext, associated with an GLDecoderContextData instance that is
// used to store copies of guest-side arrays.
class EmulatedEglContext {
  public:
    // Create a new EmulatedEglContext instance.
    // |display| is the host EGLDisplay handle.
    // |config| is the host EGLConfig to use.
    // |sharedContext| is either EGL_NO_CONTEXT of a host EGLContext handle.
    // |version| specifies the GLES version as a GLESApi.
    static std::unique_ptr<EmulatedEglContext> create(EGLDisplay display,
                                                      EGLConfig config,
                                                      EGLContext sharedContext,
                                                      HandleType hndl,
                                                      GLESApi = GLESApi_CM);

    // Destructor.
    ~EmulatedEglContext();

    // Retrieve host EGLContext value.
    EGLContext getEGLContext() const { return mContext; }

    // Return the GLES version it is trying to emulate in this context.
    // This can be different from the underlying version when using
    // GLES12Translator.
    GLESApi clientVersion() const;

    // Retrieve GLDecoderContextData instance reference for this
    // EmulatedEglContext instance.
    GLDecoderContextData& decoderContextData() { return mContextData; }

    HandleType getHndl() const { return mHndl; }

    void onSave(gfxstream::Stream* stream);
    static std::unique_ptr<EmulatedEglContext> onLoad(gfxstream::Stream* stream,
                                                      EGLDisplay display);
  private:
    EmulatedEglContext(EGLDisplay display,
                       EGLContext context,
                       HandleType hndl,
                       GLESApi version,
                       void* emulatedGLES1Context);

    // Implementation of create
    // |stream| is the stream to load from when restoring a snapshot,
    // set |stream| to nullptr if it is not loading from a snapshot
    static std::unique_ptr<EmulatedEglContext> createImpl(EGLDisplay display,
                                                          EGLConfig config,
                                                          EGLContext sharedContext,
                                                          HandleType hndl,
                                                          GLESApi version,
                                                          gfxstream::Stream *stream);

    EGLDisplay mDisplay;
    EGLContext mContext;
    HandleType mHndl;
    GLESApi mVersion;
    GLDecoderContextData mContextData;
};

typedef std::shared_ptr<EmulatedEglContext> EmulatedEglContextPtr;
typedef std::unordered_map<HandleType, EmulatedEglContextPtr> EmulatedEglContextMap;
typedef std::unordered_set<HandleType> EmulatedEglContextSet;

}  // namespace gl
}  // namespace gfxstream
