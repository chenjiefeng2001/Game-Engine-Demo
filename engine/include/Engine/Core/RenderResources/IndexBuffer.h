#pragma once

#include "Engine/Types.h"

namespace Engine {

	class IndexBuffer
	{
	public:
		virtual ~IndexBuffer() = default;

		virtual void Bind() const = 0;
		virtual void Unbind() const = 0;
		virtual uint32 GetCount() const = 0;
	};

} // namespace Engine
