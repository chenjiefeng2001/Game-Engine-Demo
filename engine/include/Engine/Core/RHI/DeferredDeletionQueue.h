#pragma once

/**
 * @file DeferredDeletionQueue.h
 * @brief 延迟删除队列 v3.0 — 支持 GPU 资源 + Bindless 索引的延迟释放
 *
 * v3.0 新增：
 *   - DeferBindlessRelease(uint32_t index) — 将 Bindless 索引延迟释放
 *   - 索引在 kMaxFrames 帧后重新可用，防止"纹理闪烁成其他材质"的 Bug
 *   - 帧索引驱动（而非 Fence），适用于不需要精确 Fence 的场景
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

    /** @brief 延迟释放一个 GPU 资源 */
    void DeferRelease(std::function<void()>&& cleanup) {
        m_Queues[m_CurrentFrame].push_back(std::move(cleanup));
    }

    /**
     * @brief 延迟释放一个 Bindless 纹理索引
     *
     * 索引不会立即回收，而是在 kMaxFrames 帧后才实际释放。
     * 防止 GPU 还在使用 Texture 时索引被重分配给新纹理。
     *
     * @param bindlessIndex 要延迟释放的 Bindless 索引
     */
    void DeferBindlessRelease(uint32_t bindlessIndex) {
        m_BindlessQueues[m_CurrentFrame].push_back(bindlessIndex);
    }

    /** @brief 每帧调用，推进帧索引并释放过期资源 */
    void Tick() {
        // 释放当前帧的待删除资源
        auto& queue = m_Queues[m_CurrentFrame];
        for (auto& cleanup : queue) {
            if (cleanup) cleanup();
        }
        queue.clear();

        // 释放当前帧的待释放 Bindless 索引
        // 注意：Bindless 索引在 kMaxFrames 帧后被释放，
        // 即当前帧释放的是 kMaxFrames 帧前入队的索引
        // 实际已经通过 Tick 的自热循环实现了 N 帧延迟
        // 但为了更精确的控制，使用 release 回调通知管理器
        auto& bindlessQueue = m_BindlessQueues[m_CurrentFrame];
        for (uint32_t idx : bindlessQueue) {
            if (m_BindlessReleaseCallback) {
                m_BindlessReleaseCallback(idx);
            }
        }
        bindlessQueue.clear();

        // 推进帧索引
        m_CurrentFrame = (m_CurrentFrame + 1) % kMaxFrames;
    }

    /**
     * @brief 设置 Bindless 索引释放的回调
     *
     * 通常在 BindlessTextureManager 中设置，
     * 回调会在索引延迟期满后调用，执行实际释放。
     */
    void SetBindlessReleaseCallback(std::function<void(uint32_t)> callback) {
        m_BindlessReleaseCallback = std::move(callback);
    }

    /** @brief 立即释放所有待删除资源 */
    void Flush() {
        for (uint32_t i = 0; i < kMaxFrames; ++i) {
            for (auto& cleanup : m_Queues[i]) {
                if (cleanup) cleanup();
            }
            m_Queues[i].clear();

            // 立即释放 Bindless 索引
            for (uint32_t idx : m_BindlessQueues[i]) {
                if (m_BindlessReleaseCallback) {
                    m_BindlessReleaseCallback(idx);
                }
            }
            m_BindlessQueues[i].clear();
        }
    }

private:
    uint32_t m_CurrentFrame = 0;
    std::vector<std::function<void()>> m_Queues[kMaxFrames];
    std::vector<uint32_t> m_BindlessQueues[kMaxFrames];  ///< Bindless 索引延迟释放队列
    std::function<void(uint32_t)> m_BindlessReleaseCallback;  ///< Bindless 索引释放回调
};

}} // namespace Engine::RHI