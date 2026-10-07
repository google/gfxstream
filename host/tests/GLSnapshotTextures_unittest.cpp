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

#include "gfxstream/host/testing/GLSnapshotTestStateUtils.h"
#include "gfxstream/host/testing/GLSnapshotTesting.h"
#include "gfxstream/host/glUtils.h"

#include <GLES2/gl2ext.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <utility>

namespace gfxstream {
namespace host {
namespace gl {
namespace {

struct GlTextureUnitState {
    GLuint binding2D;
    GLuint bindingCubeMap;
};

struct GlTextureImageState {
    GLenum format;
    GLenum type;
    GLsizei width;
    GLsizei height;

    GLboolean isCompressed;
    GLsizei compressedSize;

    std::vector<GLubyte> bytes;
};

using GlMipmapArray = std::vector<GlTextureImageState>;

struct GlTextureObjectState {
    GLenum minFilter;
    GLenum magFilter;
    GLenum wrapS;
    GLenum wrapT;

    GLenum target;
    GlMipmapArray images2D;
    std::vector<GlMipmapArray> imagesCubeMap;
};

static const GLenum kGLES2TextureCubeMapSides[] = {
        GL_TEXTURE_CUBE_MAP_POSITIVE_X, GL_TEXTURE_CUBE_MAP_NEGATIVE_X,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Y, GL_TEXTURE_CUBE_MAP_NEGATIVE_Y,
        GL_TEXTURE_CUBE_MAP_POSITIVE_Z, GL_TEXTURE_CUBE_MAP_NEGATIVE_Z,
};

static const GlMipmapArray kGLES2TestTexture2D = {{GL_RGBA,
                                                   GL_UNSIGNED_BYTE,
                                                   4,
                                                   4,
                                                   false,
                                                   0,
                                                   {
                                                           0x11,
                                                           0x22,
                                                           0x33,
                                                           0x44,
                                                           0x55,
                                                           0x66,
                                                           0x77,
                                                           0x88,
                                                           0x99,
                                                           0xaa,
                                                           0xbb,
                                                           0xcc,
                                                           0xdd,
                                                           0xee,
                                                           0xff,
                                                           0x00,
                                                   }},
                                                  {GL_RGBA,
                                                   GL_UNSIGNED_SHORT_4_4_4_4,
                                                   2,
                                                   2,
                                                   false,
                                                   0,
                                                   {
                                                           0x51,
                                                           0x52,
                                                           0x53,
                                                           0x54,
                                                   }},
                                                  {GL_RGBA,
                                                   GL_UNSIGNED_SHORT_5_5_5_1,
                                                   1,
                                                   1,
                                                   false,
                                                   0,
                                                   {
                                                           0xab,
                                                   }}};

static const std::vector<GlMipmapArray> kGLES2TestTextureCubeMap = {
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x11,
                  0x12,
                  0x13,
                  0x14,
          }}},
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x21,
                  0x22,
                  0x23,
                  0x24,
          }}},
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x31,
                  0x32,
                  0x33,
                  0x34,
          }}},
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x41,
                  0x42,
                  0x43,
                  0x44,
          }}},
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x51,
                  0x52,
                  0x53,
                  0x54,
          }}},
        {{GL_RGBA,
          GL_UNSIGNED_BYTE,
          2,
          2,
          false,
          0,
          {
                  0x61,
                  0x62,
                  0x63,
                  0x64,
          }}},
};

class SnapshotGlTextureUnitActiveTest : public SnapshotPreserveTest {
public:
    void defaultStateCheck() override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_ACTIVE_TEXTURE, GL_TEXTURE0));
    }

    void changedStateCheck() override {
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_ACTIVE_TEXTURE,
                                       GL_TEXTURE0 + m_active_texture_unit));
    }

    void stateChange() override {
        gl->glActiveTexture(GL_TEXTURE0 + m_active_texture_unit);
    }

    void useTextureUnit(GLuint unit) {
        GLint maxTextureUnits;
        gl->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                          &maxTextureUnits);
        EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

        if (unit < static_cast<GLuint>(maxTextureUnits)) {
            m_active_texture_unit = unit;
        } else {
            fprintf(stderr,
                    "Tried to use texture unit %d when max unit was %d."
                    " Defaulting to unit 0.\n",
                    unit, maxTextureUnits);
            m_active_texture_unit = 0;
        }
    }

protected:
    GLuint m_active_texture_unit;
};

TEST_F(SnapshotGlTextureUnitActiveTest, ActiveTextureUnit) {
    useTextureUnit(1);
    doCheckedSnapshot();
}

