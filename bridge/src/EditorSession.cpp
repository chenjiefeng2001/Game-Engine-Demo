/**
 * @file EditorSession.cpp
 * @brief AV-005 防腐层实现 —— 只走 GP01ProductionSession 已验证的无头契约
 */

#include "EditorSession.h"
#include "editor_bridge/capi.h"

#include <Engine/Application.h>
#include <Engine/Core/Content/AtomicFile.h>
#include <Engine/Core/Content/RevisionStore.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/Content/Utf8Path.h>
#include <Engine/Core/GameObject/GameObject.h>
#include <Engine/Core/GameObject/Component.h>
#include <Engine/Core/GameObject/ComponentRegistry_Go.h>
#include <Engine/Core/GameObject/SpriteComponent.h>
#include <Engine/Core/RHI/RenderQueue.h>
#include <Engine/Core/Physics/PhysicsRuntime2D.h>
#include <Engine/Core/Scene/Scene.h>
#include <Engine/Core/Log.h>
#include <Engine/Scripting/GameplayAPI.h>
#include <Engine/Scripting/LuaEngine.h>
#include <Engine/Scripting/ScriptAPI.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace editor_bridge {

EditorSession::EditorSession()
    : m_OwnedGfx(std::make_unique<Engine::OpenGLGraphicsFactory>())
    , m_Gfx(m_OwnedGfx.get())
    , m_TexMgr(*m_Gfx) {
}

EditorSession::EditorSession(Engine::OpenGLGraphicsFactory& factory,
                             Engine::Application* application)
    : m_Gfx(&factory)
    , m_Application(application)
    , m_TexMgr(factory) {
}

EditorSession::~EditorSession() {
    SetEventCallback(nullptr);
    if (m_Playing.load()) Stop();
    if (m_Physics) {
        m_Physics->Stop();
        m_Physics.reset();
    }
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::ScriptAPI::Reset();
    PublishRuntimeState();
    if (m_Application) m_Application->SetPlaying(false);
}

namespace {

    /// K4.7：链接 real path 等结构化诊断与错误码一起进入错误通道，
    /// 而不是只报一个无法定位的 LinkEscape。
    std::string DescribePathError(
        const Engine::Content::PathResolution& resolution) {
        std::string out = Engine::Content::ToString(resolution.error);
        if (!resolution.diagnostic.empty())
            out += " (" + resolution.diagnostic + ")";
        return out;
    }

    bool TestFailure(const char* name) {
        const char* value = std::getenv(name);
        return value && std::strcmp(value, "1") == 0;
    }

    std::string UniqueSuffix() {
        static std::atomic<uint64_t> counter{0};
        return std::to_string(
            static_cast<unsigned long long>(counter.fetch_add(1)));
    }

    bool LooksAbsolutePath(std::string_view path) {
        if (path.empty()) return false;
        if (path[0] == '/' || path[0] == '\\') return true;
        return path.size() >= 2 &&
               ((path[0] >= 'A' && path[0] <= 'Z') ||
                (path[0] >= 'a' && path[0] <= 'z')) &&
               path[1] == ':';
    }

    std::string WithForwardSlashesCopy(const std::string& input) {
        std::string out = input;
        for (char& c : out)
            if (c == '\\') c = '/';
        return out;
    }

    int32_t IndexOfGuid(const std::vector<Engine::ResourceGUID>& order,
                        const Engine::ResourceGUID& guid) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i] == guid) return static_cast<int32_t>(i);
        return -1;
    }

    std::string ScriptError(const Engine::Scripting::ScriptInstance& instance) {
        if (instance.GetEngine()) {
            return instance.GetEngine()->GetLastError().message;
        }
        return instance.GetLastError().empty() ? "(no engine)"
                                               : instance.GetLastError();
    }

    // ── 反射属性值 ↔ UTF-8 字符串（C ABI 边界）──

    std::string ComponentValueToString(const Engine::ComponentPropertyValue& v) {
        switch (v.type) {
            case Engine::ComponentValueType::Bool:   return v.boolValue ? "true" : "false";
            case Engine::ComponentValueType::Int:    return std::to_string(v.intValue);
            case Engine::ComponentValueType::Float:  {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.6g", v.floatValue);
                return buf;
            }
            case Engine::ComponentValueType::String: return v.stringValue;
        }
        return "";
    }

    bool ParseComponentValue(const std::string& str,
                             Engine::ComponentValueType type,
                             Engine::ComponentPropertyValue* out) {
        if (!out) return false;
        out->type = type;
        switch (type) {
            case Engine::ComponentValueType::Bool:
                if (str == "true" || str == "1") { out->boolValue = true; return true; }
                if (str == "false" || str == "0") { out->boolValue = false; return true; }
                return false;
            case Engine::ComponentValueType::Float:
                try { out->floatValue = std::stof(str); return true; }
                catch (...) { return false; }
            case Engine::ComponentValueType::Int:
                try { out->intValue = std::stoi(str); return true; }
                catch (...) { return false; }
            case Engine::ComponentValueType::String:
                out->stringValue = str; return true;
        }
        return false;
    }

    int FindPropertyIndex(Engine::Component* comp, const std::string& propName) {
        if (!comp) return -1;
        const size_t n = comp->GetPropertyCount();
        for (size_t i = 0; i < n; ++i) {
            Engine::ComponentPropertyDesc d;
            if (comp->GetPropertyDesc(i, &d) && d.name && propName == d.name)
                return static_cast<int>(i);
        }
        return -1;
    }
} // namespace

bool EditorSession::OpenProject(const std::string& manifestPath,
                                const std::string& scenePath) {
    // Legacy 兼容入口（K4.4）：CWD 在此一次性捕获为显式 legacy root，
    // 之后的解析全部锚定该根，不再二次读取进程工作目录。
    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (ec)
        return Fail("open project: process working directory unavailable: " +
                    ec.message());
    return OpenProjectInternal(cwd.generic_string(), manifestPath, scenePath);
}

bool EditorSession::OpenProjectRooted(const std::string& projectRoot,
                                      const std::string& manifestPath,
                                      const std::string& scenePath) {
    return OpenProjectInternal(projectRoot, manifestPath, scenePath);
}

