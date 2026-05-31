#pragma once

#include <memory>
#include "Engine/Types.h"

namespace Engine {

	class VertexBuffer;
	class IndexBuffer;

	/// 顶点属性描述
	struct VertexAttribute {
		uint32 location;   // layout(location = N)
		int32  size;       // 1~4
		uint32 stride;     // 字节跨度
		uint32 offset;     // 字节偏移
	};

	class VertexArray
	{
	public:
		virtual ~VertexArray() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;

		/** 2D 版本（5 floats: pos3 + tex2），保持向后兼容 */
		virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer) = 0;

		/** 3D 版本：允许自定义顶点属性布局 */
		virtual void AddVertexBuffer(const std::shared_ptr<VertexBuffer>& vertexBuffer,
		                             const VertexAttribute* attributes,
		                             uint32 attributeCount) = 0;

		virtual void SetIndexBuffer(const std::shared_ptr<IndexBuffer>& indexBuffer) = 0;
		virtual const std::shared_ptr<IndexBuffer>& GetIndexBuffer() const = 0;
	};

}
