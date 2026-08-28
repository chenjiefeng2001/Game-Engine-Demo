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

bool EditorSession::GetEntityPosition(int32_t index, float out3[3]) const {
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const auto& t =
        m_EditScene->GetObjects()[static_cast<size_t>(index)]->GetTransform();
    out3[0] = t.GetPosition().x;
    out3[1] = t.GetPosition().y;
    out3[2] = t.GetPosition().z;
    return true;
}

bool EditorSession::SetEntityPosition(int32_t index, const float pos3[3]) {
    if (!pos3) { Fail("set position: null pos"); return false; }
    if (IsPlaying()) {        // 编辑态纪律（GP-DX-004 家族）：运行态写入拒绝
        Fail("set position ignored while playing (edit-state only)");
        return false;
    }
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("set position: bad index " + std::to_string(index));
    auto& t =
        m_EditScene->GetObjects()[static_cast<size_t>(index)]->GetTransform();
    t.SetPosition(pos3[0], pos3[1], pos3[2]);
    Emit(EV_ENTITY_MOVED,
         "idx=" + std::to_string(index) +
         ";x=" + std::to_string(pos3[0]) +
         ";y=" + std::to_string(pos3[1]) +
         ";z=" + std::to_string(pos3[2]));
    return true;
}

bool EditorSession::GetAssetPath(int32_t index, std::string* out) const {
    if (index < 0 || index >= static_cast<int32_t>(m_Reg.Count())) return false;
    *out = m_Reg.GetAllEntries()[static_cast<size_t>(index)].path;
    return true;
}

int32_t EditorSession::GetAssetType(int32_t index) const {
    using AT = Engine::Content::AssetType;
    if (index < 0 || index >= static_cast<int32_t>(m_Reg.Count()))
        return -1;
    switch (m_Reg.GetAllEntries()[static_cast<size_t>(index)].type) {
        case AT::Texture: return 0;
        case AT::Script:  return 1;
        default:          return 2;
    }
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
