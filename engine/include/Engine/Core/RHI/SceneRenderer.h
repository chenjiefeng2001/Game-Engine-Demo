#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/Renderer/OrthographicCamera.h"
#include "Engine/Core/Renderer/SpriteBatch.h"
#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/TextureManager.h"
#include "Engine/Core/RHI/RenderQueue.h"
#include <memory>

namespace Engine {

    class Scene;

    /**
     * @brief 场景渲染器 — 负责将 Scene 中的渲染数据提交到 GPU
     *
     * RHI 原则：
     *   - 所有 GPU 资源通过 IGraphicsFactory 创建
     *   - 内部使用 RenderQueue 作为中间数据层
     *   - Scene 不直接与 ISpriteBatch 耦合
     *
     * 职责：
     *   1. 清空 RenderQueue
     *   2. 遍历 Scene 收集所有 RenderCommand
     *   3. 按纹理分组提交给 ISpriteBatch
     */
    class SceneRenderer {
    public:
        SceneRenderer(IGraphicsFactory& factory, TextureManager& textureManager);
        ~SceneRenderer() = default;

        // ── 配置 ──
        /** 设置渲染上下文（在窗口创建后调用，用于创建 SpriteBatch） */
        void SetRenderContext(IRenderContext& context);

        void SetCamera(OrthographicCamera* camera) { m_Camera = camera; }
        OrthographicCamera* GetCamera() const noexcept { return m_Camera; }

        void SetShader(const std::shared_ptr<Shader>& shader) { m_Shader = shader; }
        std::shared_ptr<Shader> GetShader() const noexcept { return m_Shader; }

        // ── 主渲染入口 ──
        /**
         * @brief 渲染一帧
         * @param scene 待渲染的场景
         * @param defaultTexture 当精灵未指定纹理时使用的默认纹理
         */
        void Render(Scene& scene,
                    const std::shared_ptr<Texture>& defaultTexture = nullptr);

        // ── 访问器 ──
        std::shared_ptr<ISpriteBatch> GetSpriteBatch() const noexcept { return m_SpriteBatch; }
        const RenderQueue& GetRenderQueue() const noexcept { return m_Queue; }

    private:
        /** 内部工具：将 RenderCommand 转换为 SpriteData 供 ISpriteBatch 消费 */
        void ExtractSpriteData(const RenderCommand& cmd, SpriteData& outData) const;

        IGraphicsFactory& m_Factory;
        TextureManager& m_TextureManager;
        RenderQueue m_Queue;
        std::shared_ptr<ISpriteBatch> m_SpriteBatch;
        std::shared_ptr<Shader> m_Shader;
        OrthographicCamera* m_Camera = nullptr;
    };

} // namespace Engine
