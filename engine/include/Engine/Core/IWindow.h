#pragma once

#include "Engine/Core/IRenderContext.h"

#include <memory>
#include <string>

namespace Engine {

	// ============================================================================
	// IWindow - 窗口抽象接口（RHI 公共 API）
	// 窗口实例由 GraphicsFactory 创建，渲染上下文通过依赖注入传入
	// ============================================================================
	class IWindow
	{
	public:
		virtual ~IWindow() = default;

		virtual void OnUpdate() = 0;
		virtual bool ShouldClose() const = 0;
		virtual IRenderContext* GetContext() = 0;
	};

} // namespace Engine
