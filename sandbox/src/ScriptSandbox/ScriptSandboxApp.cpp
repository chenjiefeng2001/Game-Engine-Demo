/**
 * @file ScriptSandboxApp.cpp
 * @brief Scripting v1 验收沙盒 + Editor Workflow v1 宿主（Ring13）
 */

#include "ScriptSandboxApp.h"
#include <Engine/Scripting/LuaEngine.h>
#include <Engine/Scripting/ScriptAPI.h>
#include <Engine/Scripting/GameplayAPI.h>
#include <Engine/Core/Log.h>
#include <Engine/Platform/FileDialog.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace Engine::Sandbox {

// 输入适配器生命周期与 App 相同（进程级持有）
static std::unique_ptr<Scripting::GameplayAPI::IScriptInputProvider> g_inputOwner;

// ═══ EditorState（pimpl 完整定义——仅 .cpp 可见，隔离 nlohmann↔spdlog）═══

struct ScriptSandboxApp::EditorState {
    // Ring13 editor
    uint32_t selected = 0;
    std::unique_ptr<Content::ContentRegistry> registry;
    std::unordered_map<uint32_t, Content::EntityContentBinding> bindings;
    static constexpr const char* sceneFile = "sandbox_scene.json";
    static constexpr const char* manifestFile = "sandbox_manifest.json";
    bool sceneLoaded = false;

    // M4-B script editor
    char  scriptBuf[16384] = {};
    bool  scriptDirty = false;
    std::string scriptPath;
    bool  scriptPanelOpen = false;

    // Asset Browser
    char assetSearch[128] = {};
    int assetFilter = 0;

    void AppendLog(const std::string& line) {
        scrollback.push_back(line);
        if (scrollback.size() > 256) scrollback.erase(scrollback.begin());
    }
};

ScriptSandboxApp::ScriptSandboxApp(IGraphicsFactory& factory)
    : Application(factory), m_TexMgr(factory) {}

void ScriptSandboxApp::AppendLog(const std::string& line) {
    m_Scrollback.push_back(line);
    if (m_Scrollback.size() > 256) m_Scrollback.erase(m_Scrollback.begin());
}

void ScriptSandboxApp::OnStartup() {
    // ── 输入 ──
    m_InputManager.Init(&GetWindow());
    if (!g_inputOwner)
        g_inputOwner = std::make_unique<GLFWScriptInputProvider>();
    Scripting::GameplayAPI::SetInputProvider(g_inputOwner.get());

    // ── Ring13：注册表跨进程恢复（Golden Gate 前提）──
    if (m_Registry.LoadManifest(kManifestPath))
        AppendLog("[registry] manifest loaded (" + std::to_string(m_Registry.Count()) + " assets)");
    else
        AppendLog("[registry] fresh session");

    // ── 渲染资源 ──
    auto* ctx = GetWindow().GetContext();
    m_Batch = m_Factory.CreateSpriteBatch(*ctx);
    m_BatchShader = m_Factory.CreateShader(
        "assets/shaders/sprite_batch.vert",
        "assets/shaders/sprite_batch.frag");
    m_Tex = m_TexMgr.Load("assets/textures/test.png");

    // ── M4-A: 仅当存在已保存场景时才恢复（含脚本）；否则空场景启动 ──
    if (std::filesystem::exists(kScenePath)) {
        AppendLog("[scene] saved scene found - restoring...");
        AppendLog("[script] loading assets/scripts/sandbox_player.lua");
        if (m_Player.Initialize("assets/scripts/sandbox_player.lua")) {
            m_Player.OnCreate();
            m_Bindings[1] = {};
            AppendLog("[script] loaded OK");
        }
        LoadScene();
    } else {
        AppendLog("[M4] Empty scene ready. Use Hierarchy to create entities.");
    }
}

void ScriptSandboxApp::OnUpdate(float32 dt) {
    if (Input::IsKeyPressed(KeyCode::F5)) {
        AppendLog("[reload] F5 pressed");
        if (m_Player.Reload())
            AppendLog("[reload] OK - handle preserved, new code active");
        else
            AppendLog("[reload] FAILED");
    }
    m_Player.OnUpdate(dt);
}