bool EditorSession::OpenProjectInternal(const std::string& projectRoot,
                                        const std::string& manifestPath,
                                        const std::string& scenePath) {
    Engine::Content::ProjectRoot root;
    Engine::Content::PathError rootError = Engine::Content::PathError::None;
    std::string rootDiagnostic;
    if (!Engine::Content::ProjectRoot::Create(projectRoot, &root, &rootError,
                                              &rootDiagnostic)) {
        return Fail("open project: project root '" + projectRoot +
                    "' rejected: " +
                    Engine::Content::ToString(rootError) + " (" +
                    rootDiagnostic + ")");
    }

    const auto manifestRes = root.ResolveAny(manifestPath);
    if (!manifestRes.ok) {
        return Fail("open project: manifest path '" + manifestPath +
                    "' rejected: " + DescribePathError(manifestRes));
    }
    const auto sceneRes = root.ResolveAny(scenePath);
    if (!sceneRes.ok) {
        return Fail("open project: scene path '" + scenePath +
                    "' rejected: " + DescribePathError(sceneRes));
    }

    // M1-D：current 是唯一提交点。读取端只认 current，不扫描 revision 目录。
    Engine::Content::RevisionStore revisions;
    const auto revisionOpen = revisions.Open(root);
    if (revisionOpen.state ==
        Engine::Content::RevisionOpenState::RecoveryRequired) {
        return Fail("open project: RecoveryRequired: " +
                    revisionOpen.diagnostic);
    }
    const bool versioned =
        revisionOpen.state == Engine::Content::RevisionOpenState::Current;

    // M1-B：严格候选加载。revision 内清单必须是规范工程相对路径；
    // 旧工程（无 current）保留 K4.4 legacy 绝对路径兼容。
    std::vector<std::string> pathWarnings;
    Engine::Content::ManifestLoadOptions loadOptions;
    loadOptions.root = &root;
    loadOptions.allowLegacyAbsolute = !versioned;
    loadOptions.caseMode = root.CaseMode();
    loadOptions.warnings = &pathWarnings;

    const std::string manifestLoadPath =
        versioned ? revisionOpen.manifestPath : manifestRes.path;
    const std::string sceneLoadPath =
        versioned ? revisionOpen.scenePath : sceneRes.path;

    Engine::Content::ContentRegistry reg;
    if (!reg.LoadManifest(manifestLoadPath, loadOptions)) {
        if (versioned) {
            return Fail("open project: RecoveryRequired: revision " +
                        std::to_string(revisionOpen.revision) +
                        " manifest failed strict validation");
        }
        return Fail("manifest load failed (strict): " + manifestLoadPath);
    }

    Engine::Content::SceneSnapshot snap;
    std::string err;
    if (!Engine::Content::LoadSnapshotFromFile(sceneLoadPath, snap, err)) {
        if (versioned) {
            return Fail("open project: RecoveryRequired: revision " +
                        std::to_string(revisionOpen.revision) +
                        " scene failed validation (" + err + ")");
        }
        return Fail("scene load failed: " + sceneLoadPath + " (" + err + ")");
    }

    auto scene = std::make_shared<Engine::Scene>("AvaloniaSession");
    auto r = Engine::Content::InstantiateScene(
        snap, *scene, m_TexMgr, reg,
        [&root](const std::string& stored) {
            const auto resolved = root.ResolveAny(stored);
            return resolved.ok ? resolved.path : std::string();
        });
    if (!r.ok) {
        if (versioned) {
            return Fail("open project: RecoveryRequired: revision " +
                        std::to_string(revisionOpen.revision) +
                        " instantiate failed");
        }
        return Fail("instantiate failed: " + sceneLoadPath);
    }
    if (m_Playing.load()) { Stop(); m_RuntimeError.clear(); }
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::ScriptAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(scene.get());
    // E3 负路径：契约内缺失 GUID 资产的非致命告警（实体保留），拼接供读取
    m_Warnings.clear();
    for (const auto& w : pathWarnings) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += w;
    }
    for (const auto& w : r.warnings) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += w;
    }

    m_Root = std::move(root);
    m_Revisions = std::move(revisions);
    m_Reg = std::move(reg);
    m_EditScene = std::move(scene);
    m_Bindings = std::move(r.bindings);
    m_ScenePath = sceneRes.path;
    m_ManifestPath = manifestRes.path;
    m_Dirty = false;
    m_RecoveryRequired = false;
    m_PendingCleanup.clear();
    // 会话稳定序：以本次打开时的注册表快照建立（此后只 append）
    m_AssetOrder.clear();
    for (const auto& e : m_Reg.GetAllEntries())
        m_AssetOrder.push_back(e.guid);
    PublishRuntimeState();

    Engine::Log::Info("[EditorBridge] project loaded: {} objects, {} assets",
                      m_EditScene->GetObjectCount(), m_Reg.Count());
    Emit(EV_PROJECT_LOADED,
         "objects=" + std::to_string(m_EditScene->GetObjectCount()) +
         ";assets=" + std::to_string(static_cast<long long>(m_Reg.Count())));
    return true;
}

bool EditorSession::SaveProject() {
    if (m_RecoveryRequired)
        return Fail("save blocked: RecoveryRequired (explicit recovery required)");
    if (!m_EditScene) return Fail("save: no project loaded");
    RetryPendingCleanup();
    RealignBindings();
    // K4.4：显式保存把 registry 路径迁移为工程相对规范形式。
    // 无法规范化 = 不能写出一份合契约的 Manifest → 结构化失败（不静默保留旧形式）。
    Engine::Content::ContentRegistry canonical;
    for (const auto& entry : m_Reg.GetAllEntries()) {
        const auto path = m_Root.ToCanonicalProjectPath(entry.path);
        if (!path.ok) {
            return Fail("save: asset " + entry.guid.ToHex() + " path '" +
                        entry.path + "' rejected: " + DescribePathError(path));
        }
        if (!canonical.RegisterExplicit(entry.guid, path.path, entry.type)) {
            return Fail("save: asset identity conflict: GUID " +
                        entry.guid.ToHex() + " path '" + path.path + "'");
        }
    }
    Engine::Content::SceneSnapshot live =
        Engine::Content::CaptureScene(*m_EditScene, m_Bindings);
    const std::string manifestJson = canonical.SerializeManifest();
    const std::string sceneJson =
        Engine::Content::SerializeSnapshot(live).dump(2);

    // M1-D：Manifest+Scene 作为同一 revision 先写盘校验，再原子替换 current。
    const auto published =
        m_Revisions.Publish(m_Root, manifestJson, sceneJson);
    if (!published.ok) {
        if (published.recoveryRequired) {
            m_RecoveryRequired = true;
            return Fail("save: RecoveryRequired (commit state uncertain): " +
                        published.diagnostic);
        }
        return Fail("save: NotCommitted (revision publish failed): " +
                    published.diagnostic);
    }
    m_Reg = std::move(canonical);
    m_Dirty = false;
    Engine::Log::Info("[EditorBridge] project saved: revision {} ({} entities)",
                      published.revision, live.entities.size());
    Emit(EV_PROJECT_SAVED,
         "entities=" + std::to_string(live.entities.size()) +
             ";revision=" + std::to_string(published.revision));

    // current 已提交：旧路径文件仅作非权威镜像；失败不回滚已提交 revision。
    const auto sceneMirror =
        Engine::Content::WriteFileAtomic(m_ScenePath, sceneJson);
    const auto manifestMirror =
        Engine::Content::WriteFileAtomic(m_ManifestPath, manifestJson);
    if (!sceneMirror.ok || !manifestMirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "save: CommittedCleanupPending; legacy mirror update failed";
        Engine::Log::Warn(
            "[EditorBridge] legacy mirror update failed after commit");
    }
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

    const std::string texturePath = ResolveAssetPath(entry.path);
    if (texturePath.empty())
        return Fail("assign sprite: asset path rejected: " + entry.path);

    const auto& objs = m_EditScene->GetObjects();
    auto obj = objs[static_cast<size_t>(entityIndex)].get();
    // 复刻 GP01ProductionSession::AssignSpriteToSelected 的 DL-02 纪律路径
    if (!obj->HasComponent<Engine::SpriteComponent>())
        obj->AddComponent<Engine::SpriteComponent>(m_TexMgr, texturePath);
    else
        obj->GetComponent<Engine::SpriteComponent>()
           ->SetTexture(m_TexMgr, texturePath);
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

// ════════════════════════════════════════════════════════════
// F1（Component Contract）：约定组件（Camera 等）增删 + 属性读写
// ════════════════════════════════════════════════════════════

bool EditorSession::AddComponent(int32_t entityIndex,
                                 const std::string& typeName) {
    if (IsPlaying())
        return Fail("add component ignored while playing (edit-state only)");
    if (typeName.empty())
        return Fail("add component: empty type name");
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("add component: bad entity index " +
                    std::to_string(entityIndex));
    if (!Engine::ComponentRegistryGo::IsRegistered(typeName))
        return Fail("add component: unknown contract type '" + typeName + "'");

    auto* obj = m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    Engine::Component* comp = obj->AddComponentByName(typeName);
    if (!comp)
        return Fail("add component: failed for '" + typeName + "'");
    MarkDirty();
    Emit(EV_COMPONENT_CHANGED,
         "idx=" + std::to_string(entityIndex) + ";type=" + typeName +
         ";action=add");
    Engine::Log::Info("[EditorBridge] component added: {} -> {}",
                      obj->GetName(), typeName);
    return true;
}

bool EditorSession::RemoveComponent(int32_t entityIndex,
                                    const std::string& typeName) {
    if (IsPlaying())
        return Fail("remove component ignored while playing (edit-state only)");
    if (typeName.empty())
        return Fail("remove component: empty type name");
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("remove component: bad entity index " +
                    std::to_string(entityIndex));

    auto* obj = m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    if (!obj->HasComponentByName(typeName))
        return Fail("remove component: entity has no '" + typeName + "'");
    obj->RemoveComponentByName(typeName);
    MarkDirty();
    Emit(EV_COMPONENT_CHANGED,
         "idx=" + std::to_string(entityIndex) + ";type=" + typeName +
         ";action=remove");
    Engine::Log::Info("[EditorBridge] component removed: {} -> {}",
                      obj->GetName(), typeName);
    return true;
}

bool EditorSession::HasComponent(int32_t entityIndex,
                                 const std::string& typeName) const {
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const auto* obj =
        m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    return obj->HasComponentByName(typeName);
}

int32_t EditorSession::GetComponentCount(int32_t entityIndex) const {
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return -1;
    const auto* obj =
        m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    int32_t count = 0;
    obj->ForEachComponent([&count](const Engine::Component& c) {
        if (c.GetComponentTypeName()) ++count;
    });
    return count;
}

bool EditorSession::GetComponentTypeAt(int32_t entityIndex, int32_t compIndex,
                                       std::string* out) const {
    if (!out || !m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount())
        || compIndex < 0)
        return false;
    const auto* obj =
        m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    int32_t seen = 0;
    bool found = false;
    obj->ForEachComponent([&](const Engine::Component& c) {
        const char* tn = c.GetComponentTypeName();
        if (!tn) return;
        if (seen == compIndex) { *out = tn; found = true; }
        ++seen;
    });
    return found;
}

