#pragma once

/**
 * @file ScriptSandboxApp.h
 * @brief Scripting v1 产品验收沙盒 —— 最小真实使用场景
 *
 * 验收链路（全部成立即 Sandbox 通过）：
 *   1. GLFW Input → GLFWScriptInputProvider → Engine.input.* → player.lua
 *      → Engine.entity/transform → 真实 GameObject 位移 → Renderer 可见
 *   2. F5 热重载 player.lua：_PERSIST 句柄存活，Player 继续响应
 *   3. ImGui Console → ScriptInstance::Execute() 直驱引擎；错误进滚动区
 *
 * 范围红线（Scripting v1 冻结原则）：场景仅 Player+Cube；
 * 无 Physics/Audio/Animation/序列化/ECS/反射/自动补全/调试器。
 */

#include <Engine/Application.h>
#include <Engine/Core/Input.h>
#include <Engine/Core/InputManager.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Renderer/SpriteBatch.h>
#include <Engine/Core/Renderer/OrthographicCamera.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/Core/Content/ContentAsset.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Scripting/ScriptInstance.h>
#include "GLFWScriptInputProvider.h"

#include <imgui.h>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>

namespace Engine::Sandbox {

class ScriptSandboxApp : public Application {
public:
    explicit ScriptSandboxApp(IGraphicsFactory& factory);

protected:
    void OnStartup() override;
    void OnUpdate(float32 dt) override;
    void OnRender() override;
    void OnImGui() override;

private:
    // ── 基础设施 ──
    InputManager                m_InputManager;
    OrthographicCamera          m_Camera{ -8.f, 8.f, -4.5f, 4.5f };
    std::shared_ptr<ISpriteBatch> m_Batch;
    std::shared_ptr<Shader>     m_BatchShader;
    std::shared_ptr<Texture>    m_Tex;
    TextureManager              m_TexMgr;

    // ── Gameplay 链路 ──
    Scene                       m_Scene;
    Scripting::ScriptInstance   m_Player;

    // ── Editor Workflow v1（Ring13）──
    uint32_t                    m_Selected = 0;
    Content::ContentRegistry    m_Registry;
    std::unordered_map<uint32_t, Content::EntityContentBinding> m_Bindings;
    static constexpr const char* kScenePath    = "sandbox_scene.json";
    static constexpr const char* kManifestPath = "sandbox_manifest.json";

    // ── Console 状态 ──
    char                        m_CmdBuf[256] = {};
    std::vector<std::string>    m_Scrollback;
    char                        m_AssetSearch[128] = {};
    int                         m_AssetFilter = 0;  // 0=All 1=Texture 2=Script

    void AppendLog(const std::string& line);
    void DrawConsolePanel();
    void DrawAssetBrowser();
    void DrawHierarchyPanel();
    void DrawInspectorPanel();
    void SaveScene();
    void LoadScene();
};

} // namespace Engine::Sandbox
