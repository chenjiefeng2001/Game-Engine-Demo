#pragma once

/**
 * @file IRHICommandList.h
 * @brief RHI 命令列表抽象 — 录制渲染命令到缓冲，延迟执行
 *
 * 设计理念：
 *   - 取代 OpenGL 立即模式：命令记录在内存中，由 RHICommandQueue::Execute() 统一提交
 *   - 多线程友好：每个工作线程可持有独立的 CommandList 同时录制
 *   - 适配 JobSystem：SceneRenderer 生成 RenderCommand → 多个 worker 并行录制
 *   - PSO 绑定：消除散件的 glEnable/glBlendFunc 调用
 *
 * 生命周期：
 *   1. device->CreateCommandList() → 获取 CommandList
 *   2. Begin() → 开始录制
 *   3. SetPipelineState / SetVertexBuffers / DrawIndexed ... → 录制
 *   4. End() → 结束录制
 *   5. queue->ExecuteCommandLists({list}) → 提交到 GPU
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/GPUMemoryBlock.h"
#include "Engine/Core/RHI/GPUAllocation.h"

namespace Engine {
namespace RHI {

    // 前向声明
    class IRHIPipelineState;
    class IRHIBuffer;
    class IRHITexture;

    // ============================================================
    // 视口 / 裁剪矩形
    // ============================================================
    struct Viewport {
        float x, y, width, height;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    struct Rect {
        int32 x, y, width, height;
    };

    // ============================================================
    // 资源屏障描述符
    // ============================================================
    struct ResourceBarrierDesc {
        enum class Type : uint8 {
            Transition,   ///< 资源布局转换
            UAV,          ///< UAV 同步
            Aliasing,     ///< 别名内存同步
        };

        Type          type        = Type::Transition;
        IRHIBuffer*   buffer      = nullptr;    ///< 或 nullptr 表示纹理
        IRHITexture*  texture     = nullptr;
        ResourceState stateBefore = ResourceState::Undefined;
        ResourceState stateAfter  = ResourceState::Undefined;
    };

    // ============================================================
    // IRHICommandList — 命令录制接口
    // ============================================================
    class IRHICommandList {
    public:
        virtual ~IRHICommandList() = default;

        // ── 录制周期 ──

        /** 开始录制命令 */
        virtual void Begin() = 0;

        /** 结束录制（之后不可再录制） */
        virtual void End() = 0;

        /** 重置命令列表以重新录制（重新回到 Begin 前的状态） */
        virtual void Reset() = 0;

        // ── 管线状态 ──

        /**
         * @brief 设置渲染管线状态对象
         *
         * 所有 blende / depth / rasterizer 状态由 PSO 一元化锁定。
         * OpenGL 后端在此使用影子状态追踪器避免冗余 GL 调用。
         */
        virtual void SetPipelineState(IRHIPipelineState* pso) = 0;

        // ── 几何体 ──

        /** 设置顶点缓冲（按槽位） */
        virtual void SetVertexBuffer(uint32 slot, IRHIBuffer* buffer,
                                     uint32 stride, uint32 offset = 0) = 0;

        /** 设置索引缓冲 */
        virtual void SetIndexBuffer(IRHIBuffer* buffer,
                                    uint32 offset = 0) = 0;

        /** 设置图元拓扑 */
        virtual void SetPrimitiveTopology(PrimitiveTopology topology) = 0;

        // ── 绘制 ──

        /** 绘制索引几何体 */
        virtual void DrawIndexed(uint32 indexCount,
                                 uint32 startIndex = 0,
                                 uint32 baseVertex = 0) = 0;

        /** 绘制未索引几何体 */
        virtual void Draw(uint32 vertexCount,
                          uint32 startVertex = 0) = 0;

        /** 间接绘制（参数从 GPU 缓冲读取） */
        virtual void DrawIndexedIndirect(IRHIBuffer* argsBuffer,
                                         uint32 offset = 0) = 0;

        // ── 视口 / 裁剪 ──

        virtual void SetViewport(const Viewport& vp) = 0;
        virtual void SetScissorRect(const Rect& rect) = 0;

        // ── 同步 ──

        /**
         * @brief 插入资源屏障
         *
         * 多线程录制场景下，渲染层通过 RenderGraph 自动推导屏障，
         * IRHICommandList 仅负责接收和提交。
         */
        virtual void ResourceBarrier(uint32 count,
                                     const ResourceBarrierDesc* barriers) = 0;

        // ── 描述符绑定 ──

        /**
         * @brief 绑定常量/Uniform Buffer（动态偏移）
         *
         * @param set      Descriptor set index
         * @param binding  Binding 槽位
         * @param buffer   GPU buffer
         * @param offset   动态偏移（用于 Dynamic UBO 切换材质参数）
         * @param size     数据大小（字节）
         */
        virtual void SetConstantBuffer(uint32 set, uint32 binding,
                                       IRHIBuffer* buffer,
                                       uint64_t offset = 0,
                                       uint64_t size = 0) = 0;

        /**
         * @brief 绑定纹理（Sampled Image）
         */
        virtual void SetShaderResource(uint32 set, uint32 binding,
                                       IRHITexture* texture) = 0;

        // ── Compute 管线 ──

        /**
         * @brief 派发 Compute Shader
         * @param groupX 线程组 X 维度
         * @param groupY 线程组 Y 维度
         * @param groupZ 线程组 Z 维度
         */
        virtual void Dispatch(uint32_t groupX = 1,
                              uint32_t groupY = 1,
                              uint32_t groupZ = 1) = 0;

        /**
         * @brief 绑定 UAV / SSBO 到 Compute Shader
         * @param slot    绑定位（对应 layout(binding = N)）
         * @param buffer  GPU buffer（Vulkan: SSBO, D3D12: UAV, OpenGL: SSBO）
         */
        virtual void SetUnorderedAccess(uint32 slot,
                                        IRHIBuffer* buffer) = 0;

        // ── 查询 ──

        /** 获取命令列表类型（Direct / Bundle / Compute / Transfer） */
        virtual CommandListType GetType() const noexcept = 0;
    };

} // namespace RHI
} // namespace Engine