bool EditorSession::GetComponentProperty(int32_t entityIndex,
                                         const std::string& typeName,
                                         const std::string& propName,
                                         std::string* out) const {
    if (!out) return false;
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const auto* obj =
        m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    auto* comp = obj->GetComponentByName(typeName);
    if (!comp) return false;
    const int i = FindPropertyIndex(comp, propName);
    if (i < 0) return false;
    Engine::ComponentPropertyValue v;
    if (!comp->GetPropertyValue(static_cast<size_t>(i), &v)) return false;
    *out = ComponentValueToString(v);
    return true;
}

bool EditorSession::SetComponentProperty(int32_t entityIndex,
                                         const std::string& typeName,
                                         const std::string& propName,
                                         const std::string& valueStr) {
    if (IsPlaying())
        return Fail("set component ignored while playing (edit-state only)");
    if (!m_EditScene || entityIndex < 0 ||
        entityIndex >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return Fail("set component: bad entity index " +
                    std::to_string(entityIndex));
    auto* obj = m_EditScene->GetObjects()[static_cast<size_t>(entityIndex)].get();
    auto* comp = obj->GetComponentByName(typeName);
    if (!comp)
        return Fail("set component: no component '" + typeName + "' on entity");
    const int i = FindPropertyIndex(comp, propName);
    if (i < 0)
        return Fail("set component: no property '" + propName + "'");
    Engine::ComponentPropertyDesc d;
    if (!comp->GetPropertyDesc(static_cast<size_t>(i), &d) || !d.editable)
        return Fail("set component: property '" + propName + "' not editable");
    Engine::ComponentPropertyValue v;
    if (!ParseComponentValue(valueStr, d.type, &v))
        return Fail("set component: bad value '" + valueStr + "' for '" + propName + "'");
    if (!comp->SetPropertyValue(static_cast<size_t>(i), v))
        return Fail("set component: write failed for '" + propName + "'");
    MarkDirty();
    Emit(EV_COMPONENT_CHANGED,
         "idx=" + std::to_string(entityIndex) + ";type=" + typeName +
         ";action=set");
    Engine::Log::Info("[EditorBridge] component prop set: {}[{}.{}] = {}",
                      obj->GetName(), typeName, propName, valueStr);
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
    return ImportAssetInternal(path, type, std::string());
}

bool EditorSession::ReattachAsset(const std::string& missingGuidHex,
                                  const std::string& sourcePath,
                                  int32_t type) {
    if (!m_EditScene) return Fail("reattach asset: no project loaded");
    const Engine::ResourceGUID guid =
        Engine::ResourceGUID::FromHex(missingGuidHex);
    if (guid.IsNull())
        return Fail("reattach asset: invalid GUID '" + missingGuidHex + "'");
    if (m_Reg.ContainsGuid(guid))
        return Fail("reattach asset: GUID already registered: " +
                    guid.ToHex());
    return ImportAssetInternal(sourcePath, type, guid.ToHex()) >= 0;
}

int32_t EditorSession::ImportAssetInternal(const std::string& input,
                                           int32_t type,
                                           const std::string& reattachHex) {
    if (m_RecoveryRequired) {
        Fail("import blocked: RecoveryRequired (explicit recovery required)");
        return -1;
    }
    if (!m_EditScene) { Fail("import asset: no project loaded"); return -1; }
    if (input.empty()) { Fail("import asset: empty path"); return -1; }
    if (input.find('\0') != std::string::npos) {
        Fail("import asset: path contains NUL");
        return -1;
    }
    using AT = Engine::Content::AssetType;
    AT t;
    if (type == 0) t = AT::Texture;
    else if (type == 1) t = AT::Script;
    else { Fail("import asset: bad type " + std::to_string(type)); return -1; }

    bool reattach = false;
    Engine::ResourceGUID reattachGuid;
    if (!reattachHex.empty()) {
        reattachGuid = Engine::ResourceGUID::FromHex(reattachHex);
        if (reattachGuid.IsNull())
            return Fail("import asset: invalid reattach GUID"), -1;
        if (!m_Reg.ContainsGuid(reattachGuid)) reattach = true;
        else return Fail("import asset: reattach GUID already registered"), -1;
    }

    const std::string fwd = WithForwardSlashesCopy(input);
    const bool absolute = LooksAbsolutePath(fwd);
    if (absolute &&
        (fwd.rfind("//?/", 0) == 0 || fwd.rfind("//./", 0) == 0)) {
        Fail("import asset: device/long-path prefix is not supported: " +
             input);
        return -1;
    }
    if (absolute && fwd.rfind("//", 0) == 0) {
        Fail("import asset: UNC path is not a valid content source: " + input);
        return -1;
    }
    if (absolute && fwd.size() >= 2 && fwd[1] == ':' &&
        !(fwd.size() >= 3 && fwd[2] == '/')) {
        Fail("import asset: drive-relative path is not absolute: " + input);
        return -1;
    }

    std::string targetRel;
    std::string sourceAbs;
    bool copySource = false;
    std::error_code ec;

    if (!absolute) {
        const auto normalized =
            Engine::Content::NormalizeProjectRelative(input);
        if (!normalized.ok) {
            Fail("import asset: path '" + input + "' rejected: " +
                 Engine::Content::ToString(normalized.error));
            return -1;
        }
        targetRel = normalized.path;
        const auto resolved = m_Root.Resolve(targetRel);
        if (!resolved.ok) {
            Fail("import asset: path '" + targetRel + "' rejected: " +
                 DescribePathError(resolved));
            return -1;
        }
        sourceAbs = resolved.path;
    } else {
        const auto inside = m_Root.ToProjectRelative(input);
        if (inside.ok) {
            targetRel = inside.path;
            const auto resolved = m_Root.Resolve(targetRel);
            if (!resolved.ok) {
                Fail("import asset: path '" + targetRel + "' rejected: " +
                     DescribePathError(resolved));
                return -1;
            }
            sourceAbs = resolved.path;
        } else if (inside.error == Engine::Content::PathError::EscapesRoot) {
            copySource = true;
            sourceAbs = fwd;
            const fs::path src = Engine::Content::PathFromUtf8(sourceAbs);
            if (!fs::is_regular_file(src, ec) || ec) {
                Fail("import asset: source file not found: " + sourceAbs);
                return -1;
            }
            const std::string filename =
                Engine::Content::PathToUtf8(src.filename());
            if (filename.empty()) {
                Fail("import asset: source has no filename: " + sourceAbs);
                return -1;
            }
            targetRel = std::string("assets/imported/") +
                        (t == AT::Texture ? "tex" : "scripts") + "/" +
                        filename;
            const auto normalized =
                Engine::Content::NormalizeProjectRelative(targetRel);
            if (!normalized.ok) {
                Fail(std::string("import asset: target path rejected: ") +
                     Engine::Content::ToString(normalized.error));
                return -1;
            }
            targetRel = normalized.path;
        } else {
            Fail("import asset: path '" + input + "' rejected: " +
                 Engine::Content::ToString(inside.error));
            return -1;
        }
    }

    const auto targetResolved = m_Root.Resolve(targetRel);
    if (!targetResolved.ok) {
        Fail("import asset: target path '" + targetRel + "' rejected: " +
             DescribePathError(targetResolved));
        return -1;
    }
    const std::string targetAbs = targetResolved.path;

    // K4/PathKey：目标不得与任何既有资产冲突（reattach 的 GUID 尚未注册）。
    for (const auto& other : m_Reg.GetAllEntries()) {
        if (other.guid == reattachGuid) continue;
        if (!m_Root.KeysEqual(other.path, targetRel)) continue;
        if (!copySource && !reattach) {
            const int32_t existing = IndexOfGuid(m_AssetOrder, other.guid);
            if (existing >= 0) return existing;   // 同路径幂等
        }
        Fail("import asset: target path '" + targetRel +
             "' conflicts with asset " + other.guid.ToHex());
        return -1;
    }

    if (copySource) {
        if (fs::exists(Engine::Content::PathFromUtf8(targetAbs), ec)) {
            Fail("import asset: target already exists: " + targetRel);
            return -1;
        }
        fs::create_directories(
            Engine::Content::PathFromUtf8(targetAbs).parent_path(), ec);
        if (ec) {
            Fail("import asset: cannot create target directory: " +
                 ec.message());
            return -1;
        }
        const auto copied = Engine::Content::CopyFileNoOverwrite(
            sourceAbs, targetAbs);
        if (!copied.ok) {
            Fail("import asset: copy failed: " +
                 std::string(Engine::Content::ToString(copied.error)) + " (" +
                 copied.diagnostic + ")");
            return -1;
        }
    } else {
        if (!fs::is_regular_file(Engine::Content::PathFromUtf8(sourceAbs), ec) ||
            ec) {
            Fail("import asset: file not found: " + sourceAbs);
            return -1;
        }
    }

    Engine::Content::ContentRegistry candidate = m_Reg;
    Engine::ResourceGUID guid;
    if (reattach) {
        if (!candidate.RegisterExplicit(reattachGuid, targetRel, t)) {
            if (copySource)
                fs::remove(Engine::Content::PathFromUtf8(targetAbs), ec);
            return Fail("import asset: reattach registration failed"), -1;
        }
        guid = reattachGuid;
    } else {
        guid = candidate.Import(targetRel, t);
        if (guid.IsNull()) {
            if (copySource)
                fs::remove(Engine::Content::PathFromUtf8(targetAbs), ec);
            return Fail("import asset: GUID allocation failed"), -1;
        }
    }

    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene)) {
        if (copySource)
            fs::remove(Engine::Content::PathFromUtf8(targetAbs), ec);
        Fail("import asset: committed scene unavailable");
        return -1;
    }
    const std::string manifestJson = candidate.SerializeManifest();
    const auto published =
        m_Revisions.Publish(m_Root, manifestJson, committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) {
            m_RecoveryRequired = true;
            Fail("import asset: RecoveryRequired (commit state uncertain): " +
                 published.diagnostic);
            return -1;
        }
        if (copySource)
            fs::remove(Engine::Content::PathFromUtf8(targetAbs), ec);
        Fail("import asset: NotCommitted (revision publish failed): " +
             published.diagnostic);
        return -1;
    }

    m_Reg = std::move(candidate);
    int32_t idx = IndexOfGuid(m_AssetOrder, guid);
    if (idx < 0) {
        m_AssetOrder.push_back(guid);
        idx = static_cast<int32_t>(m_AssetOrder.size()) - 1;
    }
    const auto mirror =
        Engine::Content::WriteFileAtomic(m_ManifestPath, manifestJson);
    if (!mirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "import: CommittedCleanupPending; legacy manifest mirror "
            "update failed";
    }
    if (reattach) BindEditSceneSpriteTextures();
    MarkDirty();
    Engine::Log::Info("[EditorBridge] asset imported: {} ({})",
                      targetRel, Engine::Content::ToString(t));
    Emit(EV_ASSET_IMPORTED,
         "idx=" + std::to_string(idx) + ";guid=" + guid.ToHex() +
         ";path=" + targetRel + ";type=" + std::to_string(type) +
         ";reattach=" + (reattach ? "1" : "0"));
    return idx;
}

