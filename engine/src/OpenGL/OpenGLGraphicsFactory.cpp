#include "Engine/OpenGL/OpenGLGraphicsFactory.h"
#include "Engine/OpenGL/OpenGLAntiAliasing.h"

#include "Engine/Platform/GlfwWindow.h"
#include "Engine/Platform/GlfwLifetime.h"
#include "Engine/OpenGL/OpenGLContext.h"
#include "Engine/Core/Memory/StackAllocatorAdaptor.h"

#include "Resources/OpenGLShader.h"
#include "Resources/OpenGLTexture.h"
#include "Resources/OpenGLVertexBuffer.h"
#include "Resources/OpenGLIndexBuffer.h"
#include "Resources/OpenGLVertexArray.h"
#include "Resources/OpenGLVertexBuffer_RHI.h"
#include "Resources/OpenGLIndexBuffer_RHI.h"
#include "Resources/OpenGLVertexArray_RHI.h"
#include "Resources/OpenGLSpriteBatch.h"
#include "Resources/OpenGLPrimitiveBatch.h"
#include "Resources/ImGuiUIManager.h"
#include "OpenGLGBuffer.h"

#include "Engine/Core/Log.h"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>

namespace {
    Engine::Logger s_Log("OpenGLGraphicsFactory");

    bool TestFailure(const char* name) {
        const char* value = std::getenv(name);
        return value && std::strcmp(value, "1") == 0;
    }
}

namespace Engine {

	OpenGLGraphicsFactory::OpenGLGraphicsFactory(bool persistentMode)
		: m_PersistentLifetime(persistentMode)
		, m_OwnerThreadId(std::this_thread::get_id())
	{
		if (TestFailure("EDITOR_BRIDGE_FAIL_GLFW_INIT"))
		{
			m_LastError = "injected GLFW initialization failure";
			return;
		}
		if (!AcquireGlfwLifetime(m_PersistentLifetime))
		{
			m_LastError = "failed to initialize GLFW";
			s_Log.Error("{}", m_LastError);
			return;
		}
		m_GlfwInitialized = true;

		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		GLFWwindow* tempWindow = glfwCreateWindow(1, 1, "GLAD Loader", nullptr, nullptr);
		if (tempWindow)
		{
			glfwMakeContextCurrent(tempWindow);
			int version = gladLoadGLContext(&m_GL, glfwGetProcAddress);
			if (version == 0)
			{
				m_LastError = "failed to initialize GLAD";
				s_Log.Error("{}", m_LastError);
			}
			else
			{
				m_GladReady = true;
				s_Log.Info("OpenGL {}.{} loaded", GLAD_VERSION_MAJOR(version), GLAD_VERSION_MINOR(version));
			}
			glfwMakeContextCurrent(nullptr);
			glfwDestroyWindow(tempWindow);
		}
		else
		{
			m_LastError = "failed to create temporary GLAD loader window";
			s_Log.Error("{}", m_LastError);
		}
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
	}

	OpenGLGraphicsFactory::~OpenGLGraphicsFactory()
	{
		if (std::this_thread::get_id() != m_OwnerThreadId) {
			s_Log.Error("graphics factory destroyed on non-owner thread");
			std::terminate();
		}
		if (m_LiveWindowCount.load() != 0) {
			s_Log.Error("graphics factory destroyed before legacy windows");
			std::terminate();
		}
		const bool contextShutdown = ShutdownPersistentContext();
		m_RenderContext = nullptr;
		if (m_GlfwInitialized && contextShutdown)
		{
			ReleaseGlfwLifetime(m_PersistentLifetime);
			m_GlfwInitialized = false;
		}
		else if (m_GlfwInitialized)
		{
			s_Log.Error("{}", m_LastError);
		}
	}

