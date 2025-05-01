// Copyright 2025 The Android Open Source Project
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
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "aemu/base/files/Stream.h"
#include "render-utils/small_vector.h"

namespace gfxstream {

class ITextureSaver {
  public:
    ITextureSaver() = default;
    virtual ~ITextureSaver() = default;

    ITextureSaver(ITextureSaver&) = delete;
    ITextureSaver& operator=(ITextureSaver&) = delete;

    using Buffer = SmallVector<unsigned char>;
    using saver_t = std::function<void(android::base::Stream*, Buffer*)>;

    // Save texture to a stream as well as update the index
    virtual void saveTexture(uint32_t texId, const saver_t& saver) = 0;
};

class ITextureLoader {
  public:
    ITextureLoader() = default;
    virtual ~ITextureLoader() { join(); }

    ITextureLoader(ITextureLoader&) = delete;
    ITextureLoader& operator=(ITextureLoader&) = delete;

    virtual bool start() = 0;

    using loader_t = std::function<void(android::base::Stream*)>;

    // Move file position to texId and trigger loader
    virtual void loadTexture(uint32_t texId, const loader_t& loader) = 0;

    // Callbacks to interact with any async use of this ITextureLoader
    // by the Gfxstream renderer.
    struct AsyncUseCallbacks {
        // A callback to interrupt any async use of this ITextureLoader
        // by the Gfxstream renderer.
        std::function<void()> interrupt;
        // A callback to wait until any async use of this ITextureLoader
        // by the Gfxstream renderer has completed.
        std::function<void()> join;
    };
    void setAsyncUseCallbacks(AsyncUseCallbacks callbacks) {
        mAsyncUseCallbacks = callbacks;
    }

    virtual void interrupt() {
        if (mAsyncUseCallbacks) {
            mAsyncUseCallbacks->interrupt();
        }
    }

    virtual void join() {
        if (mAsyncUseCallbacks) {
            mAsyncUseCallbacks->join();
        }
    }

  private:
    std::optional<AsyncUseCallbacks> mAsyncUseCallbacks;
};

using ITextureSaverPtr = std::shared_ptr<ITextureSaver>;
using ITextureLoaderPtr = std::shared_ptr<ITextureLoader>;
using ITextureLoaderWPtr = std::weak_ptr<ITextureLoader>;

}  // namespace gfxstream
