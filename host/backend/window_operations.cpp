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

#include "gfxstream/host/window_operations.h"

namespace gfxstream {
namespace {

bool DefaultGfxstreamWindowIsCurrentThreadUiThread() { return false; } 
void DefaultGfxstreamWindowRunOnUiThread(gfxstream_window_ui_thread_runnable_t, void*, bool) {}

bool DefaultGfxstreamWindowPaintMultiDisplayWindow(uint32_t, uint32_t) { return false; }

bool DefaultGfxstreamWindowIsFolded() { return false; }
bool DefaultGfxstreamWindowGetFoldedArea(int*, int*, int*, int*) { return false; }

gfxstream_window_ops sGfxstreamWindowOps = {
    .is_current_thread_ui_thread = DefaultGfxstreamWindowIsCurrentThreadUiThread,
    .run_on_ui_thread = DefaultGfxstreamWindowRunOnUiThread,

    .paint_multi_display_window = DefaultGfxstreamWindowPaintMultiDisplayWindow,

    .is_folded = DefaultGfxstreamWindowIsFolded,
    .get_folded_area = DefaultGfxstreamWindowGetFoldedArea,
};

}  // namespace

void set_gfxstream_window_operations(const gfxstream_window_ops& ops) {
    sGfxstreamWindowOps = ops;
}

const gfxstream_window_ops& get_gfxstream_window_operations() {
    return sGfxstreamWindowOps;
}

}  // namespace gfxstream
