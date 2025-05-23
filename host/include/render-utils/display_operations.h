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

typedef bool (*gfxstream_multi_display_is_multi_display_enabled_t)();
typedef bool (*gfxstream_multi_display_is_multi_display_window_t)();

typedef bool (*gfxstream_multi_display_is_pixel_fold_t)();

typedef void (*gfxstream_multi_display_get_combined_display_size_t)(uint32_t* outWidth,
                                                                    uint32_t* outHeight);

typedef bool (*gfxstream_multi_display_get_display_info_t)(uint32_t displayId, int32_t* outX,
                                                           int32_t* outY, uint32_t* outW,
                                                           uint32_t* outH, uint32_t* outDpi,
                                                           uint32_t* outFlags, bool* outEnabled);
typedef bool (*gfxstream_multi_display_get_next_display_info_t)(
    int32_t previousDisplayId, uint32_t* outNextDisplayId, int32_t* outX, int32_t* outY,
    uint32_t* outW, uint32_t* outH, uint32_t* outDpi, uint32_t* outFlags,
    uint32_t* outColorBufferHandle);

typedef int (*gfxstream_multi_display_create_display_t)(uint32_t* displayId);
typedef int (*gfxstream_multi_display_destroy_display_t)(uint32_t displayId);

typedef int (*gfxstream_multi_display_get_display_color_buffer_t)(uint32_t displayId,
                                                                  uint32_t* outColorBufferHandle);
typedef int (*gfxstream_multi_display_set_display_color_buffer_t)(uint32_t displayId,
                                                                  uint32_t colorBufferHandle);

typedef int (*gfxstream_multi_display_get_color_buffer_display_t)(uint32_t colorBufferHandle,
                                                                  uint32_t* outDisplayId);

typedef int (*gfxstream_multi_display_get_display_pose_t)(uint32_t displayId, int32_t* x,
                                                          int32_t* y, uint32_t* w, uint32_t* h);
typedef int (*gfxstream_multi_display_set_display_pose_t)(uint32_t displayId, int32_t x, int32_t y,
                                                          uint32_t w, uint32_t h, uint32_t dpi);

typedef struct gfxstream_multi_display_ops {
    gfxstream_multi_display_is_multi_display_enabled_t is_multi_display_enabled;
    gfxstream_multi_display_is_multi_display_window_t is_multi_window;

    gfxstream_multi_display_is_pixel_fold_t is_pixel_fold;

    gfxstream_multi_display_get_combined_display_size_t get_combined_size;

    gfxstream_multi_display_get_display_info_t get_display_info;
    gfxstream_multi_display_get_next_display_info_t get_next_display_info;

    gfxstream_multi_display_create_display_t create_display;
    gfxstream_multi_display_destroy_display_t destroy_display;

    gfxstream_multi_display_get_display_color_buffer_t get_display_color_buffer;
    gfxstream_multi_display_set_display_color_buffer_t set_display_color_buffer;

    gfxstream_multi_display_get_color_buffer_display_t get_color_buffer_display;

    gfxstream_multi_display_get_display_pose_t get_display_pose;
    gfxstream_multi_display_set_display_pose_t set_display_pose;
} gfxstream_multi_display_ops;
