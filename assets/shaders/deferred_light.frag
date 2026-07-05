#version 460 core
// ═══════════════════════════════════════════════════════════════
// 延迟渲染 — 光照通道 Fragment Shader
// PBR Cook-Torrance BRDF + 级联阴影贴图 (CSM)
//
// 输入:
//   G-Buffer 纹理 (layout 与 CompactGBuffer 一致)
//   LightsUBO (set=0, binding=0)
//   CSMUBO    (set=0, binding=1)
//   阴影贴图数组 (set=1, binding=4)
//
// 输出:
//   HDR Scene Color
// ═══════════════════════════════════════════════════════════════

in vec2 v_UV;
out vec4 FragColor;

// ═══════════════════════════════════════════════════════════════
// G-Buffer 纹理输入 (Compact GBuffer 布局)
// ═══════════════════════════════════════════════════════════════

layout(binding = 0) uniform sampler2D u_AlbedoAO;     // RGB10A2: R=BaseColor.r, G=BaseColor.g, B=BaseColor.b, A=AO(2bit)
layout(binding = 1) uniform sampler2D u_NormalRgh;     // RGB10A2: R=EncNormalX, G=EncNormalY, B=Roughness(10bit), A=MaterialMask
layout(binding = 2) uniform sampler2D u_Emissive;      // RGBA8:   RGB=Emissive, A=ShadingModelID
layout(binding = 3) uniform sampler2D u_Depth;         // D32S8:   Depth

// ═══════════════════════════════════════════════════════════════
// 光源数据 UBO (std140)
// ═══════════════════════════════════════════════════════════════

#define MAX_LIGHTS 256

struct GPULight {
    vec4  position;        // xyz + pad
    vec4  direction;       // xyz + pad
    vec4  color;           // RGB + intensity
    float range;
    float spotInnerAngle;  // cos
    float spotOuterAngle;  // cos
    int   type;            // 0=directional, 1=point, 2=spot
};

layout(std140, binding = 0) uniform LightsUBO {
    GPULight lights[MAX_LIGHTS];
    int      lightCount;
    vec3     _padLights;
};

// ═══════════════════════════════════════════════════════════════
// CSM 数据 UBO (std140)
// ═══════════════════════════════════════════════════════════════

struct CascadeParams {
    mat4 lightViewProj;       // 64 bytes
    float splitDepth;         // 视图空间 Z 分割深度
    vec3  _padCascade;
};

#define MAX_CASCADES 4

layout(std140, binding = 1) uniform CSMUBO {
    CascadeParams cascades[MAX_CASCADES];
    int           cascadeCount;
    float         shadowMapSize;
    float         shadowBias;
    float         _padCSM;
};

layout(binding = 4) uniform sampler2DArray u_ShadowMap;

// ═══════════════════════════════════════════════════════════════
// 相机参数
// ═══════════════════════════════════════════════════════════════

uniform vec3 u_ViewPos;
uniform mat4 u_View;
uniform mat4 u_Proj;
uniform mat4 u_InvProj;
uniform vec3 u_AmbientColor;
uniform float u_AmbientIntensity = 1.0;

// ═══════════════════════════════════════════════════════════════
// PBR 常量
// ═══════════════════════════════════════════════════════════════

const float PI = 3.14159265358979323846;
const float INV_PI = 0.3183098861837907;

// ═══════════════════════════════════════════════════════════════
// Octahedron Normal 解码
// ═══════════════════════════════════════════════════════════════

vec3 DecodeOctNormal(vec2 enc) {
    vec3 n;
    n.x = enc.x * 2.0 - 1.0;
    n.y = enc.y * 2.0 - 1.0;
    n.z = 1.0 - abs(n.x) - abs(n.y);
    if (n.z < 0.0) {
        float tcx = (1.0 - abs(n.y)) * (n.x >= 0.0 ? 1.0 : -1.0);
        float tcy = (1.0 - abs(n.x)) * (n.y >= 0.0 ? 1.0 : -1.0);
        n.x = tcx; n.y = tcy;
    }
    return normalize(n);
}

// ═══════════════════════════════════════════════════════════════
// 从深度重建世界空间位置
// ═══════════════════════════════════════════════════════════════

vec3 ReconstructWorldPos(vec2 uv, float depth) {
    // NDC 坐标
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 clip = u_InvProj * ndc;
    vec3 viewPos = clip.xyz / clip.w;
    // 视图空间 → 世界空间 (使用 u_View 的逆矩阵)
    vec4 worldPos = inverse(u_View) * vec4(viewPos, 1.0);
    return worldPos.xyz / worldPos.w;
}

// ═══════════════════════════════════════════════════════════════
// PBR Cook-Torrance BRDF
// ═══════════════════════════════════════════════════════════════

// 法线分布函数 GGX/Trowbridge-Reitz
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// 几何遮蔽函数 Smith-GGX
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) *
           GeometrySchlickGGX(NdotL, roughness);
}

// 菲涅尔 Schlick 近似
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Cook-Torrance BRDF 计算
vec3 CalculateBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    float NDF = DistributionGGX(N, H, roughness);
    float G   = GeometrySmith(N, V, L, roughness);
    vec3  F   = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);

    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 specular = NDF * G * F / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);
    vec3 diffuse  = kD * albedo * INV_PI;

    return (diffuse + specular) * NdotL;
}

// ═══════════════════════════════════════════════════════════════
// 点光源衰减
// ═══════════════════════════════════════════════════════════════

