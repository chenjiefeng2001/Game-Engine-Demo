#pragma once

/**
 * @file ScriptSandboxApp.h
 * @brief Editor Workflow v1.1 — Milestone 4 DX
 *
 * NOTE: Content/nlohmann types are isolated via pimpl (EditorState) in .cpp.
 */

#include <Engine/Application.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/Shader.h>
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
    std::shared_ptr<ISpriteBatch> m_Batch;
    std::shared_ptr<Shader> m_Shader;
    std::shared_ptr<Texture> m_Tex;
    std::unique_ptr<TextureManager> m_TexMgr;

    Scene              m_Scene;
    Scripting::ScriptInstance m_Player;

    void AppendLog(const std::string& line);

    void DrawHierarchyPanel();
    void DrawInspectorPanel();
    void DrawAssetBrowser();
    void DrawConsolePanel();
    void DrawScriptEditor();
    void DrawHudOverlay();

    void SaveScene();
    void LoadScene();
    void NewScene();
    void OpenScriptEditor(const std::string& path);
};

} // namespace Engine::Sandbox
