#pragma once

#include "Engine/Core/RenderResources/Shader.h"
#include "Engine/Core/IGraphicsFactory.h"
#include "Engine/Types.h"
#include <glad/gl.h>
#include <string>
#include <vector>

namespace Engine {

	class OpenGLShader : public Shader
	{
	public:
		OpenGLShader(const std::string& vertexPath, const std::string& fragmentPath, GladGLContext& gl);
		virtual ~OpenGLShader() override;

		// ── 旧接口 ──
		virtual void Bind() const override;
		virtual void Unbind() const override;
		virtual void SetMat4(const std::string& name, const float32* data) override;
		virtual void SetVec2(const std::string& name, const float32* data) override;
		virtual void SetVec3(const std::string& name, const float32* data) override;
		virtual void SetVec4(const std::string& name, const float32* data) override;
		virtual void SetMat3(const std::string& name, const float32* data) override;
		virtual void SetFloat(const std::string& name, float32 value) override;
		virtual void SetInt(const std::string& name, int32 value) override;

		// ── 新接口：多阶段加载 ──
		virtual bool LoadStages(const std::vector<ShaderStage>& stages) override;
		virtual void BindUniformBlock(const std::string& name,
		                              uint64 bufferID,
		                              uint32 binding) override;
		virtual void BindStorageBlock(const std::string& name,
		                              uint64 bufferID,
		                              uint32 binding) override;
		virtual const ShaderReflection& GetReflection() const override;
		virtual uint32 GetStageCount() const override { return (uint32)m_Stages.size(); }
		virtual ShaderStageType GetStageType(uint32 index) const override;
		virtual bool IsCompute() const override;
		virtual uint64 GetNativeHandle() const override { return m_RendererID; }

		// ── 加载后初始化钩子 ──
		bool PostLoad(IGraphicsFactory* factory) override;

		// ── 热加载 ──
		bool Reload() override;

	private:
		std::string ReadFile(const std::string& filepath);
		uint32 CompileShader(unsigned int type, const std::string& source);
		uint32 CompileShaderFromStage(const ShaderStage& stage);
		bool LinkProgram();
		void ReflectUniforms();

		// 将 ShaderStageType 转换为 GL 枚举
		static unsigned int StageTypeToGL(ShaderStageType type);
		static ShaderStageType GLToStageType(unsigned int glType);

		GladGLContext& m_GL;
		GLuint m_RendererID = 0;

		// 保存阶段描述以便热加载和查询
		std::vector<ShaderStage> m_Stages;

		// 旧接口的路径（向后兼容）
		std::string m_VertexPath;
		std::string m_FragmentPath;

		// 反射缓存
		mutable ShaderReflection m_Reflection;
		mutable bool m_ReflectionDirty = true;
	};

} // namespace Engine
