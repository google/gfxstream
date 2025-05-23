#version 460

vec2 kPositions[3] = vec2[](
    vec2(-1.0, -1.0),
    vec2( 3.0, -1.0),
    vec2(-1.0,  3.0)
);

vec2 kUVs[3] = vec2[](
    vec2(0.0, 0.0),
    vec2(2.0, 0.0),
    vec2(0.0, 2.0)
);

layout (location = 0) out vec2 oUV;

void main() {
    gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
    oUV = kUVs[gl_VertexIndex];
}