#pragma once

#include <memory>
#include <stdint.h>

namespace Engine {

	class VertexArray;

	class IRenderContext
	{
	public:
		virtual ~IRenderContext() = default;

		virtual void Init() = 0;
		virtual void SwapBuffers() = 0;
		virtual void ClearColor(float r, float g, float b, float a) = 0;

		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) = 0;
	};

}
