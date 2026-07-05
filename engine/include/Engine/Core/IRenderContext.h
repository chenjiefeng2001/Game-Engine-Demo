#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include "Engine/Types.h"
#include "Engine/Core/ViewMode.h"
#include "Engine/Core/BufferVisualization.h"
#include "Engine/Core/LightingDebug.h"
#include "Engine/Core/GeometryDebug.h"
#include "Engine/Core/PostProcessingDebug.h"
#include "Engine/Core/TextureDebug.h"
#include "Engine/Core/HelperToggles.h"

namespace Engine {
	class VertexArray;

	/// 单帧 GPU Pass 时间戳记录
	struct GPUTimestampRecord {
		std::string passName;
		uint64      beginTimestamp = 0; // 纳秒
		uint64      endTimestamp   = 0; // 纳秒
		float       elapsedMs      = 0.0f;
	};

	/// GPU 时间戳查询结果（一帧内所有 Pass）
	struct GPUProfileFrame {
		static constexpr uint32 kMaxPasses = 32;
		GPUTimestampRecord passes[kMaxPasses];
		uint32             passCount = 0;
		/// 获取特定 Pass 的耗时 (ms)
		float GetPassMs(const std::string& name) const {
			for (uint32 i = 0; i < passCount; ++i)
				if (passes[i].passName == name) return passes[i].elapsedMs;
			return 0.0f;
		}
		/// 总 GPU 耗时 (ms)
		float GetTotalMs() const {
			float total = 0.0f;
			for (uint32 i = 0; i < passCount; ++i)
				total += passes[i].elapsedMs;
			return total;
		}
	};

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

		/** 获取并重置本帧的顶点计数 */
		virtual uint32 GetAndResetVertexCount() { return 0; }

		/** 获取并重置本帧的三角形/索引计数 */
		virtual uint32 GetAndResetTriangleCount() { return 0; }

		// ── GPU 时间戳查询 ──
		/**
		 * @brief 开始一个 GPU Pass 时间戳查询
		 * @param passName Pass 名称标识
		 * @param queryIndex 返回分配的查询索引 (0 ~ kMaxPasses-1)，-1 表示失败
		 */
		virtual int32 BeginGPUPass(const std::string& passName) { (void)passName; return -1; }

		/** 结束一个 GPU Pass 时间戳查询 */
		virtual void EndGPUPass(int32 queryIndex) { (void)queryIndex; }

		/**
		 * @brief 获取本帧 GPU Profiler 结果
		 * @param[out] outFrame 填充结果
		 * @return 是否有有效数据
		 */
		virtual bool GetGPUProfileFrame(GPUProfileFrame& outFrame) {
			(void)outFrame;
			return false;
		}

		// ── 渲染模式切换 ──
		/** 设置当前渲染调试模式 */
		virtual void SetViewMode(ViewMode mode) { m_ViewMode = mode; }
		/** 获取当前渲染调试模式 */
		virtual ViewMode GetViewMode() const { return m_ViewMode; }
		/** 将当前 view mode 绑定为 shader uniform（在所有渲染前调用） */
		virtual void BindViewModeUniform(const class Shader* shader) { (void)shader; }

		// ── 缓冲区可视化 ──
		/** 获取当前帧的缓冲区可视化数据（纹理句柄等） */
		virtual bool GetBufferVisData(BufferVisFrameData& outData) {
			(void)outData;
			return false;
		}

		/** 读取深度缓冲区像素（线性化后的 float 深度值，用于 UI 显示） */
		virtual bool ReadDepthBuffer(std::vector<float>& outDepth, int32& outW, int32& outH) {
			(void)outDepth; (void)outW; (void)outH;
			return false;
		}

		// ── 光照与阴影调试 ──
		/** 获取当前光照调试数据（包含开关状态和光源列表） */
		virtual LightingDebugFrameData* GetLightingDebugData() { return nullptr; }

		// ── 几何体与空间管理调试 ──
		/** 获取当前几何体与空间管理调试数据 */
		virtual GeometryDebugFrameData* GetGeometryDebugData() { return nullptr; }

		// ── 后期处理调试 ──
		/** 获取当前后期处理调试数据（包含所有后期开关和参数） */
		virtual PostProcessingDebugFrameData* GetPostProcessingDebugData() { return nullptr; }

		// ── 纹理与资源调试 ──
		/** 获取当前纹理调试数据 */
		virtual TextureDebugFrameData* GetTextureDebugData() { return nullptr; }

		// ── 常用调试开关 ──
		/** 获取常用调试开关数据 */
		virtual HelperTogglesFrameData* GetHelperTogglesData() { return nullptr; }

		// ── GPU 显存统计（可选重写） ──
		/** 获取已创建的纹理总显存 (bytes) */
		virtual uint64 GetTextureVRAMBytes() const { return 0; }
		/** 获取已创建的缓冲总显存 (bytes) */
		virtual uint64 GetBufferVRAMBytes() const { return 0; }

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

		// ── 深度预渲染状态控制（用于 Early-Z 优化） ──
		/** 设置深度写入掩码 */
		virtual void SetDepthMask(bool enable) { (void)enable; }
		/** 设置颜色通道写入掩码 */
		virtual void SetColorMask(bool r, bool g, bool b, bool a) { (void)r; (void)g; (void)b; (void)a; }
		/** 设置深度比较函数 */
		virtual void SetDepthFunc(uint32 func) { (void)func; }

		// ── 管道状态重置（用于 UI Pass 前，防止 3D 深度/混合状态污染 ImGui） ──
		/** 重置所有渲染状态到默认值 */
		virtual void ResetPipelineState() {}

	protected:
		ViewMode m_ViewMode = ViewMode::Normal;
	};
}
