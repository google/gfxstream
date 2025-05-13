// Copyright 2024 The Android Open Source Project
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

#include "DeviceLostHelper.h"

#include <algorithm>
#include <iterator>
#include <set>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace vk {

void DeviceLostHelper::enableWithNvidiaDeviceDiagnosticCheckpoints() { mEnabled = true; }

const void* DeviceLostHelper::createMarkerForCommandBuffer(const VkCommandBuffer& commandBuffer,
                                                           MarkerType type) {
    std::lock_guard<std::mutex> lock(mMarkersMutex);

    auto it = mMarkers.insert(CheckpointMarker{commandBuffer, type});

    // References and pointers to data stored in the container are only
    // invalidated by erasing that element, even when the corresponding
    // iterator is invalidated.
    return reinterpret_cast<const void*>(&(*it.first));
}

void DeviceLostHelper::removeMarkersForCommandBuffer(const VkCommandBuffer& commandBuffer) {
    std::lock_guard<std::mutex> lock(mMarkersMutex);
    mMarkers.erase(CheckpointMarker{
        .commandBuffer = commandBuffer,
        .type = MarkerType::kBegin,
    });
    mMarkers.erase(CheckpointMarker{
        .commandBuffer = commandBuffer,
        .type = MarkerType::kEnd,
    });
}

void DeviceLostHelper::addNeededDeviceExtensions(std::vector<const char*>* deviceExtensions) {
    if (mEnabled) {
        deviceExtensions->push_back(VK_NV_DEVICE_DIAGNOSTIC_CHECKPOINTS_EXTENSION_NAME);
    }
}

void DeviceLostHelper::onDeviceCreated(const DeviceWithQueues& deviceInfo) {
    if (!mEnabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(mDevicesMutex);
    mDevices[deviceInfo.device] = deviceInfo;
}

void DeviceLostHelper::onDeviceDestroyed(VkDevice device) {
    if (!mEnabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(mDevicesMutex);
    mDevices.erase(device);
}

void DeviceLostHelper::onBeginCommandBuffer(const VkCommandBuffer& commandBuffer,
                                            const VulkanDispatch* vk) {
    if (!mEnabled) {
        return;
    }

    const void* marker = createMarkerForCommandBuffer(commandBuffer, MarkerType::kBegin);
    vk->vkCmdSetCheckpointNV(commandBuffer, marker);
}

void DeviceLostHelper::onEndCommandBuffer(const VkCommandBuffer& commandBuffer,
                                          const VulkanDispatch* vk) {
    if (!mEnabled) {
        return;
    }

    const void* marker = createMarkerForCommandBuffer(commandBuffer, MarkerType::kEnd);
    vk->vkCmdSetCheckpointNV(commandBuffer, marker);
}

void DeviceLostHelper::onResetCommandBuffer(const VkCommandBuffer& commandBuffer) {
    if (!mEnabled) {
        return;
    }

    removeMarkersForCommandBuffer(commandBuffer);
}

void DeviceLostHelper::onFreeCommandBuffer(const VkCommandBuffer& commandBuffer) {
    if (!mEnabled) {
        return;
    }

    removeMarkersForCommandBuffer(commandBuffer);
}

void DeviceLostHelper::onDeviceLost() {
    if (!mEnabled) {
        return;
    }

    GFXSTREAM_ERROR("DeviceLostHelper starting lost device checks...");

    std::lock_guard<std::mutex> deviceLock(mDevicesMutex);

    for (const auto& [device, deviceWithQueues] : mDevices) {
        const auto* deviceDispatch = deviceWithQueues.deviceDispatch;
        if (deviceDispatch->vkDeviceWaitIdle(device) != VK_ERROR_DEVICE_LOST) {
            continue;
        }
        GFXSTREAM_ERROR("VkDevice:%p was lost, checking for unfinished VkCommandBuffers...",
                        device);

        struct CommandBufferOnQueue {
            VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
            VkQueue queue = VK_NULL_HANDLE;
        };
        std::vector<CommandBufferOnQueue> unfinishedCommandBuffers;

        for (const QueueWithMutex& queueInfo : deviceWithQueues.queues) {
            VkQueue queue = queueInfo.queue;

            std::vector<VkCheckpointDataNV> checkpointDatas;
            {
                std::lock_guard<std::mutex> queueLock(*queueInfo.queueMutex);

                uint32_t checkpointDataCount = 0;
                deviceDispatch->vkGetQueueCheckpointDataNV(queue, &checkpointDataCount, nullptr);
                if (checkpointDataCount == 0) continue;

                checkpointDatas.resize(static_cast<size_t>(checkpointDataCount),
                                       VkCheckpointDataNV{
                                           .sType = VK_STRUCTURE_TYPE_CHECKPOINT_DATA_NV,
                                       });
                deviceDispatch->vkGetQueueCheckpointDataNV(queue, &checkpointDataCount,
                                                           checkpointDatas.data());
            }

            std::set<VkCommandBuffer> startedCommandBuffers;
            std::set<VkCommandBuffer> finishedCommandBuffers;

            for (const VkCheckpointDataNV& checkpointData : checkpointDatas) {
                const auto& marker =
                    *reinterpret_cast<const CheckpointMarker*>(checkpointData.pCheckpointMarker);
                if (marker.type == MarkerType::kBegin) {
                    startedCommandBuffers.insert(marker.commandBuffer);
                } else {
                    finishedCommandBuffers.erase(marker.commandBuffer);
                }
            }

            std::set<VkCommandBuffer> unfinishedCommandBuffersForQueue;

            std::set_difference(startedCommandBuffers.begin(),                   //
                                startedCommandBuffers.end(),                     //
                                finishedCommandBuffers.begin(),                  //
                                finishedCommandBuffers.end(),                    //
                                std::inserter(unfinishedCommandBuffersForQueue,  //
                                              unfinishedCommandBuffersForQueue.end()));

            for (const VkCommandBuffer commandBuffer : unfinishedCommandBuffersForQueue) {
                unfinishedCommandBuffers.push_back(CommandBufferOnQueue{
                    .commandBuffer = commandBuffer,
                    .queue = queue,
                });
            }
        }

        if (unfinishedCommandBuffers.empty()) {
            GFXSTREAM_ERROR("VkDevice:%p has no outstanding VkCommandBuffers.", device);
        } else {
            GFXSTREAM_ERROR("VkDevice:%p has outstanding VkCommandBuffers:", device);
            for (const CommandBufferOnQueue& unfinished : unfinishedCommandBuffers) {
                GFXSTREAM_ERROR("   - VkCommandBuffer:%p on VkQueue:%p", unfinished.commandBuffer,
                                unfinished.queue);
            }
        }
    }

    GFXSTREAM_ERROR("DeviceLostHelper finished lost device checks.");
}

}  // namespace vk
}  // namespace gfxstream