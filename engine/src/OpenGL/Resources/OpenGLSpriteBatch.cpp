#include "OpenGLSpriteBatch.h"
#include "Engine/Core/RenderResources/Texture.h"
#include <glad/gl.h>
#include <cmath>
#include <cstring>
#include <iostream>

namespace Engine {
    OpenGLSpriteBatch::OpenGLSpriteBatch(GladGLContext& gl, IRenderContext& renderContext)
        : m_GL(gl)
        , m_RenderContext(renderContext)
    {
        // 创建 GPU 资源
        m_GL.GenVertexArrays(1, &m_VAO);
        m_GL.GenBuffers(1, &m_VBO);
        m_GL.GenBuffers(1, &m_EBO);

        // 设置 VAO 布局: vec2 pos + vec2 uv + vec4 color
        m_GL.BindVertexArray(m_VAO);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);

        // position (vec2) — location 0
        m_GL.EnableVertexAttribArray(0);
        m_GL.VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                                 sizeof(SpriteVertex), (void*)0);
        // texcoord (vec2) — location 1
        m_GL.EnableVertexAttribArray(1);
        m_GL.VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                                 sizeof(SpriteVertex), (void*)(2 * sizeof(float)));
        // color (vec4) — location 2
        m_GL.EnableVertexAttribArray(2);
        m_GL.VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE,
                                 sizeof(SpriteVertex), (void*)(4 * sizeof(float)));

        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);

        m_GL.BindVertexArray(0);

        // 预分配 CPU 缓冲区
        SetCapacity(m_MaxSprites);
    }

    OpenGLSpriteBatch::~OpenGLSpriteBatch() {
        m_GL.DeleteVertexArrays(1, &m_VAO);
        m_GL.DeleteBuffers(1, &m_VBO);
        m_GL.DeleteBuffers(1, &m_EBO);
    }

    void OpenGLSpriteBatch::SetCapacity(uint32_t maxSprites) {
        m_MaxSprites = maxSprites;
        m_VertexBuffer.resize(m_MaxSprites * 4);
        GenerateIndices(m_MaxSprites);

        // 上传预生成的索引到 GPU
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER,
                        m_MaxSprites * 6 * sizeof(uint32_t),
                        m_IndexBuffer.data(),
                        GL_STATIC_DRAW);
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void OpenGLSpriteBatch::GenerateIndices(uint32_t maxSprites) {
        if (m_IndicesGenerated && m_IndexBuffer.size() >= maxSprites * 6)
            return;

        m_IndexBuffer.resize(maxSprites * 6);
        for (uint32_t i = 0; i < maxSprites; i++) {
            uint32_t offset = i * 4;  // 第 i 个精灵的顶点起始索引
            uint32_t* idx = &m_IndexBuffer[i * 6];
            idx[0] = offset + 0;
            idx[1] = offset + 1;
            idx[2] = offset + 2;
            idx[3] = offset + 2;
            idx[4] = offset + 3;
            idx[5] = offset + 0;
        }
        m_IndicesGenerated = true;
    }

    void OpenGLSpriteBatch::BuildVertices(
        const SpriteData& sprite,
        SpriteVertex* outVerts)
    {
        const auto& t = sprite.transform;

        // 半宽半高（单位 quad 的一半 = 0.5，乘以缩放）
        float hw = 0.5f * t.scaleX;
        float hh = 0.5f * t.scaleY;

        // 4 个角的本地坐标（未旋转）
        float localX[4] = { -hw,  hw,  hw, -hw };
        float localY[4] = { -hh, -hh,  hh,  hh };

        // 4 个角的 UV
        float uv[4][2] = {
            { sprite.uvX,               sprite.uvY + sprite.uvH },
            { sprite.uvX + sprite.uvW,  sprite.uvY + sprite.uvH },
            { sprite.uvX + sprite.uvW,  sprite.uvY },
            { sprite.uvX,               sprite.uvY }
        };

        // 旋转的 sin/cos
        float sinA = std::sin(t.angle);
        float cosA = std::cos(t.angle);

        for (int i = 0; i < 4; i++) {
            // 应用旋转
            float rx = localX[i] * cosA - localY[i] * sinA;
            float ry = localX[i] * sinA + localY[i] * cosA;

            // 应用平移
            outVerts[i].x = rx + t.x;
            outVerts[i].y = ry + t.y;

            // UV
            outVerts[i].u = uv[i][0];
            outVerts[i].v = uv[i][1];

            // 颜色
            outVerts[i].r = sprite.colorR;
            outVerts[i].g = sprite.colorG;
            outVerts[i].b = sprite.colorB;
            outVerts[i].a = sprite.colorA;
        }
    }


    void OpenGLSpriteBatch::Begin(const std::shared_ptr<Texture>& texture) {
        if (m_Began) {
            // 如果已经开始，先结束上一个批次
            End();
        }

        m_CurrentTexture = texture;
        m_SpriteCount = 0;
        m_Began = true;
    }

    void OpenGLSpriteBatch::Draw(const SpriteData& sprite) {
        if (!m_Began) {
            std::cerr << "[OpenGLSpriteBatch] Draw() called without Begin()!" << std::endl;
            return;
        }

        // 缓冲区满了 → 自动刷新
        if (m_SpriteCount >= m_MaxSprites) {
            Flush();
        }

        // 构建顶点数据到 CPU 缓冲区
        SpriteVertex* dest = m_VertexBuffer.data() + m_SpriteCount * 4;
        BuildVertices(sprite, dest);
        m_SpriteCount++;
    }

    void OpenGLSpriteBatch::End() {
        if (!m_Began || m_SpriteCount == 0) {
            m_Began = false;
            return;
        }

        SubmitBatch();
        m_Began = false;
    }

    void OpenGLSpriteBatch::Flush() {
        if (m_SpriteCount == 0) return;
        SubmitBatch();
        m_SpriteCount = 0;  // 重置计数器，继续收集
    }

    void OpenGLSpriteBatch::SubmitBatch() {
        if (m_SpriteCount == 0) return;

        uint32_t vertexCount = m_SpriteCount * 4;
        uint32_t indexCount  = m_SpriteCount * 6;

        // 1. 上传顶点数据到 VBO
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
        m_GL.BufferData(GL_ARRAY_BUFFER,
                        vertexCount * sizeof(SpriteVertex),
                        m_VertexBuffer.data(),
                        GL_DYNAMIC_DRAW);

        // 2. 绑定纹理（到 0 号单元）
        if (m_CurrentTexture) {
            m_CurrentTexture->Bind(0);
        }

        // 3. 绑定 VAO（包含 VBO + EBO + 属性布局）
        m_GL.BindVertexArray(m_VAO);

        // 4. 单次 Draw Call！
        m_GL.DrawElements(GL_TRIANGLES,
                          static_cast<GLsizei>(indexCount),
                          GL_UNSIGNED_INT,
                          nullptr);

        m_GL.BindVertexArray(0);
    }

}
