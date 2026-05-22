#pragma once
#include "Engine/Core/Renderer/SpriteBatch.h"
#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/RenderResources/VertexArray.h"
#include "Engine/Core/RenderResources/VertexBuffer.h"
#include "Engine/Core/RenderResources/IndexBuffer.h"
#include "Engine/Core/IRenderContext.h"
#include <glad/gl.h>
#include <vector>
#include <cstdint>

namespace Engine {

    class OpenGLSpriteBatch : public ISpriteBatch {
    public:
        // 每个顶点的数据布局（32 字节）
        struct SpriteVertex {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        OpenGLSpriteBatch(GladGLContext& gl, IRenderContext& renderContext);
        virtual ~OpenGLSpriteBatch() override;

        // ── ISpriteBatch 接口 ──
        virtual void Begin(const std::shared_ptr<Texture>& texture) override;
        virtual void Draw(const SpriteData& sprite) override;
        virtual void End() override;
        virtual void Flush() override;
        virtual uint32_t GetSpriteCount() const override { return m_SpriteCount; }
        virtual void SetCapacity(uint32_t maxSprites) override;

    private:
        // 内部提交：将当前 CPU 缓冲区数据上传 GPU 并绘制
        void SubmitBatch();

        // 构建单个精灵的 4 个顶点（CPU 端变换）
        void BuildVertices(
            const SpriteData& sprite,
            SpriteVertex* outVerts  // 指向连续 4 个顶点的起始位置
        );

        GladGLContext& m_GL;
        IRenderContext& m_RenderContext;

        // ── GPU 资源 ──
        GLuint m_VAO = 0;
        GLuint m_VBO = 0;
        GLuint m_EBO = 0;

        // ── 当前批次状态 ──
        std::shared_ptr<Texture> m_CurrentTexture;
        uint32_t m_MaxSprites = 1000;
        uint32_t m_SpriteCount = 0;
        bool m_Began = false;

        // ── CPU 暂存缓冲区 ──
        std::vector<SpriteVertex> m_VertexBuffer;
        std::vector<uint32_t>     m_IndexBuffer;

        // ── 预生成的索引数据（所有精灵共享） ──
        // 每个精灵 6 个索引: 0,1,2, 2,3,0
        // 第 i 个精灵的索引偏移 = i*4
        void GenerateIndices(uint32_t maxSprites);
        bool m_IndicesGenerated = false;
    };

}
