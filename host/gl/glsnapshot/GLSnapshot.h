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

#include "OpenGLESDispatch/GLESv2Dispatch.h"

#include <map>
#include <string>
#include <vector>

#include <inttypes.h>

#include <GLES2/gl2.h>

namespace gfxstream {
namespace gl {
namespace snapshot {

struct GLValue {
    std::vector<GLenum> enums;
    std::vector<unsigned char> bytes;
    std::vector<uint16_t> shorts;
    std::vector<uint32_t> ints;
    std::vector<float> floats;
    std::vector<uint64_t> int64s;
};

typedef std::map<GLenum, GLValue> GlobalStateMap;
typedef std::map<GLenum, bool> GlobalEnables;

struct GLShaderState {
    GLenum type;
    std::string source;
    bool compileStatus;
};

struct GLProgramState {
    std::map<GLenum, GLuint> linkage;
    bool linkStatus;
};

class GLSnapshotState {
public:
    GLSnapshotState(const GLESv2Dispatch* gl);
    void save();
    void restore();

    // Shaders
    GLuint createShader(GLuint shader, GLenum shaderType);
    GLuint createProgram(GLuint program);
    void shaderString(GLuint shader, const GLchar* string);
    void genBuffers(GLsizei n, GLuint* buffers);
    GLuint getProgramName(GLuint name);

private:
    void getGlobalStateEnum(GLenum name, int size);
    void getGlobalStateByte(GLenum name, int size);
    void getGlobalStateInt(GLenum name, int size);
    void getGlobalStateFloat(GLenum name, int size);
    void getGlobalStateInt64(GLenum name, int size);

    void getGlobalStateEnable(GLenum name);

    const GLESv2Dispatch* mGL;
    GlobalStateMap mGlobals;
    GlobalEnables mEnables;

    GLuint mProgramCounter = 1;

    std::map<GLuint, GLuint> mProgramNames;
    std::map<GLuint, GLuint> mProgramNamesBack;
    std::map<GLuint, GLShaderState> mShaderState;
    std::map<GLuint, GLProgramState> mShaderProgramState;

};

}  // namespace snapshot
}  // namespace gl
}  // namespace gfxstream