class SnapshotGlTextureUnitBindingsTest : public SnapshotPreserveTest {
public:
    void defaultStateCheck() override {
        GLint maxTextureUnits;
        gl->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                          &maxTextureUnits);
        for (int i = 0; i < maxTextureUnits; i++) {
            gl->glActiveTexture(GL_TEXTURE0 + i);
            EXPECT_TRUE(compareGlobalGlInt(gl, GL_TEXTURE_BINDING_2D, 0));
            EXPECT_TRUE(compareGlobalGlInt(gl, GL_TEXTURE_BINDING_CUBE_MAP, 0));
        }
    }

    void changedStateCheck() override {
        GLint maxTextureUnits;
        gl->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                          &maxTextureUnits);
        EXPECT_EQ(m_unit_states.size(), maxTextureUnits);

        for (int i = 0; i < maxTextureUnits; i++) {
            gl->glActiveTexture(GL_TEXTURE0 + i);
            EXPECT_TRUE(compareGlobalGlInt(gl, GL_TEXTURE_BINDING_2D,
                                           m_unit_states[i].binding2D));
            EXPECT_TRUE(compareGlobalGlInt(gl, GL_TEXTURE_BINDING_CUBE_MAP,
                                           m_unit_states[i].bindingCubeMap));
        }
    }

    void stateChange() override {
        GLint maxTextureUnits;
        gl->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                          &maxTextureUnits);
        m_unit_states.resize(maxTextureUnits);

        m_state_changer();
    }

    void setStateChanger(std::function<void()> changer) {
        m_state_changer = changer;
    }

protected:
    // Create a texture object, bind to texture unit |unit| at binding point
    // |bindPoint|, and record that we've done so.
    GLuint createAndBindTexture(GLuint unit, GLenum bindPoint) {
        GLint maxTextureUnits;
        gl->glGetIntegerv(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS,
                          &maxTextureUnits);
        if (unit >= static_cast<GLuint>(maxTextureUnits)) {
            fprintf(stderr,
                    "Cannot bind to unit %d: max units is %d. Binding to %d "
                    "instead.\n",
                    unit, maxTextureUnits, maxTextureUnits - 1);
            unit = static_cast<GLuint>(maxTextureUnits) - 1;
        }

        GLuint testTexture;
        gl->glGenTextures(1, &testTexture);
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(bindPoint, testTexture);
        EXPECT_EQ(GL_NO_ERROR, gl->glGetError());
        switch (bindPoint) {
            case GL_TEXTURE_2D:
                m_unit_states[unit].binding2D = testTexture;
                break;
            case GL_TEXTURE_CUBE_MAP:
                m_unit_states[unit].bindingCubeMap = testTexture;
                break;
            default:
                ADD_FAILURE() << "Unsupported texture unit bind point " +
                                         describeGlEnum(bindPoint);
        }
        return testTexture;
    }

    std::vector<GlTextureUnitState> m_unit_states;
    std::function<void()> m_state_changer = [] {};
};

TEST_F(SnapshotGlTextureUnitBindingsTest, BindTextures) {
    setStateChanger([this] {
        createAndBindTexture(1, GL_TEXTURE_2D);
        createAndBindTexture(8, GL_TEXTURE_CUBE_MAP);
        createAndBindTexture(16, GL_TEXTURE_2D);
        createAndBindTexture(32, GL_TEXTURE_CUBE_MAP);
    });
    doCheckedSnapshot();
}

class SnapshotGlTextureObjectTest : public SnapshotPreserveTest {
public:
    void defaultStateCheck() override {
        EXPECT_EQ(GL_FALSE, gl->glIsTexture(m_object_name));
    }

