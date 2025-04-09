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

#include <stdint.h>

#include <memory>

#include "VirtioGpu.h"
#include "render-utils/RenderChannel.h"

namespace gfxstream {
namespace host {

class VirtioGpuPipeImpl {
   public:
    virtual ~VirtioGpuPipeImpl() = default;
    virtual int TransferToHost(const char* data, size_t dataSize) = 0;
    virtual int TransferFromHost(char* outRequestedData, size_t outRequestedDataSize) = 0;
};

class VirtioGpuProcessPipe : public VirtioGpuPipeImpl {
   public:
    static std::unique_ptr<VirtioGpuPipeImpl> Create(VirtioGpuContextId id);
    ~VirtioGpuProcessPipe();

    int TransferToHost(const char* data, size_t dataSize) override;
    int TransferFromHost(char* outRequestedData, size_t outRequestedDataSize) override;

   private:
    VirtioGpuProcessPipe(uint64_t id);

    const uint64_t mUniqueId;
    bool mSentUniqueId = false;
    bool mReceivedConfirmation = false;
};

class VirtioGpuRenderThreadPipe : public VirtioGpuPipeImpl {
   public:
    static std::unique_ptr<VirtioGpuPipeImpl> Create(VirtioGpuContextId id);
    ~VirtioGpuRenderThreadPipe();

    int TransferToHost(const char* data, size_t dataSize) override;
    int TransferFromHost(char* outRequestedData, size_t outRequestedDataSize) override;

   private:
    VirtioGpuRenderThreadPipe(VirtioGpuContextId id, RenderChannelPtr channel);

    VirtioGpuContextId mId;
    RenderChannelPtr mChannel;
    RenderChannel::Buffer mReadBuffer;
};

class VirtioGpuPipe {
   public:
    VirtioGpuPipe(VirtioGpuContextId id);

    int TransferToHost(const char* data, size_t dataSize);
    int TransferFromHost(char* outRequestedData, size_t outRequestedDataSize);

   private:
    void CreateUnderlyingPipe(const char* data, size_t dataSize);

    const VirtioGpuContextId mContextId;
    std::unique_ptr<VirtioGpuPipeImpl> mUnderlyingPipe;
};

}  // namespace host
}  // namespace gfxstream