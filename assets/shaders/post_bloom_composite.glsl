#version 460 core

/**
 * @file post_bloom_composite.glsl
 * @brief Bloom 合成 — 将 HDR 场景与 Bloom 纹理混合
 */

layout(binding = 0) uniform sampler2D u_HDRScene;
layout(binding = 1) uniform sampler2D u_BloomTex;
layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

uniform float u_BloomIntensity = 1.0;

void main() {
    vec3 hdrColor = texture(u_HDRScene, v_UV).rgb;
    vec3 bloomColor = texture(u_BloomTex, v_UV).rgb;

    // 附加混合（HDR + Bloom）
    vec3 result = hdrColor + bloomColor * u_BloomIntensity;

    o_Color = vec4(result, 1.0);
}