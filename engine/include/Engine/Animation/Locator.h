#pragma once

/**
 * @file Locator.h
 * @brief 定位器系统 — 世界空间参考点与跨物体对接
 *
 * ════════════════════════════════════════════
 * 定位器概念
 * ════════════════════════════════════════════
 *
 * 定位器（Locator）是一个可命名的世界空间参考点。
 * 它可以：
 *   1. 依附于骨骼 — 随着骨骼动画移动（如"持物点"）
 *   2. 世界空间固定 — 场景中的参考位置（如"目标位置"）
 *   3. 跨物体引用 — 一个物体的定位器可作为另一物体约束的目标
 *
 * ════════════════════════════════════════════
 * 使用场景
 * ════════════════════════════════════════════
 *
 *   // 角色 A 的手部定位器
 *   Locator handLoc("player_hand", handBoneIndex,
 *                   Vec3(0,0,0), Quat::Identity());
 *
 *   // 角色 B 的约束使用该定位器作为目标
 *   auto* constraint = solver.AddConstraint<PointConstraint>("grab");
 *   constraint->SetAffectedBone(boneIndex);
 *   constraint->AddTarget(ConstraintTarget::FromLocator("player_hand"));
 */

#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Animation/Skeleton.h"
#include <string>
#include <vector>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 定位器
    // ============================================================
    struct Locator {
        std::string name;              ///< 定位器名称（在 LocatorSystem 中唯一）
        bool        worldSpace = true; ///< 世界空间固定点
        int32       boneIndex  = -1;   ///< 依附的骨骼索引（-1 表示不依附）

        // ── 相对于骨骼的偏移（boneIndex >= 0 时生效） ──
        Vec3 localPosition{0, 0, 0};
        Quat localRotation{0, 0, 0, 1};
        Vec3 localScale{1, 1, 1};

        // ── 计算结果：世界空间变换 ──
        Vec3 worldPosition{0, 0, 0};
        Quat worldRotation{0, 0, 0, 1};
        Vec3 worldScale{1, 1, 1};

        Locator() = default;

        /** 创建世界空间固定定位器 */
        explicit Locator(std::string name_)
            : name(std::move(name_)), worldSpace(true) {}

        /** 创建依附于骨骼的定位器 */
        Locator(std::string name_, int32 boneIdx,
                const Vec3& pos = Vec3(0, 0, 0),
                const Quat& rot = Quat::Identity())
            : name(std::move(name_)), worldSpace(false), boneIndex(boneIdx)
            , localPosition(pos), localRotation(rot) {}

        /**
         * @brief 计算定位器的世界空间变换
         * @param skeleton 当前骨骼（用于 boneIndex 查找）
         *
         * 若 worldSpace==true 且 boneIndex<0，保留已有 worldPosition。
         * 若依附于骨骼，从骨骼的 currentPoseMatrix 分解出 T/R/S，
         * 再叠加上 localPosition/localRotation/localScale 偏移。
         */
        void Compute(const Skeleton* skeleton);
    };

    // ============================================================
    // 定位器系统
    // ============================================================
    class LocatorSystem {
    public:
        LocatorSystem() = default;
        ~LocatorSystem() = default;

        // ── 管理 ──

        /** 添加定位器。若名称已存在则替换。返回索引。 */
        int32 AddLocator(const Locator& locator);

        /** 移除指定名称的定位器 */
        void RemoveLocator(const std::string& name);

        /** 按名称查找定位器 */
        Locator* FindLocator(const std::string& name);
        const Locator* FindLocator(const std::string& name) const;

        /** 清空所有定位器 */
        void Clear();

        // ── 更新 ──

        /** 更新所有定位器的世界空间位置 */
        void Update(const Skeleton* skeleton);

        /**
         * @brief 更新指定骨骼引用的定位器
         * @param skeleton 骨骼数据
         * @param filterBoneIndex 只更新依附于此骨骼的定位器
         */
        void UpdateForBone(const Skeleton* skeleton, int32 filterBoneIndex);

        // ── 访问 ──

        std::vector<Locator>& GetAll() noexcept { return m_Locators; }
        const std::vector<Locator>& GetAll() const noexcept { return m_Locators; }
        size_t GetCount() const noexcept { return m_Locators.size(); }

        // ── 跨系统共享（用于跨物体对接） ──

        /**
         * @brief 导入外部定位器（从另一个 LocatorSystem）
         * @param external 外部定位器列表
         *
         * 同名定位器会被覆盖。这允许一个角色的 LocatorSystem
         * 引用另一个角色的定位器作为约束目标。
         */
        void ImportLocators(const std::vector<Locator>& external);

        /** 导出所有定位器（供其他系统使用） */
        const std::vector<Locator>& GetLocators() const noexcept { return m_Locators; }

    private:
        std::vector<Locator> m_Locators;
    };

} // namespace Engine
