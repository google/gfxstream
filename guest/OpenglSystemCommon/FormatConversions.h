// Copyright 2016 The Android Open Source Project
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

#ifndef __GOLDFISH_FORMATCONVERSIONS_H__
#define __GOLDFISH_FORMATCONVERSIONS_H__

#include <inttypes.h>

// format conversions and helper functions
bool gralloc_is_yuv_format(int format); // e.g. HAL_PIXEL_FORMAT_YCbCr_420_888

void get_yv12_offsets(int width, int height,
                      uint32_t* yStride_out,
                      uint32_t* cStride_out,
                      uint32_t* totalSz_out);
void get_yuv420p_offsets(int width, int height,
                         uint32_t* yStride_out,
                         uint32_t* cStride_out,
                         uint32_t* totalSz_out);
signed clamp_rgb(signed value);
void rgb565_to_yv12(char* dest, char* src, int width, int height,
                    int left, int top, int right, int bottom);
void rgb888_to_yv12(char* dest, char* src, int width, int height,
                    int left, int top, int right, int bottom);
void rgb888_to_yuv420p(char* dest, char* src, int width, int height,
                       int left, int top, int right, int bottom);
void yv12_to_rgb565(char* dest, char* src, int width, int height,
                    int left, int top, int right, int bottom);
void yv12_to_rgb888(char* dest, char* src, int width, int height,
                    int left, int top, int right, int bottom);
void yuv420p_to_rgb888(char* dest, char* src, int width, int height,
                       int left, int top, int right, int bottom);
void copy_rgb_buffer_from_unlocked(char* _dst, const char* raw_data,
                                   int unlockedWidth,
                                   int width, int height, int top, int left,
                                   int bpp);
#endif
