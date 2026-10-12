/**
 * @file StubGraphicsFactory.h
 * @brief 不触碰 OpenGL 的图形工厂，供内容绑定 / 反序列化用例使用
 *
 * 背景：这些用例验证的是**绑定契约**——GUID 能否解析、缺失是否非致命、
 * binding 是否如实记账。纹理是否真的上传到 GPU 与该契约无关：
 * SpriteComponent::SetTexture 只是接收 TextureManager::Load 的返回值
 * （允许为空），而 EntityContentBinding 的 spriteGuid / scriptGuid 记账
 * 是无条件写入的。
 *
 * 为什么不能直接用 OpenGLGraphicsFactory：它的构造函数会请求一个 4.6 core
 * 上下文来加载 glad。在没有 OpenGL 上下文的 runner 上（例如 Linux CI，
 * llvmpipe 最高只给 4.5），这个临时窗口创建失败，m_GL 保持全零——
 * 一个 GL 入口点都没加载。随后第一次 CreateTexture 就会经空函数指针调用
 * OpenGL，直接段错误，用例连断言都到不了。
 *
 * 这里提供不碰 GL 的实现：CreateTexture 返回一个纯 CPU 侧的纹理对象，
 * TextureManager 的缓存路径照样被真实执行；其余工厂能力显式声明为不支持，
 * 而不是静默假装可用——这样一旦有人真的用到了它，会立刻拿到 nullptr。
 */
#pragma once

#include "Engine/Core/IWindow.h"
#include "Engine/Core/IRenderContext.h"
#include "Engine/Core/IUIManager.h"
#include "Engine/Core/RHI/GBuffer.h"
#include "Engine/Core/RHI/IPrimitiveBatch.h"
#include "Engine/Core/RenderResources/Texture.h"

#include <memory>
#include <string>
#include <vector>

// Texture 是抽象类（Bind / GetWidth / GetHeight 为纯虚），需要一个具体实现。
// 刻意不做任何 GPU 绑定：绑定契约不依赖它。
class StubTexture : public Texture {
public:
    explicit StubTexture(const std::string& path) : Texture(path) {}
    void Bind(uint32) const override {}
    uint32 GetWidth() const override { return 1; }
    uint32 GetHeight() const override { return 1; }
};

class StubGraphicsFactory : public IGraphicsFactory {
public:
    std::shared_ptr<Texture> CreateTexture(const std::string& path) override {
        return std::make_shared<StubTexture>(path);
    }

    // ── 其余能力本组用例一律不涉及，显式不支持 ──
    std::unique_ptr<IWindow> CreateWindow(int, int, const std::string&) override { return nullptr; }
    std::unique_ptr<IRenderContext> CreateRenderContext(void*) override { return nullptr; }
    std::shared_ptr<Shader> CreateShader(const std::string&, const std::string&) override { return nullptr; }
    std::shared_ptr<Shader> CreateShaderFromStages(const std::vector<ShaderStage>&) override { return nullptr; }
    std::shared_ptr<VertexBuffer> CreateVertexBuffer(float*, uint32) override { return nullptr; }
    std::shared_ptr<IndexBuffer> CreateIndexBuffer(uint32*, uint32) override { return nullptr; }
    std::shared_ptr<VertexArray> CreateVertexArray() override { return nullptr; }
    std::shared_ptr<RHI::IRHIVertexBuffer> CreateVertexBuffer_RHI(const void*, size_t, uint32) override { return nullptr; }
    std::shared_ptr<RHI::IRHIIndexBuffer> CreateIndexBuffer_RHI(const uint32*, uint32) override { return nullptr; }
    std::shared_ptr<RHI::IRHIVertexArray> CreateVertexArray_RHI() override { return nullptr; }
    std::shared_ptr<ISpriteBatch> CreateSpriteBatch(IRenderContext&) override { return nullptr; }
    std::unique_ptr<IPrimitiveBatch> CreatePrimitiveBatch(uint32) override { return nullptr; }
    std::unique_ptr<GBuffer> CreateGBuffer(IRenderContext&) override { return nullptr; }
    std::unique_ptr<IUIManager> CreateUIManager() override { return nullptr; }
};