	bool OpenGLGraphicsFactory::InitializePersistentHiddenContext(int width, int height)
	{
		if (std::this_thread::get_id() != m_OwnerThreadId)
		{
			m_LastError = "persistent context must be created on the owner thread";
			return false;
		}
		if (m_PersistentContextReady) return IsPersistentContextCurrent();
		if (!m_GlfwInitialized)
		{
			m_LastError = "GLFW is not initialized";
			return false;
		}
		if (width <= 0 || height <= 0)
		{
			m_LastError = "persistent context dimensions must be positive";
			return false;
		}
		if (TestFailure("EDITOR_BRIDGE_FAIL_GL_CONTEXT"))
		{
			m_LastError = "injected persistent context failure";
			return false;
		}

		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
		glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
		glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		GLFWwindow* window = glfwCreateWindow(width, height, "Editor Offscreen", nullptr, nullptr);
		if (!window)
		{
			m_LastError = "failed to create persistent hidden GLFW window";
			return false;
		}
		glfwMakeContextCurrent(window);
		if (glfwGetCurrentContext() != window)
		{
			m_LastError = "failed to make persistent context current";
			glfwDestroyWindow(window);
			return false;
		}
		const int version = TestFailure("EDITOR_BRIDGE_FAIL_GLAD")
			? 0
			: gladLoadGLContext(&m_GL, glfwGetProcAddress);
		if (version == 0)
		{
			m_LastError = "failed to initialize GLAD for persistent context";
			glfwMakeContextCurrent(nullptr);
			glfwDestroyWindow(window);
			return false;
		}
		glfwSwapInterval(0);
		m_GladReady = true;
		++m_MakeCurrentCount;
		m_PersistentWindow = window;
		m_PersistentContextReady = true;
		m_PersistentContextCurrent = true;
		m_LastError.clear();
		return true;
	}

	bool OpenGLGraphicsFactory::ShutdownPersistentContext()
	{
		if (std::this_thread::get_id() != m_OwnerThreadId)
		{
			m_LastError = "persistent context must be destroyed on the owner thread";
			return false;
		}
		if (!m_PersistentWindow) return true;
		if (TestFailure("EDITOR_BRIDGE_FAIL_GRAPHICS_TEARDOWN"))
		{
			m_LastError = "injected persistent graphics teardown failure";
			return false;
		}
		if (glfwGetCurrentContext() == m_PersistentWindow)
		{
			glfwMakeContextCurrent(nullptr);
			++m_UncurrentCount;
		}
		glfwDestroyWindow(m_PersistentWindow);
		m_PersistentWindow = nullptr;
		m_PersistentContextReady = false;
		m_PersistentContextCurrent = false;
		m_RenderContext = nullptr;
		return true;
	}

	bool OpenGLGraphicsFactory::IsPersistentContextCurrent() const
	{
		return m_PersistentContextReady.load() &&
		       m_PersistentContextCurrent.load();
	}

	bool OpenGLGraphicsFactory::MakePersistentContextCurrent()
	{
		if (TestFailure("EDITOR_BRIDGE_FAIL_BOOTSTRAP_CURRENT"))
		{
			m_LastError = "injected persistent context activation failure";
			return false;
		}
		if (!m_PersistentContextReady)
		{
			m_LastError = "persistent context is not available";
			return false;
		}
		const bool wasCurrent = glfwGetCurrentContext() == m_PersistentWindow;
		glfwMakeContextCurrent(m_PersistentWindow);
		if (glfwGetCurrentContext() != m_PersistentWindow)
		{
			m_LastError = "failed to make persistent context current";
			return false;
		}
		if (!wasCurrent) ++m_MakeCurrentCount;
		m_PersistentContextCurrent = true;
		return true;
	}

	bool OpenGLGraphicsFactory::UncurrentPersistentContext()
	{
		GLFWwindow* current = glfwGetCurrentContext();
		if (current == nullptr) {
			m_PersistentContextCurrent = false;
			return true;
		}
		if (!m_PersistentContextReady || current != m_PersistentWindow)
		{
			m_LastError = "current context is not the persistent context";
			return false;
		}
		glfwMakeContextCurrent(nullptr);
		if (glfwGetCurrentContext() != nullptr) return false;
		++m_UncurrentCount;
		m_PersistentContextCurrent = false;
		return true;
	}


