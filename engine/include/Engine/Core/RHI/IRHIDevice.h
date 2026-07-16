#pragma once

/**
 * @file IRHIDevice.h
 * @brief RHI 设备抽象 — GPU 设备的顶层入口，所有渲染资源的创建源
 *
 * 设计理念：
 *   - 封装底层物理设备（Vulkan VkDevice / D3D12 ID3D12Device）
 *   - 资源创建（Buffer / Texture）通过 IGPUMemoryAllocator 对接归口
 *   - CommandList 和 Queue 为多线程录制提供基础
 *   - SwapChain 管理显示输出
 *   - PSO 创建通过 PSOCache 去重
 *
 * 生命周期：
 *   1. Create() → 获取设备实例（内部已调用 IGPUMemoryAllocator 初始化）
 *   2. CreateSwapChain() → 创建交换链
 *   3. CreateBuffer / CreateTexture → 创建渲染资源
 *   4. CreateCommandList() → 创建命令录制器（每 worker 一个）
 *   5. GetQueue() → 获取提交队列
 *   6. Present() → 显示帧
 */

#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/GPUAllocation.h"
#include "Engine/Core/RHI/GPUMemoryBlock.h"
#include "Engine/Core/RHI/IGPUMemoryAllocator.h"
#include "Engine/Core/RHI/PSODesc.h"
#include <memory>
#include <string>
#include <vector>

namespace Engine {
namespace RHI {

    // 前向声明
    class IRHICommandList;
    class IRHIPipelineState;
    class IRHICommandQueue;
    class IRHISwapChain;

    // RHI Buffer 描述符（不使用 GPUAllocation.h 中的简单 BufferDesc）
    struct RHIBufferDesc {
        uint64_t    size        = 0;
        uint32_t    stride      = 0;
        MemoryUsage memoryUsage = MemoryUsage::GPU_Only;
        const void* initialData = nullptr;
    };

    // ============================================================
    // Texture 描述符
    // ============================================================
    struct TextureDesc {
        uint32_t    width       = 0;
        uint32_t    height      = 0;
        uint32_t    depth       = 1;
        uint32_t    mipLevels   = 1;
        uint32_t    arrayLayers = 1;
        Format      format      = Format::RGBA8_UNorm;
        uint32_t    sampleCount = 1;
        MemoryUsage memoryUsage = MemoryUsage::GPU_Only;
        const void* initialData = nullptr;
    };

    // ============================================================
    // 交换链描述符
    // ============================================================
    struct SwapChainDesc {
        uint32_t    width        = 1280;
        uint32_t    height       = 720;
        Format      format       = Format::RGBA8_UNorm;
        uint32_t    bufferCount  = 2;         ///< 2=Double, 3=Triple buffering
        bool        vSync        = true;
        void*       windowHandle = nullptr;
    };

    // ============================================================
    // IRHIBuffer / IRHITexture — 资源句柄
    // ============================================================
    class IRHIBuffer {
    public:
        virtual ~IRHIBuffer() = default;
        virtual uint64_t  GetSize() const noexcept = 0;
        virtual const GPUAllocation& GetAllocation() const noexcept = 0;
        /** 获取持久映射指针（如无映射返回 nullptr） */
        virtual void* GetMappedPtr() const noexcept { return nullptr; }
    };

    class IRHITexture {
    public:
        virtual ~IRHITexture() = default;
        virtual uint32_t GetWidth()  const noexcept = 0;
        virtual uint32_t GetHeight() const noexcept = 0;
        virtual Format   GetFormat() const noexcept = 0;
    };

    class IRHIPipelineState {
    public:
        virtual ~IRHIPipelineState() = default;
    };

    // ============================================================
    // IRHICommandQueue — 命令提交队列
    // ============================================================
    class IRHICommandQueue {
    public:
        virtual ~IRHICommandQueue() = default;

        /** 提交一组已录制的命令列表执行 */
        virtual void ExecuteCommandLists(uint32 count,
                                         IRHICommandList** lists) = 0;

        /** 等待 GPU 执行完此队列中的所有命令 */
        virtual void WaitIdle() = 0;

        /** 获取队列类型 */
        virtual QueueType GetType() const noexcept = 0;
    };

