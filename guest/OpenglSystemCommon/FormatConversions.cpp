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

#include "FormatConversions.h"

#include <string.h>
#include <stdio.h>

#include <hardware/gralloc.h>

#include "gfxstream/common/logging.h"

#define DEBUG 0
#if DEBUG
#define DD(...) GFXSTREAM_DEBUG(__VA_ARGS__)
#else
#define DD(...)
#endif

static int get_rgb_offset(int row, int width, int rgbStride) {
    return row * width * rgbStride;
}

#define OMX_COLOR_FormatYUV420Planar 0x13

bool gralloc_is_yuv_format(const int format) {
    switch (format) {
    case HAL_PIXEL_FORMAT_YV12:
    case HAL_PIXEL_FORMAT_YCbCr_420_888:
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
    case OMX_COLOR_FormatYUV420Planar:
        return true;

    default:
        return false;
    }
}

void get_yv12_offsets(int width, int height,
                             uint32_t* yStride_out,
                             uint32_t* cStride_out,
                             uint32_t* totalSz_out) {
    uint32_t align = 16;
    uint32_t yStride = (width + (align - 1)) & ~(align-1);
    uint32_t uvStride = (yStride / 2 + (align - 1)) & ~(align-1);
    uint32_t uvHeight = height / 2;
    uint32_t sz = yStride * height + 2 * (uvHeight * uvStride);

    if (yStride_out) *yStride_out = yStride;
    if (cStride_out) *cStride_out = uvStride;
    if (totalSz_out) *totalSz_out = sz;
}

void get_yuv420p_offsets(int width, int height,
                                uint32_t* yStride_out,
                                uint32_t* cStride_out,
                                uint32_t* totalSz_out) {
    uint32_t align = 1;
    uint32_t yStride = (width + (align - 1)) & ~(align-1);
    uint32_t uvStride = (yStride / 2 + (align - 1)) & ~(align-1);
    uint32_t uvHeight = height / 2;
    uint32_t sz = yStride * height + 2 * (uvHeight * uvStride);

    if (yStride_out) *yStride_out = yStride;
    if (cStride_out) *cStride_out = uvStride;
    if (totalSz_out) *totalSz_out = sz;
}

signed clamp_rgb(signed value) {
    if (value > 255) {
        value = 255;
    } else if (value < 0) {
        value = 0;
    }
    return value;
}

void rgb565_to_yv12(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 2;

    int align = 16;
    int yStride = (width + (align -1)) & ~(align-1);
    int cStride = (yStride / 2 + (align - 1)) & ~(align-1);
    int cSize = cStride * height/2;

    uint16_t *rgb_ptr0 = (uint16_t *)src;
    uint8_t *yv12_y0 = (uint8_t *)dest;
    uint8_t *yv12_v0 = yv12_y0 + yStride * height;

    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *yv12_v = yv12_v0 + (j/2) * cStride;
        uint8_t *yv12_u = yv12_v + cSize;
        uint16_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j, width, rgb_stride) / 2;
        bool jeven = (j & 1) == 0;
        for (int i = left; i <= right; ++i) {
            uint8_t r = ((rgb_ptr[i]) >> 11) & 0x01f;
            uint8_t g = ((rgb_ptr[i]) >> 5) & 0x03f;
            uint8_t b = (rgb_ptr[i]) & 0x01f;
            // convert to 8bits
            // http://stackoverflow.com/questions/2442576/how-does-one-convert-16-bit-rgb565-to-24-bit-rgb888
            uint8_t R = (r * 527 + 23) >> 6;
            uint8_t G = (g * 259 + 33) >> 6;
            uint8_t B = (b * 527 + 23) >> 6;
            // convert to YV12
            // frameworks/base/core/jni/android_hardware_camera2_legacy_LegacyCameraDevice.cpp
            yv12_y[i] = clamp_rgb((77 * R + 150 * G +  29 * B) >> 8);
            bool ieven = (i & 1) == 0;
            if (jeven && ieven) {
                yv12_u[i] = clamp_rgb((( -43 * R - 85 * G + 128 * B) >> 8) + 128);
                yv12_v[i] = clamp_rgb((( 128 * R - 107 * G - 21 * B) >> 8) + 128);
            }
        }
    }
}

