// Copyright 2022 The Android Open Source Project
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

#include "gfxstream/host/AstcCpuDecompressor.h"

namespace gfxstream {
namespace vk {
namespace {

class AstcCpuDecompressorNoOp : public AstcCpuDecompressor {
   public:
    bool available() const override { return false; }

    int32_t decompress(uint32_t imgWidth, uint32_t imgHeight, uint32_t blockWidth,
                       uint32_t blockHeight, const uint8_t* astcData, size_t astcDataLength,
                       uint8_t* output) override {
        return -1;
    };

    const char* getStatusString(int32_t statusCode) const override {
        return "ASTC CPU decomp not available";
    }
};

}  // namespace

AstcCpuDecompressor& AstcCpuDecompressor::get() {
    static AstcCpuDecompressorNoOp instance;
    return instance;
}

}  // namespace vk
}  // namespace gfxstream
