#pragma once
#include <memory>
#include "Engine/Types.h"

namespace Engine {
	class VertexArray;

	class IRenderContext {
	public:
		virtual ~IRenderContext() = default;

		virtual void Init() = 0;
		virtual void ClearColor(float r, float g, float b, float a) = 0;
		virtual void SwapBuffers() = 0;
		virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) = 0;

		virtual void OnResize(int32 width, int32 height) = 0;

		// ── 统计 ──
		/** 获取并重置本帧的 DrawCall 次数 */
		virtual uint32 GetAndResetDrawCallCount() { return 0; }
	};
}