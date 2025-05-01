// Copyright 2022 The Android Open Source Project
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

#include <optional>
#include <string>

#include "VkDecoder.h"
#include "render-utils/stream.h"

namespace gfxstream {
namespace vk {

struct RenderThreadInfoVk {
    // Create new instance. Only call this once per thread.
    // Future calls to get() will return this instance until
    // it is destroyed.
    RenderThreadInfoVk();

    // Destructor.
    ~RenderThreadInfoVk();

    // Return the current thread's instance, if any, or NULL.
    static RenderThreadInfoVk* get();

    void onSave(gfxstream::Stream* stream);
    bool onLoad(gfxstream::Stream* stream);

    uint32_t ctx_id = 0;
    VkDecoder m_vkDec;
};

}  // namespace vk
}  // namespace gfxstream
