// Copyright (C) 2025 The Android Open Source Project
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

namespace gfxstream {
namespace gl {

// Tracks all the possible OpenGL ES API versions.
enum GLESApi {
    GLESApi_CM = 1,
    GLESApi_2 = 2,
    GLESApi_3_0 = 3,
    GLESApi_3_1 = 4,
    GLESApi_3_2 = 5,
};

// Used to query max GLES version support based on what the dispatch mechanism
// has found in the GLESv2 library.
// First, a enum for tracking the detected GLES version based on dispatch.
// We support 2 minimally.
enum GLESDispatchMaxVersion {
    GLES_DISPATCH_MAX_VERSION_2 = 0,
    GLES_DISPATCH_MAX_VERSION_3_0 = 1,
    GLES_DISPATCH_MAX_VERSION_3_1 = 2,
    GLES_DISPATCH_MAX_VERSION_3_2 = 3,
};

}  // namespace gl
}  // namespace gfxstream
