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

#pragma once

#include <array>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <vector>

#include "gfxstream/host/display_operations.h"
#include "gfxstream/host/gfxstream_format.h"
#include "handle.h"
#include "render-utils/Renderer.h"

namespace gfxstream {
namespace host {

class ColorBuffer;

// Posting
enum class PostCmd {
    Post = 0,
    Viewport = 1,
    Compose = 2,
    Clear = 3,
    Screenshot = 4,
    Exit = 5,
    Block = 6,
};

struct Post {
    struct Block {
        // schduledSignal will be set when the block task is scheduled.
        std::promise<void> scheduledSignal;
        // The block task won't stop until continueSignal is ready.
        std::future<void> continueSignal;
    };
    using CompletionCallback =
        std::function<void(std::shared_future<void> waitForGpu)>;
    PostCmd cmd;
    int composeVersion;
    std::vector<char> composeBuffer;
    std::unique_ptr<CompletionCallback> completionCallback = nullptr;
    std::unique_ptr<Block> block = nullptr;
    HandleType cbHandle = 0;
    std::optional<std::array<float, 16>> colorTransform;

    //TODO: remove union here and separate into message structures
    union {
        ColorBuffer* cb;
        struct {
            int width;
            int height;
        } viewport;
        struct {
            ColorBuffer* cb;
            int screenwidth;
            int screenheight;
            int rotation;
            GfxstreamFormat pixelsFormat;
            void* pixels;
            Rect rect;
        } screenshot;
    };

    static std::optional<std::array<float, 16>> GetColorTransform(uint32_t displayId = 0) {
        float displayColorTransformData[16];
        if (get_gfxstream_multi_display_operations().get_color_transform_matrix(
                displayId, displayColorTransformData)) {
            return std::nullopt;
        }

        // Only set it if not identity to allow faster codepaths
        bool isIdentity = true;
        const float eps = 1e-6f;
        for(int i = 0; i < 16; i++) {
            const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
            if (std::abs(displayColorTransformData[i] - expected) > eps) {
                isIdentity = false;
                break;
            }
        }
        if (isIdentity) {
            return std::nullopt;
        }

        std::array<float, 16> matrix;
        std::copy(std::begin(displayColorTransformData), std::end(displayColorTransformData),
                  std::begin(matrix));
        return matrix;
    }
};

}  // namespace host
}  // namespace gfxstream