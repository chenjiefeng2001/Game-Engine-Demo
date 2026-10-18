#version 450
// GPU 蒙皮像素验证的片元着色器（Vulkan 路线专用）。原因见 skin_pixel.vert。

layout(location = 0) out vec4 outColor;

void main() {
    // 固定输出一个明显亮于清除色的颜色：清除色是 (0,0,0,1)，共享断言的口径是
    // 「任一通道 > 8 即为亮」，因此几何与背景在字节层面必然可区分。
    outColor = vec4(0.25, 0.55, 0.85, 1.0);
}
