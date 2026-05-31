#version 460 core

in vec3 v_WorldPos;
in vec3 v_ViewPos;
in vec3 v_Normal;
in vec2 v_TexCoord;

uniform sampler2D u_Texture;
uniform bool     u_HasTexture;
uniform vec4     u_ObjectColor;
uniform vec3     u_LightPos;
uniform vec3     u_LightColor;
uniform float    u_LightIntensity;
uniform vec3     u_ViewPos;      // 相机在世界空间中的位置
uniform vec3     u_AmbientColor; // 环境光颜色

out vec4 FragColor;

void main() {
    // 归一化法线和方向向量（都在世界空间中）
    vec3 norm = normalize(v_Normal);
    vec3 lightDir = normalize(u_LightPos - v_WorldPos);
    vec3 viewDir  = normalize(u_ViewPos - v_WorldPos);

    // ── 环境光 ──
    vec3 ambient = u_AmbientColor;

    // ── 漫反射 (Lambert) ──
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor;

    // ── 镜面反射 (Blinn-Phong) ──
    float specularStrength = 0.5;
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(norm, halfDir), 0.0), 32);
    vec3 specular = specularStrength * spec * u_LightColor;

    // ── 纹理 / 颜色 ──
    vec4 baseColor;
    if (u_HasTexture) {
        baseColor = texture(u_Texture, v_TexCoord);
    } else {
        baseColor = u_ObjectColor;
    }

    vec3 result = (ambient + diffuse + specular) * baseColor.rgb * u_LightIntensity;
    FragColor = vec4(result, baseColor.a);
}
