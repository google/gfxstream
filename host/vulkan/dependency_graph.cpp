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
#include "dependency_graph.h"

#include <string.h>

#include <algorithm>

#include "vulkan_boxed_handles.h"

namespace gfxstream {
namespace host {
namespace vk {

#define DEBUG_RECONSTRUCTION 0

#if DEBUG_RECONSTRUCTION

#define DEBUG_RECON(fmt, ...) INFO(fmt, ##__VA_ARGS__);

#else

#define DEBUG_RECON(fmt, ...)

#endif

void DependencyGraph::removeGrandChildren(const NodeId id,
                                          std::vector<ApiCallId>* outUnusedApiIds) {
    auto* nd = getDepNode(id);
    if (!nd || nd->childNodeIds.empty()) return;
    const std::vector<NodeId> children(nd->childNodeIds.begin(), nd->childNodeIds.end());
    for (NodeId child : children) {
        removeDescendantsOfHandle(child, outUnusedApiIds);
    }
}

void DependencyGraph::removeDescendantsOfHandle(const NodeId id,
                                                std::vector<ApiCallId>* outUnusedApiIds) {
    auto* nd = getDepNode(id);
    if (!nd || nd->childNodeIds.empty()) return;
    const std::vector<NodeId> children(nd->childNodeIds.begin(), nd->childNodeIds.end());
    for (NodeId child : children) {
        removeNodeAndDescendants(child, outUnusedApiIds);
    }
    if (auto* refreshed = getDepNode(id)) {
        refreshed->childNodeIds.clear();
    }
}

bool DependencyGraph::isDeleteDelayedType(NodeId id) const {
    switch (getNodeIdType(id)) {
        case Tag_VkDescriptorSetLayout:
        case Tag_VkPipelineCache:
        case Tag_VkRenderPass:
        case Tag_VkSampler:
        case Tag_VkSamplerYcbcrConversion:
        case Tag_VkSamplerYcbcrConversionKHR:
        case Tag_VkShaderModule:
            return true;
        default:
            return false;
    }
}

// TODO: b/559186898, This potentially keeps alive many vk objects. Replace
// with proper parent-child object dependencies.
void DependencyGraph::removeNodesAndDescendants(const NodeId* toRemove, uint32_t count,
                                                std::vector<ApiCallId>* outUnusedApiIds) {
    for (uint32_t i = 0; i < count; ++i) {
        if (isDeleteDelayedType(toRemove[i])) {
            continue;
        }
        removeNodeAndDescendants(toRemove[i], outUnusedApiIds);
    }
}

void DependencyGraph::unlinkNodeFromApiCall(NodeId nodeId, ApiCallId apiCallId,
                                            std::vector<ApiCallId>* outUnusedApiIds) {
    if (apiCallId == 0) {
        return;
    }
    auto apiIt = mApiId2ApiNode.find(apiCallId);
    if (apiIt == mApiId2ApiNode.end()) {
        return;
    }
    auto& created = apiIt->second->createdNodeIds;
    auto it = std::find(created.begin(), created.end(), nodeId);
    if (it != created.end()) {
        *it = created.back();
        created.pop_back();
    }
    if (created.empty()) {
        mApiId2ApiNode.erase(apiIt);
        if (outUnusedApiIds) {
            outUnusedApiIds->push_back(apiCallId);
        }
    }
}

void DependencyGraph::removeNodeAndDescendants(NodeId id,
                                               std::vector<ApiCallId>* outUnusedApiIds) {
    auto it = mDepId2DepNode.find(id);
    if (it == mDepId2DepNode.end()) {
        return;
    }
    if (!it->second->childNodeIds.empty()) {
        removeDescendantsOfHandle(id, outUnusedApiIds);
        it = mDepId2DepNode.find(id);
        if (it == mDepId2DepNode.end()) {
            return;
        }
    }

    for (NodeId parentId : it->second->parentNodeIds) {
        if (auto* parent = getDepNode(parentId)) {
            parent->childNodeIds.erase(id);
        }
    }

    const ApiCallId apiCallId = it->second->apiCallId;
    mDepId2DepNode.erase(it);
    unlinkNodeFromApiCall(id, apiCallId, outUnusedApiIds);
}

void DependencyGraph::addNodes(const NodeId* ids, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        addDepNode(ids[i]);
    }
}

void DependencyGraph::addDepNode(NodeId id) {
    auto [it, inserted] = mDepId2DepNode.try_emplace(id);
    if (!inserted) {
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
    it->second = std::move(nd);
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
    std::unordered_set<uint64_t> apiset;
    for (const auto& [timestamp, item] : time2node) {
        auto apiCallId = item->apiCallId;
        if (auto [_, inserted] = apiset.insert(apiCallId); inserted) {
            uniqApiRefsByTopoOrder.push_back(apiCallId);
        }
    }
}

DependencyGraph::DepNode* DependencyGraph::getDepNode(NodeId id) {
    auto it = mDepId2DepNode.find(id);
    return it == mDepId2DepNode.end() ? nullptr : it->second.get();
}

DependencyGraph::ApiNode* DependencyGraph::getApiNode(NodeId id) {
    auto it = mApiId2ApiNode.find(id);
    return it == mApiId2ApiNode.end() ? nullptr : it->second.get();
}

void DependencyGraph::setCreatedNodeIdsForApi(ApiCallId apiCallId, const NodeId* nodeIds,
                                              uint32_t count,
                                              std::vector<ApiCallId>* outUnusedApiIds) {
    associateWithApiCall(nodeIds, count, apiCallId, outUnusedApiIds);
}
DependencyGraph::ApiNode* DependencyGraph::addApiNode(ApiCallId id) {
    auto [it, inserted] = mApiId2ApiNode.try_emplace(id);
    if (inserted) {
        auto nd = std::make_unique<ApiNode>();
        nd->id = id;
        it->second = std::move(nd);
    }
    return it->second.get();
}
void DependencyGraph::removeApiNode(ApiCallId id) {
    mApiId2ApiNode.erase(id);
}
void DependencyGraph::clearChildDependencies(NodeId parentId) {
    auto* nd = getDepNode(parentId);
    if (nd && !nd->childNodeIds.empty()) {
        for (NodeId childId : nd->childNodeIds) {
            if (auto* child = getDepNode(childId)) {
                auto& parents = child->parentNodeIds;
                auto it = std::find(parents.begin(), parents.end(), parentId);
                if (it != parents.end()) {
                    *it = parents.back();
                    parents.pop_back();
                }
            }
        }
        nd->childNodeIds.clear();
    }
}

void DependencyGraph::associateWithApiCall(const NodeId* nodeIds, uint32_t count,
                                           ApiCallId apiCallId,
                                           std::vector<ApiCallId>* outUnusedApiIds) {
    if (apiCallId == 0 || !nodeIds || count == 0) {
        return;
    }
    ApiNode* apiNode = nullptr;
    for (uint64_t i = 0; i < count; ++i) {
        auto* nd = getDepNode(nodeIds[i]);
        if (!nd || nd->apiCallId == apiCallId) {
            // Unknown node, or already linked to this call (setCreatedNodeIdsForApi
            // usually follows forEachHandleAddApi for the same call).
            continue;
        }
        if (!apiNode) {
            apiNode = addApiNode(apiCallId);
        }
        if (nd->apiCallId != 0) {
            unlinkNodeFromApiCall(nodeIds[i], nd->apiCallId, outUnusedApiIds);
        }
        // Only the creating call changes; the node keeps its timestamp. The new
        // call recreates the same handle (e.g. a repeated vkGetDeviceQueue or
        // vkEnumeratePhysicalDevices), so on replay it must still run at the
        // node's original position, ahead of the calls that created the node's
        // children in the meantime.
        nd->apiCallId = apiCallId;
        // The node was not linked to this call (checked above), so it cannot be
        // in createdNodeIds yet.
        apiNode->createdNodeIds.push_back(nodeIds[i]);
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
        case Tag_VkPipeline:
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

    auto& parents = child->parentNodeIds;
    if (std::find(parents.begin(), parents.end(), parent_id) == parents.end()) {
        parents.push_back(parent_id);
    }
    parent->childNodeIds.insert(child_id);
}

}  // namespace vk
}  // namespace host
}  // namespace gfxstream
