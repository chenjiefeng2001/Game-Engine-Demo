#version 460 core

// 深度预渲染 — 不输出任何颜色，仅写入深度缓冲
// OpenGL 在片段着色器执行后会自动写入 gl_FragDepth
void main() {
    // 空片段着色器：无颜色输出，仅深度写入
}
