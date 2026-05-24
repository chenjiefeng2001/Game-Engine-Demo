#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Engine {

    class TextureManager;  // 前向声明

    /**
     * @brief 精灵组件 — 描述一个可渲染的 2D 精灵
     *
     * 与现有的 ISpriteBatch 系统配合使用，
     * 通过 SpriteData 结构将渲染数据提交给批渲染器。
     */
    class SpriteComponent {
    public:
        SpriteComponent();
        explicit SpriteComponent(std::shared_ptr<Texture> texture);
        SpriteComponent(std::shared_ptr<Texture> texture,
                        const glm::vec4& color);

        // 通过 TextureManager 按路径加载纹理
        explicit SpriteComponent(TextureManager& texMgr, const std::string& path);
        SpriteComponent(TextureManager& texMgr, const std::string& path,
                        const glm::vec4& color);

        // ── 纹理 ──
        void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
        void SetTexture(TextureManager& texMgr, const std::string& path);
        std::shared_ptr<Texture> GetTexture() const noexcept { return m_Texture; }
        bool HasTexture() const noexcept { return m_Texture != nullptr; }

        // ── 颜色 ──
        void SetColor(const glm::vec4& color) { m_Color = color; }
        void SetColor(float r, float g, float b, float a = 1.0f) {
            m_Color = { r, g, b, a };
        }
        const glm::vec4& GetColor() const noexcept { return m_Color; }

        // ── UV 变换 ──
        void SetUV(float u, float v, float w, float h) {
            m_UVX = u; m_UVY = v; m_UVW = w; m_UVH = h;
        }
        float GetUVX() const noexcept { return m_UVX; }
        float GetUVY() const noexcept { return m_UVY; }
        float GetUVW() const noexcept { return m_UVW; }
        float GetUVH() const noexcept { return m_UVH; }
        void SetTiling(const glm::vec2& tiling) { m_Tiling = tiling; }
        void SetOffset(const glm::vec2& offset) { m_Offset = offset; }
        const glm::vec2& GetTiling() const noexcept { return m_Tiling; }
        const glm::vec2& GetOffset() const noexcept { return m_Offset; }

        // ── 排序 ──
        void SetSortingLayer(int layer) { m_SortingLayer = layer; }
        int  GetSortingLayer() const noexcept { return m_SortingLayer; }

        void SetOrderInLayer(int order) { m_OrderInLayer = order; }
        int  GetOrderInLayer() const noexcept { return m_OrderInLayer; }

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const noexcept { return m_Visible; }

        // ── 转换为 SpriteData（给 ISpriteBatch 使用） ──
        /**
         * @brief 从给定的变换矩阵构建 SpriteData
         * @param worldMatrix 游戏对象的世界变换矩阵
         * @return 可直接提交给 ISpriteBatch::Draw 的 SpriteData
         */
        SpriteData ToSpriteData(const glm::mat4& worldMatrix) const;

    private:
        std::shared_ptr<Texture> m_Texture = nullptr;

        glm::vec4 m_Color = { 1.0f, 1.0f, 1.0f, 1.0f };

        // UV 参数 (直接传给 SpriteData)
        float m_UVX = 0.0f;
        float m_UVY = 0.0f;
        float m_UVW = 1.0f;
        float m_UVH = 1.0f;

        // Tiling & Offset (可选的额外变换，目前保留但不用于 SpriteData)
        glm::vec2 m_Tiling  = { 1.0f, 1.0f };
        glm::vec2 m_Offset  = { 0.0f, 0.0f };

        // 渲染排序
        int m_SortingLayer  = 0;
        int m_OrderInLayer  = 0;

        // 可见性
        bool m_Visible = true;
    };

} // namespace Engine
