#version 460 core

/**
 * @file post_bloom_downsample.glsl
 * @brief Bloom 下采样 — 提取高光 + Kawase 降采样
 *
 * 算法：Kawase Dual Filtering
 *   1. 提取 HDR 颜色中超过亮度阈值的部分
 *   2. 4x 下采样（每级使用 4x2 线性采样）
 *   3. 生成 Mip 链用于后续上采样
 */

layout(binding = 0) uniform sampler2D u_InputTex;
layout(rgba16f, binding = 1) uniform image2D u_OutputImg;

uniform float u_Threshold = 1.0;
uniform float u_Intensity = 1.0;

layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

void main() {
    ivec2 imgSize = imageSize(u_OutputImg);
    vec2 texelSize = 1.0 / vec2(textureSize(u_InputTex, 0));

    // Kawase 双滤波核（偏移采样）
    vec2 offsets[4] = vec2[](
        vec2(-0.5, -0.5), vec2( 0.5, -0.5),
        vec2(-0.5,  0.5), vec2( 0.5,  0.5)
    );

    vec3 color = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        color += texture(u_InputTex, v_UV + offsets[i] * texelSize).rgb;
    }
    color *= 0.25;

    // 提取超过阈值的高光部分
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    float bloomAmount = max(luminance - u_Threshold, 0.0) / max(luminance, 0.0001);
    vec3 bloom = color * bloomAmount * u_Intensity;

    o_Color = vec4(bloom, 1.0);
}