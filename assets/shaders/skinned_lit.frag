#version 330 core
// 与顶点着色器一致：330 下带 location 的片元输出同样需要该扩展。
#extension GL_ARB_separate_shader_objects : enable

// GPU 蒙皮片元着色器（生产路径）
//
// 刻意保持极简：单一方向光 + 恒定环境项。目的是让"像素随骨骼姿态变化"
// 可被离屏读回直接验证，不引入纹理采样或复杂光照，以免验证信号被其它因素
// 淹没。蒙皮是否生效，以像素分布随 u_BoneMatrices 改变为准。

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec2 v_TexCoord;

layout(location = 0) out vec4 o_Color;

const vec3 k_LightDir = normalize(vec3(0.4, 0.8, 0.45));
const vec3 k_BaseColor = vec3(0.85, 0.35, 0.25);
const float k_Ambient = 0.25;

void main()
{
    vec3 n = normalize(v_Normal);
    // 双面光照：避免某一帧恰好背面朝光导致整片变黑而掩盖蒙皮信号。
    float ndl = abs(dot(n, k_LightDir));
    o_Color = vec4(k_BaseColor * (k_Ambient + (1.0 - k_Ambient) * ndl), 1.0);
}
