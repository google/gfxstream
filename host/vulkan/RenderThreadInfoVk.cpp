// Copyright 2023 The Android Open Source Project
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

#include "RenderThreadInfoVk.h"

#include "gfxstream/host/logging.h"

namespace gfxstream {
namespace vk {

static thread_local RenderThreadInfoVk* tlThreadInfo = nullptr;

RenderThreadInfoVk::RenderThreadInfoVk() {
    if (tlThreadInfo != nullptr) {
        GFXSTREAM_FATAL("Attempted to thread local RenderThreadInfoVk twice.");
    }
    tlThreadInfo = this;
}

RenderThreadInfoVk::~RenderThreadInfoVk() { tlThreadInfo = nullptr; }

RenderThreadInfoVk* RenderThreadInfoVk::get() { return tlThreadInfo; }

void RenderThreadInfoVk::onSave(gfxstream::Stream* stream) {
    stream->putBe32(ctx_id);
}

bool RenderThreadInfoVk::onLoad(gfxstream::Stream* stream) {
    ctx_id = stream->getBe32();
    return true;
}

}  // namespace vk
}  // namespace gfxstream
