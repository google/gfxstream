// Copyright (C) 2026 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string_view>
#include <vector>

#include "vulkan_boxed_handles.h"

namespace gfxstream {
namespace host {
namespace vk {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::UnorderedElementsAre;

using ApiCallId = DependencyGraph::ApiCallId;
using NodeId = DependencyGraph::NodeId;

struct ApiHelper {
    ApiCallId next_id{1};
    ApiCallId Next(std::string_view /*unused*/) { return next_id++; }
};

// Builds a node id with the boxed handle layout (<tag>-<gen>-<index>), so
// DependencyGraph::getNodeIdType() sees `tag`.
NodeId MakeNodeId(BoxedHandleTypeTag tag, uint64_t index) {
    return BoxedHandleManager::Store::EM::makeHandle(index, /*generation=*/1, tag);
}

std::vector<NodeId> Parents(const DependencyGraph::DepNode& node) {
    return std::vector<NodeId>(node.parentNodeIds.begin(), node.parentNodeIds.end());
}

std::vector<NodeId> Created(const DependencyGraph::ApiNode& api) {
    return std::vector<NodeId>(api.createdNodeIds.begin(), api.createdNodeIds.end());
}

// Adds `node` as a child of `parent` created by API call `api`, mirroring what
// VkDecoderSnapshot does for a vkCreate*/vkAllocate* call.
void AddCreated(DependencyGraph& graph, NodeId node, NodeId parent, ApiCallId api,
                std::vector<ApiCallId>* unused = nullptr) {
    graph.addNodes(&node, 1);
    if (parent != 0) {
        graph.addDep(node, parent);
    }
    graph.associateWithApiCall(node, api, unused);
}

TEST(DependencyGraphTest, NodeAndApiCountersTrackLiveEntries) {
    DependencyGraph graph;
    ApiHelper api;
    EXPECT_EQ(0u, graph.getDepNodeCount());
    EXPECT_EQ(0u, graph.getApiNodeCount());

    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    AddCreated(graph, device, 0, api.Next("vkCreateDevice()"));
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());

    // Adding the same node twice (e.g. vkGetDeviceQueue called twice) does
    // not create a second entry.
    graph.addNodes(&device, 1);
    EXPECT_EQ(1u, graph.getDepNodeCount());

    graph.clear();
    EXPECT_EQ(0u, graph.getDepNodeCount());
    EXPECT_EQ(0u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, AssociatingUnknownOrAlreadyLinkedNodesDoesNotCreateApiNodes) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId vkimage1 = MakeNodeId(Tag_VkImage, 1);
    std::vector<ApiCallId> unused;

    // A call on a handle the graph does not know about, a modify-only call
    // creating no handles, or an invalid ApiCallId (0) must not allocate an
    // ApiNode that nothing would ever free.
    graph.associateWithApiCall(vkimage1, api.Next("vkCreateImage()"), &unused);
    graph.associateWithApiCall(nullptr, 0, api.Next("vkBeginCommandBuffer()"), &unused);
    graph.associateWithApiCall(vkimage1, /*apiCallId=*/0, &unused);
    EXPECT_EQ(0u, graph.getApiNodeCount());
    EXPECT_THAT(unused, IsEmpty());

    // setCreatedNodeIdsForApi after forEachHandleAddApi for the same call is a
    // no-op: no re-link, no "unused" report.
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    const ApiCallId createDeviceApi = api.Next("vkCreateDevice()");
    AddCreated(graph, device, 0, createDeviceApi, &unused);
    graph.setCreatedNodeIdsForApi(createDeviceApi, &device, 1, &unused);
    EXPECT_EQ(1u, graph.getApiNodeCount());
    EXPECT_THAT(unused, IsEmpty());
    ASSERT_NE(nullptr, graph.getApiNode(createDeviceApi));
    EXPECT_THAT(Created(*graph.getApiNode(createDeviceApi)), ElementsAre(device));
}

TEST(DependencyGraphTest, SupersedingApiCallReclaimsTheOlderCall) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId queue = MakeNodeId(Tag_VkQueue, 1);
    std::vector<ApiCallId> unused;

