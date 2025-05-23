// Copyright 2009 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "GLcommon/rgtc.h"
#include <cstring>
#include <assert.h>
#include <type_traits>

// From https://www.khronos.org/registry/OpenGL/extensions/EXT/EXT_texture_compression_rgtc.txt
// according to the spec
// RGTC1_RED = BC4_UNORM,
// RGTC1_SIGNED_RED = BC4_SNORM,
// RGTC2_RG = BC5_UNORM,
// RGTC2_SIGNED_RG = BC5_SNORM.
// the full codec spec can be found here
// https://docs.microsoft.com/en-gb/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression#bc5

static constexpr int kBlockSize = 4;

inline size_t rgtc_get_block_size(RGTCImageFormat format) {
    switch (format) {
    case BC4_UNORM:
    case BC4_SNORM:
        return 8;
    case BC5_UNORM:
    case BC5_SNORM:
        return 16;
    default:
        assert(0);
        return 0;
    }
}

size_t rgtc_get_decoded_pixel_size(RGTCImageFormat format) {
    switch (format) {
        case BC4_UNORM:
        case BC4_SNORM:
            return 1;
        case BC5_UNORM:
        case BC5_SNORM:
            return 2;
        default:
            assert(0);
            return 0;
    }
}

template <typename genType>
struct get_expand_type {};

template <>
struct get_expand_type<int8_t> {
    typedef int32_t type;
};

template <>
struct get_expand_type<uint8_t> {
    typedef uint32_t type;
};

template <class T>
void rgtc_decode_subblock(const uint32_t* data, T* out, int step, T d0, T d1 ) {
    T r0 = static_cast<T>(data[0] & 0xff);
    T r1 = static_cast<T>((data[0] >> 8) & 0xff);
    uint64_t color_indexs = ((uint64_t)data[1] << 32 | data[0]) >> 16;
    T colors[8] = {r0, r1,};
    typename get_expand_type<T>::type c0 = r0;
    typename get_expand_type<T>::type c1 = r1;
    if (c0 > c1) {
        // 6 interpolated color values
        for (int i = 2; i < 8; i++) {
            colors[i] = static_cast<T>((c0 * (8 - i) + c1 * (i - 1)) / 7.0f + 0.5f);
        }
    } else {
        // 4 interpolated color values
        for (int i = 2; i < 6; i++) {
            colors[i] = static_cast<T>((c0 * (6 - i) + c1 * (i - 1)) / 5.0f + 0.5f);
        }
        colors[6] = d0;
        colors[7] = d1;
    }
    uint64_t index = color_indexs;
    for (int i = 0 ; i < 16 ; i ++) {
        *out = colors[index & 0x7];
        out += step;
        index >>= 3;
    }
}

int rgtc_decode_image(const uint8_t* in, RGTCImageFormat format, uint8_t* out, uint32_t width,
                      uint32_t height, uint32_t stride) {
    size_t data_block_size = rgtc_get_block_size(format);
    size_t texel_size = rgtc_get_decoded_pixel_size(format);
    const uint8_t* data_in = in;
    // BC5 2 bytes per pixel
    uint8_t pixels[kBlockSize * kBlockSize * 2];
    for (uint32_t y = 0; y < height; y += kBlockSize) {
        uint32_t yEnd = height - y;
        if (yEnd > kBlockSize) {
            yEnd = kBlockSize;
        }
        for (uint32_t x = 0; x < width; x += kBlockSize) {
            uint32_t xEnd = width - x;
            if (xEnd > kBlockSize) {
                xEnd = kBlockSize;
            }
            switch (format) {
            case BC4_UNORM:
                rgtc_decode_subblock<uint8_t>((const uint32_t*)data_in, pixels, 1, 0, 1);
                break;
            case BC4_SNORM:
                rgtc_decode_subblock<int8_t>((const uint32_t*)data_in, (int8_t*)pixels, 1, -1, 1);
                break;
            case BC5_UNORM:
                rgtc_decode_subblock<uint8_t>((const uint32_t*)data_in, pixels, 2, 0, 1);
                rgtc_decode_subblock<uint8_t>((const uint32_t*)data_in + 2, pixels + 1, 2, 0, 1);
                break;
            case BC5_SNORM:
                rgtc_decode_subblock<int8_t>((const uint32_t*)data_in, (int8_t*)pixels, 2, -1, 1);
                rgtc_decode_subblock<int8_t>((const uint32_t*)data_in + 2, (int8_t*)(pixels + 1),
                                             2, -1, 1);
                break;
            }
            for (uint32_t cy = 0; cy < yEnd; cy ++) {
                uint8_t* data_out = out + (y + cy) * stride + x * texel_size;
                std::memcpy(data_out, pixels + kBlockSize * texel_size * cy, texel_size * xEnd);
            }
            data_in += data_block_size;
        }
    }
    return 0;
}

size_t rgtc_get_encoded_image_size(RGTCImageFormat format, uint32_t width, uint32_t height) {
    uint32_t w = (width + kBlockSize - 1) / kBlockSize;
    uint32_t h = (height + kBlockSize - 1) / kBlockSize;
    return w * h * rgtc_get_block_size(format);
}