void ScriptSandboxApp::OnRender() {
    auto* ctx = GetWindow().GetContext();
    ctx->ClearColor(0.08f, 0.09f, 0.12f, 1.0f);

    m_BatchShader->Bind();
    m_BatchShader->SetMat4("u_ViewProjection", m_Camera.GetViewProjectionMatrixPtr());
    m_Batch->Begin(m_Tex);

    float px = 2.f, py = 0.f, pz = 0.f;
    Scripting::GameplayAPI::HandleGetPosition(1, px, py, pz);

    {   // Cube（静态参照）
        SpriteData cube;
        cube.transform.x = -3.0f;
        cube.transform.y = 0.0f;
        cube.transform.scaleX = 0.8f;
        cube.transform.scaleY = 0.8f;
        cube.colorB = 1.0f;
        m_Batch->Draw(cube);
    }
    {   // Player（真实 GameObject —— 链路终点可视化）
        SpriteData player;
        player.transform.x = px;
        player.transform.y = py;
        player.transform.scaleX = 0.6f;
        player.transform.scaleY = 0.6f;
        player.colorG = 1.0f;
        m_Batch->Draw(player);
    }

    m_Batch->End();
}

// ═══════════════ Ring13: Editor Workflow v1 ═══════════════

void ScriptSandboxApp::DrawHierarchyPanel() {
    ImGui::Begin("Hierarchy");
    if (ImGui::Button("Create Entity")) {
        const auto h = Scripting::GameplayAPI::HandleSpawn(
            "Entity_" + std::to_string(Scripting::GameplayAPI::HandleCount() + 1));
        m_Bindings[h] = {};
        m_Selected = h;
        AppendLog("[hierarchy] created entity " + std::to_string(h));
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete Selected") && m_Selected) {
        Scripting::GameplayAPI::HandleDestroy(m_Selected);
        m_Bindings.erase(m_Selected);
        AppendLog("[hierarchy] deleted entity " + std::to_string(m_Selected));
        m_Selected = 0;
    }
    ImGui::Separator();

    for (const auto& obj : m_Scene.GetObjects()) {
        uint32_t h = 0;
        for (auto& [hh, eb] : m_Bindings)
            if (Scripting::GameplayAPI::HandleGetName(hh) == obj->GetName()) { h = hh; break; }
        bool sel = (m_Selected == h);
        if (ImGui::Selectable((obj->GetName() + "  [" + std::to_string(h) + "]").c_str(), sel))
            m_Selected = h;
    }
    ImGui::End();
}

void ScriptSandboxApp::DrawInspectorPanel() {
    ImGui::Begin("Inspector");
    if (!m_Selected) { ImGui::TextDisabled("No selection"); ImGui::End(); return; }

    float x = 0, y = 0, z = 0;
    Scripting::GameplayAPI::HandleGetPosition(m_Selected, x, y, z);
    float pos[3] = { x, y, z };
    if (ImGui::DragFloat3("Position", pos, 0.05f))
        Scripting::GameplayAPI::HandleSetPosition(m_Selected, pos[0], pos[1], pos[2]);

    auto it = m_Bindings.find(m_Selected);
    if (it == m_Bindings.end())
        it = m_Bindings.emplace(m_Selected, Content::EntityContentBinding{}).first;
    Content::EntityContentBinding& b = it->second;

    ImGui::Separator();
    ImGui::Text("Sprite GUID: %s",
                b.spriteGuid.IsNull() ? "<none>" : b.spriteGuid.ToHex().c_str());
    ImGui::Text("Script GUID: %s",
                b.scriptGuid.IsNull() ? "<none>" : b.scriptGuid.ToHex().c_str());

    static char texPath[192] = "assets/textures/test.png";
    ImGui::InputText("Sprite asset", texPath, sizeof(texPath));
    ImGui::SameLine();
    if (ImGui::Button("Assign##tex")) {
        b.spriteGuid = m_Registry.Import(texPath, Content::AssetType::Texture);
        AppendLog("[inspector] sprite assigned: " + std::string(texPath));
    }

    static char scrPath[192] = "assets/scripts/sandbox_player.lua";
    ImGui::InputText("Script asset", scrPath, sizeof(scrPath));
    ImGui::SameLine();
    if (ImGui::Button("Assign##scr")) {
        b.scriptGuid = m_Registry.Import(scrPath, Content::AssetType::Script);
        AppendLog("[inspector] script assigned: " + std::string(scrPath));
    }

    ImGui::End();
}

// ── Scene Save / Load（复用 SceneSerializerV1 —— 不产生第二套格式）──

