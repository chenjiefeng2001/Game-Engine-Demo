#pragma once
#include <memory>
#include <string>
#include <cstdint> // 必须包含，识别 uint32_t

namespace Engine {
    // 前向声明
    class VertexArray;
    class VertexBuffer;
    class IndexBuffer;
    class Shader;
    class Texture;

    class IRenderContext {
    public:
        virtual ~IRenderContext() = default;

        // 基础上下文管理
        virtual void Init() = 0;
        virtual void ClearColor(float r, float g, float b, float a) = 0;
        virtual void SwapBuffers() = 0;
        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& va) = 0;

        // 资源工厂方法
        virtual std::shared_ptr<Shader> CreateShader(const std::string& vPath, const std::string& fPath) = 0;
        virtual std::shared_ptr<Texture> CreateTexture(const std::string& path) = 0;
        virtual std::shared_ptr<VertexArray> CreateVertexArray() = 0;
        virtual std::shared_ptr<VertexBuffer> CreateVertexBuffer(float* vertices, uint32_t size) = 0;
        virtual std::shared_ptr<IndexBuffer> CreateIndexBuffer(uint32_t* indices, uint32_t count) = 0;
    };
}