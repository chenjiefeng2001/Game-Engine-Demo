#version 460 core
in vec3 v_WorldPos;

out vec4 FragColor;

uniform vec4 u_BaseColor;
uniform vec3 u_LightDir;
uniform vec3 u_LightColor;
uniform vec3 u_CamPos;
uniform int  u_EntityID = -1;
uniform int  u_IsSelected = 0;

void main() {
    // 使用屏幕空间导数近似计算面法线（无需顶点法线属性）
    vec3 normal = normalize(cross(dFdx(v_WorldPos), dFdy(v_WorldPos)));

    // 环境光
    vec3 ambient = vec3(0.15, 0.15, 0.2);

    // 方向光漫反射
    vec3 lightDir = normalize(u_LightDir);
    float diff = max(dot(normal, -lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor;

    // 高光（Blinn-Phong 简化）
    vec3 viewDir = normalize(u_CamPos - v_WorldPos);
    vec3 halfDir = normalize(-lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 16.0);
    vec3 specular = spec * u_LightColor * 0.3;

    // 合成
    vec3 lighting = ambient + diffuse + specular;
    vec3 finalColor = u_BaseColor.rgb * lighting;

    // 选中高亮：叠加亮橙色
    if (u_IsSelected == 1) {
        finalColor = mix(finalColor, vec3(1.0, 0.6, 0.0), 0.4);
    }

    FragColor = vec4(finalColor, u_BaseColor.a);
}