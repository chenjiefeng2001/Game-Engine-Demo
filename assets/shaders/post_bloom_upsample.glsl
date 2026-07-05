#version 460 core

/**
 * @file post_bloom_upsample.glsl
 * @brief Bloom 上采样混合 — Kawase 双滤波上采样
 */

layout(binding = 0) uniform sampler2D u_InputTex;
layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

uniform float u_BlendFactor = 1.0;

void main() {
    vec2 texelSize = 1.0 / vec2(textureSize(u_InputTex, 0));

    vec2 offsets[4] = vec2[](
        vec2(-0.5, -0.5), vec2( 0.5, -0.5),
        vec2(-0.5,  0.5), vec2( 0.5,  0.5)
    );

    vec3 color = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        color += texture(u_InputTex, v_UV + offsets[i] * texelSize).rgb;
    }
    color *= 0.25 * u_BlendFactor;

    o_Color = vec4(color, 1.0);
}