    // vkGetDeviceQueue returns the same handle every time; only the latest
    // call is needed to recreate it, so the previous call becomes unused.
    const ApiCallId getQueue1 = api.Next("vkGetDeviceQueue()");
    const ApiCallId getQueue2 = api.Next("vkGetDeviceQueue()");
    const ApiCallId getQueue3 = api.Next("vkGetDeviceQueue()");
    AddCreated(graph, queue, 0, getQueue1, &unused);
    EXPECT_THAT(unused, IsEmpty());
    AddCreated(graph, queue, 0, getQueue2, &unused);
    EXPECT_THAT(unused, ElementsAre(getQueue1));
    AddCreated(graph, queue, 0, getQueue3, &unused);
    EXPECT_THAT(unused, ElementsAre(getQueue1, getQueue2));

    EXPECT_EQ(nullptr, graph.getApiNode(getQueue1));
    EXPECT_EQ(nullptr, graph.getApiNode(getQueue2));
    ASSERT_NE(nullptr, graph.getApiNode(getQueue3));
    EXPECT_THAT(Created(*graph.getApiNode(getQueue3)), ElementsAre(queue));
    EXPECT_EQ(getQueue3, graph.getDepNode(queue)->apiCallId);
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, SupersedingApiCallKeepsBatchCallAliveForRemainingNodes) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId cmdBufs[2] = {MakeNodeId(Tag_VkCommandBuffer, 1),
                               MakeNodeId(Tag_VkCommandBuffer, 2)};
    std::vector<ApiCallId> unused;

    const ApiCallId allocBatch = api.Next("vkAllocateCommandBuffers()");
    graph.addNodes(cmdBufs, 2);
    graph.associateWithApiCall(cmdBufs, 2, allocBatch, &unused);

    // Re-creating only one of the two handles leaves the batch call backing
    // the other one.
    const ApiCallId realloc0 = api.Next("vkAllocateCommandBuffers()");
    graph.associateWithApiCall(cmdBufs[0], realloc0, &unused);
    EXPECT_THAT(unused, IsEmpty());
    ASSERT_NE(nullptr, graph.getApiNode(allocBatch));
    EXPECT_THAT(Created(*graph.getApiNode(allocBatch)), ElementsAre(cmdBufs[1]));
    EXPECT_THAT(Created(*graph.getApiNode(realloc0)), ElementsAre(cmdBufs[0]));

    const ApiCallId realloc1 = api.Next("vkAllocateCommandBuffers()");
    graph.associateWithApiCall(cmdBufs[1], realloc1, &unused);
    EXPECT_THAT(unused, ElementsAre(allocBatch));
    EXPECT_EQ(2u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, ReassociatingNodeKeepsItsCreationOrderForReplay) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId physDev = MakeNodeId(Tag_VkPhysicalDevice, 1);
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);

    // vkEnumeratePhysicalDevices, vkCreateDevice, then the guest enumerates
    // again and gets the same physical device.
    AddCreated(graph, physDev, 0, api.Next("vkEnumeratePhysicalDevices()"));
    const ApiCallId createDevice = api.Next("vkCreateDevice()");
    AddCreated(graph, device, physDev, createDevice);
    const ApiCallId reenumeratePhysDev = api.Next("vkEnumeratePhysicalDevices()");
    AddCreated(graph, physDev, 0, reenumeratePhysDev);

    // The node deliberately keeps its original timestamp: the superseding
    // call recreates the same handle, so on replay it must still run before
    // the calls that created the node's children.
    std::vector<ApiCallId> replayOrder;
    graph.getIdsByTimestamp(replayOrder);
    EXPECT_THAT(replayOrder, ElementsAre(reenumeratePhysDev, createDevice));
}

