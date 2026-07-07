/**
 * @file BindlessTextureManager.cpp
 * @brief Bindless 纹理管理器 — 将 Texture 系统与 BindlessDescriptor 对接
 *
 * 所有 Texture 创建时自动从 BindlessAllocator 获取全局唯一索引。
 * Shader 中通过 bindless index 直接采样，消除每材质绑定开销。
 */

#include "Engine/Core/RHI/BindlessDescriptor.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RHI/DescriptorHeap.h"
#include <unordered_map>
#include <mutex>

namespace Engine { namespace Rendering {

    // ════════════════════════════════════════════════════════════
    // 全局 Bindless 纹理管理器
    // ════════════════════════════════════════════════════════════

    class BindlessTextureManager {
    public:
        static BindlessTextureManager& Get() {
            static BindlessTextureManager instance;
            return instance;
        }

        /**
         * @brief 初始化 Bindless 纹理系统
         * @param maxTextures 最大纹理数
         */
        void Initialize(uint32_t maxTextures = 4096) {
            std::lock_guard<std::mutex> lock(m_Mutex);
            if (m_Initialized) return;

            m_MaxTextures = maxTextures;
            m_NextIndex = 1;  // 0 保留为 null/black texture

            m_Initialized = true;
        }

        /**
         * @brief 注册一个纹理并分配 bindless 索引
         * @param texture 纹理指针
         * @return bindless 索引
         */
        uint32_t RegisterTexture(Texture* texture) {
            if (!texture) return 0;

            std::lock_guard<std::mutex> lock(m_Mutex);
            uint32_t index = m_NextIndex++;

            // 存储纹理 → 索引映射
            m_TextureToIndex[texture] = index;
            m_IndexToTexture[index] = texture;

            return index;
        }

        /**
         * @brief 获取纹理的 bindless 索引
         */
        uint32_t GetIndex(Texture* texture) const {
            if (!texture) return 0;
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto it = m_TextureToIndex.find(texture);
            if (it != m_TextureToIndex.end()) {
                return it->second;
            }
            return 0;
        }

        /**
         * @brief 释放纹理的 bindless 索引
         */
        void UnregisterTexture(Texture* texture) {
            if (!texture) return;
            std::lock_guard<std::mutex> lock(m_Mutex);

            auto it = m_TextureToIndex.find(texture);
            if (it != m_TextureToIndex.end()) {
                m_IndexToTexture.erase(it->second);
                m_TextureToIndex.erase(it);
            }
        }

        uint32_t GetMaxTextures() const { return m_MaxTextures; }
        bool IsInitialized() const { return m_Initialized; }

    private:
        BindlessTextureManager() = default;
        ~BindlessTextureManager() = default;
        BindlessTextureManager(const BindlessTextureManager&) = delete;
        BindlessTextureManager& operator=(const BindlessTextureManager&) = delete;

        bool m_Initialized = false;
        uint32_t m_MaxTextures = 0;
        uint32_t m_NextIndex = 1;

        mutable std::mutex m_Mutex;
        std::unordered_map<Texture*, uint32_t> m_TextureToIndex;
        std::unordered_map<uint32_t, Texture*> m_IndexToTexture;
    };

}} // namespace Engine::Rendering