	std::unique_ptr<IWindow> OpenGLGraphicsFactory::CreateWindow(
		int width, int height, const std::string& title)
	{
		if (m_PersistentLifetime || !m_GlfwInitialized || !m_GladReady)
		{
			m_LastError = "legacy window creation is unavailable for this graphics owner";
			return nullptr;
		}
		if (std::this_thread::get_id() != m_OwnerThreadId)
		{
			m_LastError = "legacy window must be created on the owner thread";
			return nullptr;
		}
		glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
		glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
		if (m_SampleCount > 0) {
			glfwWindowHint(GLFW_SAMPLES, m_SampleCount);
		} else {
			glfwWindowHint(GLFW_SAMPLES, 0);
		}

		GLFWwindow* nativeWindow = glfwCreateWindow(
			width, height, title.c_str(), nullptr, nullptr);
		if (!nativeWindow) {
			m_LastError = "failed to create legacy GLFW window";
			return nullptr;
		}

		try {
			auto context = CreateRenderContext(nativeWindow);
			if (!context)
			{
				m_LastError = "failed to create legacy render context";
				glfwDestroyWindow(nativeWindow);
				return nullptr;
			}

			glfwMakeContextCurrent(nativeWindow);
			if (glfwGetCurrentContext() != nativeWindow)
			{
				m_LastError = "failed to make legacy context current";
				context.reset();
				glfwDestroyWindow(nativeWindow);
				return nullptr;
			}
			GladGLContext& ctxGL = static_cast<OpenGLContext*>(context.get())->GetGL();
			const int version = gladLoadGLContext(&ctxGL, glfwGetProcAddress);
			if (version == 0) {
				m_LastError = "failed to initialize GLAD for legacy window";
				context.reset();
				glfwMakeContextCurrent(nullptr);
				glfwDestroyWindow(nativeWindow);
				return nullptr;
			}

			context->Init();
			IRenderContext* contextPtr = context.get();
			auto window = std::make_unique<GlfwWindow>(
				nativeWindow,
				std::move(context),
				[this, contextPtr]() {
					if (m_RenderContext == contextPtr) m_RenderContext = nullptr;
					--m_LiveWindowCount;
				});
			m_RenderContext = contextPtr;
			++m_LiveWindowCount;
			return window;
		} catch (const std::exception& exception) {
			m_LastError = exception.what();
		} catch (...) {
			m_LastError = "unknown legacy window creation exception";
		}

		if (glfwGetCurrentContext() == nativeWindow) {
			glfwMakeContextCurrent(nullptr);
		}
		glfwDestroyWindow(nativeWindow);
		return nullptr;
	}

