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
#include <Engine/Scripting/ScriptInstance.h>
#include <Engine/Scripting/LuaEngine.h>

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

    // 若存在运行态，先无损拆卸（镜像 GP01 LoadProject 的 TeardownRuntime）
    if (m_Playing) { Stop(); m_RuntimeError.clear(); }

    auto scene = std::make_shared<Engine::Scene>("AvaloniaSession");
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(scene.get());
    auto r = Engine::Content::InstantiateScene(snap, *scene, m_TexMgr, reg);
    if (!r.ok)
        return Fail("instantiate failed: " + scenePath);
    // E3 负路径：契约内缺失 GUID 资产的非致命告警（实体保留），拼接供读取
    m_Warnings.clear();
    for (const auto& w : r.warnings) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += w;
    }

    m_Reg = std::move(reg);
    m_EditScene = std::move(scene);
    m_Bindings = std::move(r.bindings);
    m_ScenePath = scenePath;
    m_ManifestPath = manifestPath;
    m_Dirty = false;
    // 会话稳定序：以本次打开时的注册表快照建立（此后只 append）
    m_AssetOrder.clear();
    for (const auto& e : m_Reg.GetAllEntries())
        m_AssetOrder.push_back(e.guid);

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

    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))   // 稳定索引（会话序）
        return Fail("assign sprite: bad asset index " +
                    std::to_string(assetIndex));
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

bool EditorSession::GetEntityScript(int32_t index, std::string* out) const {
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const Engine::Content::EntityContentBinding& b =
        m_Bindings[static_cast<size_t>(index)];
    if (b.scriptGuid.IsNull()) { *out = ""; return true; }
    *out = m_Reg.ResolvePath(b.scriptGuid);
    return true;
}

bool EditorSession::AssignScript(int32_t assetIndex, int32_t entityIndex) {
    if (IsPlaying()) {
        return Fail("assign script ignored while playing (edit-state only)");
    }
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("assign script: bad asset index " +
                    std::to_string(assetIndex));
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("assign script: bad entity index " +
                    std::to_string(entityIndex));

    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))   // 稳定索引（会话序）
        return Fail("assign script: bad asset index " +
                    std::to_string(assetIndex));
    if (entry.type != Engine::Content::AssetType::Script)
        return Fail("assign script: asset is not a script");

    const auto& objs = m_EditScene->GetObjects();
    const auto& obj = objs[static_cast<size_t>(entityIndex)];
    // 实体级脚本绑定是契约内数据（场景 JSON "script" 字段），只写 binding 表；
    // 运行时（Play）按 GP01 语义消费第一个非空 scriptGuid 作 director。
    RealignBindings();
    m_Bindings[static_cast<size_t>(entityIndex)].scriptGuid = entry.guid;
    MarkDirty();
    Engine::Log::Info("[EditorBridge] script assigned: {} -> {}",
                      obj->GetName(), entry.path);
    Emit(EV_ENTITY_SCRIPT_ASSIGNED,
         "idx=" + std::to_string(entityIndex) +
         ";asset=" + std::to_string(assetIndex) +
         ";script=" + entry.path);
    return true;
}

bool EditorSession::GetAssetGuid(int32_t index, std::string* out) const {
    Engine::Content::AssetEntry e;
    if (!AssetAt(index, &e)) return false;
    *out = e.guid.ToHex();
    return true;
}

// ════════════════════════════════════════════════════════════
// Phase 3-C：Asset Browser（Import / Rename，引擎零改动）
// ════════════════════════════════════════════════════════════

int32_t EditorSession::ImportAsset(const std::string& path, int32_t type) {
    if (!m_EditScene) { Fail("import asset: no project loaded"); return -1; }
    if (path.empty()) { Fail("import asset: empty path"); return -1; }
    using AT = Engine::Content::AssetType;
    AT t;
    if (type == 0) t = AT::Texture;
    else if (type == 1) t = AT::Script;
    else { Fail("import asset: bad type " + std::to_string(type)); return -1; }

    // 文件必须真实存在（避免登记不存在的资产造成假成功，GP-DX-007 纪律）
    const std::string filePath = ResolveContentPath(path);
    std::error_code ec;
    if (!fs::is_regular_file(filePath, ec) || ec) {
        Fail("import asset: file not found: " + filePath);
        return -1;
    }

    // ContentRegistry::Import 幂等：同路径返回既有 GUID
    const Engine::ResourceGUID guid = m_Reg.Import(path, t);
    MarkDirty();
    // 会话稳定序：新 GUID 只 append，既有索引不位移
    if (std::find(m_AssetOrder.begin(), m_AssetOrder.end(), guid) ==
        m_AssetOrder.end())
        m_AssetOrder.push_back(guid);
    int32_t idx = -1;
    for (size_t i = 0; i < m_AssetOrder.size(); ++i)
        if (m_AssetOrder[i] == guid) { idx = static_cast<int32_t>(i); break; }
    Engine::Log::Info("[EditorBridge] asset imported: {} ({})", path,
                      Engine::Content::ToString(t));
    Emit(EV_ASSET_IMPORTED,
         "idx=" + std::to_string(idx) + ";guid=" + guid.ToHex() +
         ";path=" + path + ";type=" + std::to_string(type));
    return idx;
}

