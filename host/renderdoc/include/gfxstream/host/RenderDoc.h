/*
 * Copyright (C) 2011-2021 The Android Open Source Project
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
#ifndef RENDER_DOC_H
#define RENDER_DOC_H

#include <renderdoc_app.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

#include "gfxstream/SharedLibrary.h"
#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace host {

class RenderDoc {
   public:
    using RenderDocApi = RENDERDOC_API_1_4_1;
    static std::unique_ptr<RenderDoc> create(const gfxstream::base::SharedLibrary* renderDocLib) {
        if (!renderDocLib) {
            GFXSTREAM_ERROR("The renderdoc shared library is null.");
            return nullptr;
        }
        pRENDERDOC_GetAPI RENDERDOC_GetAPI =
            reinterpret_cast<pRENDERDOC_GetAPI>(renderDocLib->findSymbol("RENDERDOC_GetAPI"));
        if (!RENDERDOC_GetAPI) {
            GFXSTREAM_ERROR("Failed to find the RENDERDOC_GetAPI symbol.");
            return nullptr;
        }
        RenderDocApi* rdocApi = nullptr;
        int ret =
            RENDERDOC_GetAPI(eRENDERDOC_API_Version_1_4_1, reinterpret_cast<void**>(&rdocApi));
        if (ret != 1 || rdocApi == nullptr) {
            GFXSTREAM_ERROR("Failed to load renderdoc API. %d is returned from RENDERDOC_GetAPI.");
            return nullptr;
        }
        return std::unique_ptr<RenderDoc>(new RenderDoc(rdocApi));
    }

    static constexpr auto kSetActiveWindow = &RenderDocApi::SetActiveWindow;
    static constexpr auto kGetCaptureOptionU32 = &RenderDocApi::GetCaptureOptionU32;
    static constexpr auto kIsFrameCapturing = &RenderDocApi::IsFrameCapturing;
    static constexpr auto kStartFrameCapture = &RenderDocApi::StartFrameCapture;
    static constexpr auto kEndFrameCapture = &RenderDocApi::EndFrameCapture;
    template <class F, class... Args, typename = std::enable_if_t<std::is_invocable_v<F, Args...>>>
    // Call a RenderDoc in-application API given the function pointer and parameters, and guard the
    // API call with a mutex.
    auto call(F(RenderDocApi::*function), Args... args) const {
        std::lock_guard<std::mutex> guard(mMutex);
        return (mRdocApi->*function)(args...);
    }

   private:
    RenderDoc(RenderDocApi* rdocApi) : mRdocApi(rdocApi) {}

    mutable std::mutex mMutex;
    RenderDocApi* mRdocApi = nullptr;
};

template <class RenderDocT>
class RenderDocWithMultipleVkInstancesBase {
   public:
    RenderDocWithMultipleVkInstancesBase(RenderDocT& renderDoc) : mRenderDoc(renderDoc) {}

    void onFrameDelimiter(VkInstance vkInstance) {
        std::lock_guard<std::mutex> guard(mMutex);
        mCaptureContexts.erase(vkInstance);
        if (mRenderDoc.call(RenderDoc::kIsFrameCapturing)) {
            mCaptureContexts.emplace(vkInstance,
                                     std::make_unique<CaptureContext>(mRenderDoc, vkInstance));
        }
    }

    void removeVkInstance(VkInstance vkInstance) {
        std::lock_guard<std::mutex> guard(mMutex);
        mCaptureContexts.erase(vkInstance);
    }

   private:
    class CaptureContext {
       public:
        CaptureContext(RenderDocT& renderDoc, VkInstance vkInstance)
            : mRenderDoc(renderDoc), mVkInstance(vkInstance) {
            mRenderDoc.call(RenderDoc::kStartFrameCapture,
                            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(mVkInstance), nullptr);
        }
        ~CaptureContext() {
            mRenderDoc.call(RenderDoc::kEndFrameCapture,
                            RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(mVkInstance), nullptr);
        }

       private:
        const RenderDocT& mRenderDoc;
        const VkInstance mVkInstance;
    };
    std::mutex mMutex;
    std::unordered_map<VkInstance, std::unique_ptr<CaptureContext>> mCaptureContexts;
    RenderDocT& mRenderDoc;
};

using RenderDocWithMultipleVkInstances = RenderDocWithMultipleVkInstancesBase<RenderDoc>;

}  // namespace host
}  // namespace gfxstream

#endif
