// Copyright 2020 The Android Open Source Project
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

#ifdef USE_ANGLE_SHADER_PARSER
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <GLES2/gl2.h>


#include "ShaderTranslator.h"

namespace ANGLEShaderParser {

// Convenient to query those
extern ST_BuiltInResources kResources;

// For performing link-time validation of shader programs.
struct ShaderLinkInfo {
    int esslVersion;
    std::vector<ST_ShaderVariable> uniforms;
    std::vector<ST_ShaderVariable> varyings;
    std::vector<ST_ShaderVariable> attributes;
    std::vector<ST_ShaderVariable> outputVars;
    std::vector<ST_InterfaceBlock> interfaceBlocks;
    std::map<std::string, std::string> nameMap;
    std::map<std::string, std::string> nameMapReverse;

    ShaderLinkInfo();
    ShaderLinkInfo(const ShaderLinkInfo& other);
    ShaderLinkInfo& operator=(const ShaderLinkInfo& other);
    ShaderLinkInfo(ShaderLinkInfo&& other);
    ShaderLinkInfo& operator=(ShaderLinkInfo&& other);
    ~ShaderLinkInfo();

protected:
    void copyFromOther(const ShaderLinkInfo& other);
    void clear();
};

using BuiltinResourcesEditCallback = std::function<void(ST_BuiltInResources&)>;

bool globalInitialize(
    bool isGles2Gles,
    BuiltinResourcesEditCallback callback);

bool translate(bool hostUsesCoreProfile,
               const char* src, GLenum shaderType,
               std::string* outInfolog, std::string* outObjCode,
               ShaderLinkInfo* outShaderLinkInfo);

} // namespace ANGLEShaderParser

#endif
