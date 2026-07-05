#pragma once

/**
 * @file ConstraintSolver.h
 * @brief 约束求解器 — 管理定位器与约束的求值
 *
 * ════════════════════════════════════════════
 * 架构
 * ════════════════════════════════════════════
 *
 *   ConstraintSolver 是约束系统的核心管理器，负责：
 *
 *   1. 管理 LocatorSystem — 添加/更新/查找定位器
 *   2. 管理约束列表 — 添加/移除/排序约束
 *   3. 按顺序求值所有约束 — 每个约束修改 PoseLocalData
 *   4. 跨系统定位器共享 — 导入外部定位器用于跨物体对接
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   ConstraintSolver solver;
 *
 *   // 添加定位器
 *   Locator handLoc("hand_attach", handBoneIndex);
 *   solver.AddLocator(handLoc);
 *
 *   // 添加约束
 *   auto* parent = solver.AddConstraint<ParentConstraint>(
 *       "object_hold", objectBoneIndex);
 *   parent->AddTarget(ConstraintTarget::FromLocator("hand_attach"));
 *   parent->SetMaintainOffset(true);
 *
 *   // 每帧更新
 *   solver.UpdateLocators(skeleton);
 *   solver.EvaluateAll(skeleton, pose);
 *
 *   // 跨物体对接：导入另一个角色的定位器
 *   solver.ImportExternalLocators(otherSolver.GetLocators());
 */

#include "Engine/Animation/Constraint.h"
#include "Engine/Animation/Locator.h"
#include "Engine/Animation/AnimationPipeline.h"
#include <memory>
#include <vector>
#include <string>
#include <algorithm>
#include <typeindex>
#include <unordered_map>

namespace Engine {

    // ============================================================
    // 约束求解器
    // ============================================================
    class ConstraintSolver {
    public:
        ConstraintSolver() = default;
        ~ConstraintSolver() = default;

        // 禁止拷贝
        ConstraintSolver(const ConstraintSolver&) = delete;
        ConstraintSolver& operator=(const ConstraintSolver&) = delete;
        ConstraintSolver(ConstraintSolver&&) = default;
        ConstraintSolver& operator=(ConstraintSolver&&) = default;

        // ════════════════════════════════════════════
        // 定位器管理
        // ════════════════════════════════════════════

        /** 添加定位器（名称重复则替换） */
        int32 AddLocator(const Locator& locator) { return m_LocatorSystem.AddLocator(locator); }

        /** 快速创建并添加骨骼定位器 */
        int32 AddBoneLocator(const std::string& name, int32 boneIndex,
                             const Vec3& offset = Vec3(0, 0, 0),
                             const Quat& rotOffset = Quat::Identity()) {
            return m_LocatorSystem.AddLocator(Locator(name, boneIndex, offset, rotOffset));
        }

        /** 快速创建并添加世界空间定位器 */
        int32 AddWorldLocator(const std::string& name,
                              const Vec3& position = Vec3(0, 0, 0)) {
            Locator loc(name);
            loc.worldPosition = position;
            return m_LocatorSystem.AddLocator(loc);
        }

        /** 移除定位器 */
        void RemoveLocator(const std::string& name) { m_LocatorSystem.RemoveLocator(name); }

        /** 查找定位器 */
        Locator* FindLocator(const std::string& name) { return m_LocatorSystem.FindLocator(name); }

        /** 更新所有定位器的世界空间位置 */
        void UpdateLocators(const Skeleton* skeleton) { m_LocatorSystem.Update(skeleton); }

        /** 获取定位器系统引用 */
        LocatorSystem& GetLocatorSystem() noexcept { return m_LocatorSystem; }
        const LocatorSystem& GetLocatorSystem() const noexcept { return m_LocatorSystem; }

        // ════════════════════════════════════════════
        // 约束管理
        // ════════════════════════════════════════════

        /**
         * @brief 添加约束
         * @tparam T 约束类型（PointConstraint/OrientConstraint/等）
         * @param name 约束名称
         * @param args 传递给约束构造函数的参数
         * @return 约束指针
         */
        template<typename T, typename... Args>
        T* AddConstraint(const std::string& name, Args&&... args) {
            auto constraint = std::make_unique<T>(name, std::forward<Args>(args)...);
            T* ptr = constraint.get();
            m_Constraints.push_back(std::move(constraint));
            return ptr;
        }

