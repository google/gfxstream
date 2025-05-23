// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef ASTC_CODEC_ASTC_CODEC_H_
#define ASTC_CODEC_ASTC_CODEC_H_

namespace astc_codec {

// These are the valid ASTC footprints according to the specification in
// Section C.2.7.
enum FootprintType {
  k4x4,
  k5x4,
  k5x5,
  k6x5,
  k6x6,
  k8x5,
  k8x6,
  k10x5,
  k10x6,
  k8x8,
  k10x8,
  k10x10,
  k12x10,
  k12x12,

  kCount
};

struct AstcFootprint {
    AstcFootprint(int width, int height);
};

}  // namespace astc_codec

#endif  // ASTC_CODEC_ASTC_CODEC_H_