bool EditorSession::RenameAsset(int32_t assetIndex, const std::string& newName) {
    if (m_RecoveryRequired)
        return Fail("rename blocked: RecoveryRequired (explicit recovery required)");
    if (!m_EditScene)
        return Fail("rename asset: no project loaded");
    if (m_ManifestPath.empty())
        return Fail("rename asset: no committed manifest path");
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("rename asset: bad asset index " +
                    std::to_string(assetIndex));
    std::string cleaned = newName;
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    if (cleaned.empty() ||
        std::all_of(cleaned.begin(), cleaned.end(), isSpace))
        return Fail("rename asset: empty name rejected");
    if (cleaned.find_first_of("/\\") != std::string::npos)
        return Fail("rename asset: name must be a bare filename");

    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))
        return Fail("rename asset: bad asset index " +
                    std::to_string(assetIndex));

    RetryPendingCleanup();

    const auto oldResolved = m_Root.ResolveAny(entry.path);
    if (!oldResolved.ok) {
        return Fail("rename asset: source path '" + entry.path +
                    "' rejected: " + DescribePathError(oldResolved));
    }
    const fs::path oldAbs = Engine::Content::PathFromUtf8(oldResolved.path);
    std::error_code ec;
    if (!fs::is_regular_file(oldAbs, ec) || ec)
        return Fail("rename asset: source is not a regular file: " +
                    oldResolved.path);

    const std::string ext = Engine::Content::PathToUtf8(oldAbs.extension());
    const fs::path newAbs =
        oldAbs.parent_path() / Engine::Content::PathFromUtf8(cleaned + ext);
    if (!fs::is_directory(newAbs.parent_path(), ec) || ec)
        return Fail("rename asset: target parent directory missing");

    const auto newRelative =
        m_Root.ToProjectRelative(Engine::Content::PathToUtf8(newAbs));
    if (!newRelative.ok) {
        return Fail("rename asset: target path rejected: " +
                    DescribePathError(newRelative));
    }

    // K2.1：目标不得与另一已注册资产在 PathKey 下冲突
    for (const auto& other : m_Reg.GetAllEntries()) {
        if (other.guid == entry.guid) continue;
        if (m_Root.KeysEqual(other.path, newRelative.path)) {
            return Fail("rename asset: target path '" + newRelative.path +
                        "' conflicts with asset " + other.guid.ToHex());
        }
    }

    const bool samePath = entry.path == newRelative.path;
    if (samePath) {
        // K2.1：规范路径相同 → no-op，不提交、不改磁盘
        return true;
    }
    const bool sameKey = m_Root.KeysEqual(entry.path, newRelative.path);

    // K1.1：rename 的 revision 复用“已提交 Scene”，不提交未保存的编辑。
    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene))
        return Fail("rename asset: committed scene unavailable");

    auto emitRenamed = [&](const std::string& newRel) {
        MarkDirty();
        Engine::Log::Info("[EditorBridge] asset renamed: {} -> {}",
                          entry.path, newRel);
        Emit(EV_ASSET_RENAMED,
             "idx=" + std::to_string(assetIndex) + ";guid=" +
                 entry.guid.ToHex() + ";old=" + entry.path + ";new=" + newRel);
    };

    if (sameKey) {
        // K4.6 大小写-only rename：两阶段临时名，GUID 不变。
        if (fs::exists(newAbs, ec)) {
            if (ec || !fs::equivalent(oldAbs, newAbs, ec))
                return Fail("rename asset: case-only target exists as a "
                            "different file: " + newRelative.path);
        }

        fs::path tempAbs;
        for (;;) {
            tempAbs = newAbs.parent_path() /
                      Engine::Content::PathFromUtf8(
                          ".m1c_case_" + UniqueSuffix() + ext);
            if (fs::exists(tempAbs, ec)) continue;
            const auto tempRel = m_Root.ToProjectRelative(
                Engine::Content::PathToUtf8(tempAbs));
            bool clash = false;
            if (tempRel.ok) {
                for (const auto& other : m_Reg.GetAllEntries()) {
                    if (m_Root.KeysEqual(other.path, tempRel.path)) {
                        clash = true;
                        break;
                    }
                }
            }
            if (!clash) break;
        }

        fs::rename(oldAbs, tempAbs, ec);
        if (ec)
            return Fail("rename asset: case-only prepare failed: " +
                        ec.message());
        fs::rename(tempAbs, newAbs, ec);
        if (ec) {
            const std::string prepareError = ec.message();
            fs::rename(tempAbs, oldAbs, ec);
            if (ec) {
                m_RecoveryRequired = true;
                return Fail("rename asset: RecoveryRequired (case-only "
                            "prepare could not be undone): " + prepareError);
            }
            return Fail("rename asset: case-only target rename failed: " +
                        prepareError);
        }

        Engine::Content::ContentRegistry candidate = m_Reg;
        candidate.Unregister(entry.guid);
        if (!candidate.RegisterExplicit(entry.guid, newRelative.path,
                                        entry.type)) {
            fs::rename(newAbs, oldAbs, ec);
            if (ec) m_RecoveryRequired = true;
            return Fail("rename asset: registry update failed" +
                        std::string(ec ? " (RecoveryRequired)" : ""));
        }
        const std::string manifestJson = candidate.SerializeManifest();
        const auto published =
            m_Revisions.Publish(m_Root, manifestJson, committedScene);
        if (!published.ok) {
            if (published.recoveryRequired) {
                m_RecoveryRequired = true;
                return Fail("rename asset: RecoveryRequired (commit state "
                            "uncertain): " + published.diagnostic);
            }
            fs::rename(newAbs, oldAbs, ec);
            if (ec) {
                m_RecoveryRequired = true;
                return Fail("rename asset: NotCommitted but old case could "
                            "not be restored (RecoveryRequired): " +
                            published.diagnostic);
            }
            return Fail("rename asset: NotCommitted (case restored): " +
                        published.diagnostic);
        }

        m_Reg = std::move(candidate);
        const auto mirror = Engine::Content::WriteFileAtomic(
            m_ManifestPath, manifestJson);
        if (!mirror.ok) {
            if (!m_Warnings.empty()) m_Warnings += "\n";
            m_Warnings +=
                "rename: CommittedCleanupPending; legacy manifest mirror "
                "update failed";
        }
        emitRenamed(newRelative.path);
        return true;
    }

    // 普通 rename：copy（不覆盖）→ manifest 提交 → 删除旧源。
    if (fs::exists(newAbs, ec))
        return Fail("rename asset: target already exists: " +
                    newRelative.path);
    if (ec)
        return Fail("rename asset: target check failed: " + ec.message());

    const Engine::Content::AtomicFileResult copied =
        Engine::Content::CopyFileNoOverwrite(
            oldResolved.path, Engine::Content::PathToUtf8(newAbs));
    if (!copied.ok) {
        if (copied.error == Engine::Content::AtomicFileError::TargetExists)
            return Fail("rename asset: target appeared during prepare: " +
                        newRelative.path);
        return Fail("rename asset: prepare failed: " +
                    std::string(Engine::Content::ToString(copied.error)) +
                    " (" + copied.diagnostic + ")");
    }

    Engine::Content::ContentRegistry candidate = m_Reg;
    candidate.Unregister(entry.guid);
    if (!candidate.RegisterExplicit(entry.guid, newRelative.path, entry.type)) {
        fs::remove(newAbs, ec);
        return Fail("rename asset: registry update failed (new copy removed)");
    }
    const std::string manifestJson = candidate.SerializeManifest();
    const auto published =
        m_Revisions.Publish(m_Root, manifestJson, committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) {
            m_RecoveryRequired = true;
            return Fail("rename asset: RecoveryRequired (commit state "
                        "uncertain): " + published.diagnostic);
        }
        fs::remove(newAbs, ec);
        if (ec) {
            if (!m_Warnings.empty()) m_Warnings += "\n";
            m_Warnings += "rename: unregistered copy retained at '" +
                          newRelative.path + "' after commit failure";
        }
        return Fail("rename asset: NotCommitted (revision publish failed): " +
                    published.diagnostic);
    }

    m_Reg = std::move(candidate);
    const auto mirror =
        Engine::Content::WriteFileAtomic(m_ManifestPath, manifestJson);
    if (!mirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "rename: CommittedCleanupPending; legacy manifest mirror "
            "update failed";
    }
    emitRenamed(newRelative.path);

    // K2.4：提交成功后删除旧源；失败保留为可恢复副本，不回滚。
    Engine::Content::InvokeContentFaultForTesting(
        Engine::Content::ContentFaultPoint::BeforeRenameSourceCleanup);
    fs::remove(oldAbs, ec);
    if (ec) {
        m_PendingCleanup.push_back(Engine::Content::PathToUtf8(oldAbs));
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += "rename: CommittedCleanupPending; old source kept at '" +
                      entry.path + "'";
        Engine::Log::Warn("[EditorBridge] rename cleanup pending: {}",
                          entry.path);
    }
    return true;
}

