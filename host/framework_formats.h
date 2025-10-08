// Copyright 2025 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either expresso or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <optional>

namespace gfxstream {
namespace host {

// Deprecated, do not use anymore!
enum FrameworkFormat {
    FRAMEWORK_FORMAT_GL_COMPATIBLE = 0,
    FRAMEWORK_FORMAT_YV12 = 1,
    FRAMEWORK_FORMAT_YUV_420_888 = 2,
    FRAMEWORK_FORMAT_NV12 = 3,
    FRAMEWORK_FORMAT_P010 = 4,
};

}  // namespace host
}  // namespace gfxstream
