#version 460 core

/**
 * @file post_fullscreen_vs.glsl
 * @brief 全屏四边形顶点着色器 — 所有后处理效果共享
 *
 * 绘制一个覆盖整个 NDC 的四边形，不使用任何顶点缓冲。
 * 输出纹理坐标 (uv) 供片段着色器采样。
 */

layout(location = 0) out vec2 v_UV;

void main() {
    // 使用顶点 ID 生成全屏四边形
    // 三角形条带 (0, 1, 2) → (2, 1, 3)
    uint idx = gl_VertexIndex;
    float x = float(idx & 1u) * 2.0;
    float y = float((idx >> 1u) & 1u) * 2.0;

    v_UV = vec2(x, y);
    gl_Position = vec4(x * 2.0 - 1.0, y * 2.0 - 1.0, 0.0, 1.0);
}