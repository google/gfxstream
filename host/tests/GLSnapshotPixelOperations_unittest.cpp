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
#include <array>

namespace gfxstream {
namespace gl {
namespace {

struct GlStencilFunc {
    GLenum func;
    GLint ref;
    GLuint mask;
};

struct GlStencilOp {
    GLenum sfail;
    GLenum dpfail;
    GLenum dppass;
};

struct GlBlendFunc {
    GLenum srcRGB;
    GLenum dstRGB;
    GLenum srcAlpha;
    GLenum dstAlpha;
};

struct GlWriteMask {
    GLboolean colorMaskR;
    GLboolean colorMaskG;
    GLboolean colorMaskB;
    GLboolean colorMaskA;
};

static const int kTestMaxDrawBuffers = 8;

typedef std::array<GlWriteMask, kTestMaxDrawBuffers> GlWriteMaskArray;
typedef std::array<GlBlendFunc, kTestMaxDrawBuffers> GlBlendFuncArray;

static constexpr GlWriteMaskArray kGLES2TestWriteMaski {{
          {GL_TRUE, GL_FALSE, GL_TRUE, GL_TRUE},
          {GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE},
          {GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE},
          {GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE},
          {GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE},
          {GL_TRUE, GL_FALSE, GL_FALSE, GL_TRUE},
          {GL_TRUE, GL_TRUE, GL_FALSE, GL_FALSE},
          {GL_FALSE, GL_FALSE, GL_TRUE, GL_TRUE}
}};

static constexpr GlBlendFuncArray kGLES2TestBlendFunci {{
          {GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR},
          {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA},
          {GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA},
          {GL_SRC_ALPHA_SATURATE, GL_ONE, GL_SRC_ALPHA_SATURATE, GL_ONE},
          {GL_ONE_MINUS_SRC_COLOR, GL_SRC_COLOR, GL_ONE_MINUS_DST_COLOR, GL_DST_COLOR},
          {GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, GL_ONE_MINUS_SRC_COLOR, GL_SRC_COLOR},
          {GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_ALPHA, GL_CONSTANT_ALPHA},
          {GL_ONE, GL_SRC_ALPHA_SATURATE, GL_ONE, GL_SRC_ALPHA_SATURATE},
}};

// Scissor box settings to attempt
static const std::vector<GLint> kGLES2TestScissorBox = {2, 3, 10, 20};

// Default stencil operation modes
static const GlStencilOp kGLES2DefaultStencilOp = {GL_KEEP, GL_KEEP, GL_KEEP};

// Stencil reference value to attempt
static const GLint kGLES2TestStencilRef = 1;

// Stencil mask values to attempt
static const GLuint kGLES2TestStencilMasks[] = {0, 1, 0x1000000, 0x7FFFFFFF};

// Blend function settings to attempt
static const GlBlendFunc kGLES2TestBlendFuncs[] = {
        {GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_DST_COLOR,
         GL_ONE_MINUS_DST_COLOR},
        {GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_DST_ALPHA,
         GL_ONE_MINUS_DST_ALPHA},
        {GL_CONSTANT_COLOR, GL_ONE_MINUS_CONSTANT_COLOR, GL_CONSTANT_ALPHA,
         GL_ONE_MINUS_CONSTANT_ALPHA},
        {GL_SRC_ALPHA_SATURATE, GL_ONE, GL_SRC_ALPHA_SATURATE, GL_ONE}};

class SnapshotGlScissorBoxTest
    : public SnapshotSetValueTest<std::vector<GLint>>,
      public ::testing::WithParamInterface<std::vector<GLint>> {
    void stateCheck(std::vector<GLint> expected) override {
        EXPECT_TRUE(compareGlobalGlIntv(gl, GL_SCISSOR_BOX, expected));
    }
    void stateChange() override {
        gl->glScissor(GetParam()[0], GetParam()[1], GetParam()[2],
                      GetParam()[3]);
    }
};

TEST_P(SnapshotGlScissorBoxTest, SetScissorBox) {
    // different drivers act differently; get the default scissorbox
    std::vector<GLint> defaultBox;
    defaultBox.resize(4);
    gl->glGetIntegerv(GL_SCISSOR_BOX, &defaultBox[0]);
    EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

    setExpectedValues(defaultBox, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlScissorBoxTest,
                         ::testing::Values(kGLES2TestScissorBox));

// Tests preservation of stencil test conditional state, set by glStencilFunc.
class SnapshotGlStencilConditionsTest
    : public SnapshotSetValueTest<GlStencilFunc> {
    void stateCheck(GlStencilFunc expected) override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_FUNC, expected.func));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_REF, expected.ref));
        EXPECT_TRUE(
                compareGlobalGlInt(gl, GL_STENCIL_VALUE_MASK, expected.mask));
        EXPECT_TRUE(
                compareGlobalGlInt(gl, GL_STENCIL_BACK_FUNC, expected.func));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_BACK_REF, expected.ref));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_BACK_VALUE_MASK,
                                       expected.mask));
    }
    void stateChange() override {
        GlStencilFunc sFunc = *m_changed_value;
        gl->glStencilFunc(sFunc.func, sFunc.ref, sFunc.mask);
    }
};

