#pragma once
#include <memory>
#include <vector>
#include <cstdint>
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

		// ── 截屏（崩溃预捕获） ──
		/**
		 * @brief 捕获当前帧缓冲区像素
		 * @param[out] outWidth  帧宽度
		 * @param[out] outHeight 帧高度
		 * @param[out] outPixels RGB888 像素数据
		 * @return 是否成功
		 */
		virtual bool CaptureFrameBuffer(int32& outWidth, int32& outHeight,
		                                std::vector<uint8_t>& outPixels) { return false; }
	};
}