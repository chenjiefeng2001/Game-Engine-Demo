#pragma once

/**
 * @file PSOCache.h
 * @brief PSO 缓存 — 全局管线状态对象缓存，按描述符哈希去重
 *
 * 设计理念：
 *   - 每个 GraphicsPSODesc 生成 64 位哈希，同哈希 = 同管线
 *   - 避免驱动端重复创建管线（创建 PSO 在 Vulkan/D3D12 中开销很大）
 *   - 线程安全（内部 mutex）
 *   - PSO 生命周期由 IRHIDevice 管理（缓存只存储指针/句柄）
 */

#include "Engine/Core/RHI/PSODesc.h"
#include <unordered_map>
#include <mutex>
#include <cstdint>

namespace Engine {
namespace RHI {

    // 前向声明
    class IRHIDevice;
    class IRHIPipelineState;

    /**
     * @brief PSO 缓存 — 全局单例
     *
     * 使用方式：
     * @code
     *   GraphicsPSODesc desc = GraphicsPSODesc::DefaultOpaque();
     *   desc.vertexShader = SID("main_vs");
     *   desc.pixelShader  = SID("main_ps");
     *   IRHIPipelineState* pso = PSOCache::Get().GetOrCreate(device, desc);
     * @endcode
     */
    class PSOCache {
    public:
        static PSOCache& Get() {
            static PSOCache instance;
            return instance;
        }

        PSOCache(const PSOCache&) = delete;
        PSOCache& operator=(const PSOCache&) = delete;

        /**
         * @brief 获取或创建 Graphics PSO
         *
         * @param device RHI 设备（用于创建 PSO）
         * @param desc   管线描述符
         * @return 指向管线状态对象的指针（生命周期由设备管理）
         *
         * 线程安全。相同 desc 返回相同的 PSO。
         */
        IRHIPipelineState* GetOrCreate(IRHIDevice& device, const GraphicsPSODesc& desc);

        /**
         * @brief 获取或创建 Compute PSO
         */
        IRHIPipelineState* GetOrCreate(IRHIDevice& device, const ComputePSODesc& desc);

        /** 按哈希查找 PSO（不创建） */
        template<typename DescT>
        IRHIPipelineState* Find(uint64_t hash) const {
            std::lock_guard<std::mutex> lock(m_Mutex);
            const auto& cache = std::is_same_v<DescT, GraphicsPSODesc> ? m_GraphicsCache : m_ComputeCache;
            auto it = cache.find(hash);
            if (it != cache.end()) { m_Hits++; return it->second; }
            return nullptr;
        }

        /** 存储 PSO 到缓存 */
        template<typename DescT>
        void Store(uint64_t hash, IRHIPipelineState* pso) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto& cache = std::is_same_v<DescT, GraphicsPSODesc> ? m_GraphicsCache : m_ComputeCache;
            cache[hash] = pso;
            m_Misses++;
        }

        /** 清空所有缓存条目（通常在设备销毁时调用） */
        void Clear();

        /** 缓存命中次数 */
        uint64_t GetHitCount() const { return m_Hits; }

        /** 缓存未命中次数 */
        uint64_t GetMissCount() const { return m_Misses; }

    private:
        PSOCache() = default;

        mutable std::mutex m_Mutex;

        // Graphics PSO 缓存：hash → PSO
        std::unordered_map<uint64_t, IRHIPipelineState*> m_GraphicsCache;

        // Compute PSO 缓存：hash → PSO
        std::unordered_map<uint64_t, IRHIPipelineState*> m_ComputeCache;

        mutable uint64_t m_Hits   = 0;
        uint64_t m_Misses = 0;
    };

} // namespace RHI
} // namespace Engine