void ScriptSandboxApp::SaveScene() {
    std::vector<Content::EntityContentBinding> ordered;
    const auto& objs = m_Scene.GetObjects();
    ordered.reserve(objs.size());
    for (const auto& o : objs) {
        Content::EntityContentBinding b;
        for (auto& [h, eb] : m_Bindings)
            if (Scripting::GameplayAPI::HandleGetName(h) == o->GetName()) { b = eb; break; }
        ordered.push_back(b);
    }

    auto snap = Content::CaptureScene(m_Scene, ordered);
    if (Content::SaveSnapshotToFile(snap, kScenePath) &&
        m_Registry.SaveManifest(kManifestPath))
        AppendLog("[scene] saved: " + std::to_string(snap.entities.size()) + " entities");
    else
        AppendLog("[scene] SAVE FAILED");
}

void ScriptSandboxApp::LoadScene() {
    Content::SceneSnapshot snap;
    std::string err;
    if (!Content::LoadSnapshotFromFile(kScenePath, snap, err)) {
        AppendLog("[scene] LOAD FAILED: " + err);
        return;
    }
    if (m_Registry.LoadManifest(kManifestPath))
        AppendLog("[registry] manifest reloaded (" + std::to_string(m_Registry.Count()) + ")");

    // 清空现有句柄与场景（干净重建语义，与 R11/R12 已验证路径一致）
    for (auto& [h, eb] : m_Bindings)
        Scripting::GameplayAPI::HandleDestroy(h);
    m_Bindings.clear();
    m_Scene.Clear();
    m_Selected = 0;

    auto r = Content::InstantiateScene(snap, m_Scene, m_TexMgr, m_Registry);
    if (!r.ok) { AppendLog("[scene] instantiate FAILED"); return; }

    // 领养加载产物进句柄体系并重建绑定
    const auto& objs = m_Scene.GetObjects();
    for (size_t i = 0; i < objs.size(); ++i) {
        const auto h = Scripting::GameplayAPI::HandleAdopt(objs[i]);
        m_Bindings[h] = r.bindings[i];
    }

    // Player 脚本重绑：新句柄写入其 _PERSIST.h
    for (auto& [h, b] : m_Bindings)
        if (Scripting::GameplayAPI::HandleGetName(h) == "Player") {
            m_Player.Execute("if _PERSIST then _PERSIST.h = "
                             + std::to_string(h) + " end");
            break;
        }

    AppendLog("[scene] loaded: " + std::to_string(snap.entities.size()) + " entities");
}

// ── Console ─────────────────────────────────────────────

// ── M3-A/M4-C: Asset Browser ──

