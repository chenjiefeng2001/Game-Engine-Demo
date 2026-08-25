#pragma once

/**
 * @file GP01ProductionSession.h
 * @brief GP1-DX Production UI Foundation —— 把既有 Panel 接成生产闭环
 *
 * 职责（docs/GP1-DX-Production-UI-Plan.md §1 P0）：
 *   工程加载/保存（Content::ContentRegistry + SceneSerializerV1，记忆路径）
 *   ＋Entity（绑定表自动对齐）/ 内容分配（Assign Sprite → 选中实体）
 *   Play/Stop（内容管线克隆 → 冻结 API 驱动 game.lua）/ F5 热重载
 *   Script Editor（registry 内脚本打开/编辑/保存）
 *
 * 纪律：只使用冻结契约（Import/CaptureScene/InstantiateScene/GameplayAPI），
 *       不改引擎；渲染侧以 Billboard 呈现实体位置（已知限制 §3）。
 */

#include <Engine/Editor/EngineEditor.h>
#include <Engine/Core/Content/ContentAsset.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <Engine/OpenGL/OpenGLGraphicsFactory.h>
#include <Engine/Scripting/GameplayAPI.h>
#include <Engine/Scripting/ScriptInstance.h>
#include <Engine/Core/Input.h>

#include "../ScriptSandbox/GLFWScriptInputProvider.h"

#include <imgui.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace Engine {


class GP01ProductionSession {
public:
    /// onSceneReplaced：工程加载/Play/Stop 导致活动场景指针更换时回调
    /// （宿主用它重新接线 Hierarchy / SceneManager / Viewer / m_Scene）。
    void Init(EngineEditor* editor,
              std::function<void(std::shared_ptr<Scene>)> onSceneReplaced) {
        m_Editor = editor;
        m_OnSceneReplaced = std::move(onSceneReplaced);
        m_Input = std::make_unique<Sandbox::GLFWScriptInputProvider>();
    }

    // ── 工程 ──────────────────────────────────────────────
    bool LoadProject(const std::string& manifestPath,
                     const std::string& scenePath) {
        Content::ContentRegistry reg;
        if (!reg.LoadManifest(manifestPath)) {
            Log::Error("[GP01] manifest load failed: {}", manifestPath);
            return false;
        }
        Content::SceneSnapshot snap; std::string err;
        if (!Content::LoadSnapshotFromFile(scenePath, snap, err)) {
            Log::Error("[GP01] scene load failed: {} ({})", scenePath, err);
            return false;
        }
        auto scene = std::make_shared<Scene>("GP01");
        Scripting::GameplayAPI::Reset();
        Scripting::GameplayAPI::SetScene(scene.get());
        auto r = Content::InstantiateScene(snap, *scene, m_TexMgr, reg);
        if (!r.ok) {
            Log::Error("[GP01] instantiate failed");
            return false;
        }
        m_Reg = std::move(reg);
        m_EditScene = scene;
        m_Bindings = r.bindings;
        m_ScenePath = scenePath;
        m_ManifestPath = manifestPath;
        Stop();                                   // 清掉可能残留的运行态
        Log::Info("[GP01] project loaded: {} objects, {} assets",
                  scene->GetObjectCount(), m_Reg.Count());
        if (m_OnSceneReplaced) m_OnSceneReplaced(m_EditScene);
        return true;
    }

    bool SaveProject() {
        if (!m_EditScene) return false;
        RealignBindings();
        Content::SceneSnapshot live = Content::CaptureScene(*m_EditScene, m_Bindings);
        if (!Content::SaveSnapshotToFile(live, m_ScenePath)) return false;
        if (!m_Reg.SaveManifest(m_ManifestPath)) return false;
        Log::Info("[GP01] project saved: {} ({} entities)",
                  m_ScenePath, live.entities.size());
        return true;
    }

    // ── 运行时 ────────────────────────────────────────────
    bool IsPlaying() const { return m_Playing; }
    Scene* EditScene() { return m_EditScene.get(); }
    Scene* RenderScene() { return m_Playing ? m_Runtime.get() : m_EditScene.get(); }

