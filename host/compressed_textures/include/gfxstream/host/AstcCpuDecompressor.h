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
#pragma once

#include <memory>
#include <string>

namespace gfxstream {
namespace vk {

// This class is responsible for decompressing ASTC textures on the CPU.
// This class is thread-safe and all its methods can be called by any thread.
class AstcCpuDecompressor {
   public:
    // Returns the global singleton instance of this class.
    static AstcCpuDecompressor& get();

    virtual ~AstcCpuDecompressor() = default;

    // Whether the ASTC decompressor is available. Reasons why it may not be available include:
    //   - It wasn't compiled on this platform.
    //   - The CPU doesn't support AVX2 instructions.
    // If this returns false, decompress() will fail.
    virtual bool available() const = 0;

    // Decompress an ASTC texture.
    //
    // imgWidth, imgHeight: width and height of the texture, in texels.
    // blockWidth, blockHeight: ASTC encoding block size.
    // astData: pointer to the ASTC data to decompress
    // astcDataLength: size of astData
    // output: where to white the decompressed output. This buffer must be able to hold at least
    //         imgWidth * imgHeight * 4 bytes.
    //
    // Returns 0 on success, or a non-zero status code on error. Use getStatusString() to convert
    // this status code to an error string.
    virtual int32_t decompress(uint32_t imgWidth, uint32_t imgHeight, uint32_t blockWidth,
                               uint32_t blockHeight, const uint8_t* astcData,
                               size_t astcDataLength, uint8_t* output) = 0;

    // Returns an error string for a given status code. Will always return non-null.
    virtual const char* getStatusString(int32_t statusCode) const = 0;
};

}  // namespace vk
}  // namespace gfxstream
