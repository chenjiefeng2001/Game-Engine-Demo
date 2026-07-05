#pragma once

/**
 * @file GPUAllocation.h
 * @brief GPU 分配结果 — 封装一次显存分配的元数据与 API 句柄
 *
 * 设计要点：
 *   - 值语义：轻量拷贝，可安全传递
 *   - API 句柄：apiMemoryHandle 用于 Bind 操作，apiResource 为资源对象本身
 *   - DeviceAddress 支持：为 Vulkan 1.2+ / D3D12 GPU VA 预留字段
 *   - 生命周期追踪：通过 GPUMemoryBlock* 弱引用防止 block 被过早释放
 */

#include "Engine/Types.h"
#include "Engine/Core/RHI/MemoryTypes.h"
#include <string>

namespace Engine {
namespace RHI {

    // 前向声明
    class GPUMemoryBlock;

    // ============================================================
    // 资源描述符（分配器接管资源创建时使用）
    // ============================================================

    /**
     * @brief Buffer 创建描述符
     *
     * 对应 Vulkan: VkBufferCreateInfo 的核心字段
     *       D3D12:  D3D12_RESOURCE_DESC (Buffer) 的核心字段
     *       OpenGL: glCreateBuffers / glNamedBufferStorage 的参数
     */
    struct BufferDesc {
        uint64      size        = 0;        ///< 字节大小
        uint32      stride      = 0;        ///< 结构步长（0 = 非结构化）
        uint32      usageFlags  = 0;        ///< BufferUsage 掩码（后端解释）
        const void* initialData = nullptr;  ///< 初始数据（可选，nullptr = 仅分配不清零）

        BufferDesc() = default;
        BufferDesc(uint64 sz, uint32 usage = 0, const void* data = nullptr)
            : size(sz), usageFlags(usage), initialData(data) {}
    };

    /**
     * @brief Image 创建描述符
     *
     * 对应 Vulkan: VkImageCreateInfo 的核心字段
     *       D3D12:  D3D12_RESOURCE_DESC (Texture) 的核心字段
     *       OpenGL: glCreateTextures / glTexStorage2D 的参数
     */
    struct ImageDesc {
        uint32      width           = 0;
        uint32      height          = 0;
        uint32      depth           = 1;        ///< 3D 纹理高度或数组层数
        uint32      mipLevels       = 1;
        uint32      arrayLayers     = 1;
        uint32      format          = 0;        ///< 像素格式（后端解释）
        uint32      sampleCount     = 1;        ///< MSAA 样本数
        uint32      usageFlags      = 0;        ///< ImageUsage 掩码（后端解释）
        const void* initialData     = nullptr;

        ImageDesc() = default;
        ImageDesc(uint32 w, uint32 h, uint32 fmt = 0, uint32 usage = 0)
            : width(w), height(h), format(fmt), usageFlags(usage) {}
    };

    // ============================================================
    // 分配元数据
    // ============================================================
    /**
     * @brief GPUAllocation — 从 IGPUMemoryAllocator 返回的分配结果
     *
     * 此结构体可安全拷贝。内部 GPUMemoryBlock* 是弱引用性质：
     * Block 的生命周期由 IGPUMemoryAllocator 管理，分配结果
     * 仅引用其地址用于后续 Deallocate 逻辑。
     *
     * 对于 CreateBuffer/CreateImage 返回的分配，apiResource 存放了
     * 后端创建的 API 资源对象（如 VkBuffer / ID3D12Resource / GLuint）。
     */
    struct GPUAllocation {
        // ── 块信息 ──
        GPUMemoryBlock* block        = nullptr;   ///< 所属内存块（弱引用，不拥有）

        // ── 内存定位 ──
        uint64          offset       = 0;          ///< 块内字节偏移
        uint64          size         = 0;          ///< 实际分配大小（对齐后）
        uint64          requestedSize = 0;         ///< 用户请求的原始大小
        uint64          alignment    = 0;          ///< 实际对齐值

        // ── 属性 ──
        MemoryUsage     usage        = MemoryUsage::GPU_Only;
        MemoryProperty  properties   = MemoryProperty::DeviceLocal;
        AllocationStrategy strategy = AllocationStrategy::Buddy;
        ResourceNature  nature       = ResourceNature::Unknown;  ///< Buffer 还是 Image

        // ── API 句柄 ──

        /**
         * @brief 底层 API 内存对象句柄
         *
         * Vulkan:  VkDeviceMemory  （vkBindBufferMemory/vkBindImageMemory 的参数）
         * D3D12:   ID3D12Heap*
         * OpenGL:  未使用（GL 没有显式内存对象，直接分配在驱动内部）
         */
        void*           apiMemoryHandle = nullptr;

        /**
         * @brief 底层 API 资源对象句柄
         *
         * 仅当通过 CreateBuffer/CreateImage 创建时填充。
         * Vulkan:  VkBuffer / VkImage
         * D3D12:   ID3D12Resource*
         * OpenGL:  GLuint (Buffer ID / Texture ID)
         *
         * 外部通过此句柄访问资源对象，无需再调用 IGraphicsFactory::Create*。
         */
        void*           apiResource     = nullptr;

        // ── CPU 映射（仅 HostVisible 时有效） ──
        void*           mappedPtr    = nullptr;   ///< CPU 可写地址
        uint64          mappedSize   = 0;          ///< 映射范围

        // ── 设备地址（Vulkan 1.2+ / D3D12 GPU-VA） ──
        uint64          devicePtr    = 0;          ///< GPU 可见地址（0 = 不支持）

        // ── 调试标签 ──
        GPUResourceType resourceType = GPUResourceType::Unknown;
        const char*     debugTag     = nullptr;   ///< 调试名称（不拥有）

        // ── 便利方法 ──

        bool IsValid() const noexcept { return block != nullptr && size > 0; }

        /** 是否通过 CreateBuffer/CreateImage 创建（拥有 apiResource） */
        bool HasAPIResource() const noexcept { return apiResource != nullptr; }

        /** 是否有独立的 API 内存句柄（Vulkan/D3D12） */
        bool HasAPIMemoryHandle() const noexcept { return apiMemoryHandle != nullptr; }

        bool IsMapped() const noexcept { return mappedPtr != nullptr; }
        bool HasDeviceAddress() const noexcept { return devicePtr != 0; }
        uint64 MappedSize() const noexcept { return mappedSize > 0 ? mappedSize : size; }

        std::string DebugString() const {
            char buf[320];
            snprintf(buf, sizeof(buf),
                "GPUAlloc{%zu/%zu bytes, off=%zu, %s, %s, %s, res=%s, mem=%s, mapped=%s}",
                (size_t)requestedSize, (size_t)size, (size_t)offset,
                MemoryUsageName(usage),
                AllocationStrategyName(strategy),
                ResourceNatureName(nature),
                HasAPIResource() ? "yes" : "no",
                HasAPIMemoryHandle() ? "yes" : "no",
                IsMapped() ? "yes" : "no");
            return buf;
        }

        GPUAllocation() = default;
        GPUAllocation(const GPUAllocation&) = default;
        GPUAllocation& operator=(const GPUAllocation&) = default;
        GPUAllocation(GPUAllocation&&) noexcept = default;
        GPUAllocation& operator=(GPUAllocation&&) noexcept = default;
    };

    inline const GPUAllocation& NullAllocation() noexcept {
        static const GPUAllocation nullAlloc{};
        return nullAlloc;
    }

} // namespace RHI
} // namespace Engine