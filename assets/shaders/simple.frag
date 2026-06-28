#version 460 core
// 颜色附件输出
layout(location = 0) out vec4 FragColor;
// 拾取附件输出（Entity ID）
layout(location = 1) out int EntityID;

in vec3 v_FragPos;
in vec3 v_Normal;

uniform vec4 u_BaseColor;
uniform vec3 u_LightDir;
uniform vec3 u_LightColor;
uniform int  u_IsSelected = 0;
uniform int  u_EntityID    = 0;

void main() {
    // 环境光
    vec3 ambient = 0.2 * u_BaseColor.rgb;

    // 漫反射
    vec3 norm = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor * u_BaseColor.rgb;

    vec3 result = ambient + diffuse;

    // 选中高亮
    if (u_IsSelected == 1) {
        result += vec3(0.8, 0.4, 0.0);
    }

    FragColor = vec4(result, u_BaseColor.a);

    // 写入实体 ID 到拾取纹理
    EntityID = u_EntityID;
}
