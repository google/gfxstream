// Copyright (C) 2025 The Android Open Source Project
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
#include <unordered_map>
#include <vector>

#include "aemu/base/files/MemStream.h"
#include "render-utils/snapshot_operations.h"

namespace gfxstream {

class InMemoryTextureSaverLoader : public ITextureLoader, public ITextureSaver {
  public:
    void saveTexture(uint32_t textureId, const saver_t& saver) override {
        android::base::MemStream stream;
        saver(&stream, nullptr);
        mTextures[textureId] = stream.buffer();
    }

    void loadTexture(uint32_t textureId, const loader_t& callback) override {
        auto it = mTextures.find(textureId);
        if (it == mTextures.end()) {
            return;
        }
        const auto& textureData = it->second;
        std::vector<char> textureDataCopy = textureData;
        android::base::MemStream stream(std::move(textureDataCopy));
        callback(&stream);
    }

    void acquireLoaderThread(LoaderThreadPtr thread) override {
        mLoaderThread = std::move(thread);
    }

    void join() override {
        if (mLoaderThread) {
            mLoaderThread->wait();
            mLoaderThread.reset();
        }
    }

    void interrupt() override {
        if (mLoaderThread) {
            mLoaderThread->interrupt();
            mLoaderThread->wait();
            mLoaderThread.reset();
        }
    }

    bool start() override { return true; }
    bool hasError() const override { return false; }
    uint64_t diskSize() const override { return -1; }
    bool compressed() const override { return false; }
    bool getDuration(uint64_t*) { return true; }

  private:
    std::unordered_map<uint32_t, std::vector<char>> mTextures;
    LoaderThreadPtr mLoaderThread;
};

}  // namespace gfxstream
