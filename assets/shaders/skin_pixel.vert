#version 450
// GPU 蒙皮像素验证的顶点着色器（Vulkan 路线专用）。
//
// 为什么与 assets/shaders/skinned_lit.* 是两份而不是一份：
// 产品那份是 GLSL 330 + **松散 uniform**（uniform mat4 u_ViewProjection;
// uniform mat4 u_BoneMatrices[64]），这是刻意选的 —— uniform block 内的成员在 GL
// 里无法按名字寻址（实测 glGetUniformLocation 返回 -1），而 Shader::SetMat4 /
// SetMat4Array 正是按名字驱动的。SPIR-V 没有松散 uniform 这一概念，它们必须变成
// set/binding 上的 std140 UBO，因此同一份 GLSL 编译不成 SPIR-V。
//
// 把产品着色器改造成 UBO 并让产品自己也走 Vulkan，属于把产品蒙皮绘制路径移植到
// Vulkan 的一部分（见 docs/Handoff-CI-Remaining-Failures.md §7 未覆盖项 1），
// 与像素验证本身是两件事。这里只提供验证用的那一份。
//
// 骨骼索引是 ivec4：与共享顶点布局 SkinVertex::boneIndices（int[4]）一致，
// 对应 Format::RGBA32_SInt。

layout(location = 0) in vec3  a_Position;
layout(location = 1) in ivec4 a_BoneIndices;
layout(location = 2) in vec4  a_BoneWeights;

layout(set = 0, binding = 0, std140) uniform SkinParams {
    mat4 u_ViewProjection;
    mat4 u_BoneMatrices[2];   // std140：每元素 64 字节，整块 64 + 128 = 192 字节
} params;

void main() {
    mat4 skin = params.u_BoneMatrices[a_BoneIndices.x] * a_BoneWeights.x
              + params.u_BoneMatrices[a_BoneIndices.y] * a_BoneWeights.y
              + params.u_BoneMatrices[a_BoneIndices.z] * a_BoneWeights.z
              + params.u_BoneMatrices[a_BoneIndices.w] * a_BoneWeights.w;
    gl_Position = params.u_ViewProjection * (skin * vec4(a_Position, 1.0));
}
