#pragma once

#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Types.h"
#include <cstdint>
#include <vector>

namespace Engine { namespace Rendering {

    // Use raw float arrays to avoid min/max macro conflicts
    struct CullingAABB {
        float minX, minY, minZ;
        float _pad1;
        float maxX, maxY, maxZ;
        float _pad2;
    };

    struct CullingFrustumPlane {
        float x, y, z, d;
    };

    struct VisibleIndicesHeader {
        uint32_t count;
        uint32_t indices[1];
    };

    class ComputeCullingPass {
    public:
        ComputeCullingPass(RHI::IRHIDevice& device, uint32_t maxObjects = 100000);
        ~ComputeCullingPass();
        ComputeCullingPass(const ComputeCullingPass&) = delete;
        ComputeCullingPass& operator=(const ComputeCullingPass&) = delete;

        void UpdateAABBs(RHI::IRHICommandList& cmdList,
                         const CullingAABB* aabbs, uint32_t count);
        void UpdateFrustum(RHI::IRHICommandList& cmdList,
                           const CullingFrustumPlane* planes,
                           uint32_t totalCount);
        void Dispatch(RHI::IRHICommandList& cmdList);
        uint32_t GetVisibleCount() const;

        RHI::IRHIBuffer* GetVisibleIndicesBuffer() const { return m_VisibleIndicesBuffer; }
        RHI::IRHIBuffer* GetIndirectArgsBuffer() const { return m_IndirectArgsBuffer; }
        bool IsValid() const { return m_Initialized; }

    private:
        bool CreatePSO();
        RHI::IRHIDevice& m_Device;
        RHI::IRHIBuffer* m_AABBBuffer = nullptr;
        RHI::IRHIBuffer* m_FrustumBuffer = nullptr;
        RHI::IRHIBuffer* m_VisibleIndicesBuffer = nullptr;
        RHI::IRHIBuffer* m_IndirectArgsBuffer = nullptr;
        RHI::IRHIPipelineState* m_CullPSO = nullptr;
        uint32_t m_MaxObjects = 100000;
        uint32_t m_CurrentObjectCount = 0;
        bool m_Initialized = false;
    };

}}