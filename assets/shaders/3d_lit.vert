#version 460 core

layout(location = 0) in vec3 aPos;      // 模型空间顶点坐标
layout(location = 1) in vec3 aNormal;    // 模型空间法线
layout(location = 2) in vec2 aTexCoord;  // 纹理坐标（UV）

// ═══════════════════════════════════════════════════════════
// 坐标空间变换矩阵
//
//   u_Model       = 模型空间 → 世界空间     (Model  Matrix)
//   u_View        = 世界空间 → 观察空间     (View   Matrix)
//   u_Projection  = 观察空间 → 裁剪空间     (Proj   Matrix)
//   u_NormalMatrix = transpose(inverse(u_Model))  法线空间变换
//   u_MVP         = Projection * View * Model     快捷组合
// ═══════════════════════════════════════════════════════════
uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Projection;
uniform mat4 u_NormalMatrix;
uniform mat4 u_MVP;

out vec3 v_WorldPos;    // 片元在世界空间中的位置
out vec3 v_ViewPos;     // 片元在观察空间中的位置
out vec3 v_Normal;      // 片元在世界空间中的法线
out vec2 v_TexCoord;

void main() {
    // 模型空间 → 世界空间
    vec4 worldPos = u_Model * vec4(aPos, 1.0);
    v_WorldPos  = worldPos.xyz;

    // 世界空间 → 观察空间
    v_ViewPos   = (u_View * worldPos).xyz;

    // 法线变换（必须使用 NormalMatrix 而非 Model 矩阵，
    // 否则非均匀缩放会破坏法线方向）
    v_Normal    = mat3(u_NormalMatrix) * aNormal;
    v_TexCoord  = aTexCoord;

    // 模型空间 → 裁剪空间（一步到位）
    gl_Position = u_MVP * vec4(aPos, 1.0);
}
