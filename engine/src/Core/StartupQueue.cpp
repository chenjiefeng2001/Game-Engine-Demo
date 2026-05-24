#include "Engine/Core/StartupQueue.h"
#include <iostream>
#include <algorithm>

namespace Engine {

// ============================================================
// 添加步骤
// ============================================================

StartupQueue& StartupQueue::Add(const StartupStep& step) {
    m_Steps.push_back(step);
    return *this;
}

StartupQueue& StartupQueue::Add(std::string name, StartupPhase phase,
                                std::function<bool()> init,
                                std::function<void()> shutdown) {
    m_Steps.emplace_back(std::move(name), phase,
                         std::move(init), std::move(shutdown));
    return *this;
}

// ============================================================
// 执行全部步骤（按阶段排序 → 按添加顺序）
// ============================================================

bool StartupQueue::Execute() {
    if (m_Initialized) {
        std::cerr << "[StartupQueue] Already initialized, skipping." << std::endl;
        return true;
    }

    if (m_Steps.empty()) {
        std::cerr << "[StartupQueue] No steps registered!" << std::endl;
        return false;
    }

    // ── 1. 按阶段排序 ──
    std::stable_sort(m_Steps.begin(), m_Steps.end(),
        [](const StartupStep& a, const StartupStep& b) {
            return static_cast<uint8>(a.phase) < static_cast<uint8>(b.phase);
        });

    // ── 2. 逐步骤执行 ──
    size_t total = m_Steps.size();
    StartupPhase currentPhase = m_Steps[0].phase;
    std::cout << "\n==============================================" << std::endl;
    std::cout << "  Engine Startup — Begin" << std::endl;
    std::cout << "==============================================" << std::endl;

    for (size_t i = 0; i < total; ++i) {
        const auto& step = m_Steps[i];

        // 阶段标题
        if (step.phase != currentPhase) {
            currentPhase = step.phase;
            std::cout << "─── Phase " << static_cast<int>(currentPhase)
                      << " ───" << std::endl;
        }

        std::cout << "  [" << step.name << "] initializing... ";

        if (!step.onInit) {
            std::cout << "SKIPPED (no callback)" << std::endl;
            continue;
        }

        bool ok = step.onInit();
        if (ok) {
            std::cout << "OK" << std::endl;
            m_Succeeded.push_back(i);
        } else {
            std::cout << "FAILED!" << std::endl;
            m_Errors.push_back(step.name + " failed at phase " +
                               std::to_string(static_cast<int>(step.phase)));
            std::cerr << "[StartupQueue] FATAL: " << step.name
                      << " initialization failed. Aborting." << std::endl;
            Shutdown();
            return false;
        }
    }

    m_Initialized = true;
    std::cout << "==============================================" << std::endl;
    std::cout << "  Engine Startup — All " << m_Succeeded.size()
              << " steps completed successfully" << std::endl;
    std::cout << "==============================================\n" << std::endl;
    return true;
}

// ============================================================
// 逆序关闭
// ============================================================

void StartupQueue::Shutdown() {
    if (!m_Initialized && m_Succeeded.empty())
        return;

    std::cout << "\n--- Engine Shutdown ---" << std::endl;

    // 逆序遍历 m_Succeeded
    for (auto it = m_Succeeded.rbegin(); it != m_Succeeded.rend(); ++it) {
        const auto& step = m_Steps[*it];
        if (step.onShutdown) {
            std::cout << "  [" << step.name << "] shutting down..." << std::endl;
            step.onShutdown();
        }
    }

    m_Succeeded.clear();
    m_Initialized = false;
    std::cout << "--- Engine Shutdown Complete ---\n" << std::endl;
}

// ============================================================
// 查询
// ============================================================

bool StartupQueue::HasStep(const std::string& name) const {
    for (const auto& step : m_Steps) {
        if (step.name == name)
            return true;
    }
    return false;
}

std::vector<std::string> StartupQueue::GetStepNames() const {
    std::vector<std::string> names;
    names.reserve(m_Steps.size());
    for (const auto& step : m_Steps) {
        names.push_back(step.name);
    }
    return names;
}

} // namespace Engine
