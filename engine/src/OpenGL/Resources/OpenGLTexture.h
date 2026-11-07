#pragma once

#include "Engine/Core/RenderResources/Texture.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Types.h"
#include <glad/gl.h>

namespace Engine {

	class OpenGLTexture : public Texture
	{
	public:
		// srgb=false 维持既有 Linear 行为；srgb=true 选择 sRGB 内部格式。
		// （P6-1B B4-C：双参 ctor 保持旧调用兼容。）
		OpenGLTexture(const std::string& path, GladGLContext& gl,
		              bool srgb = false);
		virtual ~OpenGLTexture() override;

		virtual void Bind(uint32 slot = 0) const override;
		virtual uint32 GetWidth() const override { return m_Width; }
		virtual uint32 GetHeight() const override { return m_Height; }

		// ── 加载后初始化钩子 ──
		bool PostLoad(IGraphicsFactory* factory) override;

		// ── 热加载 ──
		bool Reload() override;

private:
	GladGLContext& m_GL;
	uint32 m_Width = 0, m_Height = 0;
	GLuint m_RendererID = 0;
	bool m_SRGB = false;
};

} // namespace Engine