void rgb888_to_yv12(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 3;

    DD("%s convert %d by %d", __func__, width, height);
    int align = 16;
    int yStride = (width + (align -1)) & ~(align-1);
    int cStride = (yStride / 2 + (align - 1)) & ~(align-1);
    int cSize = cStride * height/2;


    uint8_t *rgb_ptr0 = (uint8_t *)src;
    uint8_t *yv12_y0 = (uint8_t *)dest;
    uint8_t *yv12_u0 = yv12_y0 + yStride * height + cSize;
    uint8_t *yv12_v0 = yv12_y0 + yStride * height;

#if DEBUG
    char mybuf[1024];
    snprintf(mybuf, sizeof(mybuf), "/sdcard/raw_%d_%d_rgb.ppm", width, height);
    FILE *myfp = fopen(mybuf, "wb"); /* b - binary mode */
    (void) fprintf(myfp, "P6\n%d %d\n255\n", width, height);

    if (myfp == NULL) {
        DD("failed to open /sdcard/raw_rgb888.ppm");
    } else {
        fwrite(rgb_ptr0, width * height * rgb_stride, 1, myfp);
        fclose(myfp);
    }
#endif

    int uvcount = 0;
    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j, width, rgb_stride);
        bool jeven = (j & 1) == 0;
        for (int i = left; i <= right; ++i) {
            uint8_t R = rgb_ptr[i*rgb_stride];
            uint8_t G = rgb_ptr[i*rgb_stride+1];
            uint8_t B = rgb_ptr[i*rgb_stride+2];
            // convert to YV12
            // https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion
            // but scale up U by 1/0.96
            yv12_y[i] = clamp_rgb(1.0 * ((0.25678823529411765 * R) + (0.5041294117647058 * G) + (0.09790588235294118 * B)) + 16);
            bool ieven = (i & 1) == 0;
            if (jeven && ieven) {
                yv12_u0[uvcount] = clamp_rgb((1/0.96) * (-(0.1482235294117647 * R) - (0.2909921568627451 * G) + (0.4392156862745098 * B)) + 128);
                yv12_v0[uvcount] = clamp_rgb((1.0)* ((0.4392156862745098 * R) - (0.36778823529411764 * G) - (0.07142745098039215 * B)) + 128);
                uvcount ++;
            }
        }
        if (jeven) {
            yv12_u0 += cStride;
            yv12_v0 += cStride;
            uvcount = 0;
        }
    }

#if DEBUG
    snprintf(mybuf, sizeof(mybuf), "/sdcard/raw_%d_%d_yv12.yuv", width, height);
    FILE *yuvfp = fopen(mybuf, "wb"); /* b - binary mode */
    if (yuvfp != NULL) {
        fwrite(yv12_y0, yStride * height + 2 * cSize, 1, yuvfp);
        fclose(yuvfp);
    }
#endif

}

void rgb888_to_yuv420p(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 3;

    DD("%s convert %d by %d", __func__, width, height);
    int yStride = width;
    int cStride = yStride / 2;
    int cSize = cStride * height/2;

    uint8_t *rgb_ptr0 = (uint8_t *)src;
    uint8_t *yv12_y0 = (uint8_t *)dest;
    uint8_t *yv12_u0 = yv12_y0 + yStride * height;

    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *yv12_u = yv12_u0 + (j/2) * cStride;
        uint8_t *yv12_v = yv12_u + cSize;
        uint8_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j, width, rgb_stride);
        bool jeven = (j & 1) == 0;
        for (int i = left; i <= right; ++i) {
            uint8_t R = rgb_ptr[i*rgb_stride];
            uint8_t G = rgb_ptr[i*rgb_stride+1];
            uint8_t B = rgb_ptr[i*rgb_stride+2];
            // convert to YV12
            // frameworks/base/core/jni/android_hardware_camera2_legacy_LegacyCameraDevice.cpp
            yv12_y[i] = clamp_rgb((77 * R + 150 * G +  29 * B) >> 8);
            bool ieven = (i & 1) == 0;
            if (jeven && ieven) {
                yv12_u[i] = clamp_rgb((( -43 * R - 85 * G + 128 * B) >> 8) + 128);
                yv12_v[i] = clamp_rgb((( 128 * R - 107 * G - 21 * B) >> 8) + 128);
            }
        }
    }
}

// YV12 is aka YUV420Planar, or YUV420p; the only difference is that YV12 has
// certain stride requirements for Y and UV respectively.
void yv12_to_rgb565(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 2;

    DD("%s convert %d by %d", __func__, width, height);
    int align = 16;
    int yStride = (width + (align -1)) & ~(align-1);
    int cStride = (yStride / 2 + (align - 1)) & ~(align-1);
    int cSize = cStride * height/2;

    uint16_t *rgb_ptr0 = (uint16_t *)dest;
    uint8_t *yv12_y0 = (uint8_t *)src;
    uint8_t *yv12_v0 = yv12_y0 + yStride * height;

    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *yv12_v = yv12_v0 + (j/2) * cStride;
        uint8_t *yv12_u = yv12_v + cSize;
        uint16_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j, width, rgb_stride);
        for (int i = left; i <= right; ++i) {
            // convert to rgb
            // frameworks/av/media/libstagefright/colorconversion/ColorConverter.cpp
            signed y1 = (signed)yv12_y[i] - 16;
            signed u = (signed)yv12_u[i / 2] - 128;
            signed v = (signed)yv12_v[i / 2] - 128;

            signed u_b = u * 517;
            signed u_g = -u * 100;
            signed v_g = -v * 208;
            signed v_r = v * 409;

            signed tmp1 = y1 * 298;
            signed b1 = clamp_rgb((tmp1 + u_b) / 256);
            signed g1 = clamp_rgb((tmp1 + v_g + u_g) / 256);
            signed r1 = clamp_rgb((tmp1 + v_r) / 256);

            uint16_t rgb1 = ((r1 >> 3) << 11) | ((g1 >> 2) << 5) | (b1 >> 3);

            rgb_ptr[i-left] = rgb1;
        }
    }
}

