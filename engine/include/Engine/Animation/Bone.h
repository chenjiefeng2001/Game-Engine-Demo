#pragma once

/**
 * @file Bone.h
 * @brief 骨骼数据定义 — 单个骨骼的绑定姿势/当前姿势矩阵
 *
 * Bone 是骨骼动画的基本单元。
 * 每个 Bone 存储其名称、父级索引、绑定姿势矩阵和当前动画姿势矩阵。
 *
 * 矩阵管线：
 *   蒙皮矩阵 = currentPoseWorldMatrix * inverseBindMatrix
 *   该矩阵将顶点从 模型空间·绑定姿势 直接变换到 模型空间·当前姿势
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <string>

namespace Engine {

    /**
     * @brief 骨骼 — 描述关节的绑定姿势与当前姿势
     *
     * 约定：
     *   - bindMatrix:        绑定姿势下，模型空间→该骨骼局部空间的变换矩阵
     *                         即顶点从模型空间变换到该骨骼的局部坐标系
     *   - inverseBindMatrix: bindMatrix 的逆矩阵（预计算加速）
     *                         即顶点从该骨骼局部空间→模型空间的变换
     *                         等价于 bindPose 下该骨骼的世界矩阵
     *   - currentPoseMatrix: 当前动画姿势下，该骨骼的局部→世界变换矩阵
     *   - skinningMatrix:    currentPoseMatrix * inverseBindMatrix
     *                         直接将顶点从 绑定姿势模型空间 → 当前姿势模型空间
     */
    struct Bone {
        std::string name;               ///< 骨骼名称（用于匹配动画轨道）
        int32       parentIndex = -1;    ///< 父骨骼索引（-1 表示根骨骼）

        // ── 绑定姿势矩阵 ──
        Mat4 bindMatrix;                ///< 模型空间 → 骨骼局部空间（绑定姿势）
        Mat4 inverseBindMatrix;         ///< bindMatrix 的逆（绑定姿势下骨骼的世界矩阵）

        // ── 当前姿势矩阵（动画每帧更新） ──
        Mat4 localPoseMatrix;           ///< 当前姿势下，相对于父骨骼的局部变换
        Mat4 currentPoseMatrix;         ///< 当前姿势下，该骨骼的世界矩阵（从根累乘）
        Mat4 skinningMatrix;            ///< 蒙皮矩阵 = currentPoseMatrix * inverseBindMatrix

        Bone() = default;

        Bone(const std::string& name_, int32 parentIndex_,
             const Mat4& bindMatrix_)
            : name(name_)
            , parentIndex(parentIndex_)
            , bindMatrix(bindMatrix_) {
            ComputeInverseBind();
        }

        /** 根据 bindMatrix 计算 inverseBindMatrix */
        void ComputeInverseBind();

        /** 计算蒙皮矩阵 = currentPoseMatrix * inverseBindMatrix */
        void ComputeSkinningMatrix() {
            Mat4Multiply(currentPoseMatrix, inverseBindMatrix, skinningMatrix);
        }
    };

} // namespace Engine
