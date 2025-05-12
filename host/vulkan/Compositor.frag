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

#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(binding = 0) uniform sampler2D texSampler;
layout(binding = 1) uniform UniformBufferObject {
    mat4 posTransform;
    mat4 texcoordTransform;
    uvec4 mode;
    vec4 alpha;
    vec4 color;
} ubo;

layout(location = 0) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

void main() {
    if (ubo.mode.x == 2) {
        outColor = ubo.alpha * texture(texSampler, fragTexCoord);
    } else if (ubo.mode.x == 3) {
        outColor = ubo.alpha * ubo.color;
    } else {
        outColor = vec4(0.0, 1.0, 0.0, 1.0);
    }
}
