#include "OpenGLPrimitiveBatch.h"

namespace Engine {

	// ============================================================
	// 构造 / 析构
	// ============================================================

	OpenGLPrimitiveBatch::OpenGLPrimitiveBatch(GladGLContext& gl, uint32 capacity)
		: m_GL(gl)
		, m_MaxVertices(capacity)
		, m_MaxIndices(capacity * 3) // 修复：为索引分配明确的上限，通常是顶点的3倍
	{
		m_Vertices.reserve(m_MaxVertices);
		m_Indices.reserve(m_MaxIndices);
	}

	OpenGLPrimitiveBatch::~OpenGLPrimitiveBatch()
	{
		DestroyGPUResources();
	}

	// ============================================================
	// 生命周期
	// ============================================================

	void OpenGLPrimitiveBatch::Begin(PrimitiveType type)
	{
		if (m_Began) {
			End(); // 自动结束上一批次
		}
		m_Type = type;
		m_Vertices.clear();
		m_Indices.clear();
		m_Began = true;
	}

	void OpenGLPrimitiveBatch::Commit()
	{
		if (!m_Began || m_Vertices.empty()) return;
		Flush();
	}

	void OpenGLPrimitiveBatch::End()
	{
		if (!m_Began) return;
		Commit();
		m_Began = false;
	}

	void OpenGLPrimitiveBatch::Clear()
	{
		m_Vertices.clear();
		m_Indices.clear();
		m_Began = false; // 修复：Clear 时需要重置状态
	}

	// ============================================================
	// 安全的容量检查（修复随机崩溃的核心）
	// ============================================================
	void OpenGLPrimitiveBatch::EnsureCapacity(uint32 vCount, uint32 iCount)
	{
		if (!m_Began) return;
		// 如果即将加入的顶点或索引超过了容量，则完整提交当前批次
		if (m_Vertices.size() + vCount > m_MaxVertices ||
			m_Indices.size() + iCount > m_MaxIndices)
		{
			Flush();
		}
	}

	// ============================================================
	// 顶点 / 索引写入
	// ============================================================

	void OpenGLPrimitiveBatch::Vertex(const PrimitiveVertex& v)
	{
		if (!m_Began) return;
		EnsureCapacity(1, 0); // 检查是否装得下一个顶点
		m_Vertices.push_back(v);
	}

	void OpenGLPrimitiveBatch::Vertex(const Vec3& pos, const Vec4& color)
	{
		Vertex(PrimitiveVertex(pos, color));
	}

	void OpenGLPrimitiveBatch::Vertex(const Vec3& pos, const Vec3& normal,
		const Vec2& uv, const Vec4& color)
	{
		Vertex(PrimitiveVertex(pos, normal, uv, color));
	}

	void OpenGLPrimitiveBatch::Index(uint32 i)
	{
		if (!m_Began) return;
		EnsureCapacity(0, 1);
		m_Indices.push_back(i);
	}

	void OpenGLPrimitiveBatch::Triangle(const PrimitiveVertex& v0,
		const PrimitiveVertex& v1,
		const PrimitiveVertex& v2)
	{
		// 修复：保证这3个顶点和3个索引绝对不会被 Flush 强行拆分！
		EnsureCapacity(3, 3);

		uint32 base = (uint32)m_Vertices.size();
		m_Vertices.push_back(v0);
		m_Vertices.push_back(v1);
		m_Vertices.push_back(v2);

		m_Indices.push_back(base);
		m_Indices.push_back(base + 1);
		m_Indices.push_back(base + 2);
	}

	void OpenGLPrimitiveBatch::Line(const Vec3& from, const Vec3& to,
		const Vec4& color)
	{
		EnsureCapacity(2, 2); // 修复：保证线条不被截断
		uint32 base = (uint32)m_Vertices.size();

		m_Vertices.push_back(PrimitiveVertex(from, color));
		m_Vertices.push_back(PrimitiveVertex(to, color));

		m_Indices.push_back(base);
		m_Indices.push_back(base + 1);
	}

