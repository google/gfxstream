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

#include "VirtioGpuPipe.h"

#include <cstring>

#include "gfxstream/common/logging.h"
#include "gfxstream/system/System.h"

namespace gfxstream {
namespace host {

/*static*/
std::unique_ptr<VirtioGpuPipeImpl> VirtioGpuProcessPipe::Create(VirtioGpuContextId id) {
    static std::atomic<uint64_t> sNextUniqueId{1};
    const uint64_t uniqueId = sNextUniqueId++;
    GFXSTREAM_DEBUG("Creating VirtioGpuProcessPipe:%" PRIu64, uniqueId);

    // NOTE: historically, the process pipe would have done a
    //
    //   renderer->onGuestGraphicsProcessCreate(uniqueId);
    //
    // but virtio-gpu uses VirtioGpuContext to manage process resources.

    return std::unique_ptr<VirtioGpuPipeImpl>(new VirtioGpuProcessPipe(uniqueId));
}

VirtioGpuProcessPipe::VirtioGpuProcessPipe(uint64_t id) : mUniqueId(id) {}

VirtioGpuProcessPipe::~VirtioGpuProcessPipe() {
    GFXSTREAM_DEBUG("Destroying VirtioGpuProcessPipe:%" PRIu64, mUniqueId);

    // NOTE: historically, the process pipe would have done a
    //
    //   renderer->cleanupProcGLObjects(uniqueId);
    //
    // but virtio-gpu uses VirtioGpuContext to manage process resources.
}

int VirtioGpuProcessPipe::TransferToHost(const char* data, size_t dataSize) {
    if (mReceivedConfirmation) {
        GFXSTREAM_FATAL("Unexpected data sent to VirtioGpuProcessPipe:%" PRIu64, mUniqueId);
        return -EINVAL;
    }

    if (dataSize < 4) {
        GFXSTREAM_FATAL("Unexpected data size for confirmation for VirtioGpuProcessPipe:%" PRIu64
                        " size:%zu",
                        mUniqueId, dataSize);
        return -EINVAL;
    }

    int32_t confirmation = 0;
    std::memcpy(&confirmation, data, sizeof(confirmation));

    if (confirmation != 100) {
        GFXSTREAM_FATAL("Unexpected confirmation for VirtioGpuProcessPipe:%" PRIu64 " received:%d",
                        mUniqueId, confirmation);
        return -EINVAL;
    }

    mReceivedConfirmation = true;
    return 0;
}

int VirtioGpuProcessPipe::TransferFromHost(char* outRequestedData, size_t requestedDataSize) {
    if (mSentUniqueId) {
        GFXSTREAM_FATAL("Unexpected data request from VirtioGpuProcessPipe:%" PRIu64, mUniqueId);
        return -EINVAL;
    }

    if (requestedDataSize < sizeof(mUniqueId)) {
        GFXSTREAM_FATAL(
            "Unexpected data size for unique id request for VirtioGpuProcessPipe:%" PRIu64
            " size:%zu",
            mUniqueId, requestedDataSize);
        return -EINVAL;
    }

    std::memcpy(outRequestedData, (const char*)&mUniqueId, sizeof(mUniqueId));

    mSentUniqueId = true;
    return 0;
}

/*static*/
std::unique_ptr<VirtioGpuPipeImpl> VirtioGpuRenderThreadPipe::Create(Renderer* renderer, VirtioGpuContextId id) {
    GFXSTREAM_DEBUG("Creating RenderChannel for context:%" PRIu32, id);

    if (!renderer) {
        GFXSTREAM_ERROR("Failed to create VirtioGpuRenderThreadPipe: no renderer.");
        return nullptr;
    }

    auto channel = renderer->createRenderChannel(nullptr, id);
    if (channel == nullptr) {
        GFXSTREAM_ERROR("Failed to create RenderChannel for context: %" PRIu32, id);
        return nullptr;
    }

    return std::unique_ptr<VirtioGpuPipeImpl>(
        new VirtioGpuRenderThreadPipe(id, std::move(channel)));
}

VirtioGpuRenderThreadPipe::VirtioGpuRenderThreadPipe(VirtioGpuContextId id,
                                                     RenderChannelPtr channel)
    : mId(id), mChannel(channel) {}

VirtioGpuRenderThreadPipe::~VirtioGpuRenderThreadPipe() {
    if (mChannel) {
        GFXSTREAM_DEBUG("Stopping RenderThread for context:%" PRIu32, mId);
        mChannel->stop();
    }
}

int VirtioGpuRenderThreadPipe::TransferToHost(const char* data, size_t dataSize) {
    while (true) {
        RenderChannel::Buffer channelBuffer;
        channelBuffer.resize_noinit(dataSize);
        std::memcpy(channelBuffer.data(), data, dataSize);

        RenderChannel::IoResult result = mChannel->tryWrite(std::move(channelBuffer));
        if (result == RenderChannel::IoResult::Ok) {
            return 0;
        }

        if (result == RenderChannel::IoResult::TryAgain) {
            continue;
        }

        GFXSTREAM_ERROR("Failed to write data to RenderChannel.");
        return -EINVAL;
    }

    GFXSTREAM_FATAL("Unreachable?");
    return -EINVAL;
}

int VirtioGpuRenderThreadPipe::TransferFromHost(char* outRequestedData, size_t requestedDataSize) {
    size_t received = 0;
    while (received < requestedDataSize) {
        // Try to get some data from the RenderThread.
        if (mReadBuffer.empty()) {
            static const RenderChannel::Duration kBlockAtMostUs = 10000;
            auto currTime = gfxstream::base::getUnixTimeUs();

            RenderChannel::IoResult result =
                mChannel->readBefore(&mReadBuffer, currTime + kBlockAtMostUs);
            if (result == RenderChannel::IoResult::Timeout ||
                result == RenderChannel::IoResult::TryAgain) {
                continue;
            } else if (result != RenderChannel::IoResult::Ok) {
                GFXSTREAM_ERROR("Failed to read data from RenderChannel.");
                return EIO;
            }
        }

        const size_t requestedSizeRemaining = requestedDataSize - received;
        const size_t availableSize = mReadBuffer.size();

        const size_t toCopy = std::min(requestedSizeRemaining, availableSize);
        std::memcpy(outRequestedData + received, mReadBuffer.data(), toCopy);
        received += toCopy;

        if (toCopy == availableSize) {
            mReadBuffer.clear();
        } else {
            const size_t remaining = availableSize - toCopy;
            RenderChannel::Buffer replacement;
            replacement.resize_noinit(remaining);
            std::memcpy(replacement.data(), mReadBuffer.data() + toCopy, remaining);
            mReadBuffer = std::move(replacement);
        }
    }

    return 0;
}

VirtioGpuPipe::VirtioGpuPipe(RendererPtr renderer, VirtioGpuContextId id)
    : mRenderer(renderer), mContextId(id) {}

int VirtioGpuPipe::TransferToHost(const char* data, size_t dataSize) {
    // The first data sent to the host is a string declaring the type of pipe requested.
    if (mUnderlyingPipe == nullptr) {
        CreateUnderlyingPipe(data, dataSize);
        return 0;
    }

    return mUnderlyingPipe->TransferToHost(data, dataSize);
}

int VirtioGpuPipe::TransferFromHost(char* outRequestedData, size_t outRequestedDataSize) {
    if (mUnderlyingPipe == nullptr) {
        GFXSTREAM_FATAL("No pipe available!!!");
        return -1;
    }

    return mUnderlyingPipe->TransferFromHost(outRequestedData, outRequestedDataSize);
}

void VirtioGpuPipe::CreateUnderlyingPipe(const char* data, size_t dataSize) {
    std::string pipeType(data, dataSize);
    if (!pipeType.empty() && pipeType.back() == '\0') {
        pipeType.resize(pipeType.size() - 1);
    }

    GFXSTREAM_DEBUG("VirtioGpuPipe received type:%s", pipeType.c_str());

    if (pipeType == "pipe:GLProcessPipe") {
        mUnderlyingPipe = VirtioGpuProcessPipe::Create(mContextId);
    } else if (pipeType == "pipe:opengles") {
        mUnderlyingPipe = VirtioGpuRenderThreadPipe::Create(mRenderer.get(), mContextId);
    } else {
        GFXSTREAM_FATAL("Unhandled pipe type: '%s'.", pipeType.c_str());
    }

    if (!mUnderlyingPipe) {
        GFXSTREAM_ERROR("Failed to create underlying pipe!");
    }
}

}  // namespace host
}  // namespace gfxstream