bool EditorSession::RenameAsset(int32_t assetIndex, const std::string& newName) {
    if (!m_EditScene)
        return Fail("rename asset: no project loaded");
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("rename asset: bad asset index " +
                    std::to_string(assetIndex));
    std::string cleaned = newName;
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    if (cleaned.empty() ||
        std::all_of(cleaned.begin(), cleaned.end(), isSpace))
        return Fail("rename asset: empty name rejected");

    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))   // 稳定索引（会话序）
        return Fail("rename asset: bad asset index " +
                    std::to_string(assetIndex));
    // 只允许改 basename（不含路径分隔符/扩展名语义由调用方给裸名）
    if (cleaned.find_first_of("/\\") != std::string::npos)
        return Fail("rename asset: name must be a bare filename");

    const fs::path oldPath(ResolveContentPath(entry.path));
    const std::string ext = oldPath.extension().string();
    const fs::path newPath = oldPath.parent_path() / (cleaned + ext);

    // 物理改名（失败即止，不污染注册表）
    std::error_code ec;
    fs::rename(oldPath, newPath, ec);
    if (ec)
        return Fail("rename asset: fs rename failed: " + ec.message());

    // 注册表路径更新：GUID 不变 → 场景绑定稳定（DL-02 契约）
    m_Reg.Unregister(entry.guid);
    const std::string newRel = fs::relative(newPath, fs::current_path(ec), ec)
                                   .lexically_normal().generic_string();
    if (ec || !m_Reg.RegisterExplicit(entry.guid, newRel, entry.type)) {
        // 回滚物理改名，保持一致性
        fs::rename(newPath, oldPath, ec);
        return Fail("rename asset: registry update failed (rolled back)");
    }
    MarkDirty();
    Engine::Log::Info("[EditorBridge] asset renamed: {} -> {}",
                      entry.path, newRel);
    Emit(EV_ASSET_RENAMED,
         "idx=" + std::to_string(assetIndex) + ";guid=" + entry.guid.ToHex() +
         ";old=" + entry.path + ";new=" + newRel);
    return true;
}

bool EditorSession::ScriptRead(int32_t assetIndex, std::string* out) {
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return false;
    Engine::Content::AssetEntry entry;   // 稳定索引 + 按值拷贝（ASan 纪律）
    if (!AssetAt(assetIndex, &entry) ||
        entry.type != Engine::Content::AssetType::Script)
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
    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))
        return Fail("script save: bad asset index " +
                    std::to_string(assetIndex));
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

// ════════════════════════════════════════════════════════════
// Phase 3-D (P3-D) Script Editor 运行时：Play / Reload / Stop
// 镜像 GP01ProductionSession::Play（docs/GP1-DX-Production-UI-Plan.md 契约）：
//   编辑场景快照 → 克隆新场景 → GameplayAPI 绑定 → 解析导演脚本 →
//   ScriptInstance::Initialize → OnCreate。Reload 走 ScriptInstance::Reload
//   （保留 _PERSIST 表 S4）。错误一律 pcall 捕获，Session 不崩溃。
// ════════════════════════════════════════════════════════════

