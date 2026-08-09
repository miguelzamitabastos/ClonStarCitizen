#version 450

layout(location = 0) in vec3 frag_color;
layout(location = 1) in vec2 frag_uv;
layout(location = 2) in vec3 frag_normal;

layout(set = 0, binding = 1) uniform sampler2D tex_sampler;

layout(location = 0) out vec4 out_color;

void main()
{
    vec3 n = normalize(frag_normal);
    float ndl = clamp(dot(n, normalize(vec3(0.35, 0.85, 0.25))), 0.0, 1.0);
    float lighting = 0.35 + 0.65 * ndl;
    vec4 texel = texture(tex_sampler, frag_uv);
    out_color = vec4(texel.rgb * frag_color * lighting, 1.0);
}
