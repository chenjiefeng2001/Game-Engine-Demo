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
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/StringID.h"
#include <memory>
#include <glad/gl.h>   // GladGLContext, PFN*PROC
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine {
namespace RHI {

    // ══════════════════════════════════════════════════════
    // 前向声明
    // ══════════════════════════════════════════════════════
    struct GL46ComputeProgram;
    class GL46ComputePipelineState;

    // ══════════════════════════════════════════════════════
    // GL46 AZDO Device
    // ══════════════════════════════════════════════════════
    /// AZDO 间接绘制命令（与 Vulkan VkDrawIndexedIndirectCommand 兼容）
    struct DrawElementsIndirectCommand {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstIndex;
        int32_t  baseVertex;
        uint32_t firstInstance;
    };

    class GL46Device final : public IRHIDevice {
    public:
        GL46Device();
        ~GL46Device() override;

        bool Initialize(void* windowHandle, uint32_t width, uint32_t height) override;

        /** 用已有的 GladGLContext 初始化（不用创建窗口） */
        bool InitializeWithGLContext(GladGLContext* glContext, uint32_t width, uint32_t height);

        std::shared_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
        std::shared_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) override;
        IRHIPipelineState* CreateGraphicsPSO(const GraphicsPSODesc& desc) override;
        IRHIPipelineState* CreateComputePSO(const ComputePSODesc& desc) override;
        std::unique_ptr<IRHICommandList> CreateCommandList(CommandListType t) override;
        IRHICommandQueue* GetQueue(QueueType type) override;
        std::unique_ptr<IRHISwapChain> CreateSwapChain(const SwapChainDesc& desc) override;
        void WaitIdle() override;
        const char* GetDeviceName() const override;

        /** 获取 GladGLContext 引用 */
        GladGLContext& GetGL() const { return *m_GL; }

        /** 获取计算着色器程序 */
        GL46ComputeProgram* GetComputeProgram(uint64_t nameHash) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        GladGLContext* m_GL = nullptr;
        bool m_OwnsContext = false;
        bool m_IsStub = true;
        std::string m_DeviceName;

        std::unordered_map<uint64_t, GL46ComputeProgram> m_ComputePrograms;

        // 内部：编译计算着色器
        bool CompileComputeShaders();

        void Shutdown();
    };

    // ══════════════════════════════════════════════════════
    // GL46 Buffer — Persistent Mapping
    // ══════════════════════════════════════════════════════
    class GL46Buffer final : public IRHIBuffer {
    public:
        GL46Buffer() = default;
        ~GL46Buffer() override;

        uint64_t GetSize() const noexcept override;
        const GPUAllocation& GetAllocation() const noexcept override;
        void* GetMappedPtr() const noexcept override { return m_MappedPtr; }

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
    // GL46ComputeProgram — 计算着色器封装
    // ══════════════════════════════════════════════════════
    struct GL46ComputeProgram {
        uint32_t program = 0;
        uint64_t nameHash = 0;
    };

    // ══════════════════════════════════════════════════════
    // GL46 Compute Pipeline State
    // ══════════════════════════════════════════════════════
    class GL46ComputePipelineState final : public IRHIPipelineState {
    public:
        GL46ComputeProgram* program = nullptr;
    };

    // ══════════════════════════════════════════════════════
    // GL46 Command List — 命令镜像模式
    // ══════════════════════════════════════════════════════
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
        void SetConstantBuffer(uint32 set, uint32 binding, IRHIBuffer* buffer, uint64_t offset, uint64_t size) override;
        void SetShaderResource(uint32 set, uint32 binding, IRHITexture* texture) override;
        void Dispatch(uint32_t groupX, uint32_t groupY, uint32_t groupZ) override;
        void SetUnorderedAccess(uint32 slot, IRHIBuffer* buffer) override;
        void SetComputeFloat(const char* name, float value) override;
        void SetComputeVec3(const char* name, float x, float y, float z) override;
        void SetComputeInt(const char* name, int32_t value) override;
        CommandListType GetType() const noexcept override;

        // ── 命令镜像专用 ──
        /** 在主线程执行所有录制的命令 */
        void SetGLContext(GladGLContext* gl) { m_GL = gl; }
        void ExecuteOnMainThread();
        uint32_t GetRecordedCommandCount() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_Impl;
        GladGLContext* m_GL = nullptr;
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
        explicit GL46Queue(GladGLContext* gl) : m_GL(gl) {}

        void ExecuteCommandLists(uint32 count, IRHICommandList** lists) override;
        void WaitIdle() override;
        QueueType GetType() const noexcept override;
    private:
        GladGLContext* m_GL = nullptr;
    };

} // namespace RHI
} // namespace Engine