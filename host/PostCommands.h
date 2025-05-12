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

#include <GLES2/gl2.h>

#include <functional>
#include <future>
#include <memory>
#include <vector>

#include "Handle.h"
#include "render-utils/Renderer.h"

namespace gfxstream {

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
            GLenum format;
            GLenum type;
            int rotation;
            void* pixels;
            Rect rect;
        } screenshot;
    };
};

}  // namespace gfxstream