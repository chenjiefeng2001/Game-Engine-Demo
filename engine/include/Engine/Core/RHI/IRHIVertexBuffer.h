#pragma once

#include "Engine/Types.h"
#include <cstddef>

namespace Engine { namespace RHI {

    /**
     * @brief RHI 顶点缓冲区接口 — 完全与具体图形 API 解耦
     *
     * 负责在 GPU 上存储顶点数据。
     * 通过 IGraphicsFactory::CreateVertexBuffer_RHI() 创建。
     */
    class IRHIVertexBuffer {
    public:
        virtual ~IRHIVertexBuffer() = default;

        /** 绑定顶点缓冲区到渲染管线 */
        virtual void Bind() const = 0;

        /** 解绑顶点缓冲区 */
        virtual void Unbind() const = 0;

        /** 获取缓冲区大小（字节） */
        virtual size_t GetSize() const = 0;

        /** 获取缓冲区中顶点数量 */
        virtual uint32 GetVertexCount() const = 0;

        /**
         * @brief 更新缓冲区数据（用于动态网格）
         * @param data    源数据指针
         * @param size    数据大小（字节）
         * @param offset  偏移量（字节）
         */
        virtual void UpdateData(const void* data, size_t size, size_t offset = 0) = 0;
    };

}} // namespace Engine::RHI