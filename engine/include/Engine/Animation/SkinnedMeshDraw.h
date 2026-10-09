/**
 * @file SkinnedMeshDraw.h
 * @brief GPU 蒙皮绘制 —— 产品渲染路径与 headless 验证共用的同一份实现
 *
 * 存在的理由：骨骼网格的绘制步骤此前只存在于 EditorDemoApp 内，测试无法
 * 触达，"产品分支是否真的能画出正确结果"只能靠读代码推断。这里把绘制步骤
 * 提取为独立函数，EditorDemo 与 headless 测试**调用同一份实现**，而不是各自
 * 维护一份看起来相似的流程。
 *
 * 语义：
 * - 顶点先被骨骼矩阵变换（模型空间），随后由 viewProjection 施加视图与物体
 *   变换。skinned_lit 没有 u_Model，调用方应传入已含物体变换的矩阵。
 * - 骨骼矩阵为空或索引数为 0 时不绘制：宁可不画，也不用未初始化数据出图。
 */
#pragma once
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <cstdint>
#include <functional>
#include <vector>

namespace Engine {

/**
 * @brief 用真实的骨骼矩阵绘制一次索引网格
 *
 * @param shader          已持有 skinned_lit 的着色器
 * @param vao             含骨骼顶点布局（location 0..5）的顶点数组
 * @param indexCount      索引数量
 * @param boneMatrices    当前姿势的蒙皮矩阵，数量与骨骼数一致
 * @param viewProjection  视图投影与物体变换的乘积
 * @param issueIndexedDraw 实际的 indexed draw 调用（需要 OpenGL 上下文，
 *                        因此由调用方注入）
 *
 * @return 是否真的发起了绘制
 */
inline bool DrawSkinnedMeshIndexed(Shader& shader,
                                   VertexArray& vao,
                                   uint32 indexCount,
                                   const std::vector<Mat4>& boneMatrices,
                                   const Mat4& viewProjection,
                                   const std::function<void(uint32)>& issueIndexedDraw)
{
    if (indexCount == 0 || boneMatrices.empty() || !issueIndexedDraw) {
        return false;
    }

    shader.Bind();
    shader.SetMat4("u_ViewProjection", viewProjection.Data());
    // count 与实际骨骼数一致：让着色器只读到真实存在的矩阵。
    shader.SetMat4Array("u_BoneMatrices",
                        boneMatrices[0].Data(),
                        static_cast<uint32>(boneMatrices.size()));

    vao.Bind();
    issueIndexedDraw(indexCount);
    return true;
}

} // namespace Engine
