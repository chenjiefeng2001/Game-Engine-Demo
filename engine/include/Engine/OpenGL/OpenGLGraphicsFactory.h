#pragma once

#include "Engine/Core/IGraphicsFactory.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLGraphicsFactory : public IGraphicsFactory
	{
	public:
		OpenGLGraphicsFactory();
		virtual ~OpenGLGraphicsFactory() override;

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

		// ---- Advanced Render Toolbox----
		virtual std::shared_ptr<ISpriteBatch> CreateSpriteBatch(
			IRenderContext& renderContext) override;

		// ---- UI Manager ----
		virtual std::unique_ptr<IUIManager> CreateUIManager() override;

		// ---- 图元批处理 ----
		virtual std::unique_ptr<IPrimitiveBatch> CreatePrimitiveBatch(
			uint32 capacity = 16384) override;


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
		int32 m_SampleCount = 4;  // 默认启用 4x MSAA
		/** 存储窗口指针，用于后续 AA 配置传递 */
		mutable IRenderContext* m_RenderContext = nullptr;
		friend class OpenGLGraphicsFactory;  // CreateWindow 设置 m_RenderContext
	};

} // namespace Engine
