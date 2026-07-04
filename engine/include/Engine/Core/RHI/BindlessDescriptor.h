#pragma once

/**
 * @file BindlessDescriptor.h
 * @brief 无绑定描述符系统 — 跨后端统一纹理/缓冲绑定模型
 *
 * 核心理念：
 *   Vulkan 的 DescriptorIndexing / OpenGL 的 ARB_bindless_texture /
 *   D3D12 的 Descriptor Heap 在本质上是同一个概念：
 *   在 GPU 端维护一个巨大的资源句柄表，Shader 通过索引直接访问。
 *
 * 层次结构：
 *   DescriptorHeap → 资源数组
 *       ↓
 *   BindlessSlot → 单个资源的 64 位句柄
 *       ↓
 *   FrameSlot → 每帧分配的临时槽位
 *
 * 使用方式：
 * @code
 *   // 初始化堆
 *   DescriptorHeap heap;
 *   heap.Initialize(device, 16384);  // 最大 16384 个资源
 *
 *   // 绑定纹理
 *   BindlessSlot slot = heap.BindTexture(texture, sampler);
 *   cmd->SetMaterialSlots(&slot, 1);   // 传到 Shader
 *
 *   // Shader 中直接索引
 *   // layout(bindless_sampler) uniform sampler2D textures[];
 *   // vec4 color = texture(textures[materialID.slot0], uv);
 * @endcode
 */

#include "Engine/Types.h"
#include <cstdint>
#include <vector>

namespace Engine {

    class IRHIBuffer;
    class IRHITexture;

    namespace RHI {

        // 前向声明
        class IRHIDevice;

        // ============================================================
        // 绑定槽 — 64 位不可变句柄
        // ============================================================
        /**
         * @brief 单个资源的 GPU 句柄
         *
         * OpenGL:   glGetTextureHandleARB 返回的 GLuint64
         * Vulkan:   DescriptorIndexing 中的 uint32 descriptorIndex
         * D3D12:    Descriptor Heap 中的偏移索引
         */
        struct BindlessSlot {
            uint64_t handle   = 0;       ///< GPU 可见句柄
            uint32_t index    = UINT32_MAX; ///< 堆中的槽位索引
            uint32_t _padding = 0;

            static const BindlessSlot Invalid;

            bool IsValid() const noexcept { return handle != 0; }
        };

        // ============================================================
        // 每帧分配结构 — 动态资源绑定
        // ============================================================
        /**
         * @brief 每帧的材质绑定数据
         *
         * 材质系统在 BeginFrame 时分配这些槽位，
         * 通过 Push Constant / Root Constant 传入 Shader。
         */
        struct alignas(16) FrameMaterialSlot {
            uint32_t albedoSlot{ UINT32_MAX };
            uint32_t normalSlot{ UINT32_MAX };
            uint32_t metallicRoughnessSlot{ UINT32_MAX };
            uint32_t aoSlot{ UINT32_MAX };
            uint32_t emissiveSlot{ UINT32_MAX };
            uint32_t pad[3]{};

            /// 是否有任何纹理绑定
            bool HasAny() const noexcept {
                return albedoSlot != UINT32_MAX || normalSlot != UINT32_MAX;
            }
        };

        // ============================================================
        // 描述符堆 — 资源句柄表
        // ============================================================
        /**
         * @brief 跨后端的统一描述符堆
         *
         * 实现方式因后端而异：
         *
         *   OpenGL 4.6:
         *     glGetTextureHandleARB + glMakeTextureHandleResidentARB
         *     存储 GLuint64 handles[maxCount]
         *
         *   Vulkan:
         *     VkDescriptorSet 的 descriptorCount 设置为 maxCount
         *     使用 VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE + VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
         *
         *   D3D12:
         *     Descriptor Heap 类型 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
         *     设置为 Shader Visible
         *
         *   Metal:
         *     Argument Buffer 编码纹理句柄
         */
        class DescriptorHeap {
        public:
            DescriptorHeap() = default;
            ~DescriptorHeap() = default;

            /**
             * @brief 初始化堆
             * @param device    RHI 设备
             * @param maxCount 最大资源数（通常是 16384 或 65536）
             * @return 是否成功
             */
            bool Initialize(IRHIDevice& device, uint32_t maxCount = 16384);

            /** 关闭 */
            void Shutdown();

            /**
             * @brief 将纹理绑定到堆并返回句柄
             */
            BindlessSlot BindTexture(IRHITexture* texture, uint32_t samplerIndex = 0);

            /**
             * @brief 分配每帧材质绑定栏位
             */
            FrameMaterialSlot AllocateFrameSlots(
                const IRHITexture* albedo,
                const IRHITexture* normal,
                const IRHITexture* metallicRoughness,
                const IRHITexture* ao,
                const IRHITexture* emissive);

            /** 获取当前绑定数量 */
            uint32_t GetBindCount() const noexcept { return m_BindCount; }

            /** 是否已初始化 */
            bool IsInitialized() const noexcept { return m_Initialized; }

        private:
            bool m_Initialized = false;
            uint32_t m_MaxCount = 0;
            uint32_t m_BindCount = 0;
            std::vector<BindlessSlot> m_Slots;  // 所有已绑定的资源
        };

    } // namespace RHI
} // namespace Engine