    bool Play() {
        if (m_Playing || !m_EditScene) return false;
        RealignBindings();
        Content::SceneSnapshot live = Content::CaptureScene(*m_EditScene, m_Bindings);
        auto rt = std::make_shared<Scene>("GP01_Runtime");
        auto r = Content::InstantiateScene(live, *rt, m_TexMgr, m_Reg);
        if (!r.ok) { Log::Error("[GP01] play clone failed"); return false; }

        Scripting::GameplayAPI::Reset();
        Scripting::GameplayAPI::SetScene(rt.get());
        Scripting::GameplayAPI::SetInputProvider(m_Input.get());
        for (auto& o : rt->GetObjects()) Scripting::GameplayAPI::HandleAdopt(o);

        std::string director;
        for (auto& b : r.bindings)
            if (!b.scriptGuid.IsNull()) { director = m_Reg.ResolvePath(b.scriptGuid); break; }
        if (director.empty()) { Log::Error("[GP01] no script binding -> cannot play"); return false; }

        if (!m_Inst.Initialize(director, Scripting::ScriptInstance::Config{})) {
            Log::Error("[GP01] director init failed: {}",
                       m_Inst.GetEngine()->GetLastError().message);
            return false;
        }
        m_Inst.OnCreate();
        m_Runtime = rt;
        m_Playing = true;
        Log::Info("[GP01] PLAY ({})", director);
        if (m_OnSceneReplaced) m_OnSceneReplaced(m_Runtime);
        return true;
    }

    void Stop() {
        if (m_Playing) {
            m_Inst.OnDestroy();
            m_Inst.Shutdown();
            Scripting::GameplayAPI::Reset();
            m_Playing = false;
            m_Runtime.reset();
            Log::Info("[GP01] STOP -> edit state restored");
        }
        if (m_EditScene && m_OnSceneReplaced) m_OnSceneReplaced(m_EditScene);
    }

    void Tick(float dt) {
        static bool f5Prev = false;
        const bool f5 = Input::IsKeyDown(KeyCode::F5);
        if (f5 && !f5Prev) ReloadActiveScript();
        f5Prev = f5;
        if (m_Playing) m_Inst.OnUpdate(dt);
    }

    void ReloadActiveScript() {
        if (m_Playing && m_Inst.IsValid())
            Log::Info("[GP01] F5 reload: {}", m_Inst.Reload() ? "ok" : "failed");
        else
            Log::Info("[GP01] F5 ignored (not playing)");
    }

    // ── UI ────────────────────────────────────────────────
    void DrawWindows() {
        DrawProductionWindow();
        DrawContentWindow();
        DrawScriptWindow();
    }

private:
    void RealignBindings() {
        if (!m_EditScene) return;
        m_Bindings.resize(m_EditScene->GetObjectCount());   // 补 Null 绑定
    }

    int FindIndex(const GameObject* obj) const {
        const auto& objs = m_EditScene->GetObjects();
        for (size_t i = 0; i < objs.size(); ++i)
            if (objs[i].get() == obj) return static_cast<int>(i);
        return -1;
    }

    void AssignSpriteToSelected(const std::string& path, ResourceGUID guid) {
        auto sel = m_Editor->GetSelectedObject();
        if (!sel) { Log::Warn("[GP01] assign: nothing selected"); return; }
        if (!sel->HasComponent<SpriteComponent>())
            sel->AddComponent<SpriteComponent>(m_TexMgr, path);
        else
            sel->GetComponent<SpriteComponent>()->SetTexture(m_TexMgr, path);
        RealignBindings();
        int i = FindIndex(sel.get());
        if (i >= 0 && i < static_cast<int>(m_Bindings.size()))
            m_Bindings[static_cast<size_t>(i)].spriteGuid = guid;   // DL-02 纪律
        Log::Info("[GP01] sprite assigned: {} -> {}", sel->GetName(), path);
    }

    void CreateEntity() {
        if (!m_EditScene) return;
        const std::string base = "Entity";
        std::string name = base;
        int n = 1;
        while (m_EditScene->FindObject(name) != nullptr)
            name = base + "_" + std::to_string(++n);
        auto obj = std::make_shared<GameObject>(name);
        obj->GetTransform().SetPosition(0.f, 0.f, 0.f);
        m_EditScene->AddObject(obj);
        RealignBindings();
        Log::Info("[GP01] entity created: {}", name);
        m_Editor->OnSelectionChanged(obj.get());
    }

