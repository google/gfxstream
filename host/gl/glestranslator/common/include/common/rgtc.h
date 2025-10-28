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

#pragma once

#include <stdint.h>
#include <cstddef>

enum RGTCImageFormat { BC4_UNORM, BC4_SNORM, BC5_UNORM, BC5_SNORM };

#ifdef __cplusplus
extern "C" {
#endif

// Decode an entire image.
// pIn - pointer to encoded data.
// pOut - pointer to the image data. Will be written such that
//        pixel (x,y) is at pIn + pixelSize * x + stride * y. Must be
//        large enough to store entire image.
//        (pixelSize=3 for rgb images, pixelSize=4 for images with alpha channel)
// returns non-zero if there is an error.
int rgtc_decode_image(const uint8_t* pIn, RGTCImageFormat format, uint8_t* pOut, uint32_t width,
                      uint32_t height, uint32_t stride);

size_t rgtc_get_encoded_image_size(RGTCImageFormat format, uint32_t width, uint32_t height);

size_t rgtc_get_decoded_pixel_size(RGTCImageFormat format);

#ifdef __cplusplus
}
#endif