void ScriptSandboxApp::DrawAssetBrowser() {
    ImGui::Begin("Asset Browser");

    // Toolbar
    ImGui::InputText("##search", m_AssetSearch, sizeof(m_AssetSearch));
    ImGui::SameLine();
    const char* fl[] = {"All","Texture","Script"};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) ImGui::SameLine();
        if (ImGui::Button(fl[i])) m_AssetFilter = i;
    }
    ImGui::SameLine();
    if (ImGui::Button("Import...")) {
        auto path = FileDialog::OpenFile(
            "Textures (*.png)\0*.png\0Scripts (*.lua)\0*.lua\0All\0*.*\0");
        if (!path.empty()) {
            auto type = (path.size() > 4 && path.substr(path.size()-4) == ".lua")
                ? Content::AssetType::Script : Content::AssetType::Texture;
            m_Registry.Import(path, type);
            AppendLog("[import] " + path);
        }
    }
    ImGui::Separator();

    // Asset list
    auto entries = m_Registry.GetAllEntries();
    std::string q(m_AssetSearch);
    int shown = 0;
    for (auto& e : entries) {
        bool ok = q.empty() || e.path.find(q) != std::string::npos;
        bool tok = (m_AssetFilter == 0) ||
                   (m_AssetFilter == 1 && e.type == Content::AssetType::Texture) ||
                   (m_AssetFilter == 2 && e.type == Content::AssetType::Script);
        if (!ok || !tok) continue;
        ++shown;
        ImGui::PushID(static_cast<int>(e.guid.low));
        if (ImGui::Selectable(e.path.c_str(), false,
                              ImGuiSelectableFlags_AllowDoubleClick)) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                if (e.type == Content::AssetType::Script &&
                    e.path.size() > 4) {
                    OpenScriptEditor(e.path);
                }
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::TextDisabled("%d assets shown", shown);
    ImGui::End();
}
void ScriptSandboxApp::DrawConsolePanel() {
    ImGui::SetNextWindowSize(ImVec2(520, 280), ImGuiCond_FirstUseEver);
    ImGui::Begin("Script Console");
    ImGui::Text("api_version=%.1f entities=%u selected=%u",
                Scripting::ScriptAPI::GetApiVersion(),
                Scripting::GameplayAPI::HandleCount(), m_Selected);
    ImGui::Separator();

    ImGui::BeginChild("scroll", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()),
                      false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& l : m_Scrollback) ImGui::TextUnformatted(l.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    bool exec = ImGui::InputText("##cmd", m_CmdBuf, sizeof(m_CmdBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    exec |= ImGui::Button("Run");
    if (exec && m_CmdBuf[0] != '\0') {
        AppendLog("> " + std::string(m_CmdBuf));
        if (m_Player.Execute(m_CmdBuf))
            AppendLog("[ok]");
        else
            AppendLog("[error] " + m_Player.GetEngine()->GetLastError().message);
        m_CmdBuf[0] = '\0';
    }
    ImGui::End();
}

void ScriptSandboxApp::OnImGui() {
    DrawHierarchyPanel();
    DrawInspectorPanel();

    ImGui::Begin("Scene");
    if (ImGui::Button("Save Scene")) SaveScene();
    ImGui::SameLine();
    if (ImGui::Button("Load Scene")) LoadScene();
    ImGui::Separator();
    ImGui::Text("WASD move | F5 reload | Restart restores saved scene");
    ImGui::End();

    DrawConsolePanel();
    DrawAssetBrowser();

    // ── M005: Runtime HUD（Engine.ui.text 的可见消费端）──
    {
        const char* hud = Scripting::ScriptAPI::GetHudText();
        if (hud && hud[0]) {
            ImVec2 disp = ImGui::GetIO().DisplaySize;
            ImVec2 ts = ImGui::CalcTextSize(hud);
            ImGui::SetNextWindowPos(
                ImVec2((disp.x - ts.x) * 0.5f, disp.y * 0.08f));
            ImGui::SetNextWindowBgAlpha(0.6f);
            ImGui::Begin("##runtime_hud", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 1.0f, 0.3f, 1.0f));
            ImGui::TextUnformatted(hud);
            ImGui::PopStyleColor();
            ImGui::End();
            Scripting::ScriptAPI::ClearHudText();
        }
    }
}


// ── M4-B: Script Editor MVP ──

void ScriptSandboxApp::OpenScriptEditor(const std::string& path) {
    m_Ed->scriptPath = path;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    size_t copyLen = content.size() < sizeof(m_Ed->scriptBuf)-1 ? content.size() : sizeof(m_Ed->scriptBuf)-1;
    memcpy(m_Ed->scriptBuf, content.c_str(), copyLen);
    m_Ed->scriptBuf[copyLen] = 0;
    m_Ed->scriptDirty = false;
    m_Ed->scriptPanelOpen = true;
}

void ScriptSandboxApp::DrawScriptEditor() {
    if (!m_ScriptPath.empty()) {
        // Panel is open
    }
    ImGui::SetNextWindowSize(ImVec2(640, 480), ImGuiCond_FirstUseEver);
    ImGui::Begin("Script Editor", nullptr);

    // Path display
    ImGui::Text("%s%s", m_ScriptPath.c_str(), m_ScriptDirty ? " *" : "");
    ImGui::Separator();

    // Editable script content
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
    bool edited = ImGui::InputTextMultiline(
        "##script_src", m_ScriptBuf, sizeof(m_ScriptBuf),
        ImVec2(-1, -ImGui::GetFrameHeightWithSpacing() * 2), flags);
    if (edited) m_ScriptDirty = true;

    // Buttons
    if (ImGui::Button("Save")) {
        std::ofstream f(m_ScriptPath, std::ios::binary | std::ios::trunc);
        f.write(m_ScriptBuf, strlen(m_ScriptBuf));
        f.close();
        m_ScriptDirty = false;
        AppendLog("[editor] saved: " + m_ScriptPath);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save & Reload")) {
        std::ofstream f(m_ScriptPath, std::ios::binary | std::ios::trunc);
        f.write(m_ScriptBuf, strlen(m_ScriptBuf));
        f.close();
        m_ScriptDirty = false;
        if (m_Player.Reload())
            AppendLog("[editor] saved + reloaded OK");
        else
            AppendLog("[editor] reload FAILED: " + m_Player.GetEngine()->GetLastError().message);
    }

    // Error display
    ImGui::Separator();
    const auto& lastErr = m_Player.GetEngine()->GetLastError();
    if (!lastErr.message.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
        ImGui::TextWrapped("Error: %s", lastErr.message.c_str());
        ImGui::PopStyleColor();
    }

    ImGui::End();
}
} // namespace Engine::Sandbox
