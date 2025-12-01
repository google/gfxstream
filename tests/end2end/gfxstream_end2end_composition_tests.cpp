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

#include "gfxstream_end2end_tests.h"

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

// Tests that composing a solid color YV12 image results in fullscreen image
// of exactly that same solid color.
TEST_P(GfxstreamEnd2EndCompositionTest, BlitYV12) {
    constexpr const uint32_t kWidth = 32;
    constexpr const uint32_t kHeight = 32;

    ScopedRenderControlDevice rcDevice(*mRc);

    const PixelR8G8B8A8 rgbaColor = PixelR8G8B8A8(66, 99, 160, 255);

    const auto yuvColor = PixelY8U8V8::FromR8G8B8A8(rgbaColor);
    const auto yuvAhb =
        GFXSTREAM_ASSERT(CreateAHBWithColor(kWidth, kHeight, GFXSTREAM_AHB_FORMAT_YV12, yuvColor));

    auto resultAhb = GFXSTREAM_ASSERT(ScopedAHardwareBuffer::Allocate(
        *mGralloc, kWidth, kHeight, GFXSTREAM_AHB_FORMAT_B8G8R8A8_UNORM));

    const RenderControlComposition composition = {
        .displayId = 0,
        .compositionResultColorBufferHandle = mGralloc->getHostHandle(resultAhb),
    };
    const std::vector<RenderControlCompositionLayer> compositionLayers = {{
        {
            .colorBufferHandle = mGralloc->getHostHandle(yuvAhb),
            .composeMode = HWC2_COMPOSITION_DEVICE,
            .displayFrame =
                {
                    .left = 0,
                    .top = 0,
                    .right = kWidth,
                    .bottom = kHeight,
                },
            .crop =
                {
                    .left = 0,
                    .top = 0,
                    .right = static_cast<float>(kWidth),
                    .bottom = static_cast<float>(kHeight),
                },
            .blendMode = HWC2_BLEND_MODE_NONE,
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
    }};
    ASSERT_THAT(
        mRc->rcCompose(rcDevice, &composition, static_cast<uint32_t>(compositionLayers.size()),
                       compositionLayers.data()),
        Eq(0));

    GFXSTREAM_ASSERT(AhbIsEntirely(resultAhb, rgbaColor));
}

TEST_P(GfxstreamEnd2EndCompositionTest, BasicCompositionYV12) {
    ScopedRenderControlDevice rcDevice(*mRc);

    auto layer1Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android.png"));

    const auto layer2RgbaColor = PixelR8G8B8A8(66, 99, 160, 255);
    const auto layer2YuvColor = PixelY8U8V8::FromR8G8B8A8(layer2RgbaColor);
    auto layer2Ahb =
        GFXSTREAM_ASSERT(CreateAHBWithColor(32, 32, GFXSTREAM_AHB_FORMAT_YV12, layer2YuvColor));

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
            .blendMode = HWC2_BLEND_MODE_NONE,
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
            .blendMode = HWC2_BLEND_MODE_NONE,
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

    GFXSTREAM_ASSERT(CompareAHBWithGolden(resultAhb, "256x256_golden_basic_yv12.png"));
}

TEST_P(GfxstreamEnd2EndCompositionTest, RotatedCompositionRGBA) {
    ScopedRenderControlDevice rcDevice(*mRc);

    const auto rgbaColor1 = PixelR8G8B8A8(66, 99, 160, 255);
    const auto rgbaColor2 = PixelR8G8B8A8(222, 16, 0, 255);

    auto layer1Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android.png"));
    auto layer2Ahb = GFXSTREAM_ASSERT(
       CreateAHBWithCheckerboard(256, 256, 64, GFXSTREAM_AHB_FORMAT_R8G8B8A8_UNORM, rgbaColor1, rgbaColor2));
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
            .transform = HWC_TRANSFORM_ROT_90,
        },
    };

    ASSERT_THAT(mRc->rcCompose(rcDevice, &composition, 2, compositionLayers), Eq(0));

    GFXSTREAM_ASSERT(CompareAHBWithGolden(resultAhb, "256x256_golden_rotated_rgba.png"));
}

// TODO(b/465208161): fix and re-enable.
TEST_P(GfxstreamEnd2EndCompositionTest, DISABLED_RotatedCompositionYV12) {
    ScopedRenderControlDevice rcDevice(*mRc);

    const auto rgbaColor1 = PixelR8G8B8A8(66, 99, 160, 255);
    const auto rgbaColor2 = PixelR8G8B8A8(222, 16, 0, 255);
    const auto yuvColor1 = PixelY8U8V8::FromR8G8B8A8(rgbaColor1);
    const auto yuvColor2 = PixelY8U8V8::FromR8G8B8A8(rgbaColor2);

    auto layer1Ahb = GFXSTREAM_ASSERT(CreateAHBFromImage("256x256_android.png"));
    auto layer2Ahb = GFXSTREAM_ASSERT(
        CreateAHBWithCheckerboard(256, 256, 64, GFXSTREAM_AHB_FORMAT_YV12, yuvColor1, yuvColor2));
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
            .blendMode = HWC2_BLEND_MODE_NONE,
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
            .blendMode = HWC2_BLEND_MODE_NONE,
            .alpha = 1.0,
            .color =
                {
                    .r = 0,
                    .g = 0,
                    .b = 0,
                    .a = 0,
                },
            .transform = HWC_TRANSFORM_ROT_90,
        },
    };

    ASSERT_THAT(mRc->rcCompose(rcDevice, &composition, 2, compositionLayers), Eq(0));

    GFXSTREAM_ASSERT(CompareAHBWithGolden(resultAhb, "256x256_golden_rotated_yv12.png"));
}

INSTANTIATE_TEST_SUITE_P(GfxstreamEnd2EndTests, GfxstreamEnd2EndCompositionTest,
                         ::testing::ValuesIn({
                             TestParams{
                                 .with_gl = true,
                                 .with_vk = false,
                                 .with_features = {"MinimalLogging"},
                             },
                             TestParams{
                                 .with_gl = true,
                                 .with_vk = true,
                                 .with_features = {"MinimalLogging"},
                             },
                             TestParams{
                                 .with_gl = false,
                                 .with_vk = true,
                                 .with_features = {"MinimalLogging"},
                             },
                         }),
                         &GetTestName);

}  // namespace
}  // namespace tests
}  // namespace gfxstream