	// ---- 渲染上下文 ----
	std::unique_ptr<IRenderContext> OpenGLGraphicsFactory::CreateRenderContext(
		void* nativeWindowHandle)
	{
		auto* window = static_cast<GLFWwindow*>(nativeWindowHandle);

		return std::make_unique<OpenGLContext>(window, m_GL);
	}
	// ---- GPU 资源 ----
	std::shared_ptr<Shader> OpenGLGraphicsFactory::CreateShader(
		const std::string& vertexPath,
		const std::string& fragmentPath)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLShader> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLShader>(adaptor, vertexPath, fragmentPath, m_GL);
		}
		return std::make_shared<OpenGLShader>(vertexPath, fragmentPath, m_GL);
	}

	std::shared_ptr<Shader> OpenGLGraphicsFactory::CreateShaderFromStages(
		const std::vector<ShaderStage>& stages)
	{
		// 使用临时名称（从第一阶段派生）
		std::string name = "shader_" + std::to_string(reinterpret_cast<uint64>(stages.data()));

		auto shader = std::make_shared<OpenGLShader>("", "", m_GL);
		if (shader->LoadStages(stages)) {
			return shader;
		}
		s_Log.Error("CreateShaderFromStages failed");
		return nullptr;
	}

	std::shared_ptr<Texture> OpenGLGraphicsFactory::CreateTexture(
		const std::string& path)
	{
		// 没有**当前 GL 上下文**时必须拒绝，即使 glad 的入口指针非空。
		//
		// 指针非空不等于上下文存在：本工厂构造时用 glfwGetProcAddress 填表，
		// 那一步只要 GLFW 初始化成功就会返回非空的 libGL 分派入口。但在没有
		// current context 的情况下调用它们会跳进空的分派槽 —— Linux CI 上表现为
		// project-open 路径里的 SIGSEGV，且 faulting 指令是 `call *%rcx`（间接调用），
		// 因此「检查指针是否为 null」这种修法抓不到它。
		//
		// test_bridge / test_gp01 只用引擎的内容管线，从不创建 GL 上下文，
		// 但它们会经 EditorSession::OpenProject → InstantiateScene → SetTexture
		// 走到这里。返回 nullptr 是既有契约：TextureManager::Load 对空结果
		// 不入缓存并原样返回。
		if (!glfwGetCurrentContext()) {
			s_Log.Error("CreateTexture('{}') refused: no current OpenGL context", path);
			return nullptr;
		}

		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLTexture> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLTexture>(adaptor, path, m_GL);
		}
		return std::make_shared<OpenGLTexture>(path, m_GL);
	}

	std::shared_ptr<VertexBuffer> OpenGLGraphicsFactory::CreateVertexBuffer(
		float* vertices,
		uint32_t size)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLVertexBuffer> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLVertexBuffer>(adaptor, vertices, size, m_GL);
		}
		return std::make_shared<OpenGLVertexBuffer>(vertices, size, m_GL);
	}

	std::shared_ptr<IndexBuffer> OpenGLGraphicsFactory::CreateIndexBuffer(
		uint32_t* indices,
		uint32_t count)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLIndexBuffer> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLIndexBuffer>(adaptor, indices, count, m_GL);
		}
		return std::make_shared<OpenGLIndexBuffer>(indices, count, m_GL);
	}

	std::shared_ptr<VertexArray> OpenGLGraphicsFactory::CreateVertexArray()
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLVertexArray> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLVertexArray>(adaptor, m_GL);
		}
		return std::make_shared<OpenGLVertexArray>(m_GL);
	}

	// ---- RHI 抽象资源（全新接口） ----

	std::shared_ptr<RHI::IRHIVertexBuffer> OpenGLGraphicsFactory::CreateVertexBuffer_RHI(
		const void* data, size_t size, uint32 stride)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<RHI::OpenGLVertexBuffer_RHI> adaptor(m_Allocator);
			return std::allocate_shared<RHI::OpenGLVertexBuffer_RHI>(adaptor, data, size, stride, m_GL);
		}
		return std::make_shared<RHI::OpenGLVertexBuffer_RHI>(data, size, stride, m_GL);
	}

	std::shared_ptr<RHI::IRHIIndexBuffer> OpenGLGraphicsFactory::CreateIndexBuffer_RHI(
		const uint32* data, uint32 count)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<RHI::OpenGLIndexBuffer_RHI> adaptor(m_Allocator);
			return std::allocate_shared<RHI::OpenGLIndexBuffer_RHI>(adaptor, data, count, m_GL);
		}
		return std::make_shared<RHI::OpenGLIndexBuffer_RHI>(data, count, m_GL);
	}

	std::shared_ptr<RHI::IRHIVertexArray> OpenGLGraphicsFactory::CreateVertexArray_RHI()
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<RHI::OpenGLVertexArray_RHI> adaptor(m_Allocator);
			return std::allocate_shared<RHI::OpenGLVertexArray_RHI>(adaptor, m_GL);
		}
		return std::make_shared<RHI::OpenGLVertexArray_RHI>(m_GL);
	}

	std::shared_ptr<ISpriteBatch> OpenGLGraphicsFactory::CreateSpriteBatch(
		IRenderContext& renderContext)
	{
		if (m_Allocator) {
			StackAllocatorAdaptor<OpenGLSpriteBatch> adaptor(m_Allocator);
			return std::allocate_shared<OpenGLSpriteBatch>(adaptor, m_GL, renderContext);
		}
		return std::make_shared<OpenGLSpriteBatch>(m_GL, renderContext);
	}

	std::unique_ptr<IUIManager> OpenGLGraphicsFactory::CreateUIManager()
	{
		return std::make_unique<ImGuiUIManager>();
	}

	std::unique_ptr<IPrimitiveBatch> OpenGLGraphicsFactory::CreatePrimitiveBatch(
		uint32 capacity)
	{
		return std::make_unique<OpenGLPrimitiveBatch>(m_GL, capacity);
	}

	// ---- 抗锯齿配置 ----
	void OpenGLGraphicsFactory::SetAntiAliasingConfig(const AntiAliasingConfig& config)
	{
		if (m_RenderContext) {
			auto* ctx = static_cast<OpenGLContext*>(m_RenderContext);
			ctx->SetAntiAliasingConfig(config);
		} else {
			s_Log.Warn("SetAntiAliasingConfig: No render context available. Call CreateWindow first.");
		}
	}

	AntiAliasingConfig OpenGLGraphicsFactory::GetAntiAliasingConfig() const
	{
		if (m_RenderContext) {
			return static_cast<OpenGLContext*>(m_RenderContext)->GetAntiAliasingConfig();
		}
		return AntiAliasingConfig{};
	}

	AntiAliasingCaps OpenGLGraphicsFactory::GetAntiAliasingCaps() const
	{
		if (m_RenderContext) {
			auto* aa = static_cast<OpenGLContext*>(m_RenderContext)->GetAntiAliasing();
			if (aa) return aa->GetCaps();
		}
		return AntiAliasingCaps{};
	}

	std::unique_ptr<GBuffer> OpenGLGraphicsFactory::CreateGBuffer(
		IRenderContext& context)
	{
		auto* ctx = dynamic_cast<OpenGLContext*>(&context);
		if (!ctx) {
			s_Log.Error("CreateGBuffer: context is not OpenGLContext");
			return nullptr;
		}
		return std::make_unique<OpenGLGBuffer>(context);
	}

} // namespace Engine