    void changedStateCheck() override {
        SCOPED_TRACE("Texture object " + std::to_string(m_object_name) +
                     ", target " + describeGlEnum(m_state.target));
        EXPECT_EQ(GL_TRUE, gl->glIsTexture(m_object_name));

        EXPECT_TRUE(compareGlobalGlInt(gl, GL_ACTIVE_TEXTURE, GL_TEXTURE0));
        EXPECT_TRUE(compareGlobalGlInt(gl, getTargetBindingName(m_state.target),
                                       m_object_name));

        EXPECT_TRUE(compareParameter(GL_TEXTURE_MIN_FILTER, m_state.minFilter));
        EXPECT_TRUE(compareParameter(GL_TEXTURE_MAG_FILTER, m_state.magFilter));
        EXPECT_TRUE(compareParameter(GL_TEXTURE_WRAP_S, m_state.wrapS));
        EXPECT_TRUE(compareParameter(GL_TEXTURE_WRAP_T, m_state.wrapT));

        auto compareImageFunc = [this](GLenum imageTarget, GlMipmapArray& levels) {
            for (size_t i = 0; i < levels.size(); i++) {
                EXPECT_TRUE(compareVector<GLubyte>(
                        levels[i].bytes,
                        getTextureImageData(gl, m_object_name, imageTarget, i,
                                            levels[i].width, levels[i].height,
                                            levels[i].format, levels[i].type),
                        "mipmap level " + std::to_string(i)));
                EXPECT_EQ(GL_NO_ERROR, gl->glGetError());
            }
        };

        switch (m_state.target) {
            case GL_TEXTURE_2D: {
                compareImageFunc(m_state.target, m_state.images2D);
            } break;
            case GL_TEXTURE_CUBE_MAP: {
                if (m_state.imagesCubeMap.size() > 6) {
                    ADD_FAILURE() << "Test texture cube map had "
                                  << m_state.imagesCubeMap.size()
                                  << " 'sides' of data.";
                    break;
                }
                for (size_t j = 0; j < m_state.imagesCubeMap.size(); j++) {
                    compareImageFunc(kGLES2TextureCubeMapSides[j],
                                     m_state.imagesCubeMap[j]);
                }
            } break;
            default:
                ADD_FAILURE()
                        << "Unsupported texture target " << m_state.target;
                break;
        }
    }

    void stateChange() override {
        gl->glGenTextures(1, &m_object_name);

        // Bind to texture unit TEXTURE0 for test simplicity
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(m_state.target, m_object_name);

        // Set texture sample parameters
        gl->glTexParameteri(m_state.target, GL_TEXTURE_MIN_FILTER,
                            m_state.minFilter);
        gl->glTexParameteri(m_state.target, GL_TEXTURE_MAG_FILTER,
                            m_state.magFilter);
        gl->glTexParameteri(m_state.target, GL_TEXTURE_WRAP_S, m_state.wrapS);
        gl->glTexParameteri(m_state.target, GL_TEXTURE_WRAP_T, m_state.wrapT);

        auto initImageFunc = [this](GLenum imageTarget, GlMipmapArray& levels) {
            for (size_t i = 0; i < levels.size(); i++) {
                levels[i].bytes.resize(
                        levels[i].width * levels[i].height *
                        glUtilsPixelBitSize(
                                levels[i].format,
                                GL_UNSIGNED_BYTE /* levels[i].type */) / 8);
                gl->glTexImage2D(imageTarget, i, levels[i].format,
                                 levels[i].width, levels[i].height, 0,
                                 levels[i].format,
                                 GL_UNSIGNED_BYTE /* levels[i].type */,
                                 levels[i].bytes.data());
            }
        };

        switch (m_state.target) {
            case GL_TEXTURE_2D: {
                initImageFunc(m_state.target, m_state.images2D);
            } break;
            case GL_TEXTURE_CUBE_MAP: {
                if (m_state.imagesCubeMap.size() > 6) {
                    ADD_FAILURE() << "Test texture cube map had "
                                  << m_state.imagesCubeMap.size()
                                  << " 'sides' of data.";
                    break;
                }
                for (size_t j = 0; j < m_state.imagesCubeMap.size(); j++) {
                    GLenum side = kGLES2TextureCubeMapSides[j];
                    initImageFunc(side, m_state.imagesCubeMap[j]);
                }
            } break;
            default:
                ADD_FAILURE()
                        << "Unsupported texture target " << m_state.target;
                break;
        }
    }

protected:
    // Compares a symbolic constant value |expected| against the parameter named
    // |paramName| of the texture object which is bound in unit TEXTURE0.
    testing::AssertionResult compareParameter(GLenum paramName,
                                              GLenum expected) {
        GLint actual;
        gl->glGetTexParameteriv(m_state.target, paramName, &actual);
        return compareValue<GLint>(
                expected, actual,
                "GL texture object " + std::to_string(m_object_name) +
                        " mismatch for param " + describeGlEnum(paramName) +
                        " on target " + describeGlEnum(m_state.target));
    }

    GLenum getTargetBindingName(GLenum target) {
        switch (target) {
            case GL_TEXTURE_2D:
                return GL_TEXTURE_BINDING_2D;
            case GL_TEXTURE_CUBE_MAP:
                return GL_TEXTURE_BINDING_CUBE_MAP;
            default:
                ADD_FAILURE() << "Unsupported texture target " << target;
                return 0;
        }
    }

