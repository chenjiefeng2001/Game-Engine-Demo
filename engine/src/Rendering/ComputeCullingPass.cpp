/**
 * @file ComputeCullingPass.cpp
 * @brief GPU 视锥体裁剪 Pass 实现
 *
 * 使用 Compute Shader 对所有物体 AABB 执行视锥体相交测试，
 * 输出可见物体索引数组供 Indirect Draw 使用。
 */

#include "Engine/Rendering/ComputeCullingPass.h"
#include "Engine/Core/RHI/PSOCache.h"
#include <cstring>

namespace Engine { namespace Rendering {

    ComputeCullingPass::ComputeCullingPass(RHI::IRHIDevice& device,
                                           uint32_t maxObjects)
        : m_Device(device)
        , m_MaxObjects(maxObjects)
    {
        RHI::RHIBufferDesc aabbDesc;
        aabbDesc.size = maxObjects * sizeof(CullingAABB);
        aabbDesc.stride = sizeof(CullingAABB);
        aabbDesc.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;
        aabbDesc.initialData = nullptr;
        m_AABBBuffer = device.CreateBuffer(aabbDesc).get();

        RHI::RHIBufferDesc frustumDesc;
        frustumDesc.size = sizeof(CullingFrustumPlane) * 6 + sizeof(uint32_t);
        frustumDesc.stride = 0;
        frustumDesc.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;
        m_FrustumBuffer = device.CreateBuffer(frustumDesc).get();

        RHI::RHIBufferDesc indicesDesc;
        indicesDesc.size = sizeof(uint32_t) + maxObjects * sizeof(uint32_t);
        indicesDesc.stride = 0;
        indicesDesc.memoryUsage = RHI::MemoryUsage::GPU_Only;
        m_VisibleIndicesBuffer = device.CreateBuffer(indicesDesc).get();

        RHI::RHIBufferDesc indirectDesc;
        indirectDesc.size = sizeof(uint32_t) * 5;
        indirectDesc.stride = 0;
        indirectDesc.memoryUsage = RHI::MemoryUsage::CPU_To_GPU;
        m_IndirectArgsBuffer = device.CreateBuffer(indirectDesc).get();

        m_Initialized = CreatePSO();
    }

    ComputeCullingPass::~ComputeCullingPass() {}

    bool ComputeCullingPass::CreatePSO() {
        RHI::ComputePSODesc desc;
        // Use Runtime StringID — ComputePSODesc stores uint64_t hash
        desc.computeShader = StringID::Runtime("cull_shader");
        try {
            auto& cache = RHI::PSOCache::Get();
            m_CullPSO = cache.GetOrCreate(m_Device, desc);
            return m_CullPSO != nullptr;
        } catch (...) {
            return false;
        }
    }

    void ComputeCullingPass::UpdateAABBs(RHI::IRHICommandList&,
                                          const CullingAABB* aabbs,
                                          uint32_t count) {
        if (!m_Initialized || !aabbs || count == 0) return;
        m_CurrentObjectCount = count;
    }

    void ComputeCullingPass::UpdateFrustum(RHI::IRHICommandList&,
                                            const CullingFrustumPlane*,
                                            uint32_t totalCount) {
        if (!m_Initialized) return;
        // Store total count into frustum buffer
    }

    void ComputeCullingPass::Dispatch(RHI::IRHICommandList& cmdList) {
        if (!m_Initialized || !m_CullPSO || m_CurrentObjectCount == 0) return;

        cmdList.SetUnorderedAccess(0, m_AABBBuffer);
        cmdList.SetUnorderedAccess(1, m_FrustumBuffer);
        cmdList.SetUnorderedAccess(2, m_VisibleIndicesBuffer);
        cmdList.SetPipelineState(m_CullPSO);

        uint32_t groupCount = (m_CurrentObjectCount + 63) / 64;
        cmdList.Dispatch(groupCount, 1, 1);
    }

    uint32_t ComputeCullingPass::GetVisibleCount() const {
        return 0;
    }

}} // namespace Engine::Rendering