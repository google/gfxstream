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

#include <gtest/gtest.h>

#include "GLEScmContext.h"
#include "gfxstream/host/testing/GLTestUtils.h"
#include "gfxstream/host/testing/OpenGLTestContext.h"

namespace gfxstream {
namespace gl {
namespace {

// b/233094475: GLES1 frustumf crash on cmcontext should not crash when
// core profile is not enabled.
TEST_F(GLTest, TestGlFrustumNoCoreProfile) {
    // We cannot test this with GLES2GLES because this mode sits on top of
    // GLES2 dispatcher which does not have frustumf.
    if (isGles2Gles()) {
        GTEST_SKIP();
    }
    GLEScmContext context(1, 1, nullptr, nullptr);
    context.setCoreProfile(false);
    context.frustumf(0, 0, 0, 0, 0, 0);
}

}  // namespace
}  // namespace gl
}  // namespace gfxstream