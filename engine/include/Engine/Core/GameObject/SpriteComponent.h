#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include "Engine/Core/RHI/MathTypes.h"
#include <memory>
#include <string>

namespace Engine {

    class TextureManager;  // 前向声明

    /**
     * @brief 精灵组件 — 描述一个可渲染的 2D 精灵
     *
     * RHI 原则：头文件只依赖 RHI/MathTypes.h（纯数据），不依赖 glm。
     * 所有数学运算在 .cpp 中使用 glm 实现。
     *
     * 与现有的 ISpriteBatch 系统配合使用，
     * 通过 SpriteData 结构将渲染数据提交给批渲染器。
     */
    class SpriteComponent {
    public:
        SpriteComponent();
        explicit SpriteComponent(std::shared_ptr<Texture> texture);
        SpriteComponent(std::shared_ptr<Texture> texture,
                        const Vec4& color);

        // 通过 TextureManager 按路径加载纹理
        explicit SpriteComponent(TextureManager& texMgr, const std::string& path);
        SpriteComponent(TextureManager& texMgr, const std::string& path,
                        const Vec4& color);

        // ── 纹理 ──
        void SetTexture(std::shared_ptr<Texture> texture) { m_Texture = texture; }
        void SetTexture(TextureManager& texMgr, const std::string& path);
        std::shared_ptr<Texture> GetTexture() const noexcept { return m_Texture; }
        bool HasTexture() const noexcept { return m_Texture != nullptr; }

        // ── 颜色 ──
        void SetColor(const Vec4& color) { m_Color = color; }
        void SetColor(float32 r, float32 g, float32 b, float32 a = 1.0f) {
            m_Color = Vec4(r, g, b, a);
        }
        const Vec4& GetColor() const noexcept { return m_Color; }

        // ── UV 变换 ──
        void SetUV(float32 u, float32 v, float32 w, float32 h) {
            m_UVX = u; m_UVY = v; m_UVW = w; m_UVH = h;
        }
        float32 GetUVX() const noexcept { return m_UVX; }
        float32 GetUVY() const noexcept { return m_UVY; }
        float32 GetUVW() const noexcept { return m_UVW; }
        float32 GetUVH() const noexcept { return m_UVH; }
        void SetTiling(const Vec2& tiling) { m_Tiling = tiling; }
        void SetOffset(const Vec2& offset) { m_Offset = offset; }
        const Vec2& GetTiling() const noexcept { return m_Tiling; }
        const Vec2& GetOffset() const noexcept { return m_Offset; }

        // ── 排序 ──
        void SetSortingLayer(int32 layer) { m_SortingLayer = layer; }
        int32 GetSortingLayer() const noexcept { return m_SortingLayer; }

        void SetOrderInLayer(int32 order) { m_OrderInLayer = order; }
        int32 GetOrderInLayer() const noexcept { return m_OrderInLayer; }

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const noexcept { return m_Visible; }

        // ── 转换为 SpriteData（给 ISpriteBatch 使用） ──
        /**
         * @brief 从给定的变换矩阵构建 SpriteData
         * @param worldMatrix 游戏对象的世界变换矩阵（float32[16]）
         * @return 可直接提交给 ISpriteBatch::Draw 的 SpriteData
         */
        SpriteData ToSpriteData(const float32* worldMatrix) const;

    private:
        std::shared_ptr<Texture> m_Texture = nullptr;

        Vec4 m_Color = Vec4(1.0f, 1.0f, 1.0f, 1.0f);

        // UV 参数 (直接传给 SpriteData)
        float32 m_UVX = 0.0f;
        float32 m_UVY = 0.0f;
        float32 m_UVW = 1.0f;
        float32 m_UVH = 1.0f;

        // Tiling & Offset
        Vec2 m_Tiling  = Vec2(1.0f, 1.0f);
        Vec2 m_Offset  = Vec2(0.0f, 0.0f);

        // 渲染排序
        int32 m_SortingLayer  = 0;
        int32 m_OrderInLayer  = 0;

        // 可见性
        bool m_Visible = true;
    };

} // namespace Engine
