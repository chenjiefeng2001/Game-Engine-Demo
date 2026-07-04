/**
 * @file PSOCache.cpp
 * @brief PSO 缓存实现 — GetOrCreate + 命中率统计
 */

#include "Engine/Core/RHI/PSOCache.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/Log.h"

namespace {
    Engine::Logger s_Log("PSOCache");
}

namespace Engine {
namespace RHI {

    IRHIPipelineState* PSOCache::GetOrCreate(IRHIDevice& device, const GraphicsPSODesc& desc) {
        uint64_t hash = desc.GetHash();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_GraphicsCache.find(hash);
            if (it != m_GraphicsCache.end()) {
                m_Hits++;
                return it->second;
            }
        }

        m_Misses++;
        auto* pso = device.CreateGraphicsPSO(desc);
        if (!pso) {
            s_Log.Error("Failed to create Graphics PSO (hash={})", hash);
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_GraphicsCache[hash] = pso;
        }

        s_Log.Info("Created new Graphics PSO (hash={}, cache_size={})",
                   hash, m_GraphicsCache.size());
        return pso;
    }

    IRHIPipelineState* PSOCache::GetOrCreate(IRHIDevice& device, const ComputePSODesc& desc) {
        uint64_t hash = desc.GetHash();

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            auto it = m_ComputeCache.find(hash);
            if (it != m_ComputeCache.end()) {
                m_Hits++;
                return it->second;
            }
        }

        m_Misses++;
        auto* pso = device.CreateComputePSO(desc);
        if (!pso) {
            s_Log.Error("Failed to create Compute PSO (hash={})", hash);
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(m_Mutex);
            m_ComputeCache[hash] = pso;
        }

        return pso;
    }

    void PSOCache::Clear() {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_GraphicsCache.clear();
        m_ComputeCache.clear();
    }

} // namespace RHI
} // namespace Engine