/**
 * @file IDBufferPass.cpp
 * @brief ID-Buffer 拾取 Pass 实现
 *
 * 工作流程：
 *   1. 创建 R32_UINT 纹理作为 ID 渲染目标
 *   2. 创建对应的 staging readback buffer 用于 GPU→CPU 读回
 *   3. 鼠标点击时发起读回操作
 *
 * 注意：本实现假设 RHI 后端支持 R32_UINT 渲染目标格式。
 * Vulkan 和 D3D12 均支持此格式。
 */

#include "Engine/Rendering/IDBufferPass.h"
#include "Engine/Core/Log.h"
#include <cstring>

namespace {
    Engine::Logger s_Log("IDBuffer");
}

namespace Engine { namespace Rendering {

IDBufferPass::~IDBufferPass() {
    // 纹理和缓冲区由 shared_ptr 管理，自动释放
    m_IDTexture = nullptr;
    m_ReadbackBuffer = nullptr;
    m_StagingBuffer = nullptr;
    m_IDPSO = nullptr;
}

bool IDBufferPass::Initialize(RHI::IRHIDevice& device, uint32_t width, uint32_t height) {
    if (m_Initialized) return true;

    m_Width = width;
    m_Height = height;

    // 创建 ID-Buffer 纹理（R32_UINT 渲染目标）
    {
        RHI::TextureDesc desc;
        desc.width  = width;
        desc.height = height;
        desc.format = RHI::Format::R32_UInt;
        desc.mipLevels = 1;
        desc.memoryUsage = RHI::MemoryUsage::GPU_Only;

        auto tex = device.CreateTexture(desc);
        if (!tex) {
            s_Log.Error("Failed to create ID-Buffer texture");
            return false;
        }
        m_IDTexture = tex.get();
    }

    // 创建 Readback Buffer（用于 GPU→CPU 读回像素）
    {
        RHI::RHIBufferDesc desc;
        desc.size = width * height * sizeof(uint32_t);  // R32_UINT 像素
        desc.memoryUsage = RHI::MemoryUsage::GPU_To_CPU;

        auto buf = device.CreateBuffer(desc);
        if (!buf) {
            s_Log.Error("Failed to create ID readback buffer");
            return false;
        }
        m_ReadbackBuffer = buf.get();
    }

    // 创建 ID PSO（简单着色器将 EntityID 从 instance data 写入输出）
    {
        RHI::ComputePSODesc computeDesc;
        computeDesc.computeShader = Engine::StringID::Runtime("id_buffer.comp");
        m_IDPSO = device.CreateComputePSO(computeDesc);
        if (!m_IDPSO) {
            s_Log.Warn("ID-Buffer PSO not available, will use fallback path");
        }
    }

    m_Initialized = true;
    s_Log.Info("IDBufferPass initialized: {}x{}", width, height);
    return true;
}

void IDBufferPass::Begin(RHI::IRHICommandList& cmdList) {
    if (!m_Initialized) return;
    // ID-Buffer Pass 在当前 RenderGraph 的 GBuffer Pass 中作为额外 RT 绑定
    // 实际的 RT 绑定由上层渲染代码完成（通过 cmdList::SetRenderTarget 等）
}

void IDBufferPass::End(RHI::IRHICommandList& cmdList) {
    if (!m_Initialized) return;
    // 将 ID-Buffer 拷贝到 readback buffer
    // 注：真正的拷贝操作需要在 RenderGraph 中作为单独的 Copy Pass 执行
    // 此处仅做接口预留
}

uint32_t IDBufferPass::ReadbackPixel(uint32_t x, uint32_t y) {
    if (!m_Initialized || x >= m_Width || y >= m_Height) return 0;

    // 尝试从 readback buffer 映射并读取像素
    if (!m_ReadbackBuffer) return 0;

    const auto& alloc = m_ReadbackBuffer->GetAllocation();
    if (!alloc.mappedPtr) {
        s_Log.Warn("Readback buffer not mapped");
        return 0;
    }

    // 计算像素偏移（R32_UINT = 4 bytes per pixel）
    uint64_t pixelOffset = (static_cast<uint64_t>(y) * m_Width + x) * sizeof(uint32_t);
    if (pixelOffset + sizeof(uint32_t) > m_ReadbackBuffer->GetSize()) {
        return 0;
    }

    // 读取像素值作为 EntityID
    const uint32_t* data = static_cast<const uint32_t*>(alloc.mappedPtr);
    m_SelectedEntityID = data[x + y * m_Width];

    if (m_SelectedEntityID > 0) {
        s_Log.Info("Selected Entity #{} at pixel ({}, {})", m_SelectedEntityID, x, y);
    }

    return m_SelectedEntityID;
}

}} // namespace Engine::Rendering