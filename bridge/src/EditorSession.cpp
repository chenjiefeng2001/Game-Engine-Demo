/**
 * @file EditorSession.cpp
 * @brief AV-005 防腐层实现 —— 只走 GP01ProductionSession 已验证的无头契约
 */

#include "EditorSession.h"
#include "editor_bridge/capi.h"

#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/Log.h>
#include <Engine/Scripting/GameplayAPI.h>

namespace editor_bridge {

bool EditorSession::OpenProject(const std::string& manifestPath,
                                const std::string& scenePath) {
    Engine::Content::ContentRegistry reg;
    if (!reg.LoadManifest(manifestPath))
        return Fail("manifest load failed: " + manifestPath);

    Engine::Content::SceneSnapshot snap;
    std::string err;
    if (!Engine::Content::LoadSnapshotFromFile(scenePath, snap, err))
        return Fail("scene load failed: " + scenePath + " (" + err + ")");

    auto scene = std::make_shared<Engine::Scene>("AvaloniaSession");
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(scene.get());
    auto r = Engine::Content::InstantiateScene(snap, *scene, m_TexMgr, reg);
    if (!r.ok)
        return Fail("instantiate failed: " + scenePath);

    m_Reg = std::move(reg);
    m_EditScene = std::move(scene);
    m_Bindings = std::move(r.bindings);
    m_ScenePath = scenePath;
    m_ManifestPath = manifestPath;

    Engine::Log::Info("[EditorBridge] project loaded: {} objects, {} assets",
                      m_EditScene->GetObjectCount(), m_Reg.Count());
    Emit(EV_PROJECT_LOADED,
         "objects=" + std::to_string(m_EditScene->GetObjectCount()) +
         ";assets=" + std::to_string(static_cast<long long>(m_Reg.Count())));
    return true;
}

bool EditorSession::SaveProject() {
    if (!m_EditScene) return Fail("save: no project loaded");
    RealignBindings();
    Engine::Content::SceneSnapshot live = Engine::Content::CaptureScene(*m_EditScene, m_Bindings);
    if (!Engine::Content::SaveSnapshotToFile(live, m_ScenePath))
        return Fail("cannot write scene file: " + m_ScenePath);
    if (!m_Reg.SaveManifest(m_ManifestPath))
        return Fail("cannot write manifest: " + m_ManifestPath +
                    " (scene already written)");
    Engine::Log::Info("[EditorBridge] project saved: {} ({} entities)",
                      m_ScenePath, live.entities.size());
    return true;
}

int32_t EditorSession::GetEntityCount() const {
    return m_EditScene ? static_cast<int32_t>(m_EditScene->GetObjectCount())
                       : -1;
}

int32_t EditorSession::CreateEntity(const char* name) {
    if (!m_EditScene) { Fail("create entity: no project loaded"); return -1; }
    std::string base = (name && *name) ? name : "Entity";
    std::string unique = base;
    int n = 1;
    while (m_EditScene->FindObject(unique) != nullptr)
        unique = base + "_" + std::to_string(++n);
    auto obj = std::make_shared<Engine::GameObject>(unique);
    obj->GetTransform().SetPosition(0.f, 0.f, 0.f);
    m_EditScene->AddObject(obj);
    RealignBindings();
    Engine::Log::Info("[EditorBridge] entity created: {}", unique);
    Emit(EV_ENTITY_CREATED, unique);
    return static_cast<int32_t>(m_EditScene->GetObjectCount()) - 1;
}

bool EditorSession::GetEntityName(int32_t index, std::string* out) const {
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    *out = m_EditScene->GetObjects()[static_cast<size_t>(index)]->GetName();
    return true;
}

void EditorSession::Emit(int32_t type, const std::string& payload) {
    if (m_Event) m_Event(type, payload.c_str());
}

void EditorSession::RealignBindings() {
    if (!m_EditScene) return;
    m_Bindings.resize(m_EditScene->GetObjectCount());   // 补 Null 绑定
}

bool EditorSession::Fail(const std::string& msg) {
    m_LastError = msg;
    Engine::Log::Error("[EditorBridge] {}", msg);
    return false;
}

} // namespace editor_bridge