bool EditorSession::ScriptRead(int32_t assetIndex, std::string* out) {
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return false;
    Engine::Content::AssetEntry entry;   // 稳定索引 + 按值拷贝（ASan 纪律）
    if (!AssetAt(assetIndex, &entry) ||
        entry.type != Engine::Content::AssetType::Script)
        return false;
    const auto resolved = ResolveContentPath(entry.path);
    if (!resolved.ok) {
        Fail("script read: path '" + entry.path + "' rejected: " +
             DescribePathError(resolved));
        return false;
    }
    const std::string filePath = resolved.path;
    std::ifstream f(Engine::Content::PathFromUtf8(filePath), std::ios::binary);
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
    if (m_RecoveryRequired)
        return Fail("script save blocked: RecoveryRequired (explicit recovery required)");
    if (assetIndex < 0 || assetIndex >= static_cast<int32_t>(m_Reg.Count()))
        return Fail("script save: bad asset index " +
                    std::to_string(assetIndex));
    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry))
        return Fail("script save: bad asset index " +
                    std::to_string(assetIndex));
    if (entry.type != Engine::Content::AssetType::Script)
        return Fail("script save: asset is not a script");
    const auto resolved = ResolveContentPath(entry.path);
    if (!resolved.ok) {
        return Fail("script save: path '" + entry.path + "' rejected: " +
                    DescribePathError(resolved));
    }
    const std::string filePath = resolved.path;
    const Engine::Content::AtomicFileResult written =
        Engine::Content::WriteFileAtomic(filePath, text);
    if (!written.ok) {
        return Fail("script save failed (" +
                    std::string(Engine::Content::ToString(written.error)) +
                    "): " + filePath +
                    (written.diagnostic.empty()
                         ? std::string()
                         : " (" + written.diagnostic + ")"));
    }
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
    if (m_RecoveryRequired)
        return Fail("play blocked: RecoveryRequired (explicit recovery required)");
    if (m_Playing.load()) return Fail("play: already playing (stop first)");
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
    const auto scriptResolved = ResolveContentPath(scriptPath);
    if (!scriptResolved.ok) {
        return Fail("play: script path '" + scriptPath + "' rejected: " +
                    DescribePathError(scriptResolved));
    }
    m_RuntimeScriptPath = scriptResolved.path;

    // ── 克隆编辑场景为新建运行场景 ──
    auto rollback = [this]() noexcept {
        try {
            m_Inst.Shutdown();
        } catch (...) {
        }
        Engine::Scripting::GameplayAPI::Reset();
        Engine::Scripting::ScriptAPI::Reset();
        Engine::Scripting::GameplayAPI::SetScene(m_EditScene.get());
        if (m_Physics) {
            m_Physics->Stop();
            m_Physics.reset();
        }
        m_Runtime.reset();
        m_RuntimeScriptPath.clear();
        m_Playing.store(false);
        if (m_Application) m_Application->SetPlaying(false);
        PublishRuntimeState();
    };

    try {
        auto rt = std::make_shared<Engine::Scene>("AvaloniaRuntime");
        auto r = Engine::Content::InstantiateScene(
            live, *rt, m_TexMgr, m_Reg,
            [this](const std::string& stored) {
                return ResolveAssetPath(stored);
            });
        if (!r.ok) {
            m_RuntimeScriptPath.clear();
            PublishRuntimeState();
            return Fail("play: scene clone failed");
        }

        // T-1-C：在运行场景上建立 2D 物理 World；失败则不进入 Play。
        auto physics = std::make_unique<Engine::PhysicsRuntime2D>();
        std::string physicsError;
        if (!physics->Start(*rt, &physicsError)) {
            m_RuntimeError = physicsError;
            m_RuntimeScriptPath.clear();
            PublishRuntimeState();
            return Fail("play: physics init failed: " + physicsError);
        }

        // ── 绑定 GameplayAPI 到运行场景并登记句柄 ──
        Engine::Scripting::GameplayAPI::Reset();
        Engine::Scripting::GameplayAPI::SetScene(rt.get());
        for (const auto& o : rt->GetObjects())
            Engine::Scripting::GameplayAPI::HandleAdopt(o);
        if (TestFailure("EDITOR_BRIDGE_FAIL_PLAY_BIND")) {
            throw std::runtime_error("injected play binding failure");
        }

        // ── 加载并执行导演脚本（指令预算防死循环，沙箱已开）──
        Engine::Scripting::ScriptInstance::Config cfg;
        if (!m_Inst.Initialize(m_RuntimeScriptPath, cfg)) {
            m_RuntimeError = ScriptError(m_Inst);
            rollback();
            return Fail("play: director init failed: " + m_RuntimeError);
        }
        const bool onCreateOk = m_Inst.OnCreate();
        if (!onCreateOk) {
            m_RuntimeError = ScriptError(m_Inst);
            rollback();
            return Fail("play: OnCreate failed: " + m_RuntimeError);
        }

        m_Runtime = std::move(rt);
        m_Physics = std::move(physics);
        m_Playing.store(true);
        PublishRuntimeState();
        if (m_Application) m_Application->SetPlaying(true);
        Engine::Log::Info("[EditorBridge] PLAY (script={})", m_RuntimeScriptPath);
        Emit(EV_PLAY_STARTED, "script=" + m_RuntimeScriptPath);
        return true;
    } catch (const std::exception& exception) {
        m_RuntimeError = exception.what();
        rollback();
        return Fail("play: runtime initialization exception: " + m_RuntimeError);
    } catch (...) {
        m_RuntimeError = "unknown runtime initialization exception";
        rollback();
        return Fail("play: runtime initialization exception");
    }
}

