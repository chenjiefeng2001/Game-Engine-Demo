#pragma once

/**
 * @file MemoryTypes.h
 * @brief GPU 显存类型枚举 — 与后端 API 解耦的内存属性与用途定义
 *
 * 设计原则：
 *   - 不依赖 Vulkan/D3D12/Metal 任何具体 API 头文件
 *   - 提供统一的抽象枚举，后端实现负责映射到具体 API 的标志位
 *   - 参照 VK_MEMORY_PROPERTY_* / D3D12_HEAP_TYPE_* 的主流分类
 */

#include "Engine/Types.h"
#include <cstdint>

namespace Engine {
namespace RHI {

    // ============================================================
    // 内存属性标志位（掩码组合）
    // ============================================================
    /**
     * @brief GPU 内存属性 — 描述物理内存的位置和访问特性
     *
     * 使用位掩码组合，后端根据平台能力进行映射：
     *
     *   属性组合                   | 对应 Vulkan              | 对应 D3D12
     *   --------------------------|--------------------------|---------------------------
     *   DeviceLocal               | DEVICE_LOCAL_BIT         | D3D12_HEAP_TYPE_DEFAULT
     *   HostVisible | HostCoherent| HOST_VISIBLE+COHERENT    | D3D12_HEAP_TYPE_UPLOAD
     *   HostVisible | HostCached  | HOST_VISIBLE+CACHED      | D3D12_HEAP_TYPE_READBACK
     */
    enum class MemoryProperty : uint16 {
        DeviceLocal    = 1 << 0,   ///< GPU 本地显存（最快，CPU 不可访问）
        HostVisible    = 1 << 1,   ///< CPU 可见（映射到 CPU 地址空间）
        HostCoherent   = 1 << 2,   ///< CPU/GPU 自动一致（无需手动 Flush/Invalidate）
        HostCached     = 1 << 3,   ///< CPU 端已缓存（读取快，写入需 Flush）
        LazilyAllocated = 1 << 4,   ///< 延迟分配（仅 Vulkan，用于临时附件/交换链）
        Protected      = 1 << 5,   ///< 受保护内存（如移动端 DRM 内容）
    };

    /** @brief 位运算：检查属性是否存在 */
    inline bool HasProperty(MemoryProperty flags, MemoryProperty property) noexcept {
        using U = std::underlying_type_t<MemoryProperty>;
        return (static_cast<U>(flags) & static_cast<U>(property)) != 0;
    }

    /** @brief 位运算：合并属性 */
    inline MemoryProperty operator|(MemoryProperty a, MemoryProperty b) noexcept {
        using U = std::underlying_type_t<MemoryProperty>;
        return static_cast<MemoryProperty>(static_cast<U>(a) | static_cast<U>(b));
    }

    /** @brief 位运算：求交集 */
    inline MemoryProperty operator&(MemoryProperty a, MemoryProperty b) noexcept {
        using U = std::underlying_type_t<MemoryProperty>;
        return static_cast<MemoryProperty>(static_cast<U>(a) & static_cast<U>(b));
    }

    // ============================================================
    // 预定义内存用途
    // ============================================================
    /**
     * @brief 内存用途 — 封装了常见的 MemoryProperty 组合 + 推荐分配策略
     *
     * 使用场景对应表：
     *
     *   GPU_Only      静态 VA/VBO、纹理、渲染目标、暂不更新的资源
     *   CPU_To_GPU    Dynamic VBO/UBO、粒子数据、每帧更新的常量缓冲
     *   GPU_To_CPU    回读缓冲：截图、异步查询结果、GPU 剔除输出
     *   CPU_Only      Staging 缓冲：上传/下载中间站，仅 CPU 侧分配
     */
    enum class MemoryUsage : uint8 {
        GPU_Only   = 0,   ///< DeviceLocal  — 纯 GPU 访问，最高带宽
        CPU_To_GPU = 1,   ///< HostVisible + HostCoherent | DeviceLocal  — 每帧 CPU 写入
        GPU_To_CPU = 2,   ///< HostVisible + HostCached              — GPU 写入 CPU 读取
        CPU_Only   = 3,   ///< HostVisible + HostCoherent             — 纯 CPU 侧暂存
        COUNT
    };

    /** @brief 根据用途返回对应的 MemoryProperty 掩码（默认实现，后端可覆盖） */
    inline MemoryProperty MemoryUsageToProperty(MemoryUsage usage) noexcept {
        switch (usage) {
            case MemoryUsage::GPU_Only:   return MemoryProperty::DeviceLocal;
            case MemoryUsage::CPU_To_GPU: return MemoryProperty::HostVisible |
                                                   MemoryProperty::HostCoherent |
                                                   MemoryProperty::DeviceLocal;
            case MemoryUsage::GPU_To_CPU: return MemoryProperty::HostVisible |
                                                   MemoryProperty::HostCached;
            case MemoryUsage::CPU_Only:   return MemoryProperty::HostVisible |
                                                   MemoryProperty::HostCoherent;
            default: return MemoryProperty::DeviceLocal;
        }
    }

