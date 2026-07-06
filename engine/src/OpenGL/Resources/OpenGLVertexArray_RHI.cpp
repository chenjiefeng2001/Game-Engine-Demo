#include "OpenGLVertexArray_RHI.h"
#include "OpenGLVertexBuffer_RHI.h"
#include "OpenGLIndexBuffer_RHI.h"

namespace Engine { namespace RHI {

    OpenGLVertexArray_RHI::OpenGLVertexArray_RHI(GladGLContext& gl)
        : m_GL(gl) {
        m_GL.GenVertexArrays(1, &m_RendererID);
    }

    OpenGLVertexArray_RHI::~OpenGLVertexArray_RHI() {
        if (m_RendererID) {
            m_GL.DeleteVertexArrays(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void OpenGLVertexArray_RHI::Bind() const {
        m_GL.BindVertexArray(m_RendererID);
    }

    void OpenGLVertexArray_RHI::Unbind() const {
        m_GL.BindVertexArray(0);
    }

    void OpenGLVertexArray_RHI::AddVertexBuffer(
        const std::shared_ptr<IRHIVertexBuffer>& vertexBuffer,
        const RHIVertexAttribute* attributes,
        uint32 attributeCount)
    {
        // 通过 dynamic_cast 获取 OpenGL 具体实现
        auto* glVB = dynamic_cast<OpenGLVertexBuffer_RHI*>(vertexBuffer.get());
        if (!glVB) return;

        m_GL.BindVertexArray(m_RendererID);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, glVB->GetRendererID());

        for (uint32 i = 0; i < attributeCount; ++i) {
            const auto& attr = attributes[i];
            m_GL.EnableVertexAttribArray(attr.location);
            m_GL.VertexAttribPointer(attr.location, attr.size, GL_FLOAT, GL_FALSE,
                                     static_cast<GLsizei>(attr.stride),
                                     reinterpret_cast<void*>(static_cast<uintptr_t>(attr.offset)));
        }

        m_VertexBuffers.push_back(vertexBuffer);
    }

    void OpenGLVertexArray_RHI::SetIndexBuffer(const std::shared_ptr<IRHIIndexBuffer>& indexBuffer) {
        auto* glIB = dynamic_cast<OpenGLIndexBuffer_RHI*>(indexBuffer.get());
        if (!glIB) return;

        m_GL.BindVertexArray(m_RendererID);
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, glIB->GetRendererID());

        m_IndexBuffer = indexBuffer;
    }

    uint32 OpenGLVertexArray_RHI::GetIndexCount() const {
        if (m_IndexBuffer) {
            return m_IndexBuffer->GetCount();
        }
        return 0;
    }

}} // namespace Engine::RHI