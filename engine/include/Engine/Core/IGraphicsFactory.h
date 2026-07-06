#pragma once

#include <memory>
#include <string>
#include <vector>
#include "Engine/Types.h"
#include "Engine/Core/RHI/AntiAliasingTypes.h"
#include "Engine/Core/RHI/GBuffer.h"
#include "Engine/Core/RHI/IGPUMemoryAllocator.h"

namespace Engine {

	// 前向声明
	class IWindow;
	class IRenderContext;
	class Shader;
	class Texture;
	class VertexBuffer;
	class IndexBuffer;
	class VertexArray;
	class ISpriteBatch;
	class IPrimitiveBatch;
	class IUIManager;
	class StackAllocator;
	struct ShaderStage;
	class GBuffer;
	namespace RHI { class IGPUMemoryAllocator; }
	namespace RHI { class IRHIVertexBuffer; }
	namespace RHI { class IRHIIndexBuffer; }
	namespace RHI { class IRHIVertexArray; }

// ============================================================
// RHI 抽象工厂 — 完全与具体图形 API 解耦
// ============================================================
// 不包含任何 OpenGL / Vulkan / DirectX 等具体 API 头文件。
// 具体实现类（如 OpenGLGraphicsFactory）负责管理自己的 API 上下文。
	class IGraphicsFactory
	{
	public:
		virtual ~IGraphicsFactory() = default;

		// ---- 分配器支持 ----
		/** 设置栈分配器，工厂将使用它分配所有子系统对象 */
		void SetAllocator(StackAllocator* alloc) noexcept { m_Allocator = alloc; }
		/** 获取当前的栈分配器 */
		StackAllocator* GetAllocator() const noexcept { return m_Allocator; }

		// ---- 窗口与上下文 ----
		virtual std::unique_ptr<IWindow> CreateWindow(
			int width, int height, const std::string& title) = 0;

		virtual std::unique_ptr<IRenderContext> CreateRenderContext(
			void* nativeWindowHandle) = 0;

		// ---- GPU 资源 ----
		/** 从 vertex + fragment GLSL 文件创建着色器（向后兼容） */
		virtual std::shared_ptr<Shader> CreateShader(
			const std::string& vertexPath,
			const std::string& fragmentPath) = 0;

		/**
		 * @brief 从多个着色器阶段创建着色器（新接口）
		 *
		 * 支持任意阶段组合，包括几何/细分/计算着色器。
		 * 每个阶段可以是 GLSL 源码、SPIR-V 二进制或 HLSL（通过 shaderc）。
		 *
		 * @param stages 着色器阶段描述列表
		 * @return 编译链接后的 Shader 对象
		 *
		 * @code
		 *   auto shader = factory.CreateShaderFromStages({
		 *       ShaderStage::FromFile(ShaderStageType::Vertex, "shader.vert"),
		 *       ShaderStage::FromFile(ShaderStageType::Geometry, "shader.geom"),
		 *       ShaderStage::FromFile(ShaderStageType::Fragment, "shader.frag"),
		 *   });
		 * @endcode
		 */
		virtual std::shared_ptr<Shader> CreateShaderFromStages(
			const std::vector<ShaderStage>& stages) = 0;

		virtual std::shared_ptr<Texture> CreateTexture(
			const std::string& path) = 0;

		virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(
			float* vertices,
			uint32 size) = 0;

		virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(
			uint32* indices,
			uint32 count) = 0;

		virtual std::shared_ptr<VertexArray> CreateVertexArray() = 0;

		// ---- RHI 抽象资源（全新接口，不依赖 OpenGL 具体类型） ----

		/**
		 * @brief 创建 RHI 顶点缓冲区
		 * @param data    顶点数据指针
		 * @param size    数据大小（字节）
		 * @param stride  单个顶点步长（字节）
		 */
		virtual std::shared_ptr<RHI::IRHIVertexBuffer> CreateVertexBuffer_RHI(
			const void* data, size_t size, uint32 stride) = 0;

