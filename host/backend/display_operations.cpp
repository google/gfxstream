// Copyright (C) 2025 The Android Open Source Project
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

#include "gfxstream/host/display_operations.h"

#include <map>

#include "gfxstream/common/logging.h"

namespace gfxstream {
namespace {

// Historical defaults:
//  * 0 for default Android display
//  * 1-5 for Emulator UI
//  * 6-10 for developer from rcControl
static constexpr uint32_t kDeveloperDisplayIdBegin = 6;
static constexpr uint32_t kMaxDisplays = 11;
static constexpr uint32_t kInvalidDisplayId = 0xFFFFFFAB;

struct DisplayInfo {
    int32_t pos_x = 0 ;
    int32_t pos_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t originalWidth = 0;
    uint32_t originalHeight = 0;
    uint32_t dpi = 0;
    uint32_t flag = 0;
    uint32_t cb = 0;
    int32_t rotation = 0;
    bool enabled = false;
};

std::map<uint32_t, DisplayInfo> sDisplayInfos;

bool DefaultGfxstreamMultiDisplayIsMultiDisplayEnabled() {
    return sDisplayInfos.size() > 1;
}

bool DefaultGfxstreamMultiDisplayIsMultiDisplayWindow() { return false; }

bool DefaultGfxstreamMultiDisplayIsPixelFold() {
    return false;
}

void DefaultGfxstreamMultiDisplayGetCombinedDisplaySize(uint32_t*, uint32_t*) {}

bool DefaultGfxstreamMultiDisplayGetDisplayInfo(uint32_t id,
                                                int32_t* x,
                                                int32_t* y,
                                                uint32_t* w,
                                                uint32_t* h,
                                                uint32_t* dpi,
                                                uint32_t* flags,
                                                bool* enabled) {
    auto it = sDisplayInfos.find(id);
    if (it == sDisplayInfos.end()) {
        if (enabled) {
            *enabled = false;
        }
        return false;
    }
    const DisplayInfo& info = it->second;

    if (x) {
        *x = info.pos_x;
    }
    if (y) {
        *y = info.pos_y;
    }
    if (w) {
        *w = info.width;
    }
    if (h) {
        *h = info.height;
    }
    if (dpi) {
        *dpi = info.dpi;
    }
    if (flags) {
        *flags = info.flag;
    }
    if (enabled) {
        *enabled = info.enabled;
    }
    return true;
}

bool DefaultGfxstreamMultiDisplayGetNextDisplayInfo(int32_t previousId,
                                                    uint32_t* nextId,
                                                    int32_t* x,
                                                    int32_t* y,
                                                    uint32_t* w,
                                                    uint32_t* h,
                                                    uint32_t* dpi,
                                                    uint32_t* flags,
                                                    uint32_t* cb) {
    const uint32_t toCheckId = previousId < 0 ? 0 : previousId + 1;

    auto it = sDisplayInfos.lower_bound(toCheckId);
    if (it == sDisplayInfos.end()) {
        return false;
    }
    const DisplayInfo& info = it->second;
    if (nextId) {
        *nextId = it->first;
    }
    if (x) {
        *x = info.pos_x;
    }
    if (y) {
        *y = info.pos_y;
    }
    if (w) {
        *w = info.width;
    }
    if (h) {
        *h = info.height;
    }
    if (dpi) {
        *dpi = info.dpi;
    }
    if (flags) {
        *flags = info.flag;
    }
    if (cb) {
        *cb = info.cb;
    }
    return true;
}

int DefaultGfxstreamMultiDisplayCreateDisplay(uint32_t* displayId) {
    if (displayId == nullptr) {
        GFXSTREAM_ERROR("Cannot create display, null display id pointer.");
        return -1;
    }

    if (sDisplayInfos.size() >= kMaxDisplays) {
        GFXSTREAM_ERROR("Cannot create more displays, exceeding limits %d", kMaxDisplays);
        return -1;
    }

    if (sDisplayInfos.find(*displayId) != sDisplayInfos.end()) {
        return 0;
    }

    // displays created by internal rcCommands
    if (*displayId == kInvalidDisplayId) {
        for (uint32_t i = kDeveloperDisplayIdBegin; i < kMaxDisplays; i++) {
            if (sDisplayInfos.find(i) == sDisplayInfos.end()) {
                *displayId = i;
                break;
            }
        }
    }

    if (*displayId == kInvalidDisplayId) {
        GFXSTREAM_ERROR("Cannot create more displays, exceeding limits %d", kMaxDisplays);
        return -1;
    }

    sDisplayInfos.emplace(*displayId, DisplayInfo());
    return 0;
}

int DefaultGfxstreamMultiDisplayDestroyDisplay(uint32_t id) {
    sDisplayInfos.erase(id);
    return 0;
}

int DefaultGfxstreamMultiDisplayGetDisplayColorBuffer(uint32_t id, uint32_t* cb) {
    auto it = sDisplayInfos.find(id);
    if (it == sDisplayInfos.end()) {
        GFXSTREAM_ERROR("Failed to get display pose: cannot find display %d", id);
        return -1;
    }
    const DisplayInfo& info = it->second;
    if (cb) {
        *cb = info.cb;
    }
    return 0;
}

int DefaultGfxstreamMultiDisplaySetDisplayColorBuffer(uint32_t id, uint32_t cb) {
    auto it = sDisplayInfos.find(id);
    if (it == sDisplayInfos.end()) {
        GFXSTREAM_ERROR("Failed to get display pose: cannot find display %d", id);
        return -1;
    }
    DisplayInfo& info = it->second;
    info.cb = cb;
    return 0;
 }

int DefaultGfxstreamMultiDisplayGetColorBufferDisplay(uint32_t cb, uint32_t* outId) {
    for (const auto& [id, info] : sDisplayInfos) {
        if (info.cb == cb) {
            if (outId) {
                *outId = id;
            }
            return 0;
        }
    }
    return -1;
}

int DefaultGfxstreamMultiDisplayGetDisplayPose(uint32_t id,
                                               int32_t* x,
                                               int32_t* y,
                                               uint32_t* w,
                                               uint32_t* h) {
    auto it = sDisplayInfos.find(id);
    if (it == sDisplayInfos.end()) {
        GFXSTREAM_ERROR("Failed to get display pose: cannot find display %d", id);
        return -1;
    }
    const DisplayInfo& info = it->second;
    if (x) {
        *x = info.pos_x;
    }
    if (y) {
        *y = info.pos_y;
    }
    if (w) {
        *w = info.width;
    }
    if (h) {
        *h = info.height;
    }
    return 0;
}

int DefaultGfxstreamMultiDisplaySetDisplayPose(uint32_t id,
                                               int32_t x,
                                               int32_t y,
                                               uint32_t w,
                                               uint32_t h,
                                               uint32_t dpi) {
    auto it = sDisplayInfos.find(id);
    if (it == sDisplayInfos.end()) {
        GFXSTREAM_ERROR("Failed to set display pose: cannot find display %d", id);
        return -1;
    }
    DisplayInfo& info = it->second;
    info.pos_x = x;
    info.pos_y = y;
    info.width = w;
    info.height = h;
    info.dpi = dpi;
    return 0;
}

gfxstream_multi_display_ops sGfxstreamMultiDisplayOps = {
    .is_multi_display_enabled = DefaultGfxstreamMultiDisplayIsMultiDisplayEnabled,
    .is_multi_window = DefaultGfxstreamMultiDisplayIsMultiDisplayWindow,
    .is_pixel_fold = DefaultGfxstreamMultiDisplayIsPixelFold,
    .get_combined_size = DefaultGfxstreamMultiDisplayGetCombinedDisplaySize,
    .get_display_info = DefaultGfxstreamMultiDisplayGetDisplayInfo,
    .get_next_display_info = DefaultGfxstreamMultiDisplayGetNextDisplayInfo,
    .create_display = DefaultGfxstreamMultiDisplayCreateDisplay,
    .destroy_display = DefaultGfxstreamMultiDisplayDestroyDisplay,
    .get_display_color_buffer = DefaultGfxstreamMultiDisplayGetDisplayColorBuffer,
    .set_display_color_buffer = DefaultGfxstreamMultiDisplaySetDisplayColorBuffer,
    .get_color_buffer_display = DefaultGfxstreamMultiDisplayGetColorBufferDisplay,
    .get_display_pose = DefaultGfxstreamMultiDisplayGetDisplayPose,
    .set_display_pose = DefaultGfxstreamMultiDisplaySetDisplayPose,
};

}  // namespace

void set_gfxstream_multi_display_operations(const gfxstream_multi_display_ops& ops) {
    sGfxstreamMultiDisplayOps = ops;
}

const gfxstream_multi_display_ops& get_gfxstream_multi_display_operations() {
    return sGfxstreamMultiDisplayOps;
}

}  // namespace gfxstream
