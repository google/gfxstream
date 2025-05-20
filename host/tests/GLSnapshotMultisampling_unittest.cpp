// Copyright (C) 2018 The Android Open Source Project
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

#include "gfxstream/host/testing/GLSnapshotTesting.h"
#include "gfxstream/host/testing/OpenGLTestContext.h"

#include <gtest/gtest.h>

namespace gfxstream {
namespace gl {
namespace {

struct GlSampleCoverage {
    GLclampf value;
    GLboolean invert;
};

// Sample coverage settings to attempt
static const GlSampleCoverage kGLES2TestSampleCoverages[] = {{0.3, true},
                                                             {0, false}};

class SnapshotGlSampleCoverageTest
    : public SnapshotSetValueTest<GlSampleCoverage>,
      public ::testing::WithParamInterface<GlSampleCoverage> {
    void stateCheck(GlSampleCoverage expected) {
        EXPECT_TRUE(compareGlobalGlFloat(gl, GL_SAMPLE_COVERAGE_VALUE,
                                         expected.value));
        EXPECT_TRUE(compareGlobalGlBoolean(gl, GL_SAMPLE_COVERAGE_INVERT,
                                           expected.invert));
    }
    void stateChange() {
        gl->glSampleCoverage(GetParam().value, GetParam().invert);
    }
};

TEST_P(SnapshotGlSampleCoverageTest, SetSampleCoverage) {
    GlSampleCoverage defaultCoverage = {1.0f, false};
    setExpectedValues(defaultCoverage, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotMultisampling,
                         SnapshotGlSampleCoverageTest,
                         ::testing::ValuesIn(kGLES2TestSampleCoverages));

}  // namespace
}  // namespace gl
}  // namespace gfxstream