TEST(DependencyGraphTest, RemovingLastCreatedNodeDropsApiNode) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId cmdPool = MakeNodeId(Tag_VkCommandPool, 1);
    const NodeId cmdBufs[2] = {MakeNodeId(Tag_VkCommandBuffer, 1),
                               MakeNodeId(Tag_VkCommandBuffer, 2)};
    std::vector<ApiCallId> unused;

    AddCreated(graph, cmdPool, 0, api.Next("vkCreateCommandPool()"));
    const ApiCallId allocCmdBufs = api.Next("vkAllocateCommandBuffers()");
    graph.addNodes(cmdBufs, 2);
    graph.addNodeIdDependencies(cmdBufs, 2, cmdPool);
    graph.associateWithApiCall(cmdBufs, 2, allocCmdBufs, &unused);

    // vkFreeCommandBuffers on one of a batch keeps the vkAllocateCommandBuffers
    // call alive for the other.
    graph.removeNodesAndDescendants(&cmdBufs[0], 1, &unused);
    EXPECT_THAT(unused, IsEmpty());
    EXPECT_EQ(2u, graph.getDepNodeCount());
    ASSERT_NE(nullptr, graph.getApiNode(allocCmdBufs));
    EXPECT_THAT(Created(*graph.getApiNode(allocCmdBufs)), ElementsAre(cmdBufs[1]));
    EXPECT_THAT(graph.getDepNode(cmdPool)->childNodeIds, ElementsAre(cmdBufs[1]));

    graph.removeNodesAndDescendants(&cmdBufs[1], 1, &unused);
    EXPECT_THAT(unused, ElementsAre(allocCmdBufs));
    EXPECT_EQ(nullptr, graph.getApiNode(allocCmdBufs));
    EXPECT_THAT(graph.getDepNode(cmdPool)->childNodeIds, IsEmpty());
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, MultiParentNodeUnlinksFromEveryParentOnRemoval) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    const NodeId image = MakeNodeId(Tag_VkImage, 1);
    const NodeId memory = MakeNodeId(Tag_VkDeviceMemory, 1);
    const NodeId bind = MakeNodeId(Tag_VkBindMemory, 1);
    std::vector<ApiCallId> unused;

    AddCreated(graph, device, 0, api.Next("vkCreateDevice()"));
    const ApiCallId createImage = api.Next("vkCreateImage()");
    AddCreated(graph, image, device, createImage);
    const ApiCallId allocMemory = api.Next("vkAllocateMemory()");
    AddCreated(graph, memory, device, allocMemory);
    // vkBindImageMemory depends on both the image and the memory.
    const ApiCallId bindImageMemory = api.Next("vkBindImageMemory()");
    AddCreated(graph, bind, memory, bindImageMemory);
    graph.addDep(bind, image);

    DependencyGraph::DepNode* bindNode = graph.getDepNode(bind);
    ASSERT_NE(nullptr, bindNode);
    EXPECT_THAT(Parents(*bindNode), UnorderedElementsAre(memory, image));
    EXPECT_THAT(graph.getDepNode(image)->childNodeIds, ElementsAre(bind));
    EXPECT_THAT(graph.getDepNode(memory)->childNodeIds, ElementsAre(bind));

    // Destroying the image removes the bind, which must disappear from the
    // memory's children too instead of lingering as a dead child id.
    graph.removeNodesAndDescendants(&image, 1, &unused);
    EXPECT_THAT(unused, UnorderedElementsAre(createImage, bindImageMemory));
    EXPECT_EQ(nullptr, graph.getDepNode(bind));
    EXPECT_THAT(graph.getDepNode(memory)->childNodeIds, IsEmpty());
    EXPECT_THAT(graph.getDepNode(device)->childNodeIds, ElementsAre(memory));

    // The memory can then be freed without touching the already removed bind.
    graph.removeNodesAndDescendants(&memory, 1, &unused);
    EXPECT_THAT(unused, UnorderedElementsAre(createImage, bindImageMemory, allocMemory));
    EXPECT_THAT(graph.getDepNode(device)->childNodeIds, IsEmpty());
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, RemovingAncestorOfDiamondDependencyRemovesEachNodeOnce) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    const NodeId image = MakeNodeId(Tag_VkImage, 1);
    const NodeId memory = MakeNodeId(Tag_VkDeviceMemory, 1);
    const NodeId bind = MakeNodeId(Tag_VkBindMemory, 1);
    std::vector<ApiCallId> unused;

    const ApiCallId createDevice = api.Next("vkCreateDevice()");
    const ApiCallId createImage = api.Next("vkCreateImage()");
    const ApiCallId allocMemory = api.Next("vkAllocateMemory()");
    const ApiCallId bindImageMemory = api.Next("vkBindImageMemory()");
    AddCreated(graph, device, 0, createDevice);
    AddCreated(graph, image, device, createImage);
    AddCreated(graph, memory, device, allocMemory);
    AddCreated(graph, bind, memory, bindImageMemory);
    graph.addDep(bind, image);

    // Removing the device walks image -> bind and memory -> (bind already
    // gone). The child sets are copied before iterating, so removing the bind
    // while visiting the image's children must not disturb the walk, and each
    // API call is reported exactly once.
    graph.removeNodesAndDescendants(&device, 1, &unused);
    EXPECT_THAT(unused,
                UnorderedElementsAre(createDevice, createImage, allocMemory, bindImageMemory));
    EXPECT_EQ(0u, graph.getDepNodeCount());
    EXPECT_EQ(0u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, RemoveDescendantsOfHandleKeepsTheHandle) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId cmdPool = MakeNodeId(Tag_VkCommandPool, 1);
    const NodeId cmdBuf = MakeNodeId(Tag_VkCommandBuffer, 1);
    std::vector<ApiCallId> unused;

    const ApiCallId createCmdPool = api.Next("vkCreateCommandPool()");
    const ApiCallId allocCmdBuf = api.Next("vkAllocateCommandBuffers()");
    AddCreated(graph, cmdPool, 0, createCmdPool);
    AddCreated(graph, cmdBuf, cmdPool, allocCmdBuf);

    // vkResetCommandPool drops everything under the pool but not the pool.
    graph.removeDescendantsOfHandle(cmdPool, &unused);
    EXPECT_THAT(unused, ElementsAre(allocCmdBuf));
    EXPECT_EQ(nullptr, graph.getDepNode(cmdBuf));
    ASSERT_NE(nullptr, graph.getDepNode(cmdPool));
    EXPECT_THAT(graph.getDepNode(cmdPool)->childNodeIds, IsEmpty());
    EXPECT_EQ(createCmdPool, graph.getDepNode(cmdPool)->apiCallId);

    // Resetting a pool / command buffer with no children is a no-op.
    graph.removeDescendantsOfHandle(cmdPool, &unused);
    graph.removeDescendantsOfHandle(MakeNodeId(Tag_VkCommandPool, 99), &unused);
    EXPECT_THAT(unused, ElementsAre(allocCmdBuf));
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, RemoveGrandChildrenKeepsDirectChildren) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId descriptorPool = MakeNodeId(Tag_VkDescriptorPool, 1);
    const NodeId descriptorSet = MakeNodeId(Tag_VkDescriptorSet, 1);
    const NodeId write = MakeNodeId(Tag_VkImageView, 1);
    std::vector<ApiCallId> unused;

    AddCreated(graph, descriptorPool, 0, api.Next("vkCreateDescriptorPool()"));
    AddCreated(graph, descriptorSet, descriptorPool, api.Next("vkAllocateDescriptorSets()"));
    const ApiCallId updateDescriptorSets = api.Next("vkUpdateDescriptorSets()");
    AddCreated(graph, write, descriptorSet, updateDescriptorSets);

    // vkResetDescriptorPool keeps the pre-allocated sets but drops what was
    // written into them.
    graph.removeGrandChildren(descriptorPool, &unused);
    EXPECT_THAT(unused, ElementsAre(updateDescriptorSets));
    EXPECT_EQ(nullptr, graph.getDepNode(write));
    ASSERT_NE(nullptr, graph.getDepNode(descriptorSet));
    EXPECT_THAT(graph.getDepNode(descriptorSet)->childNodeIds, IsEmpty());
    EXPECT_THAT(graph.getDepNode(descriptorPool)->childNodeIds, ElementsAre(descriptorSet));

    // No children: nothing to do.
    graph.removeGrandChildren(descriptorSet, &unused);
    graph.removeGrandChildren(MakeNodeId(Tag_VkDescriptorPool, 99), &unused);
    EXPECT_THAT(unused, ElementsAre(updateDescriptorSets));
    EXPECT_EQ(2u, graph.getDepNodeCount());
}

