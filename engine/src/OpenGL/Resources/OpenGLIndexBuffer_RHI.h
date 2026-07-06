#pragma once

#include "Engine/Core/RHI/IRHIIndexBuffer.h"
#include "Engine/Types.h"
#include <glad/gl.h>
#include <cstddef>

namespace Engine { namespace RHI {

    /**
     * @brief OpenGL RHI 索引缓冲区实现
     *
     * 包装 GL_ELEMENT_ARRAY_BUFFER，提供 IRHIIndexBuffer 接口。
     */
    class OpenGLIndexBuffer_RHI : public IRHIIndexBuffer {
    public:
        OpenGLIndexBuffer_RHI(const uint32* data, uint32 count, GladGLContext& gl);
        virtual ~OpenGLIndexBuffer_RHI() override;

        // IRHIIndexBuffer 接口
        virtual void Bind() const override;
        virtual void Unbind() const override;
        virtual uint32 GetCount() const override { return m_Count; }
        virtual size_t GetSize() const override { return m_Size; }
        virtual void UpdateData(const void* data, size_t size, size_t offset = 0) override;

        /** 获取 OpenGL 内部 ID（供 VAO 绑定使用） */
        GLuint GetRendererID() const { return m_RendererID; }

    private:
        GladGLContext& m_GL;
        GLuint m_RendererID = 0;
        uint32 m_Count = 0;
        size_t m_Size = 0;
    };

}} // namespace Engine::RHI