bool EditorSession::Play(int32_t assetIndex) {
    m_RuntimeError.clear();
    if (m_Playing) return Fail("play: already playing (stop first)");
    if (!m_EditScene) return Fail("play: no project loaded");

    RealignBindings();
    Engine::Content::SceneSnapshot live =
        Engine::Content::CaptureScene(*m_EditScene, m_Bindings);

    // ── 解析要运行的脚本路径 ──
    std::string scriptPath;
    if (assetIndex >= 0) {
        Engine::Content::AssetEntry entry;
        if (!AssetAt(assetIndex, &entry))
            return Fail("play: bad asset index " + std::to_string(assetIndex));
        if (entry.type != Engine::Content::AssetType::Script)
            return Fail("play: asset is not a script");
        scriptPath = entry.path;
    } else {
        for (const auto& b : m_Bindings)
            if (!b.scriptGuid.IsNull()) {
                scriptPath = m_Reg.ResolvePath(b.scriptGuid);
                break;
            }
        if (scriptPath.empty())
            return Fail("play: no script binding -> cannot play");
    }
    m_RuntimeScriptPath = ResolveContentPath(scriptPath);

    // ── 克隆编辑场景为新建运行场景 ──
    auto rt = std::make_shared<Engine::Scene>("AvaloniaRuntime");
    auto r = Engine::Content::InstantiateScene(live, *rt, m_TexMgr, m_Reg);
    if (!r.ok) return Fail("play: scene clone failed");

    // ── 绑定 GameplayAPI 到运行场景并登记句柄 ──
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(rt.get());
    for (const auto& o : rt->GetObjects())
        Engine::Scripting::GameplayAPI::HandleAdopt(o);

    // ── 加载并执行导演脚本（指令预算防死循环，沙箱已开）──
    Engine::Scripting::ScriptInstance::Config cfg;
    if (!m_Inst.Initialize(m_RuntimeScriptPath, cfg)) {
        m_RuntimeError = m_Inst.GetEngine()
                             ? m_Inst.GetEngine()->GetLastError().message
                             : "(no engine)";
        Engine::Scripting::GameplayAPI::Reset();
        Engine::Scripting::GameplayAPI::SetScene(m_EditScene.get());
        return Fail("play: director init failed: " + m_RuntimeError);
    }
    m_Inst.OnCreate();

    m_Runtime = std::move(rt);
    m_Playing = true;
    Engine::Log::Info("[EditorBridge] PLAY (script={})", m_RuntimeScriptPath);
    Emit(EV_PLAY_STARTED, "script=" + m_RuntimeScriptPath);
    return true;
}

bool EditorSession::Reload() {
    m_RuntimeError.clear();
    if (!m_Playing || !m_Inst.IsValid())
        return Fail("reload: not playing");
    if (!m_Inst.Reload()) {
        m_RuntimeError = m_Inst.GetEngine()
                             ? m_Inst.GetEngine()->GetLastError().message
                             : "(no engine)";
        return Fail("reload failed: " + m_RuntimeError);
    }
    m_Inst.OnCreate();
    Engine::Log::Info("[EditorBridge] RELOAD ok (persist preserved): {}",
                      m_RuntimeScriptPath);
    return true;
}

void EditorSession::Stop() {
    if (!m_Playing) return;
    m_Inst.OnDestroy();
    m_Inst.Shutdown();
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(m_EditScene.get());   // 回到编辑态
    m_Runtime.reset();
    m_RuntimeScriptPath.clear();
    m_Playing = false;
    Engine::Log::Info("[EditorBridge] STOP -> edit state restored");
    Emit(EV_PLAY_STOPPED, "");
}

void EditorSession::RuntimeTick(float dt) {
    if (!m_Playing || !m_Inst.IsValid()) return;
    m_Inst.OnUpdate(dt);
    // OnUpdate 内运行时错误（如接触伤害路径）已由 ScriptInstance 内部 pcall
    // 捕获并记录到 GetLastError；这里非阻断，Session 不崩。
}

int32_t EditorSession::RuntimePersistInt(const std::string& key, int32_t def) {
    if (!m_Playing || !m_Inst.IsValid() || !m_Inst.GetEngine()) return def;
    if (key.empty()) return def;
    // 探针：`__p3d_persist_probe = _PERSIST[key] or nil`（key 按安全标识符/裸键引用）
    const std::string code =
        std::string("__p3d_persist_probe = _PERSIST and _PERSIST[\"") +
        key + std::string("\"] or nil");
    if (!m_Inst.Execute(code)) return def;
    return m_Inst.GetEngine()->GetGlobalInt("__p3d_persist_probe", def);
}

bool EditorSession::GetAssetPath(int32_t index, std::string* out) const {
    Engine::Content::AssetEntry e;
    if (!AssetAt(index, &e)) return false;
    *out = e.path;
    return true;
}

int32_t EditorSession::GetAssetType(int32_t index) const {
    using AT = Engine::Content::AssetType;
    Engine::Content::AssetEntry e;
    if (!AssetAt(index, &e)) return -1;
    switch (e.type) {
        case AT::Texture: return 0;
        case AT::Script:  return 1;
        default:          return 2;
    }
}

bool EditorSession::AssetAt(int32_t index, Engine::Content::AssetEntry* out) const {
    if (!out || index < 0 ||
        index >= static_cast<int32_t>(m_AssetOrder.size()))
        return false;
    const Engine::ResourceGUID& guid = m_AssetOrder[static_cast<size_t>(index)];
    // 注册表按 GUID 查回条目（不依赖 unordered_map 迭代序）；按值拷贝
    // （GetAllEntries 临时 vector 引用会悬垂，ASan 纪律）
    auto entries = m_Reg.GetAllEntries();
    for (const auto& e : entries)
        if (e.guid == guid) { *out = e; return true; }
    return false;
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