TEST(DependencyGraphTest, ClearChildDependenciesUnlinksChildrenFromThatParentOnly) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId image = MakeNodeId(Tag_VkImage, 1);
    const NodeId memory = MakeNodeId(Tag_VkDeviceMemory, 1);
    const NodeId bind = MakeNodeId(Tag_VkBindMemory, 1);

    AddCreated(graph, image, 0, api.Next("vkCreateImage()"));
    AddCreated(graph, memory, 0, api.Next("vkAllocateMemory()"));
    AddCreated(graph, bind, image, api.Next("vkBindImageMemory()"));
    graph.addDep(bind, memory);

    graph.clearChildDependencies(memory);
    EXPECT_THAT(graph.getDepNode(memory)->childNodeIds, IsEmpty());
    EXPECT_THAT(Parents(*graph.getDepNode(bind)), ElementsAre(image));
    EXPECT_THAT(graph.getDepNode(image)->childNodeIds, ElementsAre(bind));

    graph.clearChildDependencies(image);
    EXPECT_THAT(Parents(*graph.getDepNode(bind)), IsEmpty());

    // Clearing a node without children, or an unknown node, is a no-op.
    graph.clearChildDependencies(bind);
    graph.clearChildDependencies(MakeNodeId(Tag_VkImage, 99));
    EXPECT_EQ(3u, graph.getDepNodeCount());
}

