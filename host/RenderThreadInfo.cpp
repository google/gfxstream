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

#include "RenderThreadInfo.h"

#include "aemu/base/synchronization/Lock.h"
#include "FrameBuffer.h"

#include <unordered_map>
#include <unordered_set>

namespace gfxstream {

using android::base::AutoLock;
using android::base::Stream;
using android::base::Lock;

static thread_local RenderThreadInfo* s_threadInfoPtr;

struct RenderThreadRegistry {
    Lock lock;
    std::unordered_set<RenderThreadInfo*> threadInfos;
};

static RenderThreadRegistry sRegistry;

RenderThreadInfo::RenderThreadInfo() {
    s_threadInfoPtr = this;
    AutoLock lock(sRegistry.lock);
    sRegistry.threadInfos.insert(this);
}

RenderThreadInfo::~RenderThreadInfo() {
    s_threadInfoPtr = nullptr;
    AutoLock lock(sRegistry.lock);
    sRegistry.threadInfos.erase(this);
}

RenderThreadInfo* RenderThreadInfo::get() {
    return s_threadInfoPtr;
}

// Loop over all active render thread infos. Takes the global render thread info lock.
void RenderThreadInfo::forAllRenderThreadInfos(std::function<void(RenderThreadInfo*)> f) {
    AutoLock lock(sRegistry.lock);
    for (auto info: sRegistry.threadInfos) {
        f(info);
    }
}

#if GFXSTREAM_ENABLE_HOST_GLES
void RenderThreadInfo::initGl() {
    m_glInfo.emplace();
}
#endif

void RenderThreadInfo::onSave(Stream* stream) {
    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (FrameBuffer::getFB()->getFeatures().VulkanSnapshots.enabled) {
        stream->putBe64(m_puid);
        stream->putBe32(m_shouldExit);
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_glInfo) {
        stream->putBe32(1);
        m_glInfo->onSave(stream);
    } else {
        stream->putBe32(0);
    }
#endif

    if (m_vkInfo) {
        stream->putBe32(1);
        m_vkInfo->onSave(stream);
    } else {
        stream->putBe32(0);
    }
}

bool RenderThreadInfo::onLoad(Stream* stream) {
    // TODO(b/309858017): remove if when ready to bump snapshot version
    if (FrameBuffer::getFB()->getFeatures().VulkanSnapshots.enabled) {
        m_puid = stream->getBe64();
        m_shouldExit = stream->getBe32() == 1;
    }

#if GFXSTREAM_ENABLE_HOST_GLES
    const bool loadGlInfo = stream->getBe32() == 1;
    if (loadGlInfo) {
        if (!m_glInfo) {
            m_glInfo.emplace();
        }
        if (!m_glInfo->onLoad(stream)) {
            return false;
        }
    }
#endif

    const bool loadVkInfo = stream->getBe32() == 1;
    if (loadVkInfo) {
        if (!m_vkInfo) {
            m_vkInfo.emplace();
        }
        if (!m_vkInfo->onLoad(stream)) {
            return false;
        }
    }

    return true;
}

void RenderThreadInfo::postLoadRefreshCurrentContextSurfacePtrs() {
#if GFXSTREAM_ENABLE_HOST_GLES
    if (m_glInfo) {
        m_glInfo->postLoadRefreshCurrentContextSurfacePtrs();
    }
#endif
}

}  // namespace gfxstream