    GLuint m_object_name;
    GlTextureObjectState m_state = {};
};

TEST_F(SnapshotGlTextureObjectTest, SetObjectParameters) {
    m_state = {
            .minFilter = GL_LINEAR,
            .magFilter = GL_NEAREST,
            .wrapS = GL_MIRRORED_REPEAT,
            .wrapT = GL_CLAMP_TO_EDGE,
            .target = GL_TEXTURE_2D,
    };
    doCheckedSnapshot();
}

TEST_F(SnapshotGlTextureObjectTest, Create2DMipmap) {
    m_state = {.minFilter = GL_LINEAR,
               .magFilter = GL_NEAREST,
               .wrapS = GL_MIRRORED_REPEAT,
               .wrapT = GL_CLAMP_TO_EDGE,
               .target = GL_TEXTURE_2D,
               .images2D = kGLES2TestTexture2D};
    doCheckedSnapshot();
}

TEST_F(SnapshotGlTextureObjectTest, CreateCubeMap) {
    m_state = {.minFilter = GL_LINEAR,
               .magFilter = GL_NEAREST,
               .wrapS = GL_MIRRORED_REPEAT,
               .wrapT = GL_CLAMP_TO_EDGE,
               .target = GL_TEXTURE_CUBE_MAP,
               .images2D = {}, // mingw compiler cannot deal with gaps
               .imagesCubeMap = kGLES2TestTextureCubeMap};
    doCheckedSnapshot();
}

// Regression test for b/570806740 / b/557246817: hardware bitmaps and BGRA
// color buffers are specified by the guest with the sized GL_BGRA8_EXT internal
// format (GL_EXT_texture_format_BGRA8888). On desktop GL the translator rewrites
// that to GL_RGBA8 when the texture is first specified, but TextureData keeps
// GL_BGRA8_EXT, which is what the snapshot records. If restore() does not apply
// the same rewrite, desktop GL rejects glTexImage2D / glTexStorage2D with
// GL_INVALID_ENUM, the texture stays incomplete and samples as opaque black.
//
// Only reproduces with the translator on top of desktop GL
// (ANDROID_EMU_TEST_WITH_HOST_GPU=1); GLES-on-GLES backends accept
// GL_BGRA8_EXT natively, so there the test passes with or without the fix.
class SnapshotGlBgra8TextureTest : public SnapshotPreserveTest {
public:
    enum class SpecifyWith { TexImage2D, TexStorage2D };

    static constexpr GLsizei kWidth = 4;
    static constexpr GLsizei kHeight = 4;

    void configure(GLint internalFormat, SpecifyWith specifyWith) {
        m_internalFormat = internalFormat;
        m_specifyWith = specifyWith;

        // Distinctive pattern: every byte unique and non-zero, so a texture
        // that comes back black (0,0,0,*) or zero-filled is unmistakable.
        m_bgraPixels.resize(kWidth * kHeight * 4);
        for (size_t i = 0; i < m_bgraPixels.size(); ++i) {
            m_bgraPixels[i] = static_cast<GLubyte>(0x10 + i * 3);
        }
        // Readback is done as GL_RGBA, so swap the B and R bytes.
        m_expectedRgba = m_bgraPixels;
        for (size_t i = 0; i < m_expectedRgba.size(); i += 4) {
            std::swap(m_expectedRgba[i], m_expectedRgba[i + 2]);
        }
    }

    // Whether the backend accepts GL_BGRA8_EXT as an immutable storage
    // format. On desktop GL the translator rewrites it to GL_RGBA8, but on
    // GLES-on-GLES backends glTexStorage2D is passed through, and not all of
    // them accept it (SwANGLE returns GL_INVALID_ENUM). The probe texture is
    // deleted and the binding restored so the default state check still holds.
    bool backendSupportsBgra8TexStorage() {
        while (gl->glGetError() != GL_NO_ERROR) {
        }
        GLuint probe = 0;
        gl->glGenTextures(1, &probe);
        gl->glBindTexture(GL_TEXTURE_2D, probe);
        gl->glTexStorage2D(GL_TEXTURE_2D, 1, GL_BGRA8_EXT, 1, 1);
        const GLenum err = gl->glGetError();
        gl->glBindTexture(GL_TEXTURE_2D, 0);
        gl->glDeleteTextures(1, &probe);
        while (gl->glGetError() != GL_NO_ERROR) {
        }
        return err == GL_NO_ERROR;
    }

    void defaultStateCheck() override {
        EXPECT_EQ(GL_FALSE, gl->glIsTexture(m_texture));
    }