// YV12 is aka YUV420Planar, or YUV420p; the only difference is that YV12 has
// certain stride requirements for Y and UV respectively.
void yv12_to_rgb888(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 3;

    DD("%s convert %d by %d", __func__, width, height);
    int align = 16;
    int yStride = (width + (align -1)) & ~(align-1);
    int cStride = (yStride / 2 + (align - 1)) & ~(align-1);
    int cSize = cStride * height/2;

    uint8_t *rgb_ptr0 = (uint8_t *)dest;
    uint8_t *yv12_y0 = (uint8_t *)src;
    uint8_t *yv12_v0 = yv12_y0 + yStride * height;

    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *yv12_v = yv12_v0 + (j/2) * cStride;
        uint8_t *yv12_u = yv12_v + cSize;
        uint8_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j - top, right - left + 1, rgb_stride);
        for (int i = left; i <= right; ++i) {
            // convert to rgb
            // https://en.wikipedia.org/wiki/YCbCr#ITU-R_BT.601_conversion
            // but scale down U by 0.96 to mitigate rgb over/under flow
            signed y1 = (signed)yv12_y[i] - 16;
            signed u = (signed)yv12_u[i / 2] - 128;
            signed v = (signed)yv12_v[i / 2] - 128;

            signed r1 = clamp_rgb(1 * (1.1643835616438356 * y1 + 1.5960267857142856 * v));
            signed g1 = clamp_rgb(1 * (1.1643835616438356 * y1 - 0.39176229009491365 * u * 0.97  - 0.8129676472377708 * v));
            signed b1 = clamp_rgb(1 * (1.1643835616438356 * y1 + 2.017232142857143 * u * 0.97));

            rgb_ptr[(i-left)*rgb_stride] = r1;
            rgb_ptr[(i-left)*rgb_stride+1] = g1;
            rgb_ptr[(i-left)*rgb_stride+2] = b1;
        }
    }
}

// YV12 is aka YUV420Planar, or YUV420p; the only difference is that YV12 has
// certain stride requirements for Y and UV respectively.
void yuv420p_to_rgb888(char* dest, char* src, int width, int height,
        int left, int top, int right, int bottom) {
    const int rgb_stride = 3;

    DD("%s convert %d by %d", __func__, width, height);
    int yStride = width;
    int cStride = yStride / 2;
    int cSize = cStride * height/2;

    uint8_t *rgb_ptr0 = (uint8_t *)dest;
    uint8_t *yv12_y0 = (uint8_t *)src;
    uint8_t *yv12_u0 = yv12_y0 + yStride * height;

    for (int j = top; j <= bottom; ++j) {
        uint8_t *yv12_y = yv12_y0 + j * yStride;
        uint8_t *yv12_u = yv12_u0 + (j/2) * cStride;
        uint8_t *yv12_v = yv12_u + cSize;
        uint8_t *rgb_ptr = rgb_ptr0 + get_rgb_offset(j - top, right - left + 1, rgb_stride);
        for (int i = left; i <= right; ++i) {
            // convert to rgb
            // frameworks/av/media/libstagefright/colorconversion/ColorConverter.cpp
            signed y1 = (signed)yv12_y[i] - 16;
            signed u = (signed)yv12_u[i / 2] - 128;
            signed v = (signed)yv12_v[i / 2] - 128;

            signed u_b = u * 517;
            signed u_g = -u * 100;
            signed v_g = -v * 208;
            signed v_r = v * 409;

            signed tmp1 = y1 * 298;
            signed b1 = clamp_rgb((tmp1 + u_b) / 256);
            signed g1 = clamp_rgb((tmp1 + v_g + u_g) / 256);
            signed r1 = clamp_rgb((tmp1 + v_r) / 256);

            rgb_ptr[(i-left)*rgb_stride] = r1;
            rgb_ptr[(i-left)*rgb_stride+1] = g1;
            rgb_ptr[(i-left)*rgb_stride+2] = b1;
        }
    }
}

void copy_rgb_buffer_from_unlocked(
        char* dst, const char* raw_data,
        int unlockedWidth,
        int width, int height, int top, int left,
        int bpp) {
    int dst_line_len = width * bpp;
    int src_line_len = unlockedWidth * bpp;
    const char *src = raw_data + top*src_line_len + left*bpp;
    for (int y = 0; y < height; y++) {
        memcpy(dst, src, dst_line_len);
        src += src_line_len;
        dst += dst_line_len;
    }
}
