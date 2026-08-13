// Copyright 2026 The Android Open Source Project
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

#include <functional>
#include <string_view>

#include "gfxstream/CancelableFuture.h"
#include "gfxstream/host/color_buffer_interface.h"
#include "gfxstream/synchronization/Lock.h"

namespace gfxstream {
namespace host {

class GlobalState {
   public:
    virtual ~GlobalState() = default;

    virtual IColorBufferRef findColorBuffer(uint32_t colorBufferHandle) = 0;
    virtual void openColorBufferByWindow(uint32_t colorBufferHandle) = 0;
    virtual bool closeColorBufferByWindow(uint32_t colorBufferHandle) = 0;
    virtual uint32_t genHandleLocked() = 0;

    virtual void registerProcessCleanupCallback(void* key, uint64_t contextId,
                                                std::function<void()> callback) = 0;
    virtual void unregisterProcessCleanupCallback(void* key) = 0;

    virtual gfxstream::base::Lock& getGlobalLock() = 0;
    virtual void lockGlobalState() ACQUIRE(getGlobalLock()) = 0;
    virtual void unlockGlobalState() RELEASE(getGlobalLock()) = 0;

    virtual int getColorBufferScreenshot(
        IColorBuffer* cb, int targetWidth, int targetHeight, int skinRotation,
        GfxstreamFormat pixelsFormat, void* outPixels, const Rect& rect,
        const std::optional<std::array<float, 16>>& colorTransform) = 0;

    virtual float getDpr() const = 0;
    virtual int windowWidth() const = 0;
    virtual int windowHeight() const = 0;
    virtual float getPx() const = 0;
    virtual float getPy() const = 0;
    virtual int getZrot() const = 0;

    virtual bool bindContext(uint32_t p_context, uint32_t p_drawSurface,
                             uint32_t p_readSurface) = 0;

    virtual void invalidateColorBuffer(uint32_t colorBufferHandle) = 0;
    virtual void flushColorBuffer(uint32_t colorBufferHandle) = 0;
    virtual void flushColorBufferFromBytes(uint32_t colorBufferHandle, const void* bytes,
                                           size_t bytesSize) = 0;

    virtual CancelableFuture scheduleAsyncWork(std::function<void()> work,
                                               std::string_view description) = 0;

    virtual void registerVulkanInstance(uint64_t id, std::string_view appName) const {}
    virtual void unregisterVulkanInstance(uint64_t id) const {}
};

}  // namespace host
}  // namespace gfxstream