		/**
		 * @brief 创建 RHI 索引缓冲区
		 * @param data    索引数据指针（uint32）
		 * @param count   索引数量
		 */
		virtual std::shared_ptr<RHI::IRHIIndexBuffer> CreateIndexBuffer_RHI(
			const uint32* data, uint32 count) = 0;

		/**
		 * @brief 创建 RHI 顶点数组
		 */
		virtual std::shared_ptr<RHI::IRHIVertexArray> CreateVertexArray_RHI() = 0;

		// ---- 高级渲染工具 ----
		virtual std::shared_ptr<ISpriteBatch> CreateSpriteBatch(
			IRenderContext& renderContext) = 0;

		/**
		 * @brief 创建图元批处理器
		 * @param capacity 最大顶点数（默认 16384）
		 * @return 图元批处理器实例
		 *
		 * 将大量顶点/索引累积到 CPU 缓冲区，Commit() 时以单个
		 * DrawCall 提交 GPU。支持 Triangles / Lines / Points。
		 */
		virtual std::unique_ptr<IPrimitiveBatch> CreatePrimitiveBatch(
			uint32 capacity = 16384) = 0;

		// ---- 抗锯齿 ----
		/**
		 * @brief 设置多重采样样本数
		 * @param samples 样本数（0=关闭，2/4/8=FSAA 级别）
		 *
		 * 必须在 CreateWindow 之前调用才有效。
		 * 默认值由具体实现决定（OpenGL 默认 4x MSAA）。
		 */
		virtual void SetMultisampleSamples(int32 samples) { (void)samples; }

		/**
		 * @brief 获取当前多重采样样本数
		 */
		virtual int32 GetMultisampleSamples() const { return 0; }

		/**
		 * @brief 设置抗锯齿模式与配置
		 *
		 * 支持 MSAA / SSAA / CSAA / MLAA 等多种抗锯齿技术。
		 * 可在运行时切换，下次渲染时生效。
		 *
		 * @param config 抗锯齿配置
		 */
		virtual void SetAntiAliasingConfig(const AntiAliasingConfig& config) { (void)config; }

		/**
		 * @brief 获取当前抗锯齿配置
		 */
		virtual AntiAliasingConfig GetAntiAliasingConfig() const { return AntiAliasingConfig{}; }

		/**
		 * @brief 查询抗锯齿能力
		 */
		virtual AntiAliasingCaps GetAntiAliasingCaps() const { return AntiAliasingCaps{}; }

		// ---- 延迟渲染 ----
		virtual std::unique_ptr<GBuffer> CreateGBuffer(
			IRenderContext& context) = 0;

		// ---- UI 管理器 ----
		virtual std::unique_ptr<IUIManager> CreateUIManager() = 0;

		// ---- GPU 显存分配器 ----

		/**
		 * @brief 设置 GPU 显存分配器
		 *
		 * 调用时机：图形设备创建后、首次资源创建前。
		 * 工厂内部将使用此分配器为所有 Create*() 产生的 GPU 资源分配显存。
		 *
		 * 如果未设置（m_MemoryAllocator == nullptr），工厂应使用后端默认分配器
		 * 或回退到自管理的简单分配（如原 OpenGL 实现那样）。
		 *
		 * @param allocator 已初始化的分配器（unique_ptr 所有权转移）
		 */
		void SetMemoryAllocator(RHI::GPUMemoryAllocatorPtr allocator) noexcept {
			m_MemoryAllocator = std::move(allocator);
		}

		/**
		 * @brief 获取当前的 GPU 显存分配器
		 * @return 分配器指针（可能为 nullptr = 未设置/使用后端默认）
		 */
		RHI::IGPUMemoryAllocator* GetMemoryAllocator() const noexcept {
			return m_MemoryAllocator.get();
		}

		/**
		 * @brief 是否已设置显存分配器
		 */
		bool HasMemoryAllocator() const noexcept {
			return m_MemoryAllocator != nullptr && m_MemoryAllocator->IsReady();
		}

	protected:
		StackAllocator* m_Allocator = nullptr;
		RHI::GPUMemoryAllocatorPtr m_MemoryAllocator;
	};

}
