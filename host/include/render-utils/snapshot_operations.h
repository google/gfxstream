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
#include <vector>

#include "aemu/base/containers/SmallVector.h"
#include "aemu/base/files/Stream.h"
#include "aemu/base/threads/Thread.h"

namespace gfxstream {

class ITextureSaver {
  public:
    ITextureSaver() = default;
    virtual ~ITextureSaver() = default;

    ITextureSaver(ITextureSaver&) = delete;
    ITextureSaver& operator=(ITextureSaver&) = delete;

    using Buffer = android::base::SmallVector<unsigned char>;
    using saver_t = std::function<void(android::base::Stream*, Buffer*)>;

    // Save texture to a stream as well as update the index
    virtual void saveTexture(uint32_t texId, const saver_t& saver) = 0;
    virtual bool hasError() const = 0;
    virtual uint64_t diskSize() const = 0;
    virtual bool compressed() const = 0;
    virtual bool getDuration(uint64_t* duration) = 0;
};

class ITextureLoader {
  public:
    ITextureLoader() = default;
    virtual ~ITextureLoader() = default;

    ITextureLoader(ITextureLoader&) = delete;
    ITextureLoader& operator=(ITextureLoader&) = delete;

    using LoaderThreadPtr = std::shared_ptr<android::base::InterruptibleThread>;
    using loader_t = std::function<void(android::base::Stream*)>;

    virtual bool start() = 0;
    // Move file position to texId and trigger loader
    virtual void loadTexture(uint32_t texId, const loader_t& loader) = 0;
    virtual void acquireLoaderThread(LoaderThreadPtr thread) = 0;
    virtual bool hasError() const = 0;
    virtual uint64_t diskSize() const = 0;
    virtual bool compressed() const = 0;
    virtual void join() = 0;
    virtual void interrupt() = 0;
};

using ITextureSaverPtr = std::shared_ptr<ITextureSaver>;
using ITextureLoaderPtr = std::shared_ptr<ITextureLoader>;
using ITextureLoaderWPtr = std::weak_ptr<ITextureLoader>;

}  // namespace gfxstream
