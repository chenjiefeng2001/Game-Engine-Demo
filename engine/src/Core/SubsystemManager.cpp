#include "Engine/Core/SubsystemManager.h"
#include <iostream>
#include <algorithm>

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
        std::cerr << "[SubsystemManager] Already initialized, skipping." << std::endl;
        return true;
    }

    if (m_Subsystems.empty()) {
        std::cerr << "[SubsystemManager] No subsystems registered!" << std::endl;
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
    std::cout << "\n==============================================" << std::endl;
    std::cout << "  Engine Subsystem Initialization — Begin" << std::endl;
    std::cout << "==============================================" << std::endl;

    for (size_t i = 0; i < total; ++i) {
        const auto& entry = m_Subsystems[i];

        // 阶段标题
        if (entry.desc.phase != currentPhase) {
            currentPhase = entry.desc.phase;
            std::cout << "─── Phase " << static_cast<int>(currentPhase)
                      << " ───" << std::endl;
        }

        std::cout << "  [" << entry.desc.name << "] initializing... ";

        if (!entry.desc.onInit) {
            std::cout << "SKIPPED (no callback)" << std::endl;
            continue;
        }

        bool ok = entry.desc.onInit();
        if (ok) {
            std::cout << "OK" << std::endl;
            m_Succeeded.push_back(i);
        } else {
            std::cout << "FAILED!" << std::endl;
            m_Errors.push_back(entry.desc.name + " failed at phase " +
                               std::to_string(static_cast<int>(entry.desc.phase)));
            std::cerr << "[SubsystemManager] FATAL: " << entry.desc.name
                      << " initialization failed. Aborting." << std::endl;
            Shutdown();
            return false;
        }
    }

    m_Initialized = true;
    std::cout << "==============================================" << std::endl;
    std::cout << "  Engine Subsystem Initialization — All "
              << m_Succeeded.size() << " subsystems completed successfully" << std::endl;
    std::cout << "==============================================\n" << std::endl;
    return true;
}

// ============================================================
// 逆序关闭
// ============================================================

void SubsystemManager::Shutdown() {
    if (!m_Initialized && m_Succeeded.empty())
        return;

    std::cout << "\n--- Engine Subsystem Shutdown ---" << std::endl;

    // 逆序遍历 m_Succeeded
    for (auto it = m_Succeeded.rbegin(); it != m_Succeeded.rend(); ++it) {
        const auto& entry = m_Subsystems[*it];
        if (entry.desc.onShutdown) {
            std::cout << "  [" << entry.desc.name << "] shutting down..." << std::endl;
            entry.desc.onShutdown();
        }
    }

    m_Succeeded.clear();
    m_Initialized = false;
    std::cout << "--- Engine Subsystem Shutdown Complete ---\n" << std::endl;
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
