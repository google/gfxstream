/*
* Copyright (C) 2016 The Android Open Source Project
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/
#ifndef _GL_TEXTURE_SHARED_DATA_H_
#define _GL_TEXTURE_SHARED_DATA_H_

#include <GLES/gl.h>
#include <map>
#include <memory>

#include "aemu/base/synchronization/AndroidLock.h"

using gfxstream::guest::ReadWriteLock;

struct TextureDims {
    std::map<GLsizei, GLsizei> widths;
    std::map<GLsizei, GLsizei> heights;
    std::map<GLsizei, GLsizei> depths;
};

struct TextureRec {
    GLuint id;
    GLenum target;
    GLint internalformat;
    GLenum format;
    GLenum type;
    GLsizei multisamples;
    TextureDims* dims;
    bool immutable;
    bool boundEGLImage;
    bool hasStorage;
    bool hasCubeNegX;
    bool hasCubePosX;
    bool hasCubeNegY;
    bool hasCubePosY;
    bool hasCubeNegZ;
    bool hasCubePosZ;
};

struct SharedTextureDataMap {
  using MapType = std::map<GLuint, std::shared_ptr<TextureRec>>;

  using iterator = MapType::iterator;
  using const_iterator = MapType::const_iterator;

  MapType map;
  ReadWriteLock lock;
};

#endif
