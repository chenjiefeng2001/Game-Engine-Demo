#version 460 core
// ─── UI/覆盖层 顶点着色器 ───
// 使用正交投影矩阵将屏幕坐标变换到 NDC。
// 输入布局与 PrimitiveVertex 一致。

layout(location = 0) in vec3 aPos;      // 屏幕坐标 (pixels) 或 NDC
layout(location = 1) in vec3 aNormal;   // 未使用，保留占位
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aColor;

uniform mat4 u_Projection = mat4(1.0);  // 正交投影矩阵（默认单位阵 = NDC 直通）

out vec2 v_TexCoord;
out vec4 v_Color;

void main() {
    v_TexCoord = aTexCoord;
    v_Color    = aColor;
    // UI 是 2D 的，Z 拍平在最顶层
    gl_Position = u_Projection * vec4(aPos.xy, 0.0, 1.0);
}
