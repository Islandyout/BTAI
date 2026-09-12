#version 450

struct InstanceData {
    mat4 model;
    vec4 color;
};

layout(set = 0, binding = 0, std430) readonly buffer Instances {
    InstanceData instances[];
};

layout(push_constant) uniform Camera {
    mat4 viewProjection;
} camera;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 0) out vec4 color;

void main() {
    const InstanceData instance = instances[gl_InstanceIndex];
    gl_Position = camera.viewProjection * instance.model * vec4(inPosition, 1.0);
    color = vec4(inColor, 1.0) * instance.color;
}
