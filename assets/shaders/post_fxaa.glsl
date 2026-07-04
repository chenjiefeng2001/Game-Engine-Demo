#version 460 core

/**
 * @file post_fxaa.glsl
 * @brief FXAA (Fast Approximate Anti-Aliasing) 实现
 *
 * 基于 Nvidia 的 FXAA 3.11 实现
 * 单 Pass 全屏抗锯齿，无需运动矢量
 */

layout(binding = 0) uniform sampler2D u_SceneTex;
layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

uniform float u_RenderTargetWidth  = 1920.0;
uniform float u_RenderTargetHeight = 1080.0;
uniform float u_EdgeThresholdMin   = 0.0312;  // 0.0312 = 1/32
uniform float u_EdgeThresholdMax   = 0.0833;  // 0.0833 = 1/12
uniform int   u_SubpixelQuality    = 4;       // 子像素分析质量 (2/3/4)

// 亮度提取
float luminance(vec3 color) {
    return dot(color, vec3(0.299, 0.587, 0.114));
}

// FXAA 子像素去锯齿
float subPixelBlur(vec2 uv, vec2 rcpFrame) {
    vec3 colorSum = texture(u_SceneTex, uv + vec2(0.0, 0.0) * rcpFrame).rgb
                  + texture(u_SceneTex, uv + vec2(1.0, 0.0) * rcpFrame).rgb
                  + texture(u_SceneTex, uv + vec2(0.0, 1.0) * rcpFrame).rgb
                  + texture(u_SceneTex, uv + vec2(1.0, 1.0) * rcpFrame).rgb;
    return luminance(colorSum * 0.25);
}

void main() {
    vec2 rcpFrame = vec2(1.0 / u_RenderTargetWidth, 1.0 / u_RenderTargetHeight);

    // 采样 3x3 区块
    vec3 colorCenter = texture(u_SceneTex, v_UV).rgb;
    float lumaCenter = luminance(colorCenter);
    float lumaTL = luminance(textureLod(u_SceneTex, v_UV + vec2(-1, -1) * rcpFrame, 0).rgb);
    float lumaTR = luminance(textureLod(u_SceneTex, v_UV + vec2( 1, -1) * rcpFrame, 0).rgb);
    float lumaBL = luminance(textureLod(u_SceneTex, v_UV + vec2(-1,  1) * rcpFrame, 0).rgb);
    float lumaBR = luminance(textureLod(u_SceneTex, v_UV + vec2( 1,  1) * rcpFrame, 0).rgb);
    float lumaT  = luminance(textureLod(u_SceneTex, v_UV + vec2( 0, -1) * rcpFrame, 0).rgb);
    float lumaL  = luminance(textureLod(u_SceneTex, v_UV + vec2(-1,  0) * rcpFrame, 0).rgb);
    float lumaR  = luminance(textureLod(u_SceneTex, v_UV + vec2( 1,  0) * rcpFrame, 0).rgb);
    float lumaB  = luminance(textureLod(u_SceneTex, v_UV + vec2( 0,  1) * rcpFrame, 0).rgb);

    // 计算亮度梯度
    float lumaMin = min(lumaCenter, min(min(lumaTL, lumaTR), min(lumaBL, lumaBR)));
    float lumaMax = max(lumaCenter, max(max(lumaTL, lumaTR), max(lumaBL, lumaBR)));

    float range = lumaMax - lumaMin;
    if (range < max(u_EdgeThresholdMin, lumaMax * u_EdgeThresholdMin)) {
        o_Color = vec4(colorCenter, 1.0);
        return;
    }

    // 边缘方向检测
    float edgeH = abs(lumaT + lumaB - 2.0 * lumaCenter)
                + abs(lumaTL + lumaBL - 2.0 * lumaL)
                + abs(lumaTR + lumaBR - 2.0 * lumaR);
    float edgeV = abs(lumaL + lumaR - 2.0 * lumaCenter)
                + abs(lumaTL + lumaTR - 2.0 * lumaT)
                + abs(lumaBL + lumaBR - 2.0 * lumaB);

    // 沿边缘方向偏移采样
    vec2 dir = (edgeH < edgeV) ? vec2(0, -1) : vec2(-1, 0);
    float gradient = (edgeH < edgeV) ? (lumaT - lumaB) : (lumaL - lumaR);

    float sign = sign(gradient);
    dir *= sign;

    // 沿边缘多次采样以提高质量
    vec2 uv = v_UV;
    for (int i = 0; i < u_SubpixelQuality; ++i) {
        float offset = float(i) * 0.5;
        vec3 samplePos = textureLod(u_SceneTex, uv + dir * offset * rcpFrame, 0).rgb;
        vec3 sampleNeg = textureLod(u_SceneTex, uv - dir * offset * rcpFrame, 0).rgb;
        uv += (luminance(samplePos) < lumaCenter) ? -dir * 0.5 * rcpFrame
                                                   : dir * 0.5 * rcpFrame;
    }

    o_Color = vec4(textureLod(u_SceneTex, uv, 0).rgb, 1.0);
}