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

namespace gfxstream {
namespace vk {
namespace decompression_shaders {

// Compiled code of the image decompression shaders
//
// We use `inline constexpr` here, as recommended in
// https://abseil.io/tips/140#constants-in-header-files

inline constexpr uint32_t AstcToBc3_1D[] = {
#include "compiled/AstcToBc3_1D.inl"
};
inline constexpr uint32_t AstcToBc3_2D[] = {
#include "compiled/AstcToBc3_2D.inl"
};
inline constexpr uint32_t AstcToBc3_3D[] = {
#include "compiled/AstcToBc3_3D.inl"
};
inline constexpr uint32_t AstcToRgb_1D[] = {
#include "compiled/AstcToRgb_1D.inl"
};
inline constexpr uint32_t AstcToRgb_2D[] = {
#include "compiled/AstcToRgb_2D.inl"
};
inline constexpr uint32_t AstcToRgb_3D[] = {
#include "compiled/AstcToRgb_3D.inl"
};
inline constexpr uint32_t Astc_1D[] = {
#include "compiled/Astc_1D.inl"
};
inline constexpr uint32_t Astc_2D[] = {
#include "compiled/Astc_2D.inl"
};
inline constexpr uint32_t Astc_3D[] = {
#include "compiled/Astc_3D.inl"
};
inline constexpr uint32_t EacR11Snorm_1D[] = {
#include "compiled/EacR11Snorm_1D.inl"
};
inline constexpr uint32_t EacR11Snorm_2D[] = {
#include "compiled/EacR11Snorm_2D.inl"
};
inline constexpr uint32_t EacR11Snorm_3D[] = {
#include "compiled/EacR11Snorm_3D.inl"
};
inline constexpr uint32_t EacR11Unorm_1D[] = {
#include "compiled/EacR11Unorm_1D.inl"
};
inline constexpr uint32_t EacR11Unorm_2D[] = {
#include "compiled/EacR11Unorm_2D.inl"
};
inline constexpr uint32_t EacR11Unorm_3D[] = {
#include "compiled/EacR11Unorm_3D.inl"
};
inline constexpr uint32_t EacRG11Snorm_1D[] = {
#include "compiled/EacRG11Snorm_1D.inl"
};
inline constexpr uint32_t EacRG11Snorm_2D[] = {
#include "compiled/EacRG11Snorm_2D.inl"
};
inline constexpr uint32_t EacRG11Snorm_3D[] = {
#include "compiled/EacRG11Snorm_3D.inl"
};
inline constexpr uint32_t EacRG11Unorm_1D[] = {
#include "compiled/EacRG11Unorm_1D.inl"
};
inline constexpr uint32_t EacRG11Unorm_2D[] = {
#include "compiled/EacRG11Unorm_2D.inl"
};
inline constexpr uint32_t EacRG11Unorm_3D[] = {
#include "compiled/EacRG11Unorm_3D.inl"
};
inline constexpr uint32_t Etc2RGB8_1D[] = {
#include "compiled/Etc2RGB8_1D.inl"
};
inline constexpr uint32_t Etc2RGB8_2D[] = {
#include "compiled/Etc2RGB8_2D.inl"
};
inline constexpr uint32_t Etc2RGB8_3D[] = {
#include "compiled/Etc2RGB8_3D.inl"
};
inline constexpr uint32_t Etc2RGBA8_1D[] = {
#include "compiled/Etc2RGBA8_1D.inl"
};
inline constexpr uint32_t Etc2RGBA8_2D[] = {
#include "compiled/Etc2RGBA8_2D.inl"
};
inline constexpr uint32_t Etc2RGBA8_3D[] = {
#include "compiled/Etc2RGBA8_3D.inl"
};

}  // namespace decompression_shaders
}  // namespace vk
}  // namespace gfxstream
