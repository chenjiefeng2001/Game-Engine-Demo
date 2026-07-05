#version 460 core

/**
 * @file post_tonemap.glsl
 * @brief 色调映射 — ACES Filmic / Reinhard / Unreal 三种算法
 *
 * HDR 输入 → 色调映射 → 伽马校正 → LDR 输出
 */

layout(binding = 0) uniform sampler2D u_HDRScene;
layout(location = 0) in vec2 v_UV;
layout(location = 0) out vec4 o_Color;

uniform int   u_ToneMapMode = 2;  // 0=None, 1=Reinhard, 2=ACES, 3=Unreal, 4=Filmic
uniform float u_Exposure = 1.0;
uniform float u_Gamma = 2.2;

// ACES Filmic (Narkowicz 2015)
vec3 ACESToneMapping(vec3 color) {
    float a = 2.51; float b = 0.03;
    float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((color * (a * color + b)) / (color * (c * color + d) + e), 0.0, 1.0);
}

// Reinhard
vec3 ReinhardToneMapping(vec3 color) {
    return color / (color + vec3(1.0));
}

// Unreal Engine
vec3 UnrealToneMapping(vec3 color) {
    color *= u_Exposure;
    return color / (color + vec3(1.0));
    // return approximately same as Reinhard for now
}

// Filmic (Haarm-Pieter Duiker)
vec3 FilmicToneMapping(vec3 color) {
    color = max(vec3(0.0), color - vec3(0.004));
    return (color * (6.2 * color + vec3(0.5))) / (color * (6.2 * color + vec3(1.7)) + vec3(0.06));
}

void main() {
    vec3 hdrColor = texture(u_HDRScene, v_UV).rgb * u_Exposure;

    vec3 mapped;
    switch (u_ToneMapMode) {
        case 1: mapped = ReinhardToneMapping(hdrColor); break;
        case 2: mapped = ACESToneMapping(hdrColor); break;
        case 3: mapped = UnrealToneMapping(hdrColor); break;
        case 4: mapped = FilmicToneMapping(hdrColor); break;
        default: mapped = hdrColor; break;
    }

    // Gamma correction
    o_Color = vec4(pow(mapped, vec3(1.0 / u_Gamma)), 1.0);
}