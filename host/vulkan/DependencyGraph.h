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

#include <atomic>
#include <memory>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace gfxstream {
namespace vk {

class DependencyGraph {
   public:
    using ApiCallId = uint64_t;
    using NodeId = uint64_t;
    struct DepNode {
        static constexpr const uint64_t kInvalidNodeId = 0;
        // id of this depnode, 0 is invalid
        // this id comes from boxed handle manager new_xxx
        // and boxed handle manager use the following format
        // <tag>-<gen>-<index>, this format does not follow
        // the id creation order. we keep it here as is, and
        // introduce timestamp to record the id creation order
        NodeId id{kInvalidNodeId};
        // the api that created this DepNode; 0 is invalid
        ApiCallId apiCallId{0};
        std::set<NodeId> childNodeIds;
        // there could be only one parent, 0 is invalid
        NodeId parentNodeId{0};

        // the increasing timestamp when this node is created
        // used by map to sort nodes
        uint64_t timestamp{0};
    };

    // this is created by VkDecoderSnapshot, to record
    // the api trace, such as "vkCreateInstance..."
    // each of such trace is assigned a unique api call id
    struct ApiNode {
        // id of this api node, 0 is invalid
        ApiCallId id{0};
        std::set<NodeId> createdNodeIds;
    };

    void addNodes(const NodeId* ids, uint32_t count);

    void addNodeIdDependencies(const NodeId* nodeIds, uint32_t count, NodeId parentNodeId);

    uint64_t getNodeIdType(NodeId handle) const;

    void associateWithApiCall(const NodeId* nodeIds, uint32_t count, ApiCallId apiCallId);

    void clear();

    void addDep(NodeId child_id, NodeId parent_id);

    DepNode* getDepNode(NodeId id);

    ApiNode* getApiNode(NodeId id);

    void clearChildDependencies(NodeId id);

    void removeNodesAndDescendants(const NodeId* toRemove, uint32_t count);

    void setCreatedNodeIdsForApi(ApiCallId apiCallId, const NodeId* nodeIds, uint32_t count);
    void addApiNode(ApiCallId id);
    void removeApiNode(ApiCallId id);
    void addDepNode(NodeId id);

    void getIdsByTimestamp(std::vector<ApiCallId>& uniqApiRefsByTimestamp);

   private:
    void removeNodeAndDescendants(NodeId id);
    std::atomic<uint64_t> mTimestamp{DepNode::kInvalidNodeId};
    std::unordered_map<NodeId, std::unique_ptr<DepNode>> mDepId2DepNode;
    std::unordered_map<ApiCallId, std::unique_ptr<ApiNode>> mApiId2ApiNode;
};

}  // namespace vk
}  // namespace gfxstream
