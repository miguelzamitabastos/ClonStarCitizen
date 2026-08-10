#version 450

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec3 in_color;
layout(location = 3) in vec2 in_uv;

layout(location = 4) in vec4 in_model_col0;
layout(location = 5) in vec4 in_model_col1;
layout(location = 6) in vec4 in_model_col2;
layout(location = 7) in vec4 in_model_col3;

layout(set = 0, binding = 0) uniform FrameUBO {
    mat4 view_proj;
} ubo;

layout(location = 0) out vec3 frag_color;
layout(location = 1) out vec2 frag_uv;
layout(location = 2) out vec3 frag_normal;

void main()
{
    mat4 model = mat4(in_model_col0, in_model_col1, in_model_col2, in_model_col3);
    vec4 world = model * vec4(in_position, 1.0);
    gl_Position = ubo.view_proj * world;
    frag_color  = in_color;
    frag_uv     = in_uv;
    frag_normal = mat3(model) * in_normal;
}
