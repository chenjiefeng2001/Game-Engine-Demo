#pragma once

#include "Engine/Core/RHI/IRHIVertexArray.h"
#include "Engine/Types.h"
#include <glad/gl.h>
#include <vector>
#include <memory>

namespace Engine { namespace RHI {

    class IRHIVertexBuffer;
    class IRHIIndexBuffer;

    /**
     * @brief OpenGL RHI 顶点数组实现
     *
     * 包装 GL VAO，设置顶点属性指针。
     * 与 OpenGLVertexBuffer_RHI 和 OpenGLIndexBuffer_RHI 配合使用。
     */
    class OpenGLVertexArray_RHI : public IRHIVertexArray {
    public:
        OpenGLVertexArray_RHI(GladGLContext& gl);
        virtual ~OpenGLVertexArray_RHI() override;

        // IRHIVertexArray 接口
        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual void AddVertexBuffer(const std::shared_ptr<IRHIVertexBuffer>& vertexBuffer,
                                     const RHIVertexAttribute* attributes,
                                     uint32 attributeCount) override;
        virtual void SetIndexBuffer(const std::shared_ptr<IRHIIndexBuffer>& indexBuffer) override;
        virtual const std::shared_ptr<IRHIIndexBuffer>& GetIndexBuffer() const override { return m_IndexBuffer; }
        virtual uint32 GetIndexCount() const override;

    private:
        GladGLContext& m_GL;
        GLuint m_RendererID = 0;
        std::vector<std::shared_ptr<IRHIVertexBuffer>> m_VertexBuffers;
        std::shared_ptr<IRHIIndexBuffer> m_IndexBuffer;
    };

}} // namespace Engine::RHI