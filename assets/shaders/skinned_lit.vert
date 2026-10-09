#version 430 core

// GPU 蒙皮顶点着色器（生产路径）
//
// 与 SkinnedVertex 的布局严格对应（见 engine/include/Engine/Animation/SkinnedMesh.h）：
//   location 0 = position     (vec3)
//   location 1 = normal       (vec3)
//   location 2 = texCoord     (vec2)
//   location 3 = tangent      (vec3)
//   location 4 = boneIndices  (ivec4)
//   location 5 = boneWeights  (vec4)
//
// 顶点着色器在此完成经典的 4 骨骼加权混合：
//   skinned = Σ w_i * (u_BoneMatrices[i] * pos)
//
// u_BoneMatrices 由 CPU 侧 SkinningComponent::GetSkinningMatrices() 提供，
// 经 Shader::SetMat4Array 一次上传整组矩阵。

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Tangent;
layout(location = 4) in ivec4 a_BoneIndices;
layout(location = 5) in vec4  a_BoneWeights;

// 刻意使用**普通 uniform**而非 std140 uniform block。
// uniform block 内的成员无法通过 glGetUniformLocation 按名字寻址
// （实测返回 -1），因此 Shader::SetMat4 / SetMat4Array 无法驱动它们。
// 引擎既有的 uniform（如 u_ViewProjection）同样按名字设置，此处保持一致。
uniform mat4 u_ViewProjection;
uniform mat4 u_BoneMatrices[64];

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec2 v_TexCoord;

void main()
{
    // 权重未归一化时也能自洽：先除以总和，避免 bind pose 之外的姿态整体缩放。
    float weightSum = a_BoneWeights.x + a_BoneWeights.y
                    + a_BoneWeights.z + a_BoneWeights.w;
    vec4 weights = weightSum > 0.0 ? a_BoneWeights / weightSum
                                   : vec4(1.0, 0.0, 0.0, 0.0);

    int i0 = a_BoneIndices.x;
    int i1 = a_BoneIndices.y;
    int i2 = a_BoneIndices.z;
    int i3 = a_BoneIndices.w;

    mat4 skin = u_BoneMatrices[i0] * weights.x
              + u_BoneMatrices[i1] * weights.y
              + u_BoneMatrices[i2] * weights.z
              + u_BoneMatrices[i3] * weights.w;

    vec4 skinnedPos = skin * vec4(a_Position, 1.0);

    // 法线按同一线性混合近似（骨骼为刚体变换时精确）。
    mat3 skinRot = mat3(skin);
    v_WorldPos = skinnedPos.xyz;
    v_Normal   = normalize(skinRot * a_Normal);
    v_TexCoord = a_TexCoord;

    gl_Position = u_ViewProjection * skinnedPos;
}
