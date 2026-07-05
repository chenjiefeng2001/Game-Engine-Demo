#include "Engine/Core/SubsystemManager.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace {
    Engine::Logger s_Log("SubsystemManager");
}

namespace Engine {

// ============================================================
// 注册子系统
// ============================================================

SubsystemManager& SubsystemManager::Add(const SubsystemDesc& desc) {
    m_Subsystems.push_back({desc});
    return *this;
}

SubsystemManager& SubsystemManager::Add(std::string name, SubsystemPhase phase,
                                        std::function<bool()> init,
                                        std::function<void()> shutdown) {
    m_Subsystems.push_back({SubsystemDesc(
        std::move(name), phase, std::move(init), std::move(shutdown))});
    return *this;
}

// ============================================================
// 初始化全部子系统（按阶段排序 → 按注册顺序）
// ============================================================

bool SubsystemManager::Initialize() {
    if (m_Initialized) {
        s_Log.Warn("Already initialized, skipping.");
        return true;
    }

    if (m_Subsystems.empty()) {
        s_Log.Error("No subsystems registered!");
        return false;
    }

    // ── 1. 按阶段排序 ──
    std::stable_sort(m_Subsystems.begin(), m_Subsystems.end(),
        [](const SubsystemEntry& a, const SubsystemEntry& b) {
            return static_cast<uint8>(a.desc.phase) < static_cast<uint8>(b.desc.phase);
        });

    // ── 2. 逐个子系统初始化 ──
    size_t total = m_Subsystems.size();
    SubsystemPhase currentPhase = m_Subsystems[0].desc.phase;
    s_Log.Info("");
    s_Log.Info("==============================================");
    s_Log.Info("  Engine Subsystem Initialization — Begin");
    s_Log.Info("==============================================");

    for (size_t i = 0; i < total; ++i) {
        const auto& entry = m_Subsystems[i];

        // 阶段标题
        if (entry.desc.phase != currentPhase) {
            currentPhase = entry.desc.phase;
            s_Log.Info("─── Phase {} ───", static_cast<int>(currentPhase));
        }

        s_Log.Info("  [{}] initializing...", entry.desc.name);

        if (!entry.desc.onInit) {
            s_Log.Warn("  [{}] SKIPPED (no callback)", entry.desc.name);
            continue;
        }

        bool ok = entry.desc.onInit();
        if (ok) {
            s_Log.Info("  [{}] OK", entry.desc.name);
            m_Succeeded.push_back(i);
        } else {
            s_Log.Error("  [{}] FAILED", entry.desc.name);
            m_Errors.push_back(entry.desc.name + " failed at phase " +
                               std::to_string(static_cast<int>(entry.desc.phase)));
            s_Log.Error("FATAL: {} initialization failed. Aborting.", entry.desc.name);
            Shutdown();
            return false;
        }
    }

    m_Initialized = true;
    s_Log.Info("==============================================");
    s_Log.Info("  Engine Subsystem Initialization — All {} subsystems completed successfully", m_Succeeded.size());
    s_Log.Info("==============================================");
    return true;
}

// ============================================================
// 逆序关闭
// ============================================================

void SubsystemManager::Shutdown() {
    if (!m_Initialized && m_Succeeded.empty())
        return;

    s_Log.Info("--- Engine Subsystem Shutdown ---");

    // 逆序遍历 m_Succeeded
    for (auto it = m_Succeeded.rbegin(); it != m_Succeeded.rend(); ++it) {
        const auto& entry = m_Subsystems[*it];
        if (entry.desc.onShutdown) {
            s_Log.Info("  [{}] shutting down...", entry.desc.name);
            entry.desc.onShutdown();
        }
    }

    m_Succeeded.clear();
    m_Initialized = false;
    s_Log.Info("--- Engine Subsystem Shutdown Complete ---");
}

// ============================================================
// 查询
// ============================================================

bool SubsystemManager::HasSubsystem(const std::string& name) const {
    for (const auto& entry : m_Subsystems) {
        if (entry.desc.name == name)
            return true;
    }
    return false;
}

std::vector<std::string> SubsystemManager::GetSubsystemNames() const {
    std::vector<std::string> names;
    names.reserve(m_Subsystems.size());
    for (const auto& entry : m_Subsystems) {
        names.push_back(entry.desc.name);
    }
    return names;
}

} // namespace Engine
