#pragma once

/**
 * @file ScriptSandboxApp.h
 * @brief Editor Workflow v1.1 — Milestone 4 DX
 *
 * ⚠️ 本头文件不包含任何 nlohmann/content 头文件。
 *    所有 Content 类型通过 pimpl (EditorState) 隔离在 .cpp 中。
 *    这解决了 MSVC ASAN 下 nlohmann ↔ spdlog/fmt 的宏冲突。
 */

#include <Engine/Application.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Scripting/ScriptInstance.h>

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>

namespace Engine { class TextureManager; }

namespace Engine::Sandbox {

class ScriptSandboxApp : public Application {
public:
    explicit ScriptSandboxApp(IGraphicsFactory& factory);
    ~ScriptSandboxApp() override;

protected:
    void OnStartup() override;
    void OnUpdate(float32 dt) override;
    void OnRender() override;
    void OnImGui() override;

private:
    struct EditorState;
    std::unique_ptr<EditorState> m_Ed;

    InputManager       m_InputMgr;
    OrthographicCamera m_Camera{ -8.f, 8.f, -4.5f, 4.5f };
    std::shared_ptr<class ISpriteBatch> m_Batch;
    std::shared_ptr<class Shader>       m_Shader;
    std::shared_ptr<class Texture>      m_Tex;
    std::unique_ptr<TextureManager>     m_TexMgr;

    Scene              m_Scene;
    Scripting::ScriptInstance m_Player;

    void AppendLog(const std::string& line);
};

} // namespace Engine::Sandbox
