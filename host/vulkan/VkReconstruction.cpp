// Copyright (C) 2019 The Android Open Source Project
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

#include "VkReconstruction.h"

#include <string.h>

#include <unordered_map>

#include "FrameBuffer.h"
#include "VkDecoder.h"
#include "VulkanBoxedHandles.h"
#include "gfxstream/containers/EntityManager.h"

#define DEBUG_RECONSTRUCTION 0

#if DEBUG_RECONSTRUCTION

#define DEBUG_RECON(fmt, ...) GFXSTREAM_INFO(fmt, ##__VA_ARGS__);

#else

#define DEBUG_RECON(fmt, ...)

#endif

namespace gfxstream {
namespace vk {
namespace {

#if DEBUG_RECONSTRUCTION
uint32_t GetOpcode(const VkSnapshotApiCallInfo& info) {
    if (info.packet.size() <= 4) return -1;

    return *(reinterpret_cast<const uint32_t*>(info.packet.data()));
}
#endif

}  // namespace

VkReconstruction::VkReconstruction() = default;

void VkReconstruction::clear() {
    mGraph.clear();
    mApiCallManager.clear();
}

void VkReconstruction::saveReplayBuffers(gfxstream::Stream* stream) {
    DEBUG_RECON("start")

#if DEBUG_RECONSTRUCTION
    dump();
#endif

    std::vector<uint64_t> uniqApiRefsByTopoOrder;

    mGraph.getIdsByTimestamp(uniqApiRefsByTopoOrder);

    size_t totalApiTraceSize = 0;

    for (auto apiHandle : uniqApiRefsByTopoOrder) {
        const VkSnapshotApiCallInfo* info = mApiCallManager.get(apiHandle);
        totalApiTraceSize += info->packet.size();
    }

    DEBUG_RECON("total api trace size: %zu", totalApiTraceSize);

    std::vector<uint64_t> createdHandleBuffer;

    for (auto apiHandle : uniqApiRefsByTopoOrder) {
        auto item = mApiCallManager.get(apiHandle);
        for (auto createdHandle : item->createdHandles) {
            DEBUG_RECON("save handle: 0x%lx", createdHandle);
            createdHandleBuffer.push_back(createdHandle);
        }
    }

    std::vector<uint8_t> apiTraceBuffer;
    apiTraceBuffer.resize(totalApiTraceSize);

    uint8_t* apiTracePtr = apiTraceBuffer.data();

    for (auto apiHandle : uniqApiRefsByTopoOrder) {
        auto item = mApiCallManager.get(apiHandle);
        // 4 bytes for opcode, and 4 bytes for saveBufferRaw's size field
        DEBUG_RECON("saving api handle 0x%lx op code %d name %s", apiHandle, GetOpcode(*item),
                api_opcode_to_string(GetOpcode(*item)));
        memcpy(apiTracePtr, item->packet.data(), item->packet.size());
        apiTracePtr += item->packet.size();
    }

    DEBUG_RECON("created handle buffer size: %zu trace: %zu", createdHandleBuffer.size(),
                apiTraceBuffer.size());

    gfxstream::saveBuffer(stream, createdHandleBuffer);
    gfxstream::saveBuffer(stream, apiTraceBuffer);
}

/*static*/
void VkReconstruction::loadReplayBuffers(gfxstream::Stream* stream,
                                         std::vector<uint64_t>* outHandleBuffer,
                                         std::vector<uint8_t>* outDecoderBuffer) {
    DEBUG_RECON("starting to unpack decoder replay buffer");

    gfxstream::loadBuffer(stream, outHandleBuffer);
    gfxstream::loadBuffer(stream, outDecoderBuffer);

    DEBUG_RECON("finished unpacking decoder replay buffer");
}

VkSnapshotApiCallInfo* VkReconstruction::createApiCallInfo() {
    VkSnapshotApiCallHandle handle = mApiCallManager.add(VkSnapshotApiCallInfo(), 1);

    auto* info = mApiCallManager.get(handle);
    info->handle = handle;
    return info;
}

void VkReconstruction::removeHandleFromApiInfo(VkSnapshotApiCallHandle h, uint64_t toRemove) {}

void VkReconstruction::destroyApiCallInfo(VkSnapshotApiCallHandle h) {
    auto item = mApiCallManager.get(h);

    if (!item) return;

    if (!item->createdHandles.empty()) return;

    item->createdHandles.clear();

    mApiCallManager.remove(h);
    mGraph.removeApiNode(h);
}

void VkReconstruction::destroyApiCallInfoIfUnused(VkSnapshotApiCallInfo* info) {
    if (!info) return;
    auto handle = info->handle;
    auto currentInfo = mApiCallManager.get(handle);
    if (!currentInfo) return;

    if (currentInfo->packet.empty()) {
        mApiCallManager.remove(handle);
        mGraph.removeApiNode(handle);
        return;
    }

    if (!info->extraCreatedHandles.empty()) {
        currentInfo->createdHandles.insert(currentInfo->createdHandles.end(), info->extraCreatedHandles.begin(),
                                    info->extraCreatedHandles.end());
        info->extraCreatedHandles.clear();
    }
}

VkSnapshotApiCallInfo* VkReconstruction::getApiInfo(VkSnapshotApiCallHandle h) {
    return mApiCallManager.get(h);
}

void VkReconstruction::setApiTrace(VkSnapshotApiCallInfo* apiInfo, const uint8_t* packet,
                                   size_t packetLenBytes) {
    auto* info = mApiCallManager.get(apiInfo->handle);
    if (info && packet && packetLenBytes > 0) {
        info->packet.assign(packet, packet + packetLenBytes);
    }
}

void VkReconstruction::dump() { DEBUG_RECON("%s: dep graph dump", __func__); }

void VkReconstruction::addHandles(const uint64_t* toAdd, uint32_t count) {
    if (!toAdd) return;
    mGraph.addNodes(toAdd, count);
}

void VkReconstruction::removeHandles(const uint64_t* toRemove, uint32_t count, bool recursive) {
    if (!toRemove) return;

    mGraph.removeNodesAndDescendants(toRemove, count);
}

void VkReconstruction::forEachHandleAddApi(const uint64_t* toProcess, uint32_t count,
                                           uint64_t apiHandle, HandleState state) {
    if (!toProcess) return;

    if (state == VkReconstruction::CREATED) {
        mGraph.associateWithApiCall(toProcess, count, apiHandle);
    }
}

void VkReconstruction::removeDescendantsOfHandle(const uint64_t handle) {
    mGraph.removeDescendantsOfHandle(handle);
}

void VkReconstruction::removeGrandChildren(const uint64_t handle) {
    mGraph.removeGrandChildren(handle);
}

void VkReconstruction::addHandleDependency(const uint64_t* handles, uint32_t count,
                                           uint64_t parentHandle, HandleState childState,
                                           HandleState parentState) {
    if (!handles) return;

    if (!parentHandle) return;

    mGraph.addNodeIdDependencies(handles, count, parentHandle);
}

void VkReconstruction::setCreatedHandlesForApi(uint64_t apiHandle, const uint64_t* created,
                                               uint32_t count) {
    if (!created) return;

    mGraph.setCreatedNodeIdsForApi(apiHandle, created, count);
    auto item = mApiCallManager.get(apiHandle);

    if (!item) return;

    item->createdHandles.insert(item->createdHandles.end(), created, created + count);
}

}  // namespace vk
}  // namespace gfxstream
