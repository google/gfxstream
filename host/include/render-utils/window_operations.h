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
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>

typedef bool (*gfxstream_window_is_current_thread_ui_thread_t)();
typedef void (*gfxstream_window_ui_thread_runnable_t)(void* data);
typedef void (*gfxstream_window_run_on_ui_thread_t)(gfxstream_window_ui_thread_runnable_t f, void* data, bool wait);

typedef bool (*gfxstream_window_paint_multi_display_window_t)(uint32_t displayId, uint32_t colorBufferHandle);

typedef bool (*gfxstream_window_is_folded_t)();
typedef bool (*gfxstream_window_get_folded_area_t)(int* x, int* y, int* w, int* h);

typedef struct gfxstream_window_ops {
    gfxstream_window_is_current_thread_ui_thread_t is_current_thread_ui_thread;
    gfxstream_window_run_on_ui_thread_t run_on_ui_thread;

    gfxstream_window_paint_multi_display_window_t paint_multi_display_window;

    gfxstream_window_is_folded_t is_folded;
    gfxstream_window_get_folded_area_t get_folded_area;
} gfxstream_window_ops;
