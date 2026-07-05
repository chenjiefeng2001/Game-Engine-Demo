#version 460 core
// ═══════════════════════════════════════════════════════════════
// 延迟渲染 — 几何通道 Fragment Shader
// 紧凑 G-Buffer 输出 (Compact GBuffer 布局)
//
// 输出布局:
//   RT0 (RGB10A2): R=BaseColor.r(10bit), G=BaseColor.g(10bit),
//                  B=BaseColor.b(10bit), A=AO(2bit)
//   RT1 (RGB10A2): R=EncNormalX(10bit), G=EncNormalY(10bit),
//                  B=Roughness(10bit), A=MaterialMask(2bit)
//   RT2 (RGBA8):   RGB=Emissive(8bit), A=ShadingModelID(8bit)
//   Depth (D32S8): 深度缓冲
// ═══════════════════════════════════════════════════════════════

in vec3 v_WorldPos, v_Normal, v_Tangent, v_Bitangent;
in vec2 v_TexCoord;

// Compact G-Buffer 输出
layout(location = 0) out uint g_AlbedoAO;     // RGB10A2
layout(location = 1) out uint g_NormalRgh;    // RGB10A2
layout(location = 2) out vec4 g_Emissive;     // RGBA8

// 材质参数
uniform sampler2D u_Texture;
uniform bool     u_HasTexture = false;
uniform vec4     u_ObjectColor = vec4(1.0);
uniform sampler2D u_NormalMap;
uniform bool     u_HasNormalMap = false;
uniform float    u_NormalStrength = 1.0;
uniform float    u_Roughness = 0.5;
uniform float    u_Metallic  = 0.0;
uniform float    u_AO        = 1.0;
uniform vec3     u_EmissiveColor = vec3(0.0);
uniform sampler2D u_MetallicRoughnessMap;
uniform bool     u_HasMetallicRoughnessMap = false;
uniform sampler2D u_AOMap;
uniform bool     u_HasAOMap = false;

// ═══════════════════════════════════════════════════════════════
// Octahedron Normal 编码
// ═══════════════════════════════════════════════════════════════

vec2 EncodeOctNormal(vec3 n) {
    float absSum = abs(n.x) + abs(n.y) + abs(n.z);
    vec2 enc = n.xy / absSum;
    if (n.z < 0.0) {
        enc = (1.0 - abs(enc.yx)) * sign(enc.xy);
    }
    return enc * 0.5 + 0.5;  // [0, 1]
}

// ═══════════════════════════════════════════════════════════════
// Pack RGB10A2
// ═══════════════════════════════════════════════════════════════

uint PackRGB10A2(vec3 rgb, float a) {
    uint r = uint(clamp(rgb.r, 0.0, 1.0) * 1023.0 + 0.5);
    uint g = uint(clamp(rgb.g, 0.0, 1.0) * 1023.0 + 0.5);
    uint b = uint(clamp(rgb.b, 0.0, 1.0) * 1023.0 + 0.5);
    uint a2 = uint(clamp(a, 0.0, 1.0) * 3.0 + 0.5);
    return (r << 22) | (g << 12) | (b << 2) | a2;
}

// ═══════════════════════════════════════════════════════════════
// 法线贴图采样
// ═══════════════════════════════════════════════════════════════

vec3 GetNormal() {
    vec3 N = normalize(v_Normal);
    if (!u_HasNormalMap) return N;

    vec3 n = texture(u_NormalMap, v_TexCoord).rgb * 2.0 - 1.0;
    n.xy *= u_NormalStrength;
    n = normalize(n);

    vec3 T = normalize(v_Tangent);
    vec3 B = normalize(v_Bitangent);
    return normalize(mat3(T, B, N) * n);
}

// ═══════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════

void main() {
    // ── 基础色 ──
    vec4 albedo = u_HasTexture ? texture(u_Texture, v_TexCoord) : u_ObjectColor;

    // ── 法线 ──
    vec3 N = GetNormal();

    // ── 粗糙度/金属度 ──
    float roughness = u_Roughness;
    float metallic  = u_Metallic;
    if (u_HasMetallicRoughnessMap) {
        vec4 mr = texture(u_MetallicRoughnessMap, v_TexCoord);
        roughness = mr.g;   // GLTF 惯例: G 通道 = roughness
        metallic  = mr.b;   // GLTF 惯例: B 通道 = metallic
    }

    // ── AO ──
    float ao = u_AO;
    if (u_HasAOMap) {
        ao = texture(u_AOMap, v_TexCoord).r;
    }

    // ── 自发光 ──
    vec3 emissive = u_EmissiveColor;

    // ── 编码并输出紧凑 G-Buffer ──
    g_AlbedoAO  = PackRGB10A2(albedo.rgb, ao);
    g_NormalRgh = PackRGB10A2(EncodeOctNormal(N), roughness);
    g_Emissive  = vec4(emissive, 0.0);  // ShadingModelID = 0 (default lit)
}