#version 460 core

// ── PBR: Cook-Torrance BRDF with GGX distribution ──

in vec3 v_WorldPos;
in vec3 v_Normal;
in vec2 v_TexCoord;
in vec3 v_Tangent;
in vec3 v_Bitangent;

uniform vec3 u_ViewPos;
uniform vec3 u_AmbientColor;

// Lights (up to 4)
uniform int u_LightCount;
uniform vec3 u_LightPos[4];
uniform vec3 u_LightColor[4];
uniform float u_LightIntensity[4];

// Material
uniform vec4 u_ObjectColor;
uniform float u_Metallic;
uniform float u_Roughness;
uniform float u_AO;

// IBL (optional)
uniform samplerCube u_IrradianceMap;
uniform samplerCube u_PrefilterMap;
uniform bool u_HasIBL;

out vec4 o_Color;

const float PI = 3.14159265359;

// ── Trowbridge-Reitz GGX normal distribution ──
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

// ── Schlick-GGX geometry function ──
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

// ── Fresnel-Schlick approximation ──
vec3 FresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

void main() {
    vec3 N = normalize(v_Normal);
    vec3 V = normalize(u_ViewPos - v_WorldPos);

    // Material properties
    vec3 albedo = u_ObjectColor.rgb;
    float metallic = clamp(u_Metallic, 0.0, 1.0);
    float roughness = clamp(u_Roughness, 0.04, 1.0);
    float ao = u_AO;

    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    // ── Direct lighting ──
    vec3 Lo = vec3(0.0);
    for (int i = 0; i < min(u_LightCount, 4); i++) {
        vec3 L = normalize(u_LightPos[i] - v_WorldPos);
        vec3 H = normalize(V + L);
        float distance = length(u_LightPos[i] - v_WorldPos);
        float attenuation = 1.0 / (distance * distance);
        vec3 radiance = u_LightColor[i] * u_LightIntensity[i] * attenuation;

        // Cook-Torrance BRDF
        float NDF = DistributionGGX(N, H, roughness);
        float G = GeometrySmith(N, V, L, roughness);
        vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

        vec3 kS = F;
        vec3 kD = vec3(1.0) - kS;
        kD *= 1.0 - metallic;

        float NdotL = max(dot(N, L), 0.0);
        vec3 specular = (NDF * G * F) / max(4.0 * max(dot(N, V), 0.0) * NdotL, 0.001);

        Lo += (kD * albedo / PI + specular) * radiance * NdotL;
    }

    // ── Ambient (IBL or simple ambient) ──
    vec3 ambient;
    if (u_HasIBL) {
        vec3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0), F0, roughness);
        vec3 kS = F;
        vec3 kD = 1.0 - kS;
        kD *= 1.0 - metallic;

        vec3 irradiance = texture(u_IrradianceMap, N).rgb;
        vec3 diffuse = irradiance * albedo;

        vec3 R = reflect(-V, N);
        vec3 prefiltered = textureLod(u_PrefilterMap, R, roughness * 4.0).rgb;
        vec3 specularIBL = prefiltered * F;

        ambient = (kD * diffuse + specularIBL) * ao;
    } else {
        ambient = u_AmbientColor * albedo * ao;
    }

    vec3 color = ambient + Lo;

    // Tone mapping (Reinhard)
    color = color / (color + vec3(1.0));
    // Gamma correction
    color = pow(color, vec3(1.0 / 2.2));

    o_Color = vec4(color, u_ObjectColor.a);
}