        /**
         * @brief 添加已创建的约束
         * @param constraint 约束唯一指针
         */
        IConstraint* AddConstraint(std::unique_ptr<IConstraint> constraint) {
            IConstraint* ptr = constraint.get();
            m_Constraints.push_back(std::move(constraint));
            return ptr;
        }

        /** 按名称移除约束 */
        bool RemoveConstraint(const std::string& name);

        /** 按名称查找约束 */
        IConstraint* FindConstraint(const std::string& name);
        const IConstraint* FindConstraint(const std::string& name) const;

        /** 清空所有约束 */
        void ClearConstraints();

        /** 获取约束数量 */
        size_t GetConstraintCount() const noexcept { return m_Constraints.size(); }

        /** 按索引获取约束 */
        IConstraint* GetConstraint(size_t index) {
            return index < m_Constraints.size() ? m_Constraints[index].get() : nullptr;
        }
        const IConstraint* GetConstraint(size_t index) const {
            return index < m_Constraints.size() ? m_Constraints[index].get() : nullptr;
        }

        /** 启用/禁用所有约束 */
        void SetAllEnabled(bool enabled);

        // ════════════════════════════════════════════
        // 求值
        // ════════════════════════════════════════════

        /**
         * @brief 求值所有已启用的约束
         * @param skeleton 目标骨骼（读取世界姿势用于 LookAt/Aim）
         * @param pose     局部姿势数据（约束将修改此处）
         *
         * 约束按添加顺序依次求值。
         * 求值前会自动更新定位器。
         */
        void EvaluateAll(Skeleton& skeleton, PoseLocalData& pose);

        /**
         * @brief 仅更新定位器并求值约束
         * @param skeleton 目标骨骼
         * @param pose     局部姿势数据
         * @param dt       时间步长（预留，当前未使用）
         */
        void Update(Skeleton& skeleton, PoseLocalData& pose, float32 dt);

        // ════════════════════════════════════════════
        // 跨物体对接
        // ════════════════════════════════════════════

        /**
         * @brief 导入外部定位器（用于跨物体约束）
         * @param externalLocators 外部定位器列表
         *
         * 同名定位器将被覆盖。这允许一个角色的约束
         * 引用另一个角色的定位器作为目标。
         */
        void ImportExternalLocators(const std::vector<Locator>& externalLocators) {
            m_LocatorSystem.ImportLocators(externalLocators);
        }

        /** 导出定位器（供其他角色使用） */
        const std::vector<Locator>& GetLocatorsForExport() const {
            return m_LocatorSystem.GetLocators();
        }

        /**
         * @brief 设置外部定位器引用
         * @param externalLocators 指向外部定位器列表的指针
         *
         * 设置后，EvaluateAll 在求值约束目标时会同时搜索
         * 内部和外部定位器。
         */
        void SetExternalLocatorRef(const std::vector<Locator>* externalLocators) {
            m_ExternalLocatorRef = externalLocators;
        }

        // ════════════════════════════════════════════
        // 调试
        // ════════════════════════════════════════════

        struct DebugInfo {
            size_t locatorCount;
            size_t constraintCount;
            std::vector<std::string> constraintNames;
            std::vector<std::string> constraintTypes;
            std::vector<bool> constraintEnabled;
        };

        DebugInfo GetDebugInfo() const;

    private:
        LocatorSystem m_LocatorSystem;
        std::vector<std::unique_ptr<IConstraint>> m_Constraints;

        // 外部定位器引用（跨物体对接时不复制，直接引用）
        const std::vector<Locator>* m_ExternalLocatorRef = nullptr;

        /**
         * @brief 获取合并的定位器列表用于约束求值
         * @return 定位器列表的 const 引用
         *
         * 合并内部定位器与外部引用定位器。
         * 注意：此方法返回临时 vector，仅用于求值期间。
         */
        std::vector<Locator> GetMergedLocators() const;
    };

} // namespace Engine