    void DrawProductionWindow() {
        if (!ImGui::Begin("GP01 Production")) { ImGui::End(); return; }
        ImGui::TextDisabled("project: %s", m_ScenePath.c_str());
        if (ImGui::Button("Open Project")) {
            LoadProject("assets/gp01/manifest.json", "assets/gp01/Main.scene");
        }
        ImGui::SameLine();
        if (ImGui::Button("Save Project")) SaveProject();
        ImGui::SameLine();
        if (!m_Playing) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.2f, 1.f));
            if (ImGui::Button("Play")) Play();
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.f));
            if (ImGui::Button("Stop")) Stop();
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::Button("Reload (F5)")) ReloadActiveScript();
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Entity")) CreateEntity();
        ImGui::TextDisabled("WASD move / J attack (Play) | selection syncs Inspector & Viewport");
        ImGui::End();
    }

    void DrawContentWindow() {
        if (!ImGui::Begin("Content (Registry)")) { ImGui::End(); return; }
        if (m_Reg.Count() == 0) { ImGui::TextDisabled("(no project loaded)"); ImGui::End(); return; }
        static ImGuiTextFilter filter;
        filter.Draw();
        for (const auto& e : m_Reg.GetAllEntries()) {
            if (!filter.PassFilter(e.path.c_str())) continue;
            ImGui::PushID(e.path.c_str());
            const char* tag = e.type == Content::AssetType::Texture ? "tex" :
                              e.type == Content::AssetType::Script  ? "lua" : "?";
            ImGui::Text("[%s]", tag);
            ImGui::SameLine();
            ImGui::TextUnformatted(e.path.c_str());
            if (e.type == Content::AssetType::Texture) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Assign Sprite")) {
                    AssignSpriteToSelected(e.path, e.guid);
                }
            }
            ImGui::PopID();
        }
        ImGui::End();
    }

    void DrawScriptWindow() {
        if (!ImGui::Begin("Script Editor")) { ImGui::End(); return; }
        if (m_Reg.Count() == 0) { ImGui::TextDisabled("(no project loaded)"); ImGui::End(); return; }

        std::vector<const Content::AssetEntry*> scripts;
        for (const auto& e : m_Reg.GetAllEntries())
            if (e.type == Content::AssetType::Script) scripts.push_back(&e);
        if (scripts.empty()) { ImGui::TextDisabled("(no scripts)"); ImGui::End(); return; }

        // GetAllEntries() 按值返回 —— 选择态以路径记录，避免悬垂指针
        const std::string curPath =
            (!m_ScriptPath.empty() &&
             std::any_of(scripts.begin(), scripts.end(),
                         [&](const Content::AssetEntry* s){ return s->path == m_ScriptPath; }))
                ? m_ScriptPath : std::string();
        if (ImGui::BeginCombo("##script", curPath.empty() ? "<pick>" : curPath.c_str())) {
            for (auto* s : scripts) {
                if (ImGui::Selectable(s->path.c_str(), s->path == curPath)) {
                    m_ScriptPath = s->path;
                    LoadBuffer(s->path);
                }
            }
            ImGui::EndCombo();
        }
        if (curPath.empty()) { ImGui::End(); return; }

        if (ImGui::Button("Load")) LoadBuffer(curPath);
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            std::ofstream f(curPath, std::ios::binary | std::ios::trunc);
            if (f.good()) { f.write(m_Buf, static_cast<std::streamsize>(strlen(m_Buf))); m_Dirty = false; }
            Log::Info("[GP01] script saved: {}", curPath);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reload (F5)")) ReloadActiveScript();
        ImGui::TextDisabled("%s%s", m_Dirty ? "* " : "", curPath.c_str());
        if (ImGui::InputTextMultiline("##code", m_Buf, sizeof(m_Buf),
                                      ImVec2(-1, -1)))
            m_Dirty = true;
        ImGui::End();
    }

    void LoadBuffer(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f.good()) return;
        std::memset(m_Buf, 0, sizeof(m_Buf));
        f.read(m_Buf, sizeof(m_Buf) - 1);
        m_Dirty = false;
    }

    OpenGLGraphicsFactory m_Gfx;                 // 会话自有工厂（与宿主解耦）
    TextureManager m_TexMgr{m_Gfx};
    Content::ContentRegistry m_Reg;
    std::shared_ptr<Scene> m_EditScene;
    std::shared_ptr<Scene> m_Runtime;
    std::vector<Content::EntityContentBinding> m_Bindings;
    Scripting::ScriptInstance m_Inst;
    std::unique_ptr<Sandbox::GLFWScriptInputProvider> m_Input;

    EngineEditor* m_Editor = nullptr;
    std::function<void(std::shared_ptr<Scene>)> m_OnSceneReplaced;

    std::string m_ScenePath = "(none)";
    std::string m_ManifestPath = "(none)";
    bool m_Playing = false;

    std::string m_ScriptPath;
    char m_Buf[65536] = {};
    bool m_Dirty = false;
};

} // namespace Engine
