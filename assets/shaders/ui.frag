#version 460 core
// ─── UI/覆盖层 片段着色器 ───
// 支持：
//   - 字体纹理渲染（R 通道作为 alpha）
//   - 常规纹理渲染（RGBA）
//   - 纯色渲染
// 通过 u_Mode 控制渲染模式。

in vec2 v_TexCoord;
in vec4 v_Color;

out vec4 FragColor;

uniform sampler2D u_Texture;

// 渲染模式：0=纯色 (仅 v_Color)，1=字体纹理 (R→alpha)，2=常规纹理 (RGBA)
uniform int  u_Mode = 0;
uniform bool u_EnableAlpha = true;

void main() {
    vec4 finalColor = v_Color;

    if (u_Mode == 1) {
        // ── 字体渲染模式 ──
        // 纹理 R 通道存放字符覆盖率，作为 alpha 使用
        float textAlpha = texture(u_Texture, v_TexCoord).r;
        finalColor.a *= textAlpha;

    } else if (u_Mode == 2) {
        // ── 常规纹理渲染模式 ──
        vec4 texColor = texture(u_Texture, v_TexCoord);
        finalColor *= texColor;
    }

    // u_EnableAlpha = false 时强制不透明（用于纯色矩形等）
    if (!u_EnableAlpha) {
        finalColor.a = 1.0;
    }

    if (finalColor.a < 0.005) discard;
    FragColor = finalColor;
}
