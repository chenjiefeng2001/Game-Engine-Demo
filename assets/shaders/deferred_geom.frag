#version 460 core
// ═══════════════════════════════════════════════
// 延迟渲染 — 几何通道 Fragment Shader
// 多渲染目标输出到 G-Buffer:
//   RT0: WorldPosition (RGB16F)
//   RT1: Normal + Roughness (RGBA16F)
//   RT2: Albedo + Metallic (RGBA8)
//   RT3: AO + Specular (RGBA8)
// ═══════════════════════════════════════════════

in vec3 v_WorldPos,v_Normal,v_Tangent,v_Bitangent;
in vec2 v_TexCoord;

layout(location=0) out vec3 g_Position;
layout(location=1) out vec4 g_NormalRoughness;
layout(location=2) out vec4 g_AlbedoMetallic;
layout(location=3) out vec4 g_AOSpecular;

// 材质
uniform sampler2D u_Texture;
uniform bool     u_HasTexture;
uniform vec4     u_ObjectColor;
uniform sampler2D u_NormalMap;
uniform bool     u_HasNormalMap;
uniform float    u_NormalStrength=1;
uniform float    u_Shininess=32;
uniform float    u_SpecularStrength=0.5;
uniform float    u_AO=1.0;

vec3 GetNormal(){
    if(!u_HasNormalMap)return normalize(v_Normal);
    vec3 n=texture(u_NormalMap,v_TexCoord).rgb*2-1;
    n.xy*=u_NormalStrength; n=normalize(n);
    vec3 N=normalize(v_Normal),T=normalize(v_Tangent),B=normalize(v_Bitangent);
    return normalize(mat3(T,B,N)*n);
}

void main(){
    vec3 N=GetNormal();
    vec4 albedo=u_HasTexture?texture(u_Texture,v_TexCoord):u_ObjectColor;
    float roughness=clamp(1-u_Shininess/256,0,1);
    float metallic=0.0;

    g_Position=v_WorldPos;
    g_NormalRoughness=vec4(N*0.5+0.5,roughness);
    g_AlbedoMetallic=vec4(albedo.rgb,metallic);
    g_AOSpecular=vec4(u_AO,u_SpecularStrength,0,0);
}