bool EditorSession::Reload() {
    m_RuntimeError.clear();
    if (m_RecoveryRequired)
        return Fail("reload blocked: RecoveryRequired (explicit recovery required)");
    if (!m_Playing.load() || !m_Inst.IsValid())
        return Fail("reload: not playing");
    if (!m_Inst.Reload()) {
        m_RuntimeError = ScriptError(m_Inst);
        if (m_Application) Stop();
        PublishRuntimeState();
        return Fail("reload failed: " + m_RuntimeError);
    }
    const bool onCreateOk = m_Inst.OnCreate();
    if (!onCreateOk) {
        m_RuntimeError = ScriptError(m_Inst);
        Stop();
        PublishRuntimeState();
        return Fail("reload OnCreate failed: " + m_RuntimeError);
    }
    PublishRuntimeState();
    Engine::Log::Info("[EditorBridge] RELOAD ok (persist preserved): {}",
                      m_RuntimeScriptPath);
    return true;
}

void EditorSession::Stop() {
    if (!m_Playing.load()) return;
    m_StopCount.fetch_add(1);
    m_Inst.Shutdown();
    Engine::Scripting::GameplayAPI::Reset();
    Engine::Scripting::GameplayAPI::SetScene(m_EditScene.get());
    Engine::Scripting::ScriptAPI::Reset();
    if (m_Physics) {
        m_Physics->Stop();
        m_Physics.reset();
    }
    m_Runtime.reset();
    m_RuntimeScriptPath.clear();
    m_Playing.store(false);
    PublishRuntimeState();
    if (m_Application) m_Application->SetPlaying(false);
    Engine::Log::Info("[EditorBridge] STOP -> edit state restored");
    Emit(EV_PLAY_STOPPED, "");
}

bool EditorSession::RuntimeTick(float dt) {
    if (!m_Playing.load() || !m_Inst.IsValid()) return false;
    const bool updated = m_Inst.OnUpdate(dt);
    if (!updated) {
        m_RuntimeError = ScriptError(m_Inst);
        PublishRuntimeState();
        return false;
    }
    if (m_Physics) {
        std::string physicsError;
        if (!m_Physics->Step(dt, &physicsError)) {
            m_RuntimeError = "physics: " + physicsError;
            Stop();
            return false;
        }
    }
    PublishRuntimeState();
    return true;
}

