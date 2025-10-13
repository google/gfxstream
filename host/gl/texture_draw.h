// Copyright (C) 2015 The Android Open Source Project
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

#ifndef TEXTURE_DRAW_H
#define TEXTURE_DRAW_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

#include <vector>

#include "hwc2.h"
#include "gfxstream/synchronization/Lock.h"

namespace gfxstream {
namespace gl {

// Helper class used to draw a simple texture to the current framebuffer.
// Usage is pretty simple:
//
//   1) Create a TextureDraw instance.
//
//   2) Each time you want to draw a texture, call draw(texture, rotation),
//      where |texture| is the name of a GLES 2.x texture object, and
//      |rotation| is an angle in degrees describing the clockwise rotation
//      in the GL y-upwards coordinate space. This function fills the whole
//      framebuffer with texture content.
//
class TextureDraw {
public:
    TextureDraw();
    ~TextureDraw();

    // Fill the current framebuffer with the content of |texture|, which must
    // be the name of a GLES 2.x texture object. |rotationDegrees| is a
    // clockwise rotation angle in degrees (clockwise in the GL Y-upwards
    // coordinate space; only supported values are 0, 90, 180, 270). |dx,dy| is
    // the translation of the image towards the origin.
    bool draw(GLuint texture, float rotationDegrees, float dx, float dy,
              const float* colorTransform) {
        return drawImpl(texture, rotationDegrees, dx, dy, false, colorTransform);
    }
    // Same as 'draw()', but if an overlay has been provided, that overlay is
    // drawn on top of everything else.
    bool drawWithOverlay(GLuint texture, float rotationDegrees, float dx, float dy,
                         const float* colorTransform) {
        return drawImpl(texture, rotationDegrees, dx, dy, true, colorTransform);
    }

    void setScreenMask(int width, int height, const uint8_t* rgbaData);
    void setScreenBackground(int width, int height, const uint8_t* rgbaData);
    void drawLayer(const ComposeLayer& l, int frameWidth, int frameHeight,
                   int cbWidth, int cbHeight, GLuint texture);
    void prepareForDrawLayer();
    void cleanupForDrawLayer();

   private:
    bool drawImpl(GLuint texture, float rotationDegrees, float dx, float dy, bool wantOverlay,
                  const float* colorTransform);
    void preDrawLayer();

    GLuint mVertexShader;
    GLuint mFragmentShader;
    GLuint mProgram;
    GLint mAlpha;
    GLint mComposeMode;
    GLint mColor;
    GLint mCoordTranslation;
    GLint mCoordScale;
    GLint mPositionSlot;
    GLint mInCoordSlot;
    GLint mScaleSlot;
    GLint mTextureSlot;
    GLint mTranslationSlot;
    GLuint mVertexBuffer;
    GLuint mIndexBuffer;
    GLuint mColorTransform;

    struct TexturedLayer {
        std::mutex mMutex;
        std::vector<unsigned char> mPixelData GUARDED_BY(mMutex);
        int mWidth;
        int mHeight;

        GLuint mTexture;
        int mTextureWidth;
        int mTextureHeight;
        bool mTextureDirty;
        bool mIsValid;
        bool mShouldReallocateTexture;

        TexturedLayer();

        bool create();
        void update(int width, int height, const uint8_t* rgbaData);

        void destroy();

        bool preDraw();

        void draw(GLuint program, GLint scaleSlot, intptr_t indexShift);
    };

    TexturedLayer mMaskLayer;
    TexturedLayer mBackgroundLayer;
    float mBackgroundOffset[2];
    float mBackgroundSize[2];
};

}  // namespace gl
}  // namespace gfxstream

#endif  // TEXTURE_DRAW_H
