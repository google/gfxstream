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

namespace gfxstream {

// Android system might want to allocate some color buffers with formats
// that are not compatible with most OpenGL implementations,
// such as YV12.
// In this situation, we need to convert to some OpenGL format such as
// RGB888 that actually works.
// While we can do some of this conversion in the guest gralloc driver itself
// (which would make ColorBuffer see only the OpenGL formatted pixels),
// it may be advantageous to do the conversion on the host as well.
// FrameworkFormat tracks whether the incoming color buffer from the guest
// can be directly used as a GL texture (FRAMEWORK_FORMAT_GL_COMPATIBLE).
// Otherwise, we need to know which format it is (e.g., FRAMEWORK_FORMAT_YV12)
// and convert on the host.
enum FrameworkFormat {
    FRAMEWORK_FORMAT_GL_COMPATIBLE = 0,
    FRAMEWORK_FORMAT_YV12 = 1,
    FRAMEWORK_FORMAT_YUV_420_888 = 2,
    FRAMEWORK_FORMAT_NV12 = 3,
    FRAMEWORK_FORMAT_P010 = 4,
};

}  // namespace gfxstream
