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
#include "DependencyGraph.h"

#include <string.h>

#include "VulkanBoxedHandles.h"

namespace gfxstream {
namespace vk {

#define DEBUG_RECONSTRUCTION 0

#if DEBUG_RECONSTRUCTION

#define DEBUG_RECON(fmt, ...) INFO(fmt, ##__VA_ARGS__);

#else

#define DEBUG_RECON(fmt, ...)

#endif

void DependencyGraph::removeDescendantsOfHandle(const NodeId id) {
    auto* nd = getDepNode(id);
    if (nd) {
        for (auto child : nd->childNodeIds) {
            removeNodeAndDescendants(child);
        }
    }
}

void DependencyGraph::removeNodesAndDescendants(const NodeId* toRemove, uint32_t count) {
    // shader can be removed after pipeline is created, but we need it during
    // load, so do not remove it. This also apply to renderpass
    for (uint32_t i = 0; i < count; ++i) {
        if (getNodeIdType(toRemove[i]) == Tag_VkShaderModule) {
            continue;
        }
        if (getNodeIdType(toRemove[i]) == Tag_VkRenderPass) {
            continue;
        }
        removeNodeAndDescendants(toRemove[i]);
    }
}

void DependencyGraph::removeNodeAndDescendants(NodeId id) {
    removeDescendantsOfHandle(id);
    mDepId2DepNode.erase(id);
}

void DependencyGraph::addNodes(const NodeId* ids, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        addDepNode(ids[i]);
    }
}

void DependencyGraph::addDepNode(NodeId id) {
    if (getDepNode(id)) {
        // this can happen, e.g.
        // vkGetDeviceQueue, can be called
        // multiple times with same queue
        // or enumerate physical device
        // multiple times
        return;
    }
    ++mTimestamp;
    auto nd = std::make_unique<DepNode>();
    nd->id = id;
    nd->timestamp = mTimestamp;
    mDepId2DepNode[id] = std::move(nd);
}

uint64_t DependencyGraph::getNodeIdType(NodeId nodeId) const { return (nodeId >> 48) & 0xFF; }

void DependencyGraph::addNodeIdDependencies(const NodeId* childrenIds, uint32_t childrenCount,
                                            NodeId parentNodeId) {
    for (uint32_t i = 0; i < childrenCount; ++i) {
        addDep(childrenIds[i], parentNodeId);
    }
}

void DependencyGraph::getIdsByTimestamp(std::vector<NodeId>& uniqApiRefsByTopoOrder) {
    // sort depnodes based on the timestamp
    std::map<uint64_t, DepNode*> time2node;
    for (const auto& [nodeId, item] : mDepId2DepNode) {
        time2node[item->timestamp] = item.get();
    }
    for (const auto& [timestamp, item] : time2node) {
        auto apiCallId = item->apiCallId;
        uniqApiRefsByTopoOrder.push_back(apiCallId);
    }
}

DependencyGraph::DepNode* DependencyGraph::getDepNode(NodeId id) {
    if (mDepId2DepNode.find(id) == mDepId2DepNode.end()) return nullptr;
    auto& nd = mDepId2DepNode[id];
    return nd.get();
}

DependencyGraph::ApiNode* DependencyGraph::getApiNode(NodeId id) {
    if (mApiId2ApiNode.find(id) == mApiId2ApiNode.end()) return nullptr;
    auto& nd = mApiId2ApiNode[id];
    return nd.get();
}

void DependencyGraph::setCreatedNodeIdsForApi(ApiCallId apiCallId, const NodeId* nodeIds,
                                              uint32_t count) {
    addApiNode(apiCallId);
    auto* apiNode = getApiNode(apiCallId);
    if (apiNode) {
        for (uint32_t i = 0; i < count; ++i) {
            apiNode->createdNodeIds.insert(nodeIds[i]);
        }
    }
}
void DependencyGraph::addApiNode(ApiCallId id) {
    if (getApiNode(id)) return;
    auto nd = std::make_unique<ApiNode>();
    nd->id = id;
    mApiId2ApiNode[id] = std::move(nd);
}
void DependencyGraph::removeApiNode(ApiCallId id) {
    auto* nd = getApiNode(id);
    if (nd) {
        mApiId2ApiNode.erase(id);
    }
}
void DependencyGraph::clearChildDependencies(NodeId parentId) {
    auto* nd = getDepNode(parentId);
    if (nd) {
        nd->childNodeIds.clear();
    }
}

void DependencyGraph::associateWithApiCall(const NodeId* nodeIds, uint32_t count,
                                           ApiCallId apiCallId) {
    for (uint64_t i = 0; i < count; ++i) {
        auto* nd = getDepNode(nodeIds[i]);
        if (nd) {
            nd->apiCallId = apiCallId;
        }
    }
}

void DependencyGraph::clear() {
    mDepId2DepNode.clear();
    mApiId2ApiNode.clear();
}

void DependencyGraph::addDep(NodeId child_id, NodeId parent_id) {
    if (child_id == parent_id) {
        // don't do this, image depends on image, create on bound state
        // ignore; fixeme
        return;
    }
    auto ptype = getNodeIdType(parent_id);
    switch (ptype) {
        case Tag_VkInstance:
        case Tag_VkPhysicalDevice:
        case Tag_VkDevice:
        case Tag_VkDeviceMemory:
        case Tag_VkFramebuffer:
        case Tag_VkImageView:
        case Tag_VkImage:
        case Tag_VkBuffer:
        case Tag_VkBufferView:
        case Tag_VkSampler:
        case Tag_VkDescriptorSet:
        case Tag_VkDescriptorPool:
        case Tag_VkCommandPool:
        case Tag_VkCommandBuffer:
            break;
        default:
            return;
    }

    auto* child = getDepNode(child_id);
    auto* parent = getDepNode(parent_id);
    if (!child || !parent) return;

    auto ctype = getNodeIdType(child_id);

    if (ptype == Tag_VkDeviceMemory) {
        if (ctype == Tag_VkBindMemory) {
        } else if (ctype == Tag_VkMapMemory) {
        } else {
            return;
        }
    }

    child->parentNodeId = parent_id;
    parent->childNodeIds.insert(child_id);
}

}  // namespace vk
}  // namespace gfxstream