float GetAttenuation(float dist, float range) {
    // 物理正确的 inverse-square + range 平滑裁切
    // Unreal 风格的衰减
    float denom = dist * dist + 1.0;
    float attenuation = 1.0 / denom;
    // 平滑截止 (在 range 处衰减到 0)
    float ratio = dist / range;
    attenuation *= clamp(1.0 - ratio * ratio * ratio * ratio, 0.0, 1.0);
    return attenuation * attenuation;
}

// ═══════════════════════════════════════════════════════════════
// CSM 阴影采样 (Percentage Closer Filtering)
// ═══════════════════════════════════════════════════════════════

float SampleCSM(vec3 worldPos, float depthVS) {
    // 1. 选择级联
    int cascadeIndex = cascadeCount - 1;
    for (int i = 0; i < cascadeCount - 1; ++i) {
        if (depthVS < cascades[i].splitDepth) {
            cascadeIndex = i;
            break;
        }
    }

    // 2. 转换到光源裁剪空间
    vec4 clipPos = cascades[cascadeIndex].lightViewProj * vec4(worldPos, 1.0);
    vec3 proj = clipPos.xyz / clipPos.w;
    vec3 texCoord = proj * 0.5 + 0.5;  // [0, 1]

    // 检查是否在阴影贴图范围内
    if (texCoord.x < 0.0 || texCoord.x > 1.0 ||
        texCoord.y < 0.0 || texCoord.y > 1.0 ||
        texCoord.z > 1.0) {
        return 0.0;
    }

    // 3. PCF 3x3 Poisson Disk
    float currentDepth = texCoord.z - shadowBias;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(u_ShadowMap, 0).xy;

    // 3x3 PCF
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 offset = vec2(x, y) * texelSize;
            float sampleDepth = texture(u_ShadowMap, vec3(texCoord.xy + offset, cascadeIndex)).r;
            shadow += (currentDepth > sampleDepth ? 1.0 : 0.0);
        }
    }
    shadow /= 9.0;

    return shadow;
}

// ═══════════════════════════════════════════════════════════════
// IBL - 简化的环境光照
// ═══════════════════════════════════════════════════════════════

vec3 CalculateIBL(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness, float ao) {
    // 简化 IBL: 使用环境色 + AO
    // 完整实现需要 Irradiance Map + Prefiltered Map + BRDF LUT
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 kS = FresnelSchlick(max(dot(N, V), 0.0), F0);
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    return kD * albedo * u_AmbientColor * u_AmbientIntensity * ao;
}

// ═══════════════════════════════════════════════════════════════
// 主函数
// ═══════════════════════════════════════════════════════════════

void main() {
    vec2 uv = v_UV;

    // ── 1. 解码 G-Buffer ──
    vec4 albedoAORaw = texture(u_AlbedoAO, uv);
    vec3 albedo = albedoAORaw.rgb;

    // AO 从 A 通道解码 (2-bit 量化, 映射回 [0, 1])
    float ao = float(int(albedoAORaw.a * 3.0 + 0.5)) / 3.0;
    if (ao < 0.01) ao = 1.0;  // 未设置 AO 时默认 1.0

    vec4 normalRghRaw = texture(u_NormalRgh, uv);
    vec3 N = DecodeOctNormal(normalRghRaw.rg);
    float roughness = normalRghRaw.b;

    vec4 emissiveRaw = texture(u_Emissive, uv);
    vec3 emissive = emissiveRaw.rgb;

    // ── 2. 深度 → 世界位置 ──
    float depth = texture(u_Depth, uv).r;
    vec3 worldPos = ReconstructWorldPos(uv, depth);

    // 空像素 (远平面) 跳过
    if (abs(depth - 1.0) < 0.0001) {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 V = normalize(u_ViewPos - worldPos);
    float metallic = 0.0;  // GBuffer 当前未编码 metallic

    // ── 3. PBR 光照 ──
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < min(lightCount, MAX_LIGHTS); ++i) {
        vec3 L;
        float attenuation = 1.0;
        float shadow = 0.0;

        if (lights[i].type == 0) {
            // Directional Light
            L = normalize(-lights[i].direction.xyz);
            // 级联阴影
            // 视图空间 Z (用于级联选择)
            vec4 viewPos = u_View * vec4(worldPos, 1.0);
            float depthVS = -viewPos.z;
            shadow = SampleCSM(worldPos, depthVS);
        } else if (lights[i].type == 1) {
            // Point Light
            vec3 lightToWorld = lights[i].position.xyz - worldPos;
            float dist = length(lightToWorld);
            L = lightToWorld / max(dist, 0.001);
            attenuation = GetAttenuation(dist, lights[i].range);
        } else if (lights[i].type == 2) {
            // Spot Light
            vec3 lightToWorld = lights[i].position.xyz - worldPos;
            float dist = length(lightToWorld);
            L = lightToWorld / max(dist, 0.001);
            // 聚光锥体角度检测
            float cosAngle = dot(-L, normalize(lights[i].direction.xyz));
            float cosInner = lights[i].spotInnerAngle;
            float cosOuter = lights[i].spotOuterAngle;
            float spotFactor = smoothstep(cosOuter, cosInner, cosAngle);
            attenuation = GetAttenuation(dist, lights[i].range) * spotFactor;
        }

        // BRDF
        vec3 radiance = lights[i].color.rgb * lights[i].color.a * attenuation;
        vec3 brdf = CalculateBRDF(N, V, L, albedo, metallic, roughness);
        Lo += brdf * radiance * (1.0 - shadow * 0.8);
    }

    // ── 4. 环境光照 + 自发光 ──
    vec3 ambient = CalculateIBL(N, V, albedo, metallic, roughness, ao);
    vec3 finalColor = ambient + Lo + emissive;

    FragColor = vec4(finalColor, 1.0);
}