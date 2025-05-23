// Copyright (C) 2022 The Android Open Source Project
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

#include "gfxstream/ImageUtils.h"

#include <stb/stb_image.h>
#include <stb/stb_image_write.h>

#include <cstring>

namespace gfxstream {

bool LoadRGBAFromPng(const std::string& filename, uint32_t* outWidth, uint32_t* outHeight,
                     std::vector<uint32_t>* outPixels) {
    *outWidth = 0;
    *outHeight = 0;
    outPixels->clear();

    int decodedWidth;
    int decodedHeight;
    int decodedChannels;
    unsigned char* decodedPixels =
        stbi_load(filename.c_str(), &decodedWidth, &decodedHeight, &decodedChannels,
                  /*desiredChannels=*/4);
    if (decodedPixels == nullptr) {
        return false;
    }

    const std::size_t decodedSize = decodedWidth * decodedHeight;
    const std::size_t decodedSizeBytes = decodedSize * 4;

    *outWidth = static_cast<uint32_t>(decodedWidth);
    *outHeight = static_cast<uint32_t>(decodedHeight);
    outPixels->resize(decodedSize, 0);
    std::memcpy(outPixels->data(), decodedPixels, decodedSizeBytes);

    stbi_image_free(decodedPixels);

    return true;
}

bool SaveRGBAToPng(uint32_t width, uint32_t height, const uint32_t* pixels,
                   const std::string& filename) {
    if (stbi_write_png(filename.c_str(), width, height,
                       /*channels=*/4, pixels, width * 4) == 0) {
        return false;
    }
    return true;
}

}  // namespace gfxstream
