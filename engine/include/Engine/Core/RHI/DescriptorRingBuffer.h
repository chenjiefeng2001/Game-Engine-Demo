#pragma once

/**
 * @file DescriptorRingBuffer.h
 * @brief 描述符环形缓冲区 — 每帧独立池，多线程安全分配
 *
 * 解决 ExecuteParallel 多线程录制时描述符池耗尽的问题。
 * 每帧一个独立的 DescriptorHeapAllocator，通过 AdvanceFrame() 循环使用。
 * 确保 GPU 完成当前帧后才允许回收池。
 */

#include "Engine/Core/RHI/DescriptorHeap.h"
#include <vector>

namespace Engine {
namespace RHI {

    class DescriptorRingBuffer {
    public:
        static constexpr uint32_t MAX_FRAMES = 3;

        DescriptorRingBuffer() = default;
        ~DescriptorRingBuffer() { Shutdown(); }

        DescriptorRingBuffer(const DescriptorRingBuffer&) = delete;
        DescriptorRingBuffer& operator=(const DescriptorRingBuffer&) = delete;

        /**
         * @brief 初始化环形缓冲区
         * @param maxDescriptorsPerFrame 每帧最大描述符数
         */
        bool Initialize(uint32_t maxDescriptorsPerFrame = 65536) {
            for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
                if (!m_Pools[i].Initialize(maxDescriptorsPerFrame)) {
                    return false;
                }
            }
            m_Initialized = true;
            return true;
        }

        void Shutdown() {
            for (uint32_t i = 0; i < MAX_FRAMES; ++i) {
                m_Pools[i].Shutdown();
            }
            m_Initialized = false;
        }

        /// 从当前帧的池分配一个描述符索引
        uint32_t AllocateIndex() {
            return m_Pools[m_CurrentFrame].AllocateIndex();
        }

        /// 释放描述符索引（标记为空闲）
        void FreeIndex(uint32_t index) {
            m_Pools[m_CurrentFrame].FreeIndex(index);
        }

        /// 推进到下一帧（仅当 GPU Fence 确认完成时才安全）
        void AdvanceFrame() {
            // 清理 3 帧前的池（保证 GPU 已完成该帧）
            uint32_t oldFrame = (m_CurrentFrame + 1) % MAX_FRAMES;
            m_Pools[oldFrame].Shutdown();
            m_Pools[oldFrame].Initialize(m_Pools[oldFrame].GetMaxDescriptors());
            m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES;
        }

        bool IsInitialized() const { return m_Initialized; }
        uint32_t GetCurrentFrame() const { return m_CurrentFrame; }

    private:
        DescriptorHeapAllocator m_Pools[MAX_FRAMES];
        uint32_t m_CurrentFrame = 0;
        bool m_Initialized = false;
    };

} // namespace RHI
} // namespace Engine