#pragma once

/**
 * @file AutoPipelineLayout.h
 * @brief 自动 PipelineLayout 缓存 — 根据 Shader 反射数据生成 DescriptorSet Layout
 *
 * 设计目标：
 *   消除硬编码的 DescriptorSet/Binding 编号。
 *   所有材质渲染管线自动根据 SPIR-V 反射生成 Layout 并缓存。
 *
 * 核心逻辑：
 *   1. Shader 编译后提取 ShaderReflectionData
 *   2. 反射数据 → LayoutKey（哈希 = set/binding/type 的组合）
 *   3. 查询缓存 → 命中则复用，未命中则创建新 VkDescriptorSetLayout
 *
 * 线程安全：全局单例，所有操作加锁。
 */

#include "Engine/Rendering/ShaderReflection.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/RHITypes.h"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>

namespace Engine {
namespace Rendering {

    // ============================================================
    // 描述符类型 — RHI 无关的抽象
    // ============================================================
    enum class DescriptorType : uint8 {
        UniformBuffer      = 0,  // UBO
        SampledImage       = 1,  // Texture + Sampler
        StorageBuffer      = 2,  // SSBO
        StorageImage       = 3,  // UAV
        CombinedImageSampler = 4,
    };

    // ============================================================
    // 单个描述符绑定信息
    // ============================================================
    struct DescriptorBinding {
        uint32_t        set     = 0;       ///< Descriptor set index
        uint32_t        binding = 0;       ///< Binding slot
        DescriptorType  type    = DescriptorType::UniformBuffer;
        uint32_t        count   = 1;       ///< Array size (1 for non-array)
        uint32_t        stageFlags = 0;    ///< 位掩码: (1 << ShaderStageType)
    };

    // ============================================================
    // Layout 缓存键 — 根据所有绑定信息生成唯一哈希
    // ============================================================
    struct LayoutKey {
        uint64_t hash = 0;

        bool operator==(const LayoutKey& o) const noexcept { return hash == o.hash; }
        bool operator!=(const LayoutKey& o) const noexcept { return hash != o.hash; }
    };

    // ============================================================
    // PipelineLayout 描述符
    // ============================================================
    struct PipelineLayoutDesc {
        std::vector<DescriptorBinding> bindings;
        uint32_t pushConstantSize = 0;

        /** 从 ShaderReflectionData 自动生成描述 */
        static PipelineLayoutDesc FromReflection(const ShaderReflectionData& reflection);

        /** 计算哈希键 */
        LayoutKey ComputeKey() const noexcept;
    };

    // ============================================================
    // AutoPipelineLayoutCache — 全局单例
    // ============================================================
    /**
     * @brief 自动 PipelineLayout 缓存
     *
     * Shader 编译后自动调用 EnsureLayout()，返回一个 PipelineLayout 句柄。
     * 布局完全由 SPIR-V 反射数据驱动，无需手动声明。
     */
    class AutoPipelineLayoutCache {
    public:
        static AutoPipelineLayoutCache& Get() {
            static AutoPipelineLayoutCache instance;
            return instance;
        }

        AutoPipelineLayoutCache(const AutoPipelineLayoutCache&) = delete;
        AutoPipelineLayoutCache& operator=(const AutoPipelineLayoutCache&) = delete;

        /**
         * @brief 获取或创建 PipelineLayout
         *
         * @param reflection  着色器反射数据
         * @param device      RHI 设备（用于创建底层 layout 对象）
         * @return 平台相关的 PipelineLayout 句柄（void* 泛化）
         */
        void* EnsureLayout(const ShaderReflectionData& reflection,
                           RHI::IRHIDevice& device);

        /**
         * @brief 释放所有缓存的 Layout（设备销毁时调用）
         */
        void Clear();

        /** 缓存统计 */
        size_t GetCacheSize() const noexcept { std::lock_guard<std::mutex> lk(m_Mutex); return m_Cache.size(); }

    private:
        AutoPipelineLayoutCache() = default;

        struct CachedLayout {
            void* handle = nullptr;       ///< VkDescriptorSetLayout / ID3D12RootSignature 等
            uint32_t refCount = 1;
        };

        mutable std::mutex m_Mutex;
        std::unordered_map<uint64_t, CachedLayout> m_Cache;

        /** 内部：创建新的 PipelineLayout（后端相关） */
        void* CreatePipelineLayout(const PipelineLayoutDesc& desc,
                                    RHI::IRHIDevice& device);
    };

} // namespace Rendering
} // namespace Engine

// ── std::hash 特化 ──
namespace std {
    template <>
    struct hash<Engine::Rendering::LayoutKey> {
        size_t operator()(const Engine::Rendering::LayoutKey& k) const noexcept {
            return static_cast<size_t>(k.hash);
        }
    };
}