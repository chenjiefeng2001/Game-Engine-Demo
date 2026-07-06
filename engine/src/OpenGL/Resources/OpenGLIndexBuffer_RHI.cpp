#include "OpenGLIndexBuffer_RHI.h"

namespace Engine { namespace RHI {

    OpenGLIndexBuffer_RHI::OpenGLIndexBuffer_RHI(const uint32* data, uint32 count, GladGLContext& gl)
        : m_GL(gl)
        , m_Count(count)
        , m_Size(static_cast<size_t>(count) * sizeof(uint32))
    {
        m_GL.GenBuffers(1, &m_RendererID);
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_Size), data, GL_STATIC_DRAW);
    }

    OpenGLIndexBuffer_RHI::~OpenGLIndexBuffer_RHI() {
        if (m_RendererID) {
            m_GL.DeleteBuffers(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    void OpenGLIndexBuffer_RHI::Bind() const {
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
    }

    void OpenGLIndexBuffer_RHI::Unbind() const {
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    }

    void OpenGLIndexBuffer_RHI::UpdateData(const void* data, size_t size, size_t offset) {
        m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_RendererID);
        if (offset == 0 && size == m_Size) {
            m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
        } else {
            m_GL.BufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset, size, data);
        }
    }

}} // namespace Engine::RHI