class SnapshotGlStencilFuncTest : public SnapshotGlStencilConditionsTest,
                                  public ::testing::WithParamInterface<GLenum> {
};

class SnapshotGlStencilMaskTest : public SnapshotGlStencilConditionsTest,
                                  public ::testing::WithParamInterface<GLuint> {
};

TEST_P(SnapshotGlStencilFuncTest, SetStencilFunc) {
    // different drivers act differently; get the default mask
    GLint defaultStencilMask;
    gl->glGetIntegerv(GL_STENCIL_VALUE_MASK, &defaultStencilMask);
    EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

    GlStencilFunc defaultStencilFunc = {GL_ALWAYS, 0,
                                        (GLuint)defaultStencilMask};
    GlStencilFunc testStencilFunc = {GetParam(), kGLES2TestStencilRef, 0};
    setExpectedValues(defaultStencilFunc, testStencilFunc);
    doCheckedSnapshot();
}

TEST_P(SnapshotGlStencilMaskTest, SetStencilMask) {
    // different drivers act differently; get the default mask
    GLint defaultStencilMask;
    gl->glGetIntegerv(GL_STENCIL_VALUE_MASK, &defaultStencilMask);
    EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

    GlStencilFunc defaultStencilFunc = {GL_ALWAYS, 0,
                                        (GLuint)defaultStencilMask};
    GlStencilFunc testStencilFunc = {GL_ALWAYS, kGLES2TestStencilRef,
                                     GetParam()};
    setExpectedValues(defaultStencilFunc, testStencilFunc);
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlStencilFuncTest,
                         ::testing::ValuesIn(kGLES2StencilFuncs));

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlStencilMaskTest,
                         ::testing::ValuesIn(kGLES2TestStencilMasks));

class SnapshotGlStencilConsequenceTest
    : public SnapshotSetValueTest<GlStencilOp> {
    void stateCheck(GlStencilOp expected) override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_FAIL, expected.sfail));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_PASS_DEPTH_FAIL,
                                       expected.dpfail));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_PASS_DEPTH_PASS,
                                       expected.dppass));
        EXPECT_TRUE(
                compareGlobalGlInt(gl, GL_STENCIL_BACK_FAIL, expected.sfail));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_BACK_PASS_DEPTH_FAIL,
                                       expected.dpfail));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_STENCIL_BACK_PASS_DEPTH_PASS,
                                       expected.dppass));
    }
    void stateChange() override {
        GlStencilOp sOp = *m_changed_value;
        gl->glStencilOp(sOp.sfail, sOp.dpfail, sOp.dppass);
    }
};

class SnapshotGlStencilFailTest : public SnapshotGlStencilConsequenceTest,
                                  public ::testing::WithParamInterface<GLenum> {
};

class SnapshotGlStencilDepthFailTest
    : public SnapshotGlStencilConsequenceTest,
      public ::testing::WithParamInterface<GLenum> {};

