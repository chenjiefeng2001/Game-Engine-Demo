#pragma once

/**
 * @file GL46AZDODevice.h
 * @brief OpenGL 4.6 AZDO 设备实现 — DSA + 持久映射 + 间接绘制
 *
 * 升级项：
 *   1. DSA (Direct State Access): glCreate* 替代 glGen* + glBind*
 *   2. Persistent Mapping: GL_MAP_PERSISTENT_BIT 替代 glBufferSubData
 *   3. MultiDrawIndirect: 单次 glMultiDrawElementsIndirect 提交多绘制
 *   4. Bindless Texture: ARB_bindless_texture 64位GPU句柄
 *
 * 与 Vulkan 的对齐：
 *   - glCreateBuffers + glNamedBufferStorage → vkCreateBuffer + vkAllocateMemory
 *   - glMapNamedBufferRange(PERSISTENT+COHERENT) → vkMapMemory
 *   - glMultiDrawElementsIndirect → vkCmdDrawIndexedIndirect
 *   - glGetTextureHandleARB → Bindless Slot (VK_EXT_descriptor_indexing)
 */

#include "Engine/Core/RHI/IRHIDevice.h"
#include <memory>

namespace Engine {
namespace RHI {

    // ══════════════════════════════════════════════════════
    // GL46 AZDO Device
    // ══════════════════════════════════════════════════════
    class GL46Device final : public IRHIDevice {
    public:
        GL46Device();
        ~GL46Device() override;

        std::shared_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
        std::shared_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) override;
        IRHIPipelineState* CreateGraphicsPSO(const GraphicsPSODesc& desc) override;
        IRHIPipelineState* CreateComputePSO(const ComputePSODesc& desc) override;
        std::unique_ptr<IRHICommandList> CreateCommandList(CommandListType t) override;
        IRHICommandQueue* GetQueue(QueueType type) override;
        std::unique_ptr<IRHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        void WaitIdle() override;
        const char* GetDeviceName() const override;

        bool Initialize(void* windowHandle, uint32_t width, uint32_t height);

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ══════════════════════════════════════════════════════
    // GL46 Buffer — Persistent Mapping
    // ══════════════════════════════════════════════════════
    /**
     * @brief GL46 持久映射缓冲
     *
     * 创建时使用 glNamedBufferStorage + GL_MAP_PERSISTENT_BIT
     * 返回的 mappedPtr 与 Vulkan vkMapMemory 语义完全一致。
     */
    class GL46Buffer final : public IRHIBuffer {
    public:
        GL46Buffer() = default;
        ~GL46Buffer() override;

        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;

        // GL46 专有
        uint32_t GetGLHandle() const noexcept { return m_GLBuffer; }
        void*    GetPersistentPtr() const noexcept { return m_MappedPtr; }
        bool     IsPersistent() const noexcept { return m_Persistent; }

    private:
        friend class GL46Device;
        uint32_t m_GLBuffer = 0;
        void*    m_MappedPtr = nullptr;
        bool     m_Persistent = false;
        uint64_t m_Size = 0;
        GPUAllocation m_Allocation;
    };

    // ══════════════════════════════════════════════════════
    // GL46 Command List — 命令镜像模式
    // ══════════════════════════════════════════════════════
    /**
     * @brief GL46 命令列表 — 录制 GL 命令到内存，主线程执行
     *
     * 由于 OpenGL 上下文是线程绑定的，工作线程不能直接调用 GL 函数。
     * 此 CommandList 在工作线程录制命令（记录参数到内部缓冲），
     * 主线程调用 Execute() 时真实执行 GL 调用。
     */
    class GL46CommandList final : public IRHICommandList {
    public:
        GL46CommandList();
        ~GL46CommandList() override;

        void Begin() override;
        void End() override;
        void Reset() override;
        void SetPipelineState(IRHIPipelineState* pso) override;
        void SetVertexBuffer(uint32 slot, IRHIBuffer* buf, uint32 stride, uint32 off) override;
        void SetIndexBuffer(IRHIBuffer* buf, uint32 off) override;
        void SetPrimitiveTopology(PrimitiveTopology topo) override;
        void DrawIndexed(uint32 idxCount, uint32 startIdx, uint32 baseVtx) override;
        void Draw(uint32 vtxCount, uint32 startVtx) override;
        void DrawIndexedIndirect(IRHIBuffer* argsBuf, uint32 off) override;
        void SetViewport(const Viewport& vp) override;
        void SetScissorRect(const Rect& rect) override;
        void ResourceBarrier(uint32 count, const ResourceBarrierDesc* barriers) override;
        CommandListType GetType() const noexcept override;

        // ── 命令镜像专用 ──
        /** 在主线程执行所有录制的命令 */
        void ExecuteOnMainThread();

        /** 获取录制的命令数量 */
        uint32_t GetRecordedCommandCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
    };

    // ══════════════════════════════════════════════════════
    // GL46 其他资源
    // ══════════════════════════════════════════════════════
    class GL46Texture final : public IRHITexture {
    public:
        uint32_t GetWidth()  const noexcept override;
        uint32_t GetHeight() const noexcept override;
        Format   GetFormat() const noexcept override;
        uint32_t GetGLHandle() const noexcept { return m_GLTex; }
    private:
        uint32_t m_GLTex = 0;
    };

    class GL46PipelineState final : public IRHIPipelineState {
    public:
        GraphicsPSODesc Desc;
    };

    class GL46SwapChain final : public IRHISwapChain {
    public:
        void Present() override;
        void Resize(uint32_t w, uint32_t h) override;
        IRHITexture* GetBackBuffer(uint32_t idx) const override;
        uint32_t GetCurrentBackBufferIndex() const override;
        uint32_t GetBufferCount() const override;
    };

    class GL46Queue final : public IRHICommandQueue {
    public:
        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
    };

} // namespace RHI
} // namespace Engine