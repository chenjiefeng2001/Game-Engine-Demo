#pragma once

/**
 * @file Skeleton.h
 * @brief 骨骼层级 — 管理骨骼树、绑定姿势、蒙皮矩阵数组
 *
 * Skeleton 管理一组 Bone 组成的层级结构（树）。
 * 负责：
 *   - 骨骼的添加与查找
 *   - 从当前局部姿势矩阵递归计算世界姿势矩阵
 *   - 生成最终蒙皮矩阵数组（供 GPU uniform 上传）
 *   - 绑定姿势的加载与序列化
 */

#include "Engine/Animation/Bone.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace Engine {

    /**
     * @brief 骨骼层级 — 管理整个骨骼树
     *
     * 使用示例：
     * @code
     *   Skeleton skeleton;
     *   skeleton.AddRootBone("Hips", hipBindMatrix);
     *   skeleton.AddBone("Spine", "Hips", spineBindMatrix);
     *   skeleton.AddBone("Head", "Spine", headBindMatrix);
     *
     *   // 每帧更新动画姿势后：
     *   skeleton.UpdateWorldPoses();
     *   const auto& matrices = skeleton.GetSkinningMatrices();
     *   // 上传蒙皮矩阵到 GPU（通过渲染器接口）
     * @endcode
     */
    class Skeleton {
    public:
        Skeleton() = default;
        ~Skeleton() = default;

        // 禁止拷贝（允许移动）
        Skeleton(const Skeleton&) = delete;
        Skeleton& operator=(const Skeleton&) = delete;
        Skeleton(Skeleton&&) = default;
        Skeleton& operator=(Skeleton&&) = default;

        // ── 骨骼构建 ──

        /**
         * @brief 添加根骨骼
         * @param name      骨骼名称
         * @param bindMatrix 模型空间 → 骨骼局部空间的绑定矩阵
         * @return 骨骼索引
         */
        int32 AddRootBone(const std::string& name, const Mat4& bindMatrix);

        /**
         * @brief 添加子骨骼
         * @param name       骨骼名称
         * @param parentName 父骨骼名称
         * @param bindMatrix 模型空间 → 骨骼局部空间的绑定矩阵
         * @return 骨骼索引，若父骨骼不存在则返回 -1
         */
        int32 AddBone(const std::string& name, const std::string& parentName,
                      const Mat4& bindMatrix);

        /**
         * @brief 直接添加一个 Bone 对象
         * @param bone 骨骼对象
         * @return 骨骼索引
         */
        int32 AddBone(const Bone& bone);

        // ── 查找 ──

        /** 按名称查找骨骼索引 */
        int32 FindBoneIndex(const std::string& name) const;

        /** 按索引获取骨骼引用 */
        Bone&       GetBone(int32 index);
        const Bone& GetBone(int32 index) const;

        /** 获取骨骼数量 */
        size_t GetBoneCount() const noexcept { return m_Bones.size(); }

        /** 检查是否有骨骼 */
        bool IsValid() const noexcept { return !m_Bones.empty(); }

        // ── 姿势更新 ──

        /**
         * @brief 从当前局部姿势矩阵递归计算世界姿势矩阵和蒙皮矩阵
         *
         * 应当每帧在动画更新局部姿势矩阵后调用。
         * 计算流程：
         *   1. 从根骨骼开始，累乘 localPoseMatrix 到 currentPoseMatrix
         *   2. 对每个骨骼计算 skinningMatrix = currentPoseMatrix * inverseBindMatrix
         */
        void UpdateWorldPoses();

        /**
         * @brief 将骨骼重置到绑定姿势
         *
         * 每个骨骼的 localPoseMatrix 被设为 bindMatrix 的逆（即 bind pose 下的 world matrix），
         * 这样 currentPoseMatrix 恢复为 bind pose。
         */
        void ResetToBindPose();

        // ── 蒙皮矩阵数组（供 GPU 上传） ──

        /** 获取蒙皮矩阵数组（const float* 指针，可直接上传到 GPU uniform） */
        const std::vector<Mat4>& GetSkinningMatrices() const noexcept {
            return m_SkinningMatrices;
        }

        /** 获取蒙皮矩阵的 float* 指针（用于 glUniformMatrix4fv） */
        const float* GetSkinningMatrixData() const {
            return m_SkinningMatrices.empty()
                       ? nullptr
                       : m_SkinningMatrices[0].Data();
        }

        /** 获取骨骼数量（最大矩阵数量） */
        uint32 GetMatrixCount() const noexcept {
            return static_cast<uint32>(m_SkinningMatrices.size());
        }

    private:
        /** 递归更新骨骼及其子骨骼的世界姿势 */
        void UpdateBoneWorldPose(int32 boneIndex, const Mat4& parentWorld);

        std::vector<Bone>           m_Bones;
        std::vector<int32>          m_RootBones;        ///< 根骨骼索引列表
        std::unordered_map<std::string, int32> m_NameToIndex;

        // 缓存：蒙皮矩阵数组（与 m_Bones 一一对应）
        std::vector<Mat4>           m_SkinningMatrices;
    };

} // namespace Engine
