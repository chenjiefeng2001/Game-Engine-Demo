#pragma once

/**
 * @file DeferredDeletionQueue.h
 * @brief 延迟删除队列 — 确保 GPU 完成使用后再释放资源
 *
 * 用于 D3D12 和 Vulkan 后端。资源删除时不是立即释放，
 * 而是放入队列，等 GPU Fence 信号后（通常 3 帧后）再真正释放。
 */

#include "Engine/Types.h"
#include <vector>
#include <functional>
#include <cstdint>

namespace Engine { namespace RHI {

class DeferredDeletionQueue {
public:
    static constexpr uint32_t kMaxFrames = 6;

    DeferredDeletionQueue() = default;

    /** 延迟释放一个资源 */
    void DeferRelease(std::function<void()>&& cleanup) {
        m_Queues[m_CurrentFrame].push_back(std::move(cleanup));
    }

    /** 每帧调用，推进帧索引并释放过期资源 */
    void Tick() {
        // 释放当前帧的待删除资源
        auto& queue = m_Queues[m_CurrentFrame];
        for (auto& cleanup : queue) {
            if (cleanup) cleanup();
        }
        queue.clear();

        // 推进帧索引
        m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFrames;
    }

    /** 立即释放所有待删除资源 */
    void Flush() {
        for (uint32_t i = 0; i < kMaxFrames; ++i) {
            for (auto& cleanup : m_Queues[i]) {
                if (cleanup) cleanup();
            }
            m_Queues[i].clear();
        }
    }

private:
    uint32_t m_CurrentFrame = 0;
    std::vector<std::function<void()>> m_Queues[kMaxFrames];
};

}} // namespace Engine::RHI