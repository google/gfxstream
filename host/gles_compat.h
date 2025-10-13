// Copyright 2024 The Android Open Source Project
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

#ifndef GLES_COMPAT_H
#define GLES_COMPAT_H

#include <cstdint>

typedef unsigned int GLenum;
typedef int32_t EGLint;
typedef unsigned int EGLNativeWindowType;

namespace gfxstream {
namespace gl {
class EmulationGl {
   public:
    EmulationGl() {}
    ~EmulationGl() {}

   private:
    uint32_t payload;
};
}  // namespace gl
}  // namespace gfxstream

#endif