    // ============================================================
    // IRHISwapChain — 交换链
    // ============================================================
    class IRHISwapChain {
    public:
        virtual ~IRHISwapChain() = default;

        /** 显示下一帧 */
        virtual void Present() = 0;

        /** 调整交换链尺寸 */
        virtual void Resize(uint32_t width, uint32_t height) = 0;

        /** 获取当前后备缓冲纹理 */
        virtual IRHITexture* GetBackBuffer(uint32_t index) const = 0;

        /** 当前后备缓冲索引 */
        virtual uint32_t GetCurrentBackBufferIndex() const = 0;

        /** 获取交换链缓冲数量 */
        virtual uint32_t GetBufferCount() const = 0;
    };

    // ============================================================
    // IRHIDevice — GPU 设备顶层入口
    // ============================================================
    class IRHIDevice {
    public:
        virtual ~IRHIDevice() = default;

        // ── 初始化 ──

        /**
         * @brief 初始化底层硬件设备
         *
         * 必须在任何资源创建（CreateBuffer/CreateTexture/CreateCommandList等）之前调用。
         * 
         * @param windowHandle 窗口句柄，headless 模式传 nullptr
         * @param width  后备缓冲宽度
         * @param height 后备缓冲高度
         * @return true  初始化成功
         * @return false 初始化失败（如无物理设备、驱动不支持等）
         */
        virtual bool Initialize(void* windowHandle, uint32_t width, uint32_t height) = 0;

        // ── 显存分配器 ──

        /** 设置显存分配器（在 CreateBuffer/CreateTexture 前调用） */
        void SetMemoryAllocator(GPUMemoryAllocatorPtr allocator) noexcept {
            m_MemoryAllocator = std::move(allocator);
        }

        IGPUMemoryAllocator* GetMemoryAllocator() const noexcept {
            return m_MemoryAllocator.get();
        }

        // ── 资源创建 ──

        /**
         * @brief 创建 GPU Buffer
         *
         * @param desc Buffer 描述符
         * @return 指向 IRHIBuffer 的指针（所有权由 shared_ptr 管理）
         *
         * 内部流程：
         *   1. 通过 IGPUMemoryAllocator 分配显存
         *   2. 创建底层 API 资源对象（VkBuffer / ID3D12Resource）
         *   3. 若有 initialData，通过 staging buffer 上传初始数据
         */
        virtual std::shared_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) = 0;

        /**
         * @brief 创建 GPU Texture
         */
        virtual std::shared_ptr<IRHITexture> CreateTexture(const TextureDesc& desc) = 0;

        /**
         * @brief 创建 Graphics PSO
         *
         * 通常不直接调用，而是通过 PSOCache::GetOrCreate() 间接使用。
         */
        virtual IRHIPipelineState* CreateGraphicsPSO(const GraphicsPSODesc& desc) = 0;

        /**
         * @brief 创建 Compute PSO
         */
        virtual IRHIPipelineState* CreateComputePSO(const ComputePSODesc& desc) = 0;

        // ── 命令列表 ──

        /** 创建一个命令列表（用于录制渲染命令） */
        virtual std::unique_ptr<IRHICommandList> CreateCommandList(
            CommandListType type = CommandListType::Direct) = 0;

        // ── 队列 ──

        /** 获取指定类型的命令队列 */
        virtual IRHICommandQueue* GetQueue(QueueType type) = 0;

        // ── 交换链 ──

        /** 创建交换链 */
        virtual std::unique_ptr<IRHISwapChain> CreateSwapChain(
            const SwapChainDesc& desc) = 0;

        /** 等待设备空闲 */
        virtual void WaitIdle() = 0;

        // ── 统计 ──

        /** 获取设备名称（"OpenGL 4.6", "Vulkan 1.3" 等） */
        virtual const char* GetDeviceName() const = 0;

        /** 获取显存使用统计 */
        virtual MemoryStats GetMemoryStats() const {
            return m_MemoryAllocator ? m_MemoryAllocator->GetStats() : MemoryStats{};
        }

    protected:
        GPUMemoryAllocatorPtr m_MemoryAllocator;
    };

    /** IRHIDevice 的所有权指针 */
    using RHIDevicePtr = std::unique_ptr<IRHIDevice>;

} // namespace RHI
} // namespace Engine