#pragma once

#include "Engine/Types.h"
#include <cstddef>

namespace Engine { namespace RHI {

    /**
     * @brief RHI 索引缓冲区接口 — 完全与具体图形 API 解耦
     *
     * 负责在 GPU 上存储索引数据。
     * 通过 IGraphicsFactory::CreateIndexBuffer_RHI() 创建。
     */
    class IRHIIndexBuffer {
    public:
        virtual ~IRHIIndexBuffer() = default;

        /** 绑定索引缓冲区到渲染管线 */
        virtual void Bind() const = 0;

        /** 解绑索引缓冲区 */
        virtual void Unbind() const = 0;

        /** 获取索引数量 */
        virtual uint32 GetCount() const = 0;

        /** 获取缓冲区大小（字节） */
        virtual size_t GetSize() const = 0;

        /**
         * @brief 更新缓冲区数据（用于动态索引）
         * @param data    源数据指针
         * @param size    数据大小（字节）
         * @param offset  偏移量（字节）
         */
        virtual void UpdateData(const void* data, size_t size, size_t offset = 0) = 0;
    };

}} // namespace Engine::RHI