class SnapshotGlStencilDepthPassTest
    : public SnapshotGlStencilConsequenceTest,
      public ::testing::WithParamInterface<GLenum> {};

TEST_P(SnapshotGlStencilFailTest, SetStencilOps) {
    GlStencilOp testStencilOp = {GetParam(), GL_KEEP, GL_KEEP};
    setExpectedValues(kGLES2DefaultStencilOp, testStencilOp);
    doCheckedSnapshot();
}

TEST_P(SnapshotGlStencilDepthFailTest, SetStencilOps) {
    GlStencilOp testStencilOp = {GL_KEEP, GetParam(), GL_KEEP};
    setExpectedValues(kGLES2DefaultStencilOp, testStencilOp);
    doCheckedSnapshot();
}

TEST_P(SnapshotGlStencilDepthPassTest, SetStencilOps) {
    GlStencilOp testStencilOp = {GL_KEEP, GL_KEEP, GetParam()};
    setExpectedValues(kGLES2DefaultStencilOp, testStencilOp);
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlStencilFailTest,
                         ::testing::ValuesIn(kGLES2StencilOps));

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlStencilDepthFailTest,
                         ::testing::ValuesIn(kGLES2StencilOps));

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlStencilDepthPassTest,
                         ::testing::ValuesIn(kGLES2StencilOps));

class SnapshotGlDepthFuncTest : public SnapshotSetValueTest<GLenum>,
                                public ::testing::WithParamInterface<GLenum> {
    void stateCheck(GLenum expected) override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_DEPTH_FUNC, expected));
    }
    void stateChange() override { gl->glDepthFunc(*m_changed_value); }
};

TEST_P(SnapshotGlDepthFuncTest, SetDepthFunc) {
    setExpectedValues(GL_LESS, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlDepthFuncTest,
                         ::testing::ValuesIn(kGLES2StencilFuncs));

class SnapshotGlBlendEquationTest
    : public SnapshotSetValueTest<GLenum>,
      public ::testing::WithParamInterface<GLenum> {
    void stateCheck(GLenum expected) override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_BLEND_EQUATION_RGB, expected));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_BLEND_EQUATION_ALPHA, expected));
    }
    void stateChange() override { gl->glBlendEquation(*m_changed_value); }
};

TEST_P(SnapshotGlBlendEquationTest, SetBlendEquation) {
    setExpectedValues(GL_FUNC_ADD, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlBlendEquationTest,
                         ::testing::ValuesIn(kGLES2BlendEquations));

class SnapshotGlBlendFuncTest
    : public SnapshotSetValueTest<GlBlendFunc>,
      public ::testing::WithParamInterface<GlBlendFunc> {
    void stateCheck(GlBlendFunc expected) {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_BLEND_SRC_RGB, expected.srcRGB));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_BLEND_DST_RGB, expected.dstRGB));
        EXPECT_TRUE(
                compareGlobalGlInt(gl, GL_BLEND_SRC_ALPHA, expected.srcAlpha));
        EXPECT_TRUE(
                compareGlobalGlInt(gl, GL_BLEND_DST_ALPHA, expected.dstAlpha));
    }
    void stateChange() {
        GlBlendFunc target = *m_changed_value;
        gl->glBlendFuncSeparate(target.srcRGB, target.dstRGB, target.srcAlpha,
                                target.dstAlpha);
    }
};

