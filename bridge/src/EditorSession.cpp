/**
 * @file EditorSession.cpp
 * @brief AV-005 防腐层实现 —— 只走 GP01ProductionSession 已验证的无头契约
 */

#include "EditorSession.h"
#include "editor_bridge/capi.h"

#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/Log.h>
#include <Engine/Scripting/GameplayAPI.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

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
    m_Dirty = false;

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
    m_Dirty = false;
    Engine::Log::Info("[EditorBridge] project saved: {} ({} entities)",
                      m_ScenePath, live.entities.size());
    Emit(EV_PROJECT_SAVED,
         "entities=" + std::to_string(live.entities.size()));
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
    MarkDirty();
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
    MarkDirty();
    Emit(EV_ENTITY_MOVED,
         "idx=" + std::to_string(index) +
         ";x=" + std::to_string(pos3[0]) +
         ";y=" + std::to_string(pos3[1]) +
         ";z=" + std::to_string(pos3[2]));
    return true;
}

bool EditorSession::DeleteEntity(int32_t index) {
    if (IsPlaying()) {
        return Fail("delete entity ignored while playing (edit-state only)");
    }
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("delete entity: bad index " + std::to_string(index));

    const auto& objs = m_EditScene->GetObjects();
    const auto target = objs[static_cast<size_t>(index)].get();
    std::string name = target->GetName();
    if (!m_EditScene->RemoveObject(target))
        return Fail("delete entity: remove failed for " + name);
    RealignBindings();
    MarkDirty();
    Engine::Log::Info("[EditorBridge] entity deleted: {} (was idx {})",
                      name, index);
    Emit(EV_ENTITY_DELETED,
         "idx=" + std::to_string(index) + ";name=" + name);
    return true;
}

bool EditorSession::RenameEntity(int32_t index, const std::string& newName) {
    if (IsPlaying()) {
        return Fail("rename entity ignored while playing (edit-state only)");
    }
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("rename entity: bad index " + std::to_string(index));

    // 空名 / 全空白拒绝（避免语义退化为匿名实体）
    std::string cleaned = newName;
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    if (cleaned.empty() ||
        std::all_of(cleaned.begin(), cleaned.end(), isSpace))
        return Fail("rename entity: empty name rejected");

    const auto& objs = m_EditScene->GetObjects();
    auto obj = objs[static_cast<size_t>(index)].get();
    // 重名拒绝（大小写敏感），但与自身同名允许
    if (obj->GetName() != cleaned && m_EditScene->FindObject(cleaned) != nullptr)
        return Fail("rename entity: duplicate name \"" + cleaned + "\"");

    std::string old = obj->GetName();
    obj->SetName(cleaned);
    MarkDirty();
    Engine::Log::Info("[EditorBridge] entity renamed: {} -> {}", old, cleaned);
    Emit(EV_ENTITY_RENAMED,
         "idx=" + std::to_string(index) + ";old=" + old + ";new=" + cleaned);
    return true;
}

bool EditorSession::GetEntitySprite(int32_t index, std::string* out) const {
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const Engine::Content::EntityContentBinding& b =
        m_Bindings[static_cast<size_t>(index)];
    if (b.spriteGuid.IsNull()) { *out = ""; return true; }
    *out = m_Reg.ResolvePath(b.spriteGuid);
    return true;
}

bool EditorSession::AssignSprite(int32_t assetIndex, int32_t entityIndex) {
    if (IsPlaying()) {
        return Fail("assign sprite ignored while playing (edit-state only)");
    }
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("assign sprite: bad asset index " +
                    std::to_string(assetIndex));
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("assign sprite: bad entity index " +
                    std::to_string(entityIndex));

    // 注意：GetAllEntries() 按值返回临时 vector，引用会悬垂（ASan 捕获），
    // 必须按值拷贝条目再使用。
    const auto entry = m_Reg.GetAllEntries()[static_cast<size_t>(assetIndex)];
    if (entry.type != Engine::Content::AssetType::Texture)
        return Fail("assign sprite: asset is not a texture");

    const auto& objs = m_EditScene->GetObjects();
    auto obj = objs[static_cast<size_t>(entityIndex)].get();
    // 复刻 GP01ProductionSession::AssignSpriteToSelected 的 DL-02 纪律路径
    if (!obj->HasComponent<Engine::SpriteComponent>())
        obj->AddComponent<Engine::SpriteComponent>(m_TexMgr, entry.path);
    else
        obj->GetComponent<Engine::SpriteComponent>()
           ->SetTexture(m_TexMgr, entry.path);
    RealignBindings();
    m_Bindings[static_cast<size_t>(entityIndex)].spriteGuid = entry.guid;
    MarkDirty();
    Engine::Log::Info("[EditorBridge] sprite assigned: {} -> {}",
                      obj->GetName(), entry.path);
    Emit(EV_ENTITY_ASSIGNED,
         "idx=" + std::to_string(entityIndex) +
         ";asset=" + std::to_string(assetIndex) +
         ";sprite=" + entry.path);
    return true;
}

bool EditorSession::GetAssetGuid(int32_t index, std::string* out) const {
    if (index < 0 || index >= static_cast<int32_t>(m_Reg.Count())) return false;
    *out = m_Reg.GetAllEntries()[static_cast<size_t>(index)].guid.ToHex();
    return true;
}

bool EditorSession::ScriptRead(int32_t assetIndex, std::string* out) {
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return false;
    // 同上：GetAllEntries() 临时 vector 的引用悬垂，按值拷贝。
    const auto entry = m_Reg.GetAllEntries()[static_cast<size_t>(assetIndex)];
    if (entry.type != Engine::Content::AssetType::Script)
        return false;
    const std::string filePath = ResolveContentPath(entry.path);
    std::ifstream f(filePath, std::ios::binary);
    if (!f.good()) {
        Fail("script read failed (cannot open): " + filePath);
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool EditorSession::ScriptSave(int32_t assetIndex, const std::string& text) {
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("script save: bad asset index " +
                    std::to_string(assetIndex));
    // 同上：GetAllEntries() 临时 vector 的引用悬垂，按值拷贝。
    const auto entry = m_Reg.GetAllEntries()[static_cast<size_t>(assetIndex)];
    if (entry.type != Engine::Content::AssetType::Script)
        return Fail("script save: asset is not a script");
    const std::string filePath = ResolveContentPath(entry.path);
    std::ofstream f(filePath, std::ios::binary | std::ios::trunc);
    if (!f.good())
        return Fail("script save failed (cannot open): " + filePath);
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f.good())
        return Fail("script save failed (write error): " + filePath);
    // AUD-1：以真实写盘结果为准；成功才算完成（并在会话层面置 dirty）
    MarkDirty();
    Engine::Log::Info("[EditorBridge] script saved: {}", filePath);
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

void EditorSession::MarkDirty() {
    m_Dirty = true;
}

std::string EditorSession::ResolveContentPath(const std::string& path) const {
    // 与 GP01 运行时一致：registry path 相对当前工作目录（进程 CWD）解析。
    // 引擎契约不含绝对路径/VFS；保持该语义，scratch 工程由宿主以 CWD 定向。
    fs::path p(path);
    if (p.is_absolute()) return p.string();
    std::error_code ec;
    fs::path abs = fs::absolute(p, ec);
    return (ec ? p : abs).lexically_normal().string();
}

bool EditorSession::Fail(const std::string& msg) {
    m_LastError = msg;
    Engine::Log::Error("[EditorBridge] {}", msg);
    return false;
}

} // namespace editor_bridge
