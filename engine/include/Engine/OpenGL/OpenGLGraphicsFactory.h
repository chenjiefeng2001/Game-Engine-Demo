#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Core/RHI/IRHIVertexBuffer.h"
#include "Engine/Core/RHI/IRHIIndexBuffer.h"
#include "Engine/Core/RHI/IRHIVertexArray.h"
#include <glad/gl.h>
#include <atomic>
#include <string>
#include <thread>

struct GLFWwindow;

namespace Engine {

	class OpenGLGraphicsFactory : public IGraphicsFactory
	{
	public:
		explicit OpenGLGraphicsFactory(bool persistentMode = false);
		virtual ~OpenGLGraphicsFactory() override;

		bool InitializePersistentHiddenContext(int width, int height);
		bool ShutdownPersistentContext();
		bool MakePersistentContextCurrent();
		bool UncurrentPersistentContext();
		bool IsReady() const { return m_GlfwInitialized && m_GladReady; }
		bool IsPersistentContextReady() const { return m_PersistentContextReady; }
		bool IsPersistentContextCurrent() const;
		GLFWwindow* GetPersistentWindow() const { return m_PersistentWindow; }
		uint64_t GetMakeCurrentCount() const { return m_MakeCurrentCount.load(); }
		uint64_t GetUncurrentCount() const { return m_UncurrentCount.load(); }
		const std::string& GetLastError() const { return m_LastError; }

		// ---- COntext with Window ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width,
			int height,
			const std::string& title) override;

		virtual std::unique_ptr<IRenderContext> CreateRenderContext(
			void* nativeWindowHandle) override;

		// ---- GPU Resource ----
		virtual std::shared_ptr<Shader> CreateShader(
			const std::string& vertexPath,
			const std::string& fragmentPath) override;

		virtual std::shared_ptr<Shader> CreateShaderFromStages(
			const std::vector<ShaderStage>& stages) override;

		virtual std::shared_ptr<Texture> CreateTexture(
			const std::string& path) override;

		virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(
			float* vertices,
			uint32_t size) override;

		virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
			uint32_t* indices,
			uint32_t count) override;

		virtual std::shared_ptr<VertexArray> CreateVertexArray() override;

		// ---- RHI 抽象资源（全新接口） ----
		virtual std::shared_ptr<RHI::IRHIVertexBuffer> CreateVertexBuffer_RHI(
			const void* data, size_t size, uint32 stride) override;
		virtual std::shared_ptr<RHI::IRHIIndexBuffer> CreateIndexBuffer_RHI(
			const uint32* data, uint32 count) override;
		virtual std::shared_ptr<RHI::IRHIVertexArray> CreateVertexArray_RHI() override;

		// ---- Advanced Render Toolbox----
		virtual std::shared_ptr<ISpriteBatch> CreateSpriteBatch(
			IRenderContext& renderContext) override;

		// ---- UI Manager ----
		virtual std::unique_ptr<IUIManager> CreateUIManager() override;

		// ---- 图元批处理 ----
		virtual std::unique_ptr<IPrimitiveBatch> CreatePrimitiveBatch(
			uint32 capacity = 16384) override;

		// ---- 延迟渲染 ----
		virtual std::unique_ptr<GBuffer> CreateGBuffer(
			IRenderContext& context) override;


		// ---- 获取内部 OpenGL 上下文（仅限 OpenGL 实现内部使用） ----
		GladGLContext& GetGLContext() { return m_GL; }

		// ---- FSAA / 多重采样 ----
		/** 设置多重采样样本数（默认 0 = 关闭，设为 4 或 8 启用 FSAA） */
		void SetMultisampleSamples(int32 samples) override { m_SampleCount = samples; }
		int32 GetMultisampleSamples() const override { return m_SampleCount; }

		// ---- 抗锯齿配置 ----
		/**
		 * @brief 设置抗锯齿模式与配置
		 *
		 * 支持 MSAA / SSAA / CSAA / MLAA 等多种抗锯齿技术。
		 * 可在运行时切换。注意：CreateWindow 必须先于 SetAntiAliasingConfig 调用，
		 * 因为 AA 管理器存储在 IRenderContext 中。
		 */
		void SetAntiAliasingConfig(const AntiAliasingConfig& config) override;
		AntiAliasingConfig GetAntiAliasingConfig() const override;
		AntiAliasingCaps GetAntiAliasingCaps() const override;

	private:
		GladGLContext m_GL;
		GLFWwindow* m_PersistentWindow = nullptr;
		std::atomic<bool> m_PersistentContextReady{false};
		std::atomic<bool> m_PersistentContextCurrent{false};
		bool m_GlfwInitialized = false;
		bool m_GladReady = false;
		bool m_PersistentLifetime = false;
		std::thread::id m_OwnerThreadId;
		std::string m_LastError;
		std::atomic<uint64_t> m_MakeCurrentCount{0};
		std::atomic<uint64_t> m_UncurrentCount{0};
		std::atomic<int32> m_LiveWindowCount{0};
		int32 m_SampleCount = 4;  // 默认启用 4x MSAA
		/** 存储窗口指针，用于后续 AA 配置传递 */
		mutable IRenderContext* m_RenderContext = nullptr;
		// CreateWindow 设置 m_RenderContext — OpenGLRenderContext 是嵌套类，内部可访问父类私有成员
		// 移除了 self-friend 声明以避免 GCC 警告
	};

} // namespace Engine
