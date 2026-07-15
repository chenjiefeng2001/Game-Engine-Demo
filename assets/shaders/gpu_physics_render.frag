#version 430 core

/**
 * @file gpu_physics_render.frag
 * @brief GPU 物理引擎 — 实例化渲染 Fragment Shader
 *
 * 简单 Phong 光照，用于可视化粒子球体。
 */

in vec3 v_Color;
in vec3 v_Normal;
in vec3 v_FragPos;

uniform vec3 u_LightDir = vec3(0.5, 1.0, 0.3);
uniform vec3 u_LightColor = vec3(1.0, 1.0, 1.0);
uniform vec3 u_Ambient = vec3(0.1, 0.1, 0.15);

out vec4 o_Color;

void main() {
    vec3 N = normalize(v_Normal);
    vec3 L = normalize(u_LightDir);

    // 简单漫反射
    float diff = max(dot(N, L), 0.0);
    vec3 diffuse = diff * u_LightColor;

    // 简单高光（Blinn-Phong）
    vec3 V = normalize(-v_FragPos);  // 从片段到相机的方向（简化）
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    vec3 specular = spec * u_LightColor * 0.3;

    vec3 finalColor = (u_Ambient + diffuse) * v_Color + specular;

    o_Color = vec4(finalColor, 1.0);
}