#pragma once

#include "Engine/Core/IRenderContext.h"
#include "Engine/OpenGL/OpenGLAntiAliasing.h"
#include <memory>
#include <string>
#include <array>

struct GLFWwindow;
struct GladGLContext;

namespace Engine {

	/**
	 * @brief OpenGL 渲染上下文实现 — 支持 DrawCall/几何统计和 GPU 时间戳查询
	 *
	 * 新增功能：
	 *   - GetAndResetVertexCount() / GetAndResetTriangleCount()：几何体统计
	 *   - BeginGPUPass() / EndGPUPass() / GetGPUProfileFrame()：GPU 瀑布图
	 *   - GetTextureVRAMBytes() / GetBufferVRAMBytes()：显存占用估算
	 */
	class OpenGLContext final : public IRenderContext {
	public:
		OpenGLContext(GLFWwindow* windowHandle, GladGLContext& sharedGL);
		~OpenGLContext() override;

		// ── IRenderContext 实现 ──
		void Init() override;
		void ClearColor(float r, float g, float b, float a) override;
		void SwapBuffers() override;
		void DrawIndexed(const std::shared_ptr<VertexArray>& va) override;
		void OnResize(int32 width, int32 height) override;

		// ── 统计 ──
		uint32 GetAndResetDrawCallCount() override {
			uint32 count = m_DrawCallCount;
			m_DrawCallCount = 0;
			return count;
		}

		uint32 GetAndResetVertexCount() override {
			uint32 count = m_VertexCount;
			m_VertexCount = 0;
			return count;
		}

		uint32 GetAndResetTriangleCount() override {
			uint32 count = m_TriangleCount;
			m_TriangleCount = 0;
			return count;
		}

		// ── 渲染模式切换 ──
		void SetViewMode(ViewMode mode) override;
		void BindViewModeUniform(const Shader* shader) override;

		// ── GPU 时间戳查询 ──
		int32  BeginGPUPass(const std::string& passName) override;
		void   EndGPUPass(int32 queryIndex) override;
		bool   GetGPUProfileFrame(GPUProfileFrame& outFrame) override;

		// ── 缓冲区可视化 ──
		bool ReadDepthBuffer(std::vector<float>& outDepth, int32& outW, int32& outH) override;

		// ── 光照与阴影调试 ──
		LightingDebugFrameData* GetLightingDebugData() override { return &m_LightingDebug; }

		// ── 几何体与空间管理调试 ──
		GeometryDebugFrameData* GetGeometryDebugData() override { return &m_GeometryDebug; }

		// ── 后期处理调试 ──
		PostProcessingDebugFrameData* GetPostProcessingDebugData() override { return &m_PostProcessingDebug; }

		// ── 纹理与资源调试 ──
		TextureDebugFrameData* GetTextureDebugData() override { return &m_TextureDebug; }

		// ── 常用调试开关 ──
		HelperTogglesFrameData* GetHelperTogglesData() override { return &m_HelperToggles; }

		// ── GPU 显存统计 ──
		uint64 GetTextureVRAMBytes() const override { return m_TextureVRAMBytes; }
		uint64 GetBufferVRAMBytes()  const override { return m_BufferVRAMBytes; }

		// ── 显存上报（供 Texture/Buffer 创建时调用） ──
		void AddTextureVRAM(uint64 bytes) { m_TextureVRAMBytes += bytes; }
		void RemoveTextureVRAM(uint64 bytes) { m_TextureVRAMBytes -= bytes; }
		void AddBufferVRAM(uint64 bytes) { m_BufferVRAMBytes += bytes; }
		void RemoveBufferVRAM(uint64 bytes) { m_BufferVRAMBytes -= bytes; }

		// ── 截屏 ──
		bool CaptureFrameBuffer(int32& outWidth, int32& outHeight,
		                        std::vector<uint8_t>& outPixels) override;

		// ── 深度状态 ──
		void SetDepthMask(bool enable) override;
		void SetColorMask(bool r, bool g, bool b, bool a) override;
		void SetDepthFunc(uint32 func) override;

		// ── 抗锯齿 ──
		void SetAntiAliasingConfig(const AntiAliasingConfig& config);
		const AntiAliasingConfig& GetAntiAliasingConfig() const;
		OpenGLAntiAliasing* GetAntiAliasing() const { return m_AntiAliasing.get(); }
		void ResolveToDefault();

		// ── 访问器 ──
		GladGLContext& GetGL() { return m_GL; }
		void RecordTriangleCount(uint32 count) { m_TriangleCount += count; }
		void RecordVertexCount(uint32 count)   { m_VertexCount += count; }

	private:
		// ── GPU 时间戳查询（GL 实现） ──
		static constexpr uint32 kMaxGPUQueries = GPUProfileFrame::kMaxPasses * 2;

		struct GPUQuery {
			std::string passName;
			uint32      queryIndex[2] = {}; // begin / end
			bool        active = false;
		};

		bool InitGPUQueries();
		void CleanupGPUQueries();

		GLFWwindow*      m_WindowHandle = nullptr;
		GladGLContext&   m_GL;

		// ── 统计计数器 ──
		uint32 m_DrawCallCount  = 0;
		uint32 m_VertexCount    = 0;
		uint32 m_TriangleCount  = 0;

		// ── 显存占用估算 ──
		uint64 m_TextureVRAMBytes = 0;
		uint64 m_BufferVRAMBytes  = 0;

		// ── GPU 时间戳查询 ──
		uint32  m_GPUQueryObjects[kMaxGPUQueries] = {};
		uint32  m_GPUQueryCount = 0;
		bool    m_GPUQueriesReady = false;

		GPUQuery m_ActiveQueries[GPUProfileFrame::kMaxPasses];
		uint32   m_ActiveQueryCount = 0;

		GPUProfileFrame m_LastGPUProfile; // 上一帧的完整结果

		// ── 光照与阴影调试数据 ──
		LightingDebugFrameData m_LightingDebug;

		// ── 几何体与空间管理调试数据 ──
		GeometryDebugFrameData m_GeometryDebug;

		// ── 后期处理调试数据 ──
		PostProcessingDebugFrameData m_PostProcessingDebug;

		// ── 纹理与资源调试数据 ──
		TextureDebugFrameData m_TextureDebug;

		// ── 常用调试开关数据 ──
		HelperTogglesFrameData m_HelperToggles;

		// ── 抗锯齿 ──
		std::unique_ptr<OpenGLAntiAliasing> m_AntiAliasing;
		bool m_AntiAliasingInitialized = false;
	};

} // namespace Engine