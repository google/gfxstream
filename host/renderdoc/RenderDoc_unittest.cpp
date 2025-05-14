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

#include "gfxstream/host/RenderDoc.h"

#include <type_traits>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "gfxstream/SharedLibrary.h"

namespace gfxstream {
namespace host {
namespace {

using ::testing::_;
using ::testing::DoAll;
using ::testing::InSequence;
using ::testing::MockFunction;
using ::testing::Return;
using ::testing::SetArgPointee;

using FunctionPtr = gfxstream::base::SharedLibrary::FunctionPtr;
using RenderDocApi = RenderDoc::RenderDocApi;

class MockSharedLibrary : public gfxstream::base::SharedLibrary {
   public:
    MockSharedLibrary() : SharedLibrary(NULL) {}
    MOCK_METHOD(FunctionPtr, findSymbol, (const char*), (const, override));
};

TEST(RenderDocTest, InitializeWithNullSharedLibrary) {
    EXPECT_EQ(RenderDoc::create(nullptr), nullptr);
}

TEST(RenderDocTest, CantFindRENDERDOC_GetAPI) {
    MockSharedLibrary sharedLibrary;
    EXPECT_CALL(sharedLibrary, findSymbol(_)).WillRepeatedly(Return(nullptr));
    EXPECT_EQ(RenderDoc::create(&sharedLibrary), nullptr);
}

TEST(RenderDocTest, RENDERDOC_GetAPIFails) {
    MockSharedLibrary sharedLibrary;
    static MockFunction<std::remove_pointer_t<pRENDERDOC_GetAPI>> mockRENDERDOC_GetAPI;
    static pRENDERDOC_GetAPI fpMockRENDERDOC_GetAPI = [](RENDERDOC_Version version,
                                                         void** outAPIPointers) {
        return mockRENDERDOC_GetAPI.Call(version, outAPIPointers);
    };
    RenderDocApi rdocApi;

    EXPECT_CALL(sharedLibrary, findSymbol(_)).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(sharedLibrary, findSymbol("RENDERDOC_GetAPI"))
        .WillRepeatedly(Return(reinterpret_cast<FunctionPtr>(fpMockRENDERDOC_GetAPI)));

    EXPECT_CALL(mockRENDERDOC_GetAPI, Call(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(&rdocApi), Return(0)));
    EXPECT_EQ(RenderDoc::create(&sharedLibrary), nullptr);

    EXPECT_CALL(mockRENDERDOC_GetAPI, Call(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(nullptr), Return(1)));
    EXPECT_EQ(RenderDoc::create(&sharedLibrary), nullptr);
}

TEST(RenderDocTest, CreateSuccessfully) {
    MockSharedLibrary sharedLibrary;
    static MockFunction<std::remove_pointer_t<pRENDERDOC_GetAPI>> mockRENDERDOC_GetAPI;
    static pRENDERDOC_GetAPI fpMockRENDERDOC_GetAPI = [](RENDERDOC_Version version,
                                                         void** outAPIPointers) {
        return mockRENDERDOC_GetAPI.Call(version, outAPIPointers);
    };
    RenderDocApi rdocApiMock;
    static MockFunction<std::remove_pointer_t<pRENDERDOC_GetCaptureOptionU32>>
        getCaptureOptionU32Mock;
    rdocApiMock.GetCaptureOptionU32 = [](RENDERDOC_CaptureOption option) {
        return getCaptureOptionU32Mock.Call(option);
    };

    EXPECT_CALL(sharedLibrary, findSymbol(_)).WillRepeatedly(Return(nullptr));
    EXPECT_CALL(sharedLibrary, findSymbol("RENDERDOC_GetAPI"))
        .WillRepeatedly(Return(reinterpret_cast<FunctionPtr>(fpMockRENDERDOC_GetAPI)));
    EXPECT_CALL(mockRENDERDOC_GetAPI, Call(_, _))
        .WillRepeatedly(DoAll(SetArgPointee<1>(&rdocApiMock), Return(1)));
    std::unique_ptr<RenderDoc> renderDoc = RenderDoc::create(&sharedLibrary);
    EXPECT_NE(renderDoc, nullptr);

    EXPECT_CALL(getCaptureOptionU32Mock, Call(eRENDERDOC_Option_DebugOutputMute))
        .WillRepeatedly(Return(1));
    EXPECT_EQ(renderDoc->call(RenderDoc::kGetCaptureOptionU32, eRENDERDOC_Option_DebugOutputMute),
              1);
}

class RenderDocMock {
   public:
    // For StartFrameCapture
    MOCK_METHOD(void, call,
                (void (*RenderDocApi::*function)(RENDERDOC_DevicePointer, RENDERDOC_WindowHandle),
                 RENDERDOC_DevicePointer, RENDERDOC_WindowHandle),
                (const));
    // For EndFrameCapture
    MOCK_METHOD(uint32_t, call,
                (uint32_t(*RenderDocApi::*function)(RENDERDOC_DevicePointer,
                                                    RENDERDOC_WindowHandle),
                 RENDERDOC_DevicePointer, RENDERDOC_WindowHandle),
                (const));
    // For IsFrameCapturing
    MOCK_METHOD(uint32_t, call, (uint32_t(*RenderDocApi::*function)()), (const));
};

using RenderDocWithMultipleVkInstances = RenderDocWithMultipleVkInstancesBase<RenderDocMock>;

TEST(RenderDocWithMultipleVkInstancesTest,
     ShouldNotStartFrameCaptureOnFrameDelimiterWhenNotCapturing) {
    RenderDocMock renderDocMock;
    intptr_t vkInstanceInternal = 0x1234;
    VkInstance vkInstance = reinterpret_cast<VkInstance>(&vkInstanceInternal);
    RenderDocWithMultipleVkInstances renderDocWithMultipleVkInstances(renderDocMock);

    EXPECT_CALL(renderDocMock, call(RenderDoc::kIsFrameCapturing)).WillRepeatedly(Return(0));
    EXPECT_CALL(renderDocMock, call(RenderDoc::kStartFrameCapture, _, _)).Times(0);
    EXPECT_CALL(renderDocMock, call(RenderDoc::kEndFrameCapture, _, _)).Times(0);

    renderDocWithMultipleVkInstances.onFrameDelimiter(vkInstance);
}

TEST(RenderDocWithMultipleVkInstancesTest, ShouldStartAndEndFrameCaptureOnFrameDelimiter) {
    RenderDocMock renderDocMock;
    intptr_t vkInstanceInternal = 0x4321;
    VkInstance vkInstance = reinterpret_cast<VkInstance>(&vkInstanceInternal);
    RenderDocWithMultipleVkInstances renderDocWithMultipleVkInstances(renderDocMock);

    EXPECT_CALL(renderDocMock, call(RenderDoc::kIsFrameCapturing)).WillRepeatedly(Return(1));
    {
        InSequence s;

        EXPECT_CALL(renderDocMock, call(RenderDoc::kStartFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1);
        EXPECT_CALL(renderDocMock, call(RenderDoc::kEndFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1)
            .WillRepeatedly(Return(1));
        EXPECT_CALL(renderDocMock, call(RenderDoc::kStartFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1);
        EXPECT_CALL(renderDocMock, call(RenderDoc::kEndFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1)
            .WillRepeatedly(Return(1));
    }

    renderDocWithMultipleVkInstances.onFrameDelimiter(vkInstance);
    renderDocWithMultipleVkInstances.onFrameDelimiter(vkInstance);
}

TEST(RenderDocWithMultipleVkInstancesTest, ShouldEndFrameCaptureOnVkInstanceRemoved) {
    RenderDocMock renderDocMock;
    intptr_t vkInstanceInternal = 0x4321;
    VkInstance vkInstance = reinterpret_cast<VkInstance>(&vkInstanceInternal);
    RenderDocWithMultipleVkInstances renderDocWithMultipleVkInstances(renderDocMock);

    EXPECT_CALL(renderDocMock, call(RenderDoc::kIsFrameCapturing)).WillRepeatedly(Return(1));
    {
        InSequence s;

        EXPECT_CALL(renderDocMock, call(RenderDoc::kStartFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1);
        EXPECT_CALL(renderDocMock, call(RenderDoc::kEndFrameCapture,
                                        RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
            .Times(1)
            .WillRepeatedly(Return(1));
    }

    renderDocWithMultipleVkInstances.onFrameDelimiter(vkInstance);
    renderDocWithMultipleVkInstances.removeVkInstance(vkInstance);
    EXPECT_CALL(renderDocMock, call(RenderDoc::kEndFrameCapture,
                                    RENDERDOC_DEVICEPOINTER_FROM_VKINSTANCE(vkInstance), NULL))
        .Times(0);
}
}  // namespace
}  // namespace host
}  // namespace gfxstream
