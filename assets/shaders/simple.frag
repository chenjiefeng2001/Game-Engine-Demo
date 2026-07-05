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
uniform int  u_EntityID   = 0;

// ★ 视图调试模式：0=Normal, 10=GBuffer_Normal, 11=GBuffer_Depth, 12=Albedo, 13=Roughness, 14=Metallic...
uniform int  u_ViewMode  = 0;

void main() {
    // 环境光
    vec3 ambient = 0.2 * u_BaseColor.rgb;

    // 漫反射
    vec3 norm = normalize(v_Normal);
    vec3 lightDir = normalize(-u_LightDir);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * u_LightColor * u_BaseColor.rgb;

    vec3 result = ambient + diffuse;

    // 选中高亮（在 Normal 模式下生效）
    if (u_IsSelected == 1) {
        result += vec3(0.8, 0.4, 0.0);
    }

    // ═══════════════════════════════════════════════════════════════
    // ★ G-Buffer 诊断视图
    // ═══════════════════════════════════════════════════════════════
    // ViewMode 枚举值来自 ViewMode.h:
    //   Normal=0, Unlit=1, Wireframe=2, ShadedWireframe=3
    //   GBuffer_Normal=10, GBuffer_Depth=11, GBuffer_Albedo=12
    //   GBuffer_Roughness=13, GBuffer_Metallic=14, GBuffer_Specular=15
    //   GBuffer_AO=16, GBuffer_Velocity=17
    //   LightingOnly=20, LightmapDensity=21, Overdraw=22
    //   ShaderComplexity=30, LODColoration=31, CollisionDebug=32

    if (u_ViewMode == 1) {
        // Unlit — 仅显示基础色，无光照
        result = u_BaseColor.rgb;
    }
    else if (u_ViewMode == 10) {
        // GBuffer_Normal — 法线映射到 0-1 颜色
        result = norm * 0.5 + 0.5;
    }
    else if (u_ViewMode == 11) {
        // GBuffer_Depth — 伪深度（基于片段距离）
        float depth = gl_FragCoord.z;
        result = vec3(depth);
    }
    else if (u_ViewMode == 12) {
        // GBuffer_Albedo — 仅基础色
        result = u_BaseColor.rgb;
    }
    else if (u_ViewMode == 13) {
        // GBuffer_Roughness — 伪粗糙度（用模型法线方差近似）
        float roughness = abs(norm.y); // 0=光滑(上), 1=粗糙(侧面)
        result = vec3(roughness);
    }
    else if (u_ViewMode == 14) {
        // GBuffer_Metallic — 伪金属度（高光强度近似）
        float metallic = diff * 0.5 + 0.5;
        result = vec3(metallic);
    }
    else if (u_ViewMode == 20) {
        // LightingOnly — 纯光照（灰色漫反射）
        result = vec3(diff * 0.8 + 0.2);
    }
    else if (u_ViewMode == 21) {
        // LightmapDensity — 伪光照贴图密度棋盘格
        ivec2 coord = ivec2(gl_FragCoord.xy);
        bool checker = ((coord.x / 8 + coord.y / 8) & 1) == 0;
        result = checker ? vec3(0.8, 0.2, 0.2) : vec3(0.2, 0.8, 0.2);
    }
    else if (u_ViewMode == 22) {
        // Overdraw — 伪半透明叠加热力图
        // 实际需要累积缓冲区，这里仅做示意：半透明红色
        result = vec3(1.0, 0.0, 0.0);
    }
    else if (u_ViewMode == 30) {
        // ShaderComplexity — 伪着色器复杂度
        result = vec3(0.2, 0.8, 0.2); // 绿色 = 简单
    }
    else if (u_ViewMode == 31) {
        // LODColoration — LOD 着色：红=高模
        result = vec3(1.0, 0.0, 0.0);
    }

    // 在所有模式下都写入 EntityID
    FragColor = vec4(result, u_BaseColor.a);
    EntityID = u_EntityID;
}