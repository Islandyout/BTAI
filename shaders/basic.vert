#version 450

layout(push_constant) uniform PushConstants {
    mat4 viewProjection;
    mat4 model;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec3 color;

void main() {
    gl_Position = pc.viewProjection * pc.model * vec4(inPosition, 1.0);
    color = inColor;
}
