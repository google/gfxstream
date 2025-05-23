// Copyright (C) 2023 The Android Open Source Project
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

#include <string>

#include "GfxstreamEnd2EndTests.h"

namespace gfxstream {
namespace tests {
namespace {

using testing::Eq;

class GfxstreamEnd2EndCompositionTest : public GfxstreamEnd2EndTest {};

TEST_P(GfxstreamEnd2EndCompositionTest, BasicComposition) {
    ScopedRenderControlDevice rcDevice(*mRc);

    auto layer1Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android.png"));
    auto layer2Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android_with_transparency.png"));
    auto resultAhb = GFXSTREAM_ASSERT(
        ScopedAHardwareBuffer::Allocate(*mGralloc, 256, 256, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM));

    const RenderControlComposition composition = {
        .displayId = 0,
        .compositionResultColorBufferHandle = mGralloc->getHostHandle(resultAhb),
    };
    const RenderControlCompositionLayer compositionLayers[2] = {
        {
            .colorBufferHandle = mGralloc->getHostHandle(layer1Ahb),
            .composeMode = HWC2_COMPOSITION_DEVICE,
            .displayFrame =
                {
                    .left = 0,
                    .top = 0,
                    .right = 256,
                    .bottom = 256,
                },
            .crop =
                {
                    .left = 0,
                    .top = 0,
                    .right = static_cast<float>(256),
                    .bottom = static_cast<float>(256),
                },
            .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
            .alpha = 1.0,
            .color =
                {
                    .r = 0,
                    .g = 0,
                    .b = 0,
                    .a = 0,
                },
            .transform = static_cast<hwc_transform_t>(0),
        },
        {
            .colorBufferHandle = mGralloc->getHostHandle(layer2Ahb),
            .composeMode = HWC2_COMPOSITION_DEVICE,
            .displayFrame =
                {
                    .left = 64,
                    .top = 32,
                    .right = 128,
                    .bottom = 160,
                },
            .crop =
                {
                    .left = 0,
                    .top = 0,
                    .right = static_cast<float>(256),
                    .bottom = static_cast<float>(256),
                },
            .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
            .alpha = 1.0,
            .color =
                {
                    .r = 0,
                    .g = 0,
                    .b = 0,
                    .a = 0,
                },
            .transform = static_cast<hwc_transform_t>(0),
        },
    };

    ASSERT_THAT(mRc->rcCompose(rcDevice, &composition, 2, compositionLayers), Eq(0));

    GFXSTREAM_ASSERT(CompareAHBWithGolden(resultAhb, "256x256_golden_basic_composition.png"));
}

TEST_P(GfxstreamEnd2EndCompositionTest, BasicCompositionBGRA) {
    ScopedRenderControlDevice rcDevice(*mRc);

    auto layer1Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android.png"));
    auto layer2Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android_with_transparency.png"));
    auto resultAhb = GFXSTREAM_ASSERT(
        ScopedAHardwareBuffer::Allocate(*mGralloc, 256, 256, GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM));

    const RenderControlComposition composition = {
        .displayId = 0,
        .compositionResultColorBufferHandle = mGralloc->getHostHandle(resultAhb),
    };
    const RenderControlCompositionLayer compositionLayers[2] = {
        {
            .colorBufferHandle = mGralloc->getHostHandle(layer1Ahb),
            .composeMode = HWC2_COMPOSITION_DEVICE,
            .displayFrame =
                {
                    .left = 0,
                    .top = 0,
                    .right = 256,
                    .bottom = 256,
                },
            .crop =
                {
                    .left = 0,
                    .top = 0,
                    .right = static_cast<float>(256),
                    .bottom = static_cast<float>(256),
                },
            .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
            .alpha = 1.0,
            .color =
                {
                    .r = 0,
                    .g = 0,
                    .b = 0,
                    .a = 0,
                },
            .transform = static_cast<hwc_transform_t>(0),
        },
        {
            .colorBufferHandle = mGralloc->getHostHandle(layer2Ahb),
            .composeMode = HWC2_COMPOSITION_DEVICE,
            .displayFrame =
                {
                    .left = 64,
                    .top = 32,
                    .right = 128,
                    .bottom = 160,
                },
            .crop =
                {
                    .left = 0,
                    .top = 0,
                    .right = static_cast<float>(256),
                    .bottom = static_cast<float>(256),
                },
            .blendMode = HWC2_BLEND_MODE_PREMULTIPLIED,
            .alpha = 1.0,
            .color =
                {
                    .r = 0,
                    .g = 0,
                    .b = 0,
                    .a = 0,
                },
            .transform = static_cast<hwc_transform_t>(0),
        },
    };

    ASSERT_THAT(mRc->rcCompose(rcDevice, &composition, 2, compositionLayers), Eq(0));

    GFXSTREAM_ASSERT(CompareAHBWithGolden(resultAhb, "256x256_golden_basic_composition.png"));
}

INSTANTIATE_TEST_CASE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndCompositionTest,
                        ::testing::ValuesIn({
                            TestParams{
                                .with_gl = true,
                                .with_vk = false,
                            },
                            TestParams{
                                .with_gl = true,
                                .with_vk = true,
                            },
                            TestParams{
                                .with_gl = false,
                                .with_vk = true,
                            },
                        }),
                        &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
