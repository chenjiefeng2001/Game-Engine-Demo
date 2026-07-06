#include "OpenGLVertexBuffer_RHI.h"
#include <cstring>

namespace Engine { namespace RHI {

    OpenGLVertexBuffer_RHI::OpenGLVertexBuffer_RHI(const void* data, size_t size, uint32 stride, GladGLContext& gl)
        : m_GL(gl)
        , m_Size(size)
        , m_Stride(stride)
    {
        m_VertexCount = (stride > 0) ? static_cast<uint32>(size / stride) : 0;

        m_GL.GenBuffers(1, &m_RendererID);
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        m_GL.BufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    }

    OpenGLVertexBuffer_RHI::~OpenGLVertexBuffer_RHI() {
        if (m_RendererID) {
            m_GL.DeleteBuffers(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void OpenGLVertexBuffer_RHI::Bind() const {
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLVertexBuffer_RHI::Unbind() const {
        m_GL.BindBuffer(GL_ARRAY_BUFFER, 0);
    }

    void OpenGLVertexBuffer_RHI::UpdateData(const void* data, size_t size, size_t offset) {
        m_GL.BindBuffer(GL_ARRAY_BUFFER, m_RendererID);
        if (offset == 0 && size == m_Size) {
            m_GL.BufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
        } else {
            m_GL.BufferSubData(GL_ARRAY_BUFFER, offset, size, data);
        }
    }

}} // namespace Engine::RHI