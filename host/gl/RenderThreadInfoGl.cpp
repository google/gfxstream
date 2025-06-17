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

#include "RenderThreadInfoGl.h"

#include <unordered_set>

#include "FrameBuffer.h"
#include "OpenGLESDispatch/EGLDispatch.h"
#include "OpenGLESDispatch/GLESv1Dispatch.h"
#include "OpenGLESDispatch/GLESv2Dispatch.h"
#include "gfxstream/synchronization/Lock.h"
#include "gfxstream/containers/Lookup.h"
#include "gfxstream/host/stream_utils.h"

namespace gfxstream {
namespace gl {

using gfxstream::base::AutoLock;
using gfxstream::base::Lock;
using gfxstream::Stream;

static thread_local RenderThreadInfoGl* tlThreadInfo = nullptr;

RenderThreadInfoGl::RenderThreadInfoGl() {
    m_glDec.initGL(gles1_dispatch_get_proc_func, nullptr);
    m_gl2Dec.initGL(gles2_dispatch_get_proc_func, nullptr);

    if (tlThreadInfo != nullptr) {
      GFXSTREAM_FATAL("Attempted to set thread local GL render thread info twice.");
    }
    tlThreadInfo = this;
}

RenderThreadInfoGl::~RenderThreadInfoGl() {
    tlThreadInfo = nullptr;
}

RenderThreadInfoGl* RenderThreadInfoGl::get() { return tlThreadInfo; }

void RenderThreadInfoGl::onSave(Stream* stream) {
    if (currContext) {
        stream->putBe32(currContext->getHndl());
    } else {
        stream->putBe32(0);
    }
    if (currDrawSurf) {
        stream->putBe32(currDrawSurf->getHndl());
    } else {
        stream->putBe32(0);
    }
    if (currReadSurf) {
        stream->putBe32(currReadSurf->getHndl());
    } else {
        stream->putBe32(0);
    }

    saveCollection(stream, m_contextSet, [](Stream* stream, HandleType val) {
        stream->putBe32(val);
    });
    saveCollection(stream, m_windowSet, [](Stream* stream, HandleType val) {
        stream->putBe32(val);
    });

    stream->putBe64(m_puid);

    // No need to associate render threads with sync threads
    // if there is a global sync thread. This is only needed
    // to maintain backward compatibility with snapshot file format.
    // (Used to be: stream->putBe64(syncThreadAlias))
    stream->putBe64(0);
}

bool RenderThreadInfoGl::onLoad(Stream* stream) {
    FrameBuffer* fb = FrameBuffer::getFB();
    assert(fb);

    HandleType ctxHndl = stream->getBe32();
    HandleType drawSurf = stream->getBe32();
    HandleType readSurf = stream->getBe32();

    currContextHandleFromLoad = ctxHndl;
    currDrawSurfHandleFromLoad = drawSurf;
    currReadSurfHandleFromLoad = readSurf;

    fb->postLoadRenderThreadContextSurfacePtrs();

    loadCollection(stream, &m_contextSet, [](Stream* stream) {
        return stream->getBe32();
    });
    loadCollection(stream, &m_windowSet, [](Stream* stream) {
        return stream->getBe32();
    });

    m_puid = stream->getBe64();

    // (Used to be: syncThreadAlias = stream->getBe64())
    stream->getBe64();

    return true;
}

void RenderThreadInfoGl::postLoadRefreshCurrentContextSurfacePtrs() {
    FrameBuffer* fb = FrameBuffer::getFB();
    assert(fb);

    fb->postLoadRenderThreadContextSurfacePtrs();

    const HandleType ctx = currContext ? currContext->getHndl() : 0;
    const HandleType draw = currDrawSurf ? currDrawSurf->getHndl() : 0;
    const HandleType read = currReadSurf ? currReadSurf->getHndl() : 0;
    fb->bindContext(ctx, draw, read);
}

}  // namespace gl
}  // namespace gfxstream