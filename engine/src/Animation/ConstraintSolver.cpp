#include "Engine/Animation/ConstraintSolver.h"

namespace Engine {

    // ══════════════════════════════════════════════════════════════
    // 约束管理
    // ══════════════════════════════════════════════════════════════

    bool ConstraintSolver::RemoveConstraint(const std::string& name) {
        auto it = std::find_if(m_Constraints.begin(), m_Constraints.end(),
            [&](const std::unique_ptr<IConstraint>& c) { return c->GetName() == name; });
        if (it != m_Constraints.end()) {
            m_Constraints.erase(it);
            return true;
        }
        return false;
    }

    IConstraint* ConstraintSolver::FindConstraint(const std::string& name) {
        auto it = std::find_if(m_Constraints.begin(), m_Constraints.end(),
            [&](const std::unique_ptr<IConstraint>& c) { return c->GetName() == name; });
        return it != m_Constraints.end() ? it->get() : nullptr;
    }

    const IConstraint* ConstraintSolver::FindConstraint(const std::string& name) const {
        auto it = std::find_if(m_Constraints.begin(), m_Constraints.end(),
            [&](const std::unique_ptr<IConstraint>& c) { return c->GetName() == name; });
        return it != m_Constraints.end() ? it->get() : nullptr;
    }

    void ConstraintSolver::ClearConstraints() {
        m_Constraints.clear();
    }

    void ConstraintSolver::SetAllEnabled(bool enabled) {
        for (auto& c : m_Constraints) {
            c->SetEnabled(enabled);
        }
    }

    // ══════════════════════════════════════════════════════════════
    // 求值
    // ══════════════════════════════════════════════════════════════

    std::vector<Locator> ConstraintSolver::GetMergedLocators() const {
        if (!m_ExternalLocatorRef || m_ExternalLocatorRef->empty()) {
            return m_LocatorSystem.GetLocators();
        }

        // 合并内部和外部定位器，内部优先
        std::vector<Locator> merged = m_LocatorSystem.GetLocators();
        for (const auto& ext : *m_ExternalLocatorRef) {
            // 检查是否已存在同名定位器
            auto it = std::find_if(merged.begin(), merged.end(),
                [&](const Locator& l) { return l.name == ext.name; });
            if (it == merged.end()) {
                merged.push_back(ext);
            }
            // 若已存在，保留内部定位器
        }
        return merged;
    }

    void ConstraintSolver::EvaluateAll(Skeleton& skeleton, PoseLocalData& pose) {
        // 更新定位器
        m_LocatorSystem.Update(&skeleton);

        // 获取合并的定位器列表
        std::vector<Locator> mergedLocators = GetMergedLocators();

        // 按顺序求值每个约束
        for (auto& constraint : m_Constraints) {
            if (!constraint->IsEnabled()) continue;
            if (constraint->GetAffectedBone() < 0) continue;

            // 解析骨骼名称目标
            constraint->ResolveBoneNames(skeleton);

            // 求值
            constraint->Evaluate(skeleton, mergedLocators, pose);
        }
    }

    void ConstraintSolver::Update(Skeleton& skeleton, PoseLocalData& pose, float32 /*dt*/) {
        EvaluateAll(skeleton, pose);
    }

    // ══════════════════════════════════════════════════════════════
    // 调试
    // ══════════════════════════════════════════════════════════════

    ConstraintSolver::DebugInfo ConstraintSolver::GetDebugInfo() const {
        DebugInfo info;
        info.locatorCount = m_LocatorSystem.GetCount();
        info.constraintCount = m_Constraints.size();

        for (const auto& c : m_Constraints) {
            info.constraintNames.push_back(c->GetName());
            info.constraintEnabled.push_back(c->IsEnabled());

            switch (c->GetType()) {
                case ConstraintType::Point:   info.constraintTypes.push_back("Point"); break;
                case ConstraintType::Orient:  info.constraintTypes.push_back("Orient"); break;
                case ConstraintType::Scale:   info.constraintTypes.push_back("Scale"); break;
                case ConstraintType::Parent:  info.constraintTypes.push_back("Parent"); break;
                case ConstraintType::LookAt:  info.constraintTypes.push_back("LookAt"); break;
                case ConstraintType::Aim:     info.constraintTypes.push_back("Aim"); break;
                default: info.constraintTypes.push_back("Unknown"); break;
            }
        }

        return info;
    }

} // namespace Engine