int32_t EditorSession::RuntimePersistInt(const std::string& key, int32_t def) {
    if (!m_Playing.load() || !m_Inst.IsValid() || !m_Inst.GetEngine()) return def;
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

void EditorSession::SetEventCallback(EventFn cb) {
    bool attached = false;
    {
        std::lock_guard<std::recursive_mutex> lock(m_EventMutex);
        m_Event = std::move(cb);
        attached = static_cast<bool>(m_Event);
    }
    m_CallbackAttached.store(attached);
    PublishRuntimeState();
}

void EditorSession::Emit(int32_t type, const std::string& payload) {
    std::lock_guard<std::recursive_mutex> lock(m_EventMutex);
    if (m_Event) m_Event(type, payload.c_str());
}

void EditorSession::PublishRuntimeState() noexcept {
    std::lock_guard<std::mutex> lock(m_ProofMutex);
    m_RuntimeScenePresent.store(static_cast<bool>(m_Runtime));
    m_ScriptValid.store(m_Inst.IsValid());
    m_PlayingSnapshot.store(m_Playing.load());
    m_LastStats.stopCount = m_StopCount.load();
    m_LastStats.onCreateCount = m_Inst.GetOnCreateCount();
    m_LastStats.onDestroyCount = m_Inst.GetOnDestroyCount();
    m_LastStats.gameplayHandleCount = Engine::Scripting::GameplayAPI::HandleCount();
    m_LastStats.playing = m_PlayingSnapshot.load();
    m_LastStats.runtimeScenePresent = m_RuntimeScenePresent.load();
    m_LastStats.scriptValid = m_ScriptValid.load();
    m_LastStats.callbackAttached = m_CallbackAttached.load();
    m_LastStats.gameplayScenePresent = Engine::Scripting::GameplayAPI::HasScene();
    m_LastStats.hudDirty = Engine::Scripting::ScriptAPI::HasHudText();
}

bool EditorSession::GetRuntimeStats(RuntimeStats* out) const {
    if (!out) return false;
    std::lock_guard<std::mutex> lock(m_ProofMutex);
    *out = m_LastStats;
    return true;
}

void EditorSession::RealignBindings() {
    if (!m_EditScene) return;
    m_Bindings.resize(m_EditScene->GetObjectCount());   // 补 Null 绑定
}

void EditorSession::MarkDirty() {
    m_Dirty = true;
}

Engine::Content::PathResolution EditorSession::ResolveContentPath(
    const std::string& path) const {
    // M1-A：内容路径的唯一解析锚点是打开时固定的显式 ProjectRoot（K4.1/K4.3）。
    // 相对路径按工程相对解析；根内绝对路径接受（legacy 兼容读）；
    // 越界/UNC/盘符/`.`/`..` → 结构化错误，绝不回退到 CWD。
    if (!m_Root.IsValid()) {
        return Engine::Content::PathResolution{
            false, Engine::Content::PathError::RootMissing, {}};
    }
    return m_Root.ResolveAny(path);
}

std::string EditorSession::ResolveAssetPath(const std::string& stored) const {
    const auto resolved = ResolveContentPath(stored);
    return resolved.ok ? resolved.path : std::string();
}

void EditorSession::RetryPendingCleanup() {
    if (m_PendingCleanup.empty()) return;
    for (auto it = m_PendingCleanup.begin(); it != m_PendingCleanup.end();) {
        std::error_code ec;
        fs::remove(Engine::Content::PathFromUtf8(*it), ec);
        if (!ec) {
            Engine::Log::Info("[EditorBridge] rename cleanup completed: {}",
                              *it);
            it = m_PendingCleanup.erase(it);
        } else {
            ++it;
        }
    }
}

bool EditorSession::Fail(const std::string& msg) {
    m_LastError = msg;
    Engine::Log::Error("[EditorBridge] {}", msg);
    return false;
}

// ── Viewport（Phase 7）── 所有操作转发到引擎 ViewportPanel ──

uint32_t EditorSession::GetViewportTextureHandle() const {
    auto* app = m_Application;
    if (!app) return 0;
    // 引擎 ViewportPanel 的 FBO 颜色纹理句柄
    // 注意：当前为 CPU readback 路径，实际 GLuint 由引擎管理
    return 0; // TODO: GPU interop 路径返回真实句柄
}

void EditorSession::GetViewportSize(int32_t* outWidth, int32_t* outHeight) const {
    if (outWidth)  *outWidth  = m_ViewportWidth;
    if (outHeight) *outHeight = m_ViewportHeight;
}

void EditorSession::SetViewportSize(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) return;
    if (width == m_ViewportWidth && height == m_ViewportHeight && m_ViewportInitialized)
        return;
    m_ViewportWidth = width;
    m_ViewportHeight = height;
    m_ViewportInitialized = true;

    // 通知引擎 ViewportPanel 更新视口尺寸
    auto* app = m_Application;
    if (app) {
        auto& vp = app->GetViewportPanel();
        vp.GetCamera().SetViewportSize(static_cast<float>(width), static_cast<float>(height));
        vp.MarkDirty();
    }
}

void EditorSession::ViewportTick(float dt) {
    // Phase 7-B: Flush accumulated input to engine EditorCamera
    if (m_CameraExternalInput) {
        auto* app = m_Application;
        if (app) {
            auto& cam = app->GetViewportPanel().GetCamera();
            m_PendingInput.deltaTime = dt;
            cam.SetExternalInput(m_PendingInput);
            cam.SetActive(true);
            cam.ProcessInput(dt);
            cam.OnUpdate(dt);
            // Reset per-frame accumulators
            m_PendingInput.mouseDeltaX = 0.0f;
            m_PendingInput.mouseDeltaY = 0.0f;
            m_PendingInput.mouseWheel = 0.0f;
        }
    }
}

void EditorSession::SetViewportCameraPosition(float x, float y, float z) {
    auto* app = m_Application;
    if (app) {
        app->GetViewportPanel().GetCamera().SetFocusPoint(
            Engine::Vec3{x, y, z});
    }
}

void EditorSession::GetViewportCameraPosition(float out3[3]) const {
    if (!out3) return;
    auto* app = m_Application;
    if (app) {
        auto pos = app->GetViewportPanel().GetCamera().GetPosition();
        out3[0] = pos.x; out3[1] = pos.y; out3[2] = pos.z;
    } else {
        out3[0] = out3[1] = out3[2] = 0.0f;
    }
}

void EditorSession::SetViewportCameraProjection(int32_t type) {
    auto* app = m_Application;
    if (app) {
        auto& cam = app->GetViewportPanel().GetCamera();
        cam.SetProjectionType(type == 0
            ? Engine::CameraProjectionType::Perspective
            : Engine::CameraProjectionType::Orthographic);
    }
}

void EditorSession::SetViewportCameraZoom(float zoom) {
    auto* app = m_Application;
    if (app) {
        auto& cam = app->GetViewportPanel().GetCamera();
        cam.SetDistance(zoom);
    }
}

void EditorSession::ViewportMouseMove(float x, float y) {
    float dx = x - m_LastMouseX;
    float dy = y - m_LastMouseY;
    m_LastMouseX = x;
    m_LastMouseY = y;

    // Feed to engine EditorCamera
    if (m_CameraExternalInput) {
        m_PendingInput.mouseDeltaX += dx;
        m_PendingInput.mouseDeltaY += dy;
    }
}

