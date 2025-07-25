// Copyright (C) 2024 The Android Open Source Project
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

#include "GfxstreamEnd2EndTests.h"

namespace gfxstream {
namespace tests {
namespace utils {
uint32_t getMemoryType(const vkhpp::PhysicalDevice& physicalDevice,
                       const vkhpp::MemoryRequirements& memoryRequirements,
                       vkhpp::MemoryPropertyFlags memoryProperties);

void readImageData(vkhpp::Image image, uint32_t width, uint32_t height,
                   vkhpp::ImageLayout currentLayout, void* dst, uint64_t dstSize,
                   const GfxstreamEnd2EndTest::TypicalVkTestEnvironment& testEnvironment);

std::vector<uint8_t> getRandomNByteData(size_t len);

}  // namespace utils
}  // namespace tests
}  // namespace gfxstream