	void OpenGLPrimitiveBatch::Point(const Vec3& pos, const Vec4& color)
	{
		EnsureCapacity(1, 1);
		uint32 base = (uint32)m_Vertices.size();
		m_Vertices.push_back(PrimitiveVertex(pos, color));
		m_Indices.push_back(base);
	}

	void OpenGLPrimitiveBatch::SetCapacity(uint32 maxVertices)
	{
		m_MaxVertices = maxVertices;
		m_MaxIndices = maxVertices * 3;
		m_Vertices.reserve(m_MaxVertices);
		m_Indices.reserve(m_MaxIndices);
	}

	// ============================================================
	// GPU 提交与资源管理（性能优化 & 修复冗余）
	// ============================================================

	void OpenGLPrimitiveBatch::Flush()
	{
		if (m_Vertices.empty() || m_Indices.empty())
			return;

		CreateGPUResources();

		m_GL.BindVertexArray(m_VAO);

		// ── 上传数据 (去除了每次都重新设置 VertexAttribPointer 的极大性能浪费) ──
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);
		m_GL.BufferData(GL_ARRAY_BUFFER,
			(GLsizeiptr)(m_Vertices.size() * sizeof(PrimitiveVertex)),
			m_Vertices.data(),
			GL_DYNAMIC_DRAW);

		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
		m_GL.BufferData(GL_ELEMENT_ARRAY_BUFFER,
			(GLsizeiptr)(m_Indices.size() * sizeof(uint32)),
			m_Indices.data(),
			GL_DYNAMIC_DRAW);

		// ── 发出 DrawCall ──
		m_GL.DrawElements(ToGLPrimitive(m_Type),
			(GLsizei)m_Indices.size(),
			GL_UNSIGNED_INT,
			nullptr);

		m_GL.BindVertexArray(0);

		// 清理 CPU 缓冲区以便继续累积
		m_Vertices.clear();
		m_Indices.clear();
	}

	void OpenGLPrimitiveBatch::CreateGPUResources()
	{
		if (m_ResourcesValid)
			return;

		m_GL.GenVertexArrays(1, &m_VAO);
		m_GL.GenBuffers(1, &m_VBO);
		m_GL.GenBuffers(1, &m_EBO);

		// 修复：将顶点布局配置移动到初始化阶段，只需要绑定一次！
		m_GL.BindVertexArray(m_VAO);
		m_GL.BindBuffer(GL_ARRAY_BUFFER, m_VBO);

		// position (vec3)
		m_GL.EnableVertexAttribArray(0);
		m_GL.VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, position));
		// normal (vec3)
		m_GL.EnableVertexAttribArray(1);
		m_GL.VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, normal));
		// texCoord (vec2)
		m_GL.EnableVertexAttribArray(2);
		m_GL.VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, texCoord));
		// color (vec4)
		m_GL.EnableVertexAttribArray(3);
		m_GL.VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(PrimitiveVertex), (void*)offsetof(PrimitiveVertex, color));

		// 将 EBO 绑定记录在 VAO 中
		m_GL.BindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);

		m_GL.BindVertexArray(0);

		m_ResourcesValid = true;
	}

	void OpenGLPrimitiveBatch::DestroyGPUResources()
	{
		if (m_VAO)  m_GL.DeleteVertexArrays(1, &m_VAO);
		if (m_VBO)  m_GL.DeleteBuffers(1, &m_VBO);
		if (m_EBO)  m_GL.DeleteBuffers(1, &m_EBO);
		m_VAO = m_VBO = m_EBO = 0;
		m_ResourcesValid = false;
	}

	// ============================================================
	// 工具
	// ============================================================

	GLenum OpenGLPrimitiveBatch::ToGLPrimitive(PrimitiveType type) const
	{
		switch (type) {
		case PrimitiveType::Triangles:     return GL_TRIANGLES;
		case PrimitiveType::Lines:         return GL_LINES;
		case PrimitiveType::Points:        return GL_POINTS;
		case PrimitiveType::TriangleStrip: return GL_TRIANGLE_STRIP;
		case PrimitiveType::LineStrip:     return GL_LINE_STRIP;
		default: return GL_TRIANGLES;
		}
	}

} // namespace Engine