TEST_P(SnapshotGlBlendFuncTest, SetBlendFunc) {
    GlBlendFunc defaultBlendFunc = {.srcRGB = GL_ONE,
                                    .dstRGB = GL_ZERO,
                                    .srcAlpha = GL_ONE,
                                    .dstAlpha = GL_ZERO};
    setExpectedValues(defaultBlendFunc, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps,
                         SnapshotGlBlendFuncTest,
                         ::testing::ValuesIn(kGLES2TestBlendFuncs));

class SnapshotGlWriteMaskiTest
    : public SnapshotSetValueTest<GlWriteMaskArray>,
      public ::testing::WithParamInterface<GlWriteMaskArray> {

    void stateCheck(GlWriteMaskArray expected) {
        for(int i = 0 ; i < m_maxDrawBuffers ; ++i ) {
           std::vector<GLboolean> res{expected[i].colorMaskR, expected[i].colorMaskG, expected[i].colorMaskB, expected[i].colorMaskA};
           EXPECT_TRUE(compareGlobalGlBooleanv_i(gl, GL_COLOR_WRITEMASK, i, res, 4));
        }
    }

    void stateChange() {
        auto& writeMasks = *m_changed_value;
        for(int i = 0 ; i < m_maxDrawBuffers ; ++i)
          gl->glColorMaskiEXT(i,
                              writeMasks[i].colorMaskR,
                              writeMasks[i].colorMaskG,
                              writeMasks[i].colorMaskB,
                              writeMasks[i].colorMaskA);
    }

  protected:
    GLint m_maxDrawBuffers = 1;
};

TEST_P(SnapshotGlWriteMaskiTest, ColorMaski) {
    const std::string ext((const char*)gl->glGetString(GL_EXTENSIONS));
    EMUGL_SKIP_TEST_IF(ext.find("GL_EXT_draw_buffers_indexed") == std::string::npos);
    gl->glGetIntegerv(GL_MAX_DRAW_BUFFERS, &m_maxDrawBuffers);

    const GlWriteMaskArray defaultWriteMask { {
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
        {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE},
    } };
    setExpectedValues(defaultWriteMask, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps, SnapshotGlWriteMaskiTest,
                        ::testing::Values(kGLES2TestWriteMaski));

// test glBlendFunci load and restore
class SnapshotGlBlendFunciTest
    : public SnapshotSetValueTest<GlBlendFuncArray>,
      public ::testing::WithParamInterface<GlBlendFuncArray> {

    void stateCheck(GlBlendFuncArray expected) {
        for(int i = 0 ; i < m_maxDrawBuffers ; ++i ) {
           EXPECT_TRUE(compareGlobalGlInt_i(gl, GL_BLEND_SRC_RGB, i, expected[i].srcRGB));
           EXPECT_TRUE(compareGlobalGlInt_i(gl, GL_BLEND_DST_RGB, i, expected[i].dstRGB));
           EXPECT_TRUE(compareGlobalGlInt_i(gl, GL_BLEND_SRC_ALPHA, i, expected[i].srcAlpha));
           EXPECT_TRUE(compareGlobalGlInt_i(gl, GL_BLEND_DST_ALPHA, i, expected[i].dstAlpha));
        }
    }

    void stateChange() {
        auto& blendFuncs = *m_changed_value;
        for(int i = 0 ; i < m_maxDrawBuffers ; ++i) {
            gl->glBlendFuncSeparateiEXT(i, blendFuncs[i].srcRGB, blendFuncs[i].dstRGB, blendFuncs[i].srcAlpha,
                                blendFuncs[i].dstAlpha);
        }
    }

  protected:
    GLint m_maxDrawBuffers = 1;
};

TEST_P(SnapshotGlBlendFunciTest, BlendFunci) {
    const std::string ext((const char*)gl->glGetString(GL_EXTENSIONS));
    EMUGL_SKIP_TEST_IF(ext.find("GL_EXT_draw_buffers_indexed") == std::string::npos);
    gl->glGetIntegerv(GL_MAX_DRAW_BUFFERS, &m_maxDrawBuffers);

    const GlBlendFuncArray defaultBlendFunc { {
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
        {GL_ONE, GL_ZERO, GL_ONE, GL_ZERO},
    } };
    setExpectedValues(defaultBlendFunc, GetParam());
    doCheckedSnapshot();
}

INSTANTIATE_TEST_SUITE_P(GLES2SnapshotPixelOps, SnapshotGlBlendFunciTest,
                        ::testing::Values(kGLES2TestBlendFunci));

}  // namespace
}  // namespace gl
}  // namespace gfxstream