void EditorSession::ViewportMouseButton(int32_t button, int32_t pressed) {
    if (m_CameraExternalInput) {
        if (button == 0) m_PendingInput.leftDown = (pressed != 0);
        else if (button == 1) m_PendingInput.rightDown = (pressed != 0);
        else if (button == 2) m_PendingInput.middleDown = (pressed != 0);
    }
}

void EditorSession::ViewportScroll(float delta) {
    if (m_CameraExternalInput) {
        m_PendingInput.mouseWheel += delta;
    }
}

void EditorSession::ViewportKeyDown(int32_t key) {
    if (m_CameraExternalInput) {
        // Avalonia Key enum → camera key mapping (Avalonia.Input.Key)
        enum K {
            Shift = 16, Alt = 18, Space = 32, Delete = 46,
            A = 65, D = 68, E = 69, Q = 81, S = 83, W = 87,
            Left = 104, Up = 106, Right = 107, Down = 108,
        };
        switch (key) {
            case W: m_PendingInput.keyW = true; break;
            case S: m_PendingInput.keyS = true; break;
            case A: m_PendingInput.keyA = true; break;
            case D: m_PendingInput.keyD = true; break;
            case E: m_PendingInput.keyE = true; break;
            case Q: m_PendingInput.keyQ = true; break;
            case Up: m_PendingInput.keyUp = true; break;
            case Down: m_PendingInput.keyDown = true; break;
            case Left: m_PendingInput.keyLeft = true; break;
            case Right: m_PendingInput.keyRight = true; break;
            case Shift: m_PendingInput.shiftHeld = true; break;
            case Alt: m_PendingInput.altHeld = true; break;
        }
    }
}

void EditorSession::ViewportKeyUp(int32_t key) {
    if (m_CameraExternalInput) {
        enum K {
            Shift = 16, Alt = 18,
            A = 65, D = 68, E = 69, Q = 81, S = 83, W = 87,
            Left = 104, Up = 106, Right = 107, Down = 108,
        };
        switch (key) {
            case W: m_PendingInput.keyW = false; break;
            case S: m_PendingInput.keyS = false; break;
            case A: m_PendingInput.keyA = false; break;
            case D: m_PendingInput.keyD = false; break;
            case E: m_PendingInput.keyE = false; break;
            case Q: m_PendingInput.keyQ = false; break;
            case Up: m_PendingInput.keyUp = false; break;
            case Down: m_PendingInput.keyDown = false; break;
            case Left: m_PendingInput.keyLeft = false; break;
            case Right: m_PendingInput.keyRight = false; break;
            case Shift: m_PendingInput.shiftHeld = false; break;
            case Alt: m_PendingInput.altHeld = false; break;
        }
    }
}

int32_t EditorSession::ViewportPick(float ndcX, float ndcY) {
    if (!m_ViewportInitialized || !m_EditScene) return -1;

    // TODO: Phase 7-B 完成后，这里应从引擎 ViewportPanel 的 ID 纹理读取实体 ID
    // 当前返回 -1 表示无命中
    return -1;
}

void EditorSession::SetGizmoMode(int32_t mode) {
    m_GizmoMode = mode;
}

void EditorSession::SetGizmoSpace(int32_t space) {
    m_GizmoSpace = space;
}

void EditorSession::SetGizmoSnap(int32_t enabled, float value) {
    m_GizmoSnapEnabled = enabled;
    m_GizmoSnapValue = value;
}

int32_t EditorSession::GetGizmoMode() const { return m_GizmoMode; }
int32_t EditorSession::GetGizmoSpace() const { return m_GizmoSpace; }
int32_t EditorSession::GetGizmoSnapEnabled() const { return m_GizmoSnapEnabled; }
float EditorSession::GetGizmoSnapValue() const { return m_GizmoSnapValue; }

void EditorSession::SetViewportGridVisible(int32_t visible) {
    m_ViewportGridVisible = visible;
}

int32_t EditorSession::GetViewportGridVisible() const {
    return m_ViewportGridVisible;
}

// ── Phase 7-B: Camera external input mode ──

void EditorSession::SetCameraExternalInput(int32_t enabled) {
    m_CameraExternalInput = (enabled != 0);
    auto* app = m_Application;
    if (app) {
        auto& vp = app->GetViewportPanel();
        vp.GetCamera().SetExternalInputMode(m_CameraExternalInput);
    }
}

int32_t EditorSession::GetCameraExternalInput() const {
    return m_CameraExternalInput ? 1 : 0;
}

// ── Phase 7: Viewport FBO pixel readback ──

int32_t EditorSession::GetViewportPixels(uint8_t* outBuffer, int32_t bufferSize) {
    auto* app = m_Application;
    if (!app) return 0;

    auto& vp = app->GetViewportPanel();
    int32_t w = vp.GetReadbackWidth();
    int32_t h = vp.GetReadbackHeight();

    // 数据布局：[int32 width][int32 height][uint8 RGBA × width×height]
    int32_t headerSize = 8;
    int32_t pixelDataSize = w * h * 4;
    int32_t totalSize = headerSize + pixelDataSize;

    if (!outBuffer || bufferSize < totalSize) {
        return totalSize; // 返回所需字节数
    }

    std::memcpy(outBuffer, &w, 4);
    std::memcpy(outBuffer + 4, &h, 4);
    if (pixelDataSize > 0) {
        vp.CopyReadbackPixels(outBuffer + headerSize, pixelDataSize);
    }
    return totalSize;
}

  // ── HRC-3 P1：编辑场景的可渲染内容计数（production render 的前置证据）──
  //
  // 与 production render step 用同一条收集路径（Scene::CollectRenderCommands），
  // 因此"有内容"与"真的画得出来"是同一个判据，不会出现两套口径。
  // 0 = insufficient scene content：调用方必须把它当 typed failure，而不是退化成 clear-only。
  int32_t EditorSession::CountEditSceneRenderables() const {
    if (!m_EditScene) return 0;
    Engine::RenderQueue queue;
    m_EditScene->CollectRenderCommands(queue);
    return static_cast<int32_t>(queue.GetCommands().size());
  }

  // ── HRC-3 P1：把编辑场景的 sprite GUID 解析并绑到 SpriteComponent ──
  //
  // 幂等：已经有纹理的 sprite 直接跳过；不写 GUID、不改场景结构、不发事件，
  // 因此这不是新的编辑能力，只是让"场景里已经声明的 sprite"真的能被渲染。
  int32_t EditorSession::BindEditSceneSpriteTextures() {
    if (!m_EditScene) return 0;
    const auto& objs = m_EditScene->GetObjects();
    const size_t count = std::min(objs.size(), m_Bindings.size());
    int32_t bound = 0;
    for (size_t i = 0; i < count; ++i) {
      auto& guid = m_Bindings[i].spriteGuid;
      if (guid.IsNull()) continue;
      auto obj = objs[i];
      if (!obj) continue;
      auto* sprite = obj->GetComponent<Engine::SpriteComponent>();
      if (sprite && sprite->GetTexture()) continue;   // 已有纹理（显式 AssignSprite 过）
      const std::string path = ResolveAssetPath(m_Reg.ResolvePath(guid));
      if (path.empty()) continue;
      if (!sprite) {
        sprite = obj->AddComponent<Engine::SpriteComponent>(m_TexMgr, path);
      } else {
        sprite->SetTexture(m_TexMgr, path);
      }
      if (sprite && sprite->GetTexture()) ++bound;
    }
    if (bound > 0) {
      Engine::Log::Info("[HRC-3 P1] bound sprite textures for {} edit-scene object(s)", bound);
    }
    return bound;
  }

} // namespace editor_bridge