    void changedStateCheck() override {
        SCOPED_TRACE("BGRA texture " + std::to_string(m_texture) +
                     ", internalformat " + describeGlEnum(m_internalFormat) +
                     (m_specifyWith == SpecifyWith::TexStorage2D
                              ? ", glTexStorage2D"
                              : ", glTexImage2D"));
        EXPECT_EQ(GL_TRUE, gl->glIsTexture(m_texture));
        EXPECT_TRUE(compareGlobalGlInt(gl, GL_TEXTURE_BINDING_2D, m_texture));
        EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

        std::vector<GLubyte> actual = getTextureImageData(
                gl, m_texture, GL_TEXTURE_2D, 0, kWidth, kHeight, GL_RGBA,
                GL_UNSIGNED_BYTE);
        const GLenum readbackError = gl->glGetError();
        EXPECT_EQ(GL_NO_ERROR, readbackError)
                << "reading the texture back through a framebuffer failed; the "
                   "texture level is most likely missing (restore failed)";

        bool allBlack = true;
        for (size_t i = 0; i < actual.size(); i += 4) {
            if (actual[i] || actual[i + 1] || actual[i + 2]) {
                allBlack = false;
                break;
            }
        }
        EXPECT_FALSE(allBlack) << "texture contents are all black";

        EXPECT_TRUE(compareVector<GLubyte>(
                m_expectedRgba, actual,
                "level 0 contents (uploaded as BGRA, read back as RGBA)"));
    }

    void stateChange() override {
        // Record which backend the translator is running on; the bug only
        // reproduces when it sits on top of desktop GL.
        printf("[   INFO   ] GL_VENDOR=%s GL_RENDERER=%s GL_VERSION=%s\n",
               reinterpret_cast<const char*>(gl->glGetString(GL_VENDOR)),
               reinterpret_cast<const char*>(gl->glGetString(GL_RENDERER)),
               reinterpret_cast<const char*>(gl->glGetString(GL_VERSION)));

        gl->glGenTextures(1, &m_texture);
        gl->glActiveTexture(GL_TEXTURE0);
        gl->glBindTexture(GL_TEXTURE_2D, m_texture);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        EXPECT_EQ(GL_NO_ERROR, gl->glGetError());

        switch (m_specifyWith) {
            case SpecifyWith::TexImage2D:
                gl->glTexImage2D(GL_TEXTURE_2D, 0, m_internalFormat, kWidth,
                                 kHeight, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                                 m_bgraPixels.data());
                break;
            case SpecifyWith::TexStorage2D:
                gl->glTexStorage2D(GL_TEXTURE_2D, 1,
                                   static_cast<GLenum>(m_internalFormat),
                                   kWidth, kHeight);
                EXPECT_EQ(GL_NO_ERROR, gl->glGetError());
                gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kWidth, kHeight,
                                    GL_BGRA_EXT, GL_UNSIGNED_BYTE,
                                    m_bgraPixels.data());
                break;
        }
    }

protected:
    GLuint m_texture = 0;
    GLint m_internalFormat = GL_BGRA8_EXT;
    SpecifyWith m_specifyWith = SpecifyWith::TexImage2D;
    std::vector<GLubyte> m_bgraPixels;
    std::vector<GLubyte> m_expectedRgba;
};

// Sized internal format via glTexImage2D: the path used by hardware bitmaps.
TEST_F(SnapshotGlBgra8TextureTest, SizedBgra8TexImage2D) {
    configure(GL_BGRA8_EXT, SpecifyWith::TexImage2D);
    doCheckedSnapshot();
}

// Sized internal format via glTexStorage2D (immutable storage).
TEST_F(SnapshotGlBgra8TextureTest, SizedBgra8TexStorage2D) {
    if (!backendSupportsBgra8TexStorage()) {
        GTEST_SKIP() << "backend does not accept GL_BGRA8_EXT in "
                        "glTexStorage2D (e.g. SwANGLE)";
    }
    configure(GL_BGRA8_EXT, SpecifyWith::TexStorage2D);
    doCheckedSnapshot();
}

// Control: the unsized GL_BGRA_EXT internal format was already rewritten on
// restore before the fix, so this is expected to pass regardless.
TEST_F(SnapshotGlBgra8TextureTest, UnsizedBgraTexImage2D) {
    configure(GL_BGRA_EXT, SpecifyWith::TexImage2D);
    doCheckedSnapshot();
}

}  // namespace
}  // namespace gl
}  // namespace host
}  // namespace gfxstream
