#pragma once

#include "Engine/Core/RHI/IRHIVertexBuffer.h"
#include "Engine/Types.h"
#include <glad/gl.h>
#include <cstddef>

namespace Engine { namespace RHI {

    /**
     * @brief OpenGL RHI 顶点缓冲区实现
     *
     * 包装 GL_ARRAY_BUFFER，提供 IRHIVertexBuffer 接口。
     */
    class OpenGLVertexBuffer_RHI : public IRHIVertexBuffer {
    public:
        OpenGLVertexBuffer_RHI(const void* data, size_t size, uint32 stride, GladGLContext& gl);
        virtual ~OpenGLVertexBuffer_RHI() override;

        // IRHIVertexBuffer 接口
        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual size_t GetSize() const override { return m_Size; }
        virtual uint32 GetVertexCount() const override { return m_VertexCount; }
        virtual void UpdateData(const void* data, size_t size, size_t offset = 0) override;

        /** 获取 OpenGL 内部 ID（供 VAO 绑定使用） */
        GLuint GetRendererID() const { return m_RendererID; }

        /** 获取单个顶点步长 */
        uint32 GetStride() const { return m_Stride; }

    private:
        GladGLContext& m_GL;
        GLuint m_RendererID = 0;
        size_t m_Size = 0;
        uint32 m_VertexCount = 0;
        uint32 m_Stride = 0;
    };

}} // namespace Engine::RHI