TEST(DependencyGraphTest, ParentAndCreatedListsGrowBeyondInlineCapacity) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    std::vector<ApiCallId> unused;
    AddCreated(graph, device, 0, api.Next("vkCreateDevice()"));

    // One vkAllocateCommandBuffers call creating 16 handles: more than the
    // inline storage of ApiNode::createdNodeIds.
    std::vector<NodeId> cmdBufs;
    for (uint64_t i = 1; i <= 16; ++i) {
        cmdBufs.push_back(MakeNodeId(Tag_VkCommandBuffer, i));
    }
    const ApiCallId allocCmdBufs = api.Next("vkAllocateCommandBuffers()");
    graph.addNodes(cmdBufs.data(), cmdBufs.size());
    graph.addNodeIdDependencies(cmdBufs.data(), cmdBufs.size(), device);
    graph.associateWithApiCall(cmdBufs.data(), cmdBufs.size(), allocCmdBufs, &unused);
    ASSERT_NE(nullptr, graph.getApiNode(allocCmdBufs));
    EXPECT_THAT(Created(*graph.getApiNode(allocCmdBufs)),
                UnorderedElementsAre(cmdBufs[0], cmdBufs[1], cmdBufs[2], cmdBufs[3], cmdBufs[4],
                                     cmdBufs[5], cmdBufs[6], cmdBufs[7], cmdBufs[8], cmdBufs[9],
                                     cmdBufs[10], cmdBufs[11], cmdBufs[12], cmdBufs[13],
                                     cmdBufs[14], cmdBufs[15]));

    // One descriptor set update depending on 8 image views: more than the
    // inline storage of DepNode::parentNodeIds.
    std::vector<NodeId> imageViews;
    for (uint64_t i = 1; i <= 8; ++i) {
        imageViews.push_back(MakeNodeId(Tag_VkImageView, i));
        AddCreated(graph, imageViews.back(), device, api.Next("vkCreateImageView()"));
    }
    const NodeId descriptorSet = MakeNodeId(Tag_VkDescriptorSet, 1);
    const ApiCallId allocDescriptorSet = api.Next("vkAllocateDescriptorSets()");
    AddCreated(graph, descriptorSet, device, allocDescriptorSet);
    for (NodeId imageView : imageViews) {
        graph.addDep(descriptorSet, imageView);
    }
    // Adding a dependency twice does not duplicate the parent.
    graph.addDep(descriptorSet, imageViews[0]);
    EXPECT_EQ(9u, Parents(*graph.getDepNode(descriptorSet)).size());

    // Removing the set unlinks it from all 9 parents...
    graph.removeNodesAndDescendants(&descriptorSet, 1, &unused);
    EXPECT_THAT(unused, ElementsAre(allocDescriptorSet));
    for (NodeId imageView : imageViews) {
        EXPECT_THAT(graph.getDepNode(imageView)->childNodeIds, IsEmpty());
    }
    // ...and freeing the command buffers one by one shrinks the batch call
    // until the last one frees it.
    for (size_t i = 0; i < cmdBufs.size(); ++i) {
        graph.removeNodesAndDescendants(&cmdBufs[i], 1, &unused);
        EXPECT_EQ(i + 1 == cmdBufs.size(), graph.getApiNode(allocCmdBufs) == nullptr);
    }
    EXPECT_THAT(unused, ElementsAre(allocDescriptorSet, allocCmdBufs));
    EXPECT_EQ(9u, graph.getDepNodeCount());
}

