// Copyright 2025 The Android Open Source Project
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

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "VkQsriTimeline.h"

namespace gfxstream {
namespace vk {
namespace {
using ::testing::InSequence;
using ::testing::MockFunction;

TEST(VkQsriTImelineTest, signalFirstRegisterCallbackLater) {
    MockFunction<void()> mockCallback1, mockCallback2;
    VkQsriTimeline qsriTimeline;
    {
        InSequence s;
        EXPECT_CALL(mockCallback1, Call()).Times(1);
        EXPECT_CALL(mockCallback2, Call()).Times(1);
    }
    qsriTimeline.signalNextPresentAndPoll();
    qsriTimeline.signalNextPresentAndPoll();
    qsriTimeline.registerCallbackForNextPresentAndPoll(mockCallback1.AsStdFunction());
    qsriTimeline.registerCallbackForNextPresentAndPoll(mockCallback2.AsStdFunction());
}

TEST(VkQsriTImelineTest, registerCallbackFirstSignalLater) {
    MockFunction<void()> mockCallback1, mockCallback2, beforeSignal;
    VkQsriTimeline qsriTimeline;
    {
        InSequence s;
        EXPECT_CALL(beforeSignal, Call()).Times(1);
        EXPECT_CALL(mockCallback1, Call()).Times(1);
        EXPECT_CALL(mockCallback2, Call()).Times(1);
    }
    qsriTimeline.registerCallbackForNextPresentAndPoll(mockCallback1.AsStdFunction());
    qsriTimeline.registerCallbackForNextPresentAndPoll(mockCallback2.AsStdFunction());
    beforeSignal.Call();
    qsriTimeline.signalNextPresentAndPoll();
    qsriTimeline.signalNextPresentAndPoll();
}

}  // namespace
}  // namespace vk
}  // namespace gfxstream