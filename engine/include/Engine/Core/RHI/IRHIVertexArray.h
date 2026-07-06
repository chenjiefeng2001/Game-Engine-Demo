#pragma once

#include "Engine/Types.h"
#include <memory>

namespace Engine { namespace RHI {

    class IRHIVertexBuffer;
    class IRHIIndexBuffer;

    /// 顶点属性描述（RHI 版本）
    struct RHIVertexAttribute {
        uint32 location;   // layout(location = N)
        int32  size;       // 1~4 个分量
        uint32 stride;     // 字节跨度
        uint32 offset;     // 字节偏移
    };

    /**
     * @brief RHI 顶点数组接口 — 完全与具体图形 API 解耦
     *
     * 封装了顶点缓冲区、索引缓冲区和顶点属性布局的组合。
     * 通过 IGraphicsFactory::CreateVertexArray_RHI() 创建。
     */
    class IRHIVertexArray {
    public:
        virtual ~IRHIVertexArray() = default;

        /** 绑定 VAO 到渲染管线 */
        virtual void Bind() const = 0;

        /** 解绑 VAO */
        virtual void Unbind() const = 0;

        /** 添加顶点缓冲区（带自定义属性布局） */
        virtual void AddVertexBuffer(const std::shared_ptr<IRHIVertexBuffer>& vertexBuffer,
                                     const RHIVertexAttribute* attributes,
                                     uint32 attributeCount) = 0;

        /** 设置索引缓冲区 */
        virtual void SetIndexBuffer(const std::shared_ptr<IRHIIndexBuffer>& indexBuffer) = 0;

        /** 获取索引缓冲区 */
        virtual const std::shared_ptr<IRHIIndexBuffer>& GetIndexBuffer() const = 0;

        /** 获取索引数量 */
        virtual uint32 GetIndexCount() const = 0;
    };

}} // namespace Engine::RHI