TEST(DependencyGraphTest, DelayedDeleteTypesAreOnlyRemovedWithTheirAncestor) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    const NodeId shader = MakeNodeId(Tag_VkShaderModule, 1);
    const NodeId renderPass = MakeNodeId(Tag_VkRenderPass, 1);
    const NodeId buffer = MakeNodeId(Tag_VkBuffer, 1);
    std::vector<ApiCallId> unused;

    const ApiCallId createDevice = api.Next("vkCreateDevice()");
    const ApiCallId createShader = api.Next("vkCreateShaderModule()");
    const ApiCallId createRenderPass = api.Next("vkCreateRenderPass()");
    const ApiCallId createBuffer = api.Next("vkCreateBuffer()");
    AddCreated(graph, device, 0, createDevice);
    AddCreated(graph, shader, device, createShader);
    AddCreated(graph, renderPass, device, createRenderPass);
    AddCreated(graph, buffer, device, createBuffer);
    EXPECT_TRUE(graph.isDeleteDelayedType(shader));
    EXPECT_TRUE(graph.isDeleteDelayedType(renderPass));
    EXPECT_FALSE(graph.isDeleteDelayedType(buffer));

    // vkDestroyShaderModule / vkDestroyRenderPass keep their traces (pipelines
    // may still reference them), vkDestroyBuffer does not.
    const NodeId destroyed[3] = {shader, renderPass, buffer};
    graph.removeNodesAndDescendants(destroyed, 3, &unused);
    EXPECT_THAT(unused, ElementsAre(createBuffer));
    EXPECT_NE(nullptr, graph.getDepNode(shader));
    EXPECT_NE(nullptr, graph.getDepNode(renderPass));
    EXPECT_EQ(nullptr, graph.getDepNode(buffer));

    // They go away with the device.
    graph.removeNodesAndDescendants(&device, 1, &unused);
    EXPECT_THAT(unused,
                UnorderedElementsAre(createBuffer, createDevice, createShader, createRenderPass));
    EXPECT_EQ(0u, graph.getDepNodeCount());
    EXPECT_EQ(0u, graph.getApiNodeCount());
}

