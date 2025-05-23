// Copyright 2021 The Android Open Source Project
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
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VirtioGpuTimelines.h"

#include <memory>

namespace gfxstream {
namespace {

using testing::ElementsAreArray;
using testing::Eq;
using testing::ElementsAreArray;
using testing::IsEmpty;
using testing::Pair;

using Ring = VirtioGpuTimelines::Ring;
using FenceId = VirtioGpuTimelines::FenceId;
using RingGlobal = VirtioGpuRingGlobal;
using RingContextSpecific = VirtioGpuRingContextSpecific;

const auto kGlobalRing = Ring{VirtioGpuRingGlobal{}};
const auto kContext2Ring = Ring{RingContextSpecific{
    .mCtxId = 2,
    .mRingIdx = 0,
}};
const auto kContext3Ring = Ring{RingContextSpecific{
    .mCtxId = 3,
    .mRingIdx = 0,
}};

TEST(VirtioGpuTimelinesTest, Init) {
    auto noopCallback = [](const Ring&, FenceId) {};
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines;
    virtioGpuTimelines = VirtioGpuTimelines::create(noopCallback);
    virtioGpuTimelines = VirtioGpuTimelines::create(noopCallback);
}

TEST(VirtioGpuTimelinesTest, TasksShouldHaveDifferentIds) {
    auto noopCallback = [](const Ring&, FenceId) {};
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(noopCallback);
    auto taskId1 = virtioGpuTimelines->enqueueTask(kGlobalRing);
    auto taskId2 = virtioGpuTimelines->enqueueTask(kGlobalRing);
    ASSERT_NE(taskId1, taskId2);
}

TEST(VirtioGpuTimelinesTest, MultipleTasksAndFencesWithAsyncCallback) {
    std::vector<std::pair<Ring, FenceId>> signaledFences;

    auto fenceCallback =
        [&](const Ring& ring, FenceId fenceId) {
            signaledFences.push_back(std::make_pair(ring, fenceId));
        };
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(fenceCallback);

    FenceId fenceId = 0;

    auto task1Id = virtioGpuTimelines->enqueueTask(kGlobalRing);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto fence1Id = fenceId++;

    virtioGpuTimelines->enqueueFence(kGlobalRing, fence1Id);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto task2Id = virtioGpuTimelines->enqueueTask(kGlobalRing);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto fence2Id = fenceId++;

    virtioGpuTimelines->enqueueFence(kGlobalRing, fence2Id);
    EXPECT_THAT(signaledFences, IsEmpty());

    virtioGpuTimelines->notifyTaskCompletion(task1Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                }));

    auto task3Id = virtioGpuTimelines->enqueueTask(kGlobalRing);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                }));

    auto fence3Id = fenceId++;
    virtioGpuTimelines->enqueueFence(kGlobalRing, fence3Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(task2Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                    Pair(Eq(kGlobalRing), Eq(fence2Id)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(task3Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                    Pair(Eq(kGlobalRing), Eq(fence2Id)),
                    Pair(Eq(kGlobalRing), Eq(fence3Id)),
                }));
}

TEST(VirtioGpuTimelinesTest, FencesWithoutPendingTasksWithAsyncCallback) {
    std::vector<std::pair<Ring, FenceId>> signaledFences;

    auto fenceCallback =
        [&](const Ring& ring, FenceId fenceId) {
            signaledFences.push_back(std::make_pair(ring, fenceId));
        };
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(fenceCallback);

    FenceId fenceId = 0;

    auto fence1Id = fenceId++;
    virtioGpuTimelines->enqueueFence(kGlobalRing, fence1Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                }));

    auto fence2Id = fenceId++;
    virtioGpuTimelines->enqueueFence(kGlobalRing, fence2Id);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                    Pair(Eq(kGlobalRing), Eq(fence2Id)),
                }));
}