    inline const char* MemoryUsageName(MemoryUsage usage) noexcept {
        switch (usage) {
            case MemoryUsage::GPU_Only:   return "GPU_Only";
            case MemoryUsage::CPU_To_GPU: return "CPU_To_GPU";
            case MemoryUsage::GPU_To_CPU: return "GPU_To_CPU";
            case MemoryUsage::CPU_Only:   return "CPU_Only";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 资源本性：Buffer 还是 Texture（解决内存混用冲突）
    // ============================================================
    /**
     * @brief 资源本性 — 用于解决 Vulkan bufferImageGranularity 冲突
     *
     * Vulkan 规定：同一 VkDeviceMemory 页内，Buffer 和 Image 必须满足
     * bufferImageGranularity 对齐，否则不能混放。分配器内部按此枚举
     * 隔离不同的内存页（Pool），避免别名冲突。
     *
     * OpenGL 后端忽略此值。
     */
    enum class ResourceNature : uint8 {
        Unknown = 0,
        Linear  = 1,    ///< 线性资源（Buffer / Uniform / Staging）
        Image   = 2,    ///< 非线性资源（Texture / RenderTarget / DepthStencil）
        MAX
    };

    inline const char* ResourceNatureName(ResourceNature nature) noexcept {
        switch (nature) {
            case ResourceNature::Linear:  return "Linear (Buffer)";
            case ResourceNature::Image:   return "Image (Texture)";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 分配策略
    // ============================================================
    /**
     * @brief 分配策略 — 控制内存块的内部管理方式
     *
     *   Bump           线性推进 + Reset() 批量回收 → 一次分配永不释放
     *   FrameTransient 帧级环形缓冲 → 与飞行帧数绑定，每帧自动回收
     *   Pool           固定大小槽位池 → 大量同构小资源（粒子、UAV）
     *   Buddy          伙伴系统 → 通用异构大小资源（默认策略，委托 VMA）
     */
    enum class AllocationStrategy : uint8 {
        Bump            = 0,   ///< 线性分配器 — O(1) 分配，仅支持整体 Reset
        FrameTransient  = 1,   ///< 帧级瞬态 — 环形缓冲，按 FramesInFlight 自动回收
        Pool            = 2,   ///< 固定块池 — O(1) 分配/释放，零碎片
        Buddy           = 3,   ///< 伙伴分配器 — 灵活大小，2^n 对齐
    };

    inline const char* AllocationStrategyName(AllocationStrategy strategy) noexcept {
        switch (strategy) {
            case AllocationStrategy::Bump:           return "Bump";
            case AllocationStrategy::FrameTransient: return "FrameTransient";
            case AllocationStrategy::Pool:           return "Pool";
            case AllocationStrategy::Buddy:          return "Buddy";
            default: return "Unknown";
        }
    }

    // ============================================================
    // 资源类型扩展：区分 GPU 资源类别（用于预置池大小）
    // ============================================================
    /**
     * @brief GPU 资源子类型 — 进一步细化 ResourceType，用于池化分配
     */
    enum class GPUResourceType : uint8 {
        Unknown         = 0,
        Buffer_Static   = 1,   ///< 静态顶点/索引缓冲
        Buffer_Dynamic  = 2,   ///< 动态 UBO/SSBO
        Buffer_Staging  = 3,   ///< Staging/回读缓冲
        Texture_2D      = 4,   ///< 2D 纹理
        Texture_Cube    = 5,   ///< Cube 贴图
        Texture_3D      = 6,   ///< 3D 纹理
        Texture_RT      = 7,   ///< 渲染目标 / 深度模板
        Acceleration    = 8,   ///< 加速结构 (RT)
        COUNT
    };

    inline const char* GPUResourceTypeName(GPUResourceType type) noexcept {
        switch (type) {
            case GPUResourceType::Buffer_Static:   return "Static Buffer";
            case GPUResourceType::Buffer_Dynamic:  return "Dynamic Buffer";
            case GPUResourceType::Buffer_Staging:  return "Staging Buffer";
            case GPUResourceType::Texture_2D:      return "Texture 2D";
            case GPUResourceType::Texture_Cube:    return "Texture Cube";
            case GPUResourceType::Texture_3D:      return "Texture 3D";
            case GPUResourceType::Texture_RT:      return "Render Target";
            case GPUResourceType::Acceleration:    return "Acceleration";
            default: return "Unknown";
        }
    }

    /** 从 GPUResourceType 推断资源本性（Buffer vs Image） */
    inline ResourceNature GPUResourceTypeToNature(GPUResourceType type) noexcept {
        switch (type) {
            case GPUResourceType::Buffer_Static:
            case GPUResourceType::Buffer_Dynamic:
            case GPUResourceType::Buffer_Staging:
                return ResourceNature::Linear;
            case GPUResourceType::Texture_2D:
            case GPUResourceType::Texture_Cube:
            case GPUResourceType::Texture_3D:
            case GPUResourceType::Texture_RT:
                return ResourceNature::Image;
            default:
                return ResourceNature::Unknown;
        }
    }

} // namespace RHI
} // namespace Engine