TEST(DependencyGraphTest, AddDepIgnoresSelfUnknownAndUnsupportedParents) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    const NodeId queue = MakeNodeId(Tag_VkQueue, 1);
    const NodeId memory = MakeNodeId(Tag_VkDeviceMemory, 1);
    const NodeId buffer = MakeNodeId(Tag_VkBuffer, 1);
    const NodeId fence = MakeNodeId(Tag_VkFence, 1);

    AddCreated(graph, device, 0, api.Next("vkCreateDevice()"));
    AddCreated(graph, queue, device, api.Next("vkGetDeviceQueue()"));
    AddCreated(graph, memory, device, api.Next("vkAllocateMemory()"));
    AddCreated(graph, buffer, device, api.Next("vkCreateBuffer()"));
    graph.addNodes(&fence, 1);

    graph.addDep(device, device);                       // self
    graph.addDep(fence, queue);                         // queue is not a parent type
    graph.addDep(buffer, memory);                       // only bind/map may depend on memory
    graph.addDep(fence, MakeNodeId(Tag_VkDevice, 99));  // unknown parent
    graph.addDep(MakeNodeId(Tag_VkFence, 99), device);  // unknown child

    EXPECT_THAT(graph.getDepNode(device)->childNodeIds,
                UnorderedElementsAre(queue, memory, buffer));
    EXPECT_THAT(graph.getDepNode(queue)->childNodeIds, IsEmpty());
    EXPECT_THAT(graph.getDepNode(memory)->childNodeIds, IsEmpty());
    EXPECT_THAT(Parents(*graph.getDepNode(fence)), IsEmpty());
    EXPECT_THAT(Parents(*graph.getDepNode(buffer)), ElementsAre(device));
}

TEST(DependencyGraphTest, DestroyingShortLivedChildrenUnlinksLongLivedDeviceParent) {
    DependencyGraph graph;
    ApiHelper api;
    const NodeId device = MakeNodeId(Tag_VkDevice, 1);
    AddCreated(graph, device, 0, api.Next("vkCreateDevice()"));

    for (uint64_t i = 0; i < 200; ++i) {
        const NodeId image = MakeNodeId(Tag_VkImage, 1000 + i);
        const NodeId memory = MakeNodeId(Tag_VkDeviceMemory, 2000 + i);
        const NodeId bind = MakeNodeId(Tag_VkBindMemory, 3000 + i);

        const ApiCallId createImage = api.Next("vkCreateImage()");
        const ApiCallId allocMemory = api.Next("vkAllocateMemory()");
        const ApiCallId bindImageMemory = api.Next("vkBindImageMemory()");
        AddCreated(graph, image, device, createImage);
        AddCreated(graph, memory, device, allocMemory);
        AddCreated(graph, bind, memory, bindImageMemory);
        graph.addDep(bind, image);

        std::vector<ApiCallId> unused;
        graph.removeNodesAndDescendants(&image, 1, &unused);
        graph.removeNodesAndDescendants(&memory, 1, &unused);
        EXPECT_THAT(unused, UnorderedElementsAre(createImage, allocMemory, bindImageMemory));
    }

    // The device does not accumulate dead child ids over the app's lifetime.
    DependencyGraph::DepNode* deviceNode = graph.getDepNode(device);
    ASSERT_NE(nullptr, deviceNode);
    EXPECT_THAT(deviceNode->childNodeIds, IsEmpty());
    EXPECT_EQ(1u, graph.getDepNodeCount());
    EXPECT_EQ(1u, graph.getApiNodeCount());
}

}  // namespace
}  // namespace vk
}  // namespace host
}  // namespace gfxstream