TEST(VirtioGpuTimelinesTest, FencesSharingSamePendingTasksWithAsyncCallback) {
   std::vector<std::pair<Ring, FenceId>> signaledFences;

    auto fenceCallback =
        [&](const Ring& ring, FenceId fenceId) {
            signaledFences.push_back(std::make_pair(ring, fenceId));
        };
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(fenceCallback);

    FenceId fenceId = 0;

    auto taskId = virtioGpuTimelines->enqueueTask(kGlobalRing);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto fence1Id = fenceId++;
    virtioGpuTimelines->enqueueFence(kGlobalRing, fence1Id);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto fence2Id = fenceId++;
    virtioGpuTimelines->enqueueFence(kGlobalRing, fence2Id);
    EXPECT_THAT(signaledFences, IsEmpty());

    virtioGpuTimelines->notifyTaskCompletion(taskId);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(fence1Id)),
                    Pair(Eq(kGlobalRing), Eq(fence2Id)),
                }));
}

TEST(VirtioGpuTimelinesTest, TasksAndFencesOnMultipleContextsWithAsyncCallback) {
    std::vector<std::pair<Ring, FenceId>> signaledFences;

    auto fenceCallback =
        [&](const Ring& ring, FenceId fenceId) {
            signaledFences.push_back(std::make_pair(ring, fenceId));
        };
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(fenceCallback);

    auto taskId2 = virtioGpuTimelines->enqueueTask(kContext2Ring);
    EXPECT_THAT(signaledFences, IsEmpty());

    auto taskId3 = virtioGpuTimelines->enqueueTask(kContext3Ring);
    EXPECT_THAT(signaledFences, IsEmpty());

    virtioGpuTimelines->enqueueFence(kGlobalRing, 1);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(1)),
                }));

    virtioGpuTimelines->enqueueFence(kContext2Ring, 2);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(1)),
                }));

    virtioGpuTimelines->enqueueFence(kContext3Ring, 3);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(1)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(taskId2);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(1)),
                    Pair(Eq(kContext2Ring), Eq(2)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(taskId3);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kGlobalRing), Eq(1)),
                    Pair(Eq(kContext2Ring), Eq(2)),
                    Pair(Eq(kContext3Ring), Eq(3)),
                }));
}

TEST(VirtioGpuTimelinesTest, TasksAndFencesOnMultipleRingsWithAsyncCallback) {
    std::vector<std::pair<Ring, FenceId>> signaledFences;

    auto fenceCallback =
        [&](const Ring& ring, FenceId fenceId) {
            signaledFences.push_back(std::make_pair(ring, fenceId));
        };
    std::unique_ptr<VirtioGpuTimelines> virtioGpuTimelines =
        VirtioGpuTimelines::create(fenceCallback);

    const auto kContext1Ring1 = Ring{RingContextSpecific{
        .mCtxId = 1,
        .mRingIdx = 1,
    }};
    const auto kContext1Ring2 = Ring{RingContextSpecific{
        .mCtxId = 1,
        .mRingIdx = 2,
    }};
    const auto kContext1Ring3 = Ring{RingContextSpecific{
        .mCtxId = 1,
        .mRingIdx = 3,
    }};

    auto taskId2 = virtioGpuTimelines->enqueueTask(kContext1Ring2);
    auto taskId3 = virtioGpuTimelines->enqueueTask(kContext1Ring3);
    EXPECT_THAT(signaledFences, IsEmpty());

    virtioGpuTimelines->enqueueFence(kContext1Ring1, 1);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kContext1Ring1), Eq(1)),
                }));

    virtioGpuTimelines->enqueueFence(kContext1Ring2, 2);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kContext1Ring1), Eq(1)),
                }));

    virtioGpuTimelines->enqueueFence(kContext1Ring3, 3);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kContext1Ring1), Eq(1)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(taskId2);
    EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kContext1Ring1), Eq(1)),
                    Pair(Eq(kContext1Ring2), Eq(2)),
                }));

    virtioGpuTimelines->notifyTaskCompletion(taskId3);
        EXPECT_THAT(signaledFences,
                ElementsAreArray({
                    Pair(Eq(kContext1Ring1), Eq(1)),
                    Pair(Eq(kContext1Ring2), Eq(2)),
                    Pair(Eq(kContext1Ring3), Eq(3)),
                }));
}

}  // namespace
}  // namespace gfxstream
