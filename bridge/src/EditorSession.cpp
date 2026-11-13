/**
 * @file EditorSession.cpp
 * @brief AV-005 防腐层实现 —— 只走 GP01ProductionSession 已验证的无头契约
 */

#include "EditorSession.h"
#include "editor_bridge/capi.h"

#include <AssetImport/GltfParser.h>
#include <AssetImport/ObjParser.h>
#include <AssetImport/ImportDigest.h>
#include <AssetImport/MeshAssetWriter.h>
#include <AssetImport/MaterialAssetReader.h>
#include <AssetImport/MeshMaterialResolver.h>
#include <AssetImport/TexturePreprocess.h>
#include <AssetImport/ImportLimits.h>
#include <nlohmann/json.hpp>
#include <Engine/Application.h>
#include <Engine/Editor/EditorCamera.h>
#include <Engine/Core/Content/AtomicFile.h>
#include <Engine/Core/Content/RevisionStore.h>
#include <Engine/Core/Content/SceneSerializerV1.h>
#include <Engine/Core/Content/PrefabAsset.h>
#include <Engine/Core/Content/PrefabSerialization.h>
#include <Engine/Core/Content/PrefabOverrideModel.h>
#include <Engine/Core/Content/PrefabApplyCandidate.h>
#include <Engine/Core/Content/PrefabApplyRecord.h>
#include <Engine/Core/Content/PrefabReconciliationSources.h>
#include <chrono>
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

#include <map>
#include <unordered_set>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
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

    // ── P6-1B B3 helpers (model import) ─────────────────────────────────
    std::string LowerAsciiCopy(std::string s) {
        for (char& c : s)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        return s;
    }

    std::string SanitizeName(const std::string& raw) {
        std::string out;
        for (const char c : raw) {
            const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                            (c >= 'a' && c <= 'z') || c == '_' || c == '-';
            out += ok ? c : '_';
            if (out.size() >= 48u) break;
        }
        return out.empty() ? std::string("model") : out;
    }

    std::string Hex8(const std::string& bytes) {
        const std::string h = Engine::Content::RevisionStore::HashBytes(bytes);
        return h.substr(0, 8);
    }

    bool ReadAllBytesFromPath(const std::string& absoluteUtf8,
                              std::vector<uint8_t>* out) {
        std::ifstream in(absoluteUtf8, std::ios::binary);
        if (!in.good()) return false;
        in.seekg(0, std::ios::end);
        const std::streamoff size = in.tellg();
        if (size < 0) return false;
        in.seekg(0, std::ios::beg);
        out->resize(static_cast<size_t>(size));
        if (size > 0 &&
            !in.read(reinterpret_cast<char*>(out->data()), size)) return false;
        return true;
    }

    std::string ResolveRegisteredPath(const Engine::Content::ContentRegistry& reg,
                                      const Engine::ResourceGUID& guid) {
        if (guid.IsNull()) return std::string();
        for (const auto& e : reg.GetAllEntries()) {
            if (e.guid == guid) return e.path;
        }
        return std::string();
    }

    std::string ParentDirOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        return (slash == std::string::npos) ? std::string() : path.substr(0, slash);
    }

    std::string FileNameOf(const std::string& path) {
        const size_t slash = path.find_last_of('/');
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    std::string StemOf(const std::string& filename) {
        const size_t dot = filename.find_last_of('.');
        return (dot == std::string::npos) ? filename : filename.substr(0, dot);
    }

    std::string ExtOfLower(const std::string& filename) {
        const size_t dot = filename.find_last_of('.');
        if (dot == std::string::npos) return std::string();
        return LowerAsciiCopy(filename.substr(dot + 1));
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

    bool JsonToPropertyValue(const nlohmann::json& value,
                            Engine::ComponentValueType type,
                            Engine::ComponentPropertyValue* out) {
        if (!out) return false;
        out->type = type;
        switch (type) {
        case Engine::ComponentValueType::Bool:
            if (!value.is_boolean()) return false;
            out->boolValue = value.get<bool>();
            return true;
        case Engine::ComponentValueType::Int:
            if (!value.is_number_integer()) return false;
            try { out->intValue = value.get<int32>(); } catch (...) { return false; }
            return true;
        case Engine::ComponentValueType::Float:
            if (!value.is_number()) return false;
            try { out->floatValue = value.get<float>(); } catch (...) { return false; }
            return true;
        case Engine::ComponentValueType::String:
            if (!value.is_string()) return false;
            out->stringValue = value.get<std::string>();
            return true;
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
    std::string PrefabSyncStateText(Engine::Content::PrefabSyncState state) {
        switch (state) {
        case Engine::Content::PrefabSyncState::Clean: return "clean";
        case Engine::Content::PrefabSyncState::Overrides: return "overrides";
        case Engine::Content::PrefabSyncState::ApplyPending: return "apply_pending";
        case Engine::Content::PrefabSyncState::RecoveryRequired: return "recovery_required";
        case Engine::Content::PrefabSyncState::Conflict: return "conflict";
        }
        return "recovery_required";
    }

    nlohmann::json PrefabInstanceMetadataJson(
        const Engine::PrefabInstanceMetadata& metadata,
        int64_t sourceRevision, const std::string& sourceDigest,
        const std::string& sourcePath, const std::vector<Engine::Content::PrefabOverrideRecord>& overrides,
        Engine::Content::PrefabSyncState syncState) {
        nlohmann::json json = nlohmann::json::object();
        json["prefabGuid"] = metadata.prefabGuid.ToHex();
        json["sourceRevision"] = sourceRevision;
        json["sourceDigest"] = sourceDigest;
        json["sourcePath"] = sourcePath;
        json["legacySourceIdentity"] = metadata.legacySourceIdentity;
        nlohmann::json map = nlohmann::json::object();
        for (const auto& [localId, sceneId] : metadata.localToScene)
            map[std::to_string(localId)] = sceneId;
        json["localToScene"] = std::move(map);
        nlohmann::json records = nlohmann::json::array();
        for (const auto& record : overrides) {
            records.push_back({{"localId", record.localId},
                               {"component", record.component},
                               {"field", record.field},
                               {"value", record.value}});
        }
        json["overrides"] = std::move(records);
        json["syncState"] = PrefabSyncStateText(syncState);
        return json;
    }

    std::string PrefabDecisionText(
        Engine::Content::PrefabReconciliationDecision decision) {
        switch (decision) {
        case Engine::Content::PrefabReconciliationDecision::TakeLatest:
            return "take_latest";
        case Engine::Content::PrefabReconciliationDecision::KeepLocal:
            return "keep_local";
        case Engine::Content::PrefabReconciliationDecision::Conflict:
            return "conflict";
        }
        return "conflict";
    }

    std::string PrefabStructuralKindText(
        Engine::Content::PrefabStructuralChangeKind kind) {
        switch (kind) {
        case Engine::Content::PrefabStructuralChangeKind::AddEntity:
            return "add_entity";
        case Engine::Content::PrefabStructuralChangeKind::DeleteEntity:
            return "delete_entity";
        case Engine::Content::PrefabStructuralChangeKind::SetName:
            return "set_name";
        case Engine::Content::PrefabStructuralChangeKind::SetParent:
            return "set_parent";
        case Engine::Content::PrefabStructuralChangeKind::AddComponent:
            return "add_component";
        case Engine::Content::PrefabStructuralChangeKind::RemoveComponent:
            return "remove_component";
        }
        return "unknown";
    }

    nlohmann::json PrefabConflictJson(
        const Engine::Content::PrefabReconciliationConflict& conflict) {
        nlohmann::json item = {
            {"id", conflict.id},
            {"type", Engine::Content::PrefabConflictTypeText(conflict.type)},
            {"localId", conflict.localId},
            {"detail", conflict.detail},
            {"base", conflict.base},
            {"latest", conflict.latest},
            {"local", conflict.local}
        };
        if (!conflict.component.empty()) item["component"] = conflict.component;
        if (!conflict.field.empty()) item["field"] = conflict.field;
        return item;
    }

    nlohmann::json PrefabReconciliationPlanJson(
        const Engine::Content::PrefabReconciliationPlan& plan) {
        nlohmann::json json = nlohmann::json::object();
        json["outOfDate"] = plan.outOfDate;
        json["hasConflicts"] = plan.hasConflicts;
        json["fields"] = nlohmann::json::array();
        for (const auto& field : plan.fields) {
            json["fields"].push_back({
                {"decision", PrefabDecisionText(field.decision)},
                {"localId", field.localId},
                {"component", field.component},
                {"field", field.field},
                {"base", field.base},
                {"latest", field.latest},
                {"local", field.local},
                {"conflictId", field.conflictId}
            });
        }
        json["structural"] = nlohmann::json::array();
        for (const auto& change : plan.structural) {
            nlohmann::json item = {
                {"kind", PrefabStructuralKindText(change.kind)},
                {"localId", change.localId},
                {"parentLocalId", change.parentLocalId},
                {"name", change.name},
                {"component", change.component},
                {"automatic", change.automatic},
                {"conflictId", change.conflictId}
            };
            if (!change.data.is_null()) item["data"] = change.data;
            if (!change.components.empty()) {
                item["components"] = nlohmann::json::array();
                for (const auto& component : change.components) {
                    item["components"].push_back(
                        {{"type", component.type}, {"data", component.data}});
                }
            }
            json["structural"].push_back(std::move(item));
        }
        json["overrides"] = nlohmann::json::array();
        for (const auto& override : plan.overrides) {
            json["overrides"].push_back({
                {"decision", PrefabDecisionText(override.decision)},
                {"localId", override.localId},
                {"component", override.component},
                {"field", override.field},
                {"base", override.base},
                {"latestBase", override.latestBase},
                {"value", override.value},
                {"conflictId", override.conflictId}
            });
        }
        json["conflicts"] = nlohmann::json::array();
        for (const auto& conflict : plan.conflicts)
            json["conflicts"].push_back(PrefabConflictJson(conflict));
        json["validationErrors"] = plan.validationErrors;
        return json;
    }
    bool DerivePrefabCandidatePath(const std::string& oldPath,
                                   const std::string& oldDigest,
                                   const std::string& newDigest,
                                   std::string* out) {
        if (!out || oldDigest.size() != 16 || newDigest.size() != 16) return false;
        const std::string prefix = "assets/prefabs/";
        const std::string suffix = ".prefab.json";
        if (oldPath.rfind(prefix, 0) != 0 ||
            oldPath.size() <= prefix.size() + suffix.size() ||
            oldPath.compare(oldPath.size() - suffix.size(), suffix.size(), suffix) != 0)
            return false;
        const std::string base = oldPath.substr(prefix.size(),
            oldPath.size() - prefix.size() - suffix.size());
        const size_t dot = base.find_last_of('.');
        if (dot == std::string::npos || dot == 0 ||
            base.substr(dot + 1) != oldDigest)
            return false;
        const std::string stem = base.substr(0, dot);
        for (const char c : stem)
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
                  (c >= 'a' && c <= 'z') || c == '_' || c == '-'))
                return false;
        *out = prefix + stem + "." + newDigest + suffix;
        return true;
    }

    std::string MakeApplyTransactionId(uint64_t rootEntityId) {
        return "apply-" + std::to_string(rootEntityId) + "-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch().count());
    }
} // namespace

namespace {

bool LoadRecoveryManifest(const Engine::Content::ProjectRoot& root,
                          const std::string& manifestPath,
                          Engine::Content::ContentRegistry* out,
                          std::string* error) {
    if (!out) return false;
    Engine::Content::ManifestLoadOptions options;
    options.root = &root;
    options.allowLegacyAbsolute = false;
    options.caseMode = root.CaseMode();
    if (!out->LoadManifest(manifestPath, options)) {
        if (error) *error = "apply recovery: committed Manifest invalid";
        return false;
    }
    return true;
}

bool LoadRecoveryScene(const Engine::Content::RevisionStore& revisions,
                       Engine::Content::SceneSnapshot* out, std::string* error) {
    if (!out) return false;
    std::string bytes;
    if (!revisions.ReadCommittedScene("", &bytes)) {
        if (error) *error = "apply recovery: committed Scene unreadable";
        return false;
    }
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(bytes);
    } catch (const std::exception& ex) {
        if (error) *error = "apply recovery: committed Scene JSON invalid: " +
                            std::string(ex.what());
        return false;
    }
    std::string parseError;
    if (!Engine::Content::DeserializeSnapshot(json, *out, parseError)) {
        if (error) *error = "apply recovery: committed Scene invalid: " + parseError;
        return false;
    }
    return true;
}

bool FindSerializedInstance(Engine::Content::SceneSnapshot* snapshot,
                            uint64_t rootEntityId,
                            Engine::Content::SerializedEntity** out,
                            std::string* error) {
    if (!snapshot || !out) return false;
    Engine::Content::SerializedEntity* target = nullptr;
    for (auto& entity : snapshot->entities) {
        if (entity.entityId != rootEntityId) continue;
        if (target) {
            if (error) *error = "apply recovery: duplicate instance anchor";
            return false;
        }
        target = &entity;
    }
    if (!target || !target->prefabInstance.has_value()) {
        if (error) *error = "apply recovery: instance metadata missing";
        return false;
    }
    *out = target;
    return true;
}

bool RecoverySceneMatches(const Engine::Content::SerializedPrefabInstance& meta,
                          const Engine::Content::PrefabApplyRecord& record,
                          bool* isNew, std::string* error) {
    if (!isNew) return false;
    *isNew = false;
    if (meta.prefabGuid != record.prefabGuid) {
        if (error) *error = "apply recovery: instance GUID mismatch";
        return false;
    }
    if (meta.sourceDigest == record.newDigest &&
        meta.sourcePath == record.newPath) {
        *isNew = true;
        return true;
    }
    if (meta.sourceDigest != record.oldSourceDigest ||
        meta.sourcePath != record.oldSourcePath) {
        if (error) *error = "apply recovery: instance source is neither old nor new";
        return false;
    }
    return true;
}

bool PublishRecoveryPhase2(
    const Engine::Content::ProjectRoot& root,
    Engine::Content::RevisionStore* revisions,
    const Engine::Content::PrefabApplyRecord& record,
    Engine::Content::SceneSnapshot* snapshot,
    const std::string& candidateManifest,
    bool* recoveryRequired,
    std::string* error) {
    if (!revisions || !snapshot) return false;
    if (recoveryRequired) *recoveryRequired = false;
    Engine::Content::SerializedEntity* target = nullptr;
    if (!FindSerializedInstance(snapshot, record.instanceRootEntityId, &target,
                                error))
        return false;
    auto& meta = *target->prefabInstance;
    if (meta.prefabGuid != record.prefabGuid ||
        meta.sourceDigest != record.oldSourceDigest ||
        meta.sourcePath != record.oldSourcePath) {
        if (error) *error = "apply recovery: Phase 2 source mismatch";
        return false;
    }
    meta.sourceRevision = record.phase1Revision >= 1
        ? record.phase1Revision
        : static_cast<int64_t>(revisions->Revision());
    meta.sourceDigest = record.newDigest;
    meta.sourcePath = record.newPath;
    meta.legacySourceIdentity = false;
    meta.overrides.clear();
    meta.syncState = Engine::Content::PrefabSyncState::Clean;
    const std::string scene = Engine::Content::SerializeSnapshot(*snapshot).dump(2);
    const auto published = revisions->Publish(root, candidateManifest, scene);
    if (!published.ok) {
        if (recoveryRequired) *recoveryRequired = published.recoveryRequired;
        if (error) *error = "apply recovery: Phase 2 Publish failed: " +
                            published.diagnostic;
        return false;
    }
    return true;
}

bool RecoverOnePrefabApplyRecord(
    const Engine::Content::ProjectRoot& root,
    Engine::Content::RevisionStore* revisions,
    const Engine::Content::PrefabApplyRecord& record,
    std::string* error) {
    if (record.state == Engine::Content::PrefabApplyState::RecoveryRequired) {
        if (error) *error = "apply recovery: record requires explicit recovery";
        return false;
    }
    const int64_t current = revisions->Revision();
    Engine::Content::ContentRegistry registry;
    std::string parseError;
    if (!LoadRecoveryManifest(root, revisions->CommittedManifestPath(), &registry,
                              &parseError))
        return false;
    Engine::Content::SceneSnapshot snapshot;
    if (!LoadRecoveryScene(*revisions, &snapshot, &parseError))
        return false;
    Engine::Content::SerializedEntity* target = nullptr;
    if (!FindSerializedInstance(&snapshot, record.instanceRootEntityId, &target,
                                &parseError))
        return false;
    bool sceneNew = false;
    if (!RecoverySceneMatches(*target->prefabInstance, record, &sceneNew,
                              &parseError))
        return false;
    const std::string manifestPath = registry.ResolvePath(record.prefabGuid);
    const bool manifestNew = manifestPath == record.newPath;
    const bool manifestOld = manifestPath == record.oldSourcePath;

    if (sceneNew && manifestNew) {
        if (!Engine::Content::RemovePrefabApplyRecord(
                root, record.prefabGuid, record.instanceRootEntityId, &parseError))
            return false;
        return true;
    }
    if (current == record.oldRevisionSnapshot) {
        if (record.state == Engine::Content::PrefabApplyState::Prepared &&
            manifestOld && !sceneNew) {
            if (!Engine::Content::RemovePrefabApplyRecord(
                    root, record.prefabGuid, record.instanceRootEntityId,
                    &parseError))
                return false;
            return true;
        }
        if (error) *error = "apply recovery: old revision has inconsistent state";
        return false;
    }
    if (!manifestNew) {
        if (error) *error = "apply recovery: current Manifest is not Phase 1 candidate";
        return false;
    }
    if (record.state == Engine::Content::PrefabApplyState::ScenePublished) {
        if (error) *error = "apply recovery: Phase 2 record has old Scene";
        return false;
    }
    if (record.state == Engine::Content::PrefabApplyState::Prepared) {
        if (record.phase1Revision >= 1 && current != record.phase1Revision) {
            if (error) *error = "apply recovery: Phase 1 revision mismatch";
            return false;
        }
    } else if (record.phase1Revision >= 1 &&
               current != record.phase1Revision) {
        if (error) *error = "apply recovery: current is not Phase 1 revision";
        return false;
    }
    std::vector<uint8_t> manifestBytes;
    if (!ReadAllBytesFromPath(revisions->CommittedManifestPath(),
                              &manifestBytes)) {
        if (error) *error = "apply recovery: committed Manifest unreadable";
        return false;
    }
    const std::string candidateManifest(manifestBytes.begin(),
                                        manifestBytes.end());
    Engine::Content::PrefabApplyRecord effective = record;
    if (effective.state == Engine::Content::PrefabApplyState::Prepared) {
        effective.state = Engine::Content::PrefabApplyState::PrefabPublished;
        effective.phase1Revision = current;
        effective.newSourceRevision = current;
        effective.newInstanceMetadata["sourceRevision"] = current;
        if (!Engine::Content::WritePrefabApplyRecord(root, effective,
                                                      &parseError))
            return false;
    }
    bool recoveryRequired = false;
    if (!PublishRecoveryPhase2(root, revisions, effective, &snapshot,
                              candidateManifest, &recoveryRequired,
                              &parseError)) {
        if (recoveryRequired) {
            effective.state = Engine::Content::PrefabApplyState::RecoveryRequired;
            Engine::Content::WritePrefabApplyRecord(root, effective, nullptr);
        }
        if (error) *error = parseError;
        return false;
    }
    effective.state = Engine::Content::PrefabApplyState::ScenePublished;
    effective.phase2Revision = static_cast<int64_t>(revisions->Revision());
    effective.newInstanceMetadata["sourceRevision"] = effective.phase1Revision;
    if (!Engine::Content::WritePrefabApplyRecord(root, effective, &parseError)) {
        if (error) *error = "apply recovery: ScenePublished record update failed: " +
                            parseError;
        return false;
    }
    if (!Engine::Content::RemovePrefabApplyRecord(
            root, effective.prefabGuid, effective.instanceRootEntityId,
            &parseError)) {
        if (error) *error = parseError;
        return false;
    }
    return true;
}

bool RecoverPrefabApplyOnStartup(const Engine::Content::ProjectRoot& root,
                                 Engine::Content::RevisionStore* revisions,
                                 std::string* error) {
    if (!revisions) return false;
    const std::string dir = root.Path() + "/.engine-content/prefab-apply";
    std::error_code ec;
    if (!std::filesystem::exists(Engine::Content::PathFromUtf8(dir), ec)) {
        if (ec) {
            if (error) *error = "apply recovery: cannot inspect recovery directory";
            return false;
        }
        return true;
    }
    std::vector<std::string> files;
    for (const auto& item : std::filesystem::directory_iterator(
             Engine::Content::PathFromUtf8(dir), ec)) {
        if (ec) {
            if (error) *error = "apply recovery: cannot enumerate recovery directory";
            return false;
        }
        if (item.is_regular_file(ec) && !ec &&
            item.path().extension() == ".json")
            files.push_back(Engine::Content::PathToUtf8(item.path()));
    }
    std::sort(files.begin(), files.end());
    for (const auto& file : files) {
        std::vector<uint8_t> bytes;
        if (!ReadAllBytesFromPath(file, &bytes)) {
            if (error) *error = "apply recovery: recovery record unreadable: " + file;
            return false;
        }
        const std::string text(bytes.begin(), bytes.end());
        Engine::Content::PrefabApplyRecord record;
        std::string parseError;
        if (!Engine::Content::ParsePrefabApplyRecord(text, &record,
                                                      &parseError)) {
            if (error) *error = "apply recovery: " + parseError + " (" + file + ")";
            return false;
        }
        if (!RecoverOnePrefabApplyRecord(root, revisions, record, &parseError)) {
            if (error) *error = parseError;
            return false;
        }
    }
    return true;
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
    if (versioned) {
        std::string recoveryError;
        if (!RecoverPrefabApplyOnStartup(root, &revisions, &recoveryError))
            return Fail("open project: RecoveryRequired: " + recoveryError);
    }

    // M1-B：严格候选加载。revision 内清单必须是规范工程相对路径；
    // 旧工程（无 current）保留 K4.4 legacy 绝对路径兼容。
    std::vector<std::string> pathWarnings;
    Engine::Content::ManifestLoadOptions loadOptions;
    loadOptions.root = &root;
    loadOptions.allowLegacyAbsolute = !versioned;
    loadOptions.caseMode = root.CaseMode();
    loadOptions.warnings = &pathWarnings;

    const std::string manifestLoadPath =
        versioned ? revisions.CommittedManifestPath() : manifestRes.path;
    const std::string sceneLoadPath =
        versioned ? revisions.CommittedScenePath() : sceneRes.path;

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
    LoadMaterialEdits();
    m_Revisions = std::move(revisions);
    m_Reg = std::move(reg);
    m_EditScene = std::move(scene);
    m_Bindings = std::move(r.bindings);
    m_BindingsById = std::move(r.bindingsById);
    m_SavedSceneJson = CaptureSceneJson();
    m_HasSavedScene = true;
    m_SceneHistory.clear();
    m_SceneHistoryCursor = 0;
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
        Engine::Content::CaptureSceneById(*m_EditScene, m_BindingsById);
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
    m_SavedSceneJson = sceneJson;
    m_HasSavedScene = true;
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

uint64 EditorSession::GetEntitySceneId(int32_t index) const {
    if (!m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return 0;
    return m_EditScene->GetObjects()[static_cast<size_t>(index)]->GetSceneEntityId();
}

int32_t EditorSession::GetTotalEntityCount() const {
    return m_EditScene ? static_cast<int32_t>(m_EditScene->GetAllEntities().size()) : -1;
}

bool EditorSession::SetEntityParent(uint64 childId, uint64 parentId,
                                    int32_t mode) {
    if (!m_EditScene) return Fail("set parent: no project loaded");
    if (mode != 0 && mode != 1) return Fail("set parent: invalid mode");
    std::string error;
    const Engine::ReparentMode reparent =
        mode == 1 ? Engine::ReparentMode::KeepWorld
                  : Engine::ReparentMode::KeepLocal;
    if (!m_EditScene->SetEntityParent(childId, parentId, reparent, &error))
        return Fail("set parent failed: " + error);
    RealignBindings();
    MarkDirty();
    Emit(EV_ENTITY_MOVED, "id=" + std::to_string(childId));
    return true;
}

bool EditorSession::DeleteEntityById(uint64 entityId) {
    if (!m_EditScene) return Fail("delete entity: no project loaded");
    if (!m_EditScene->DeleteEntity(entityId))
        return Fail("delete entity: not found");
    RealignBindings();
    MarkDirty();
    Emit(EV_ENTITY_DELETED, "id=" + std::to_string(entityId));
    return true;
}
bool EditorSession::GetEntityNameById(uint64 entityId,
                                       std::string* out) const {
    if (!out || !m_EditScene) return false;
    const Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    if (!obj) return false;
    *out = obj->GetName();
    return true;
}

uint64 EditorSession::GetEntityParentId(uint64 entityId) const {
    return m_EditScene ? m_EditScene->GetParentEntityId(entityId) : 0;
}

int32_t EditorSession::GetEntityChildCount(uint64 entityId) const {
    if (!m_EditScene) return -1;
    const Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    return obj ? static_cast<int32_t>(obj->GetChildren().size()) : -1;
}

uint64 EditorSession::GetEntityChildId(uint64 entityId,
                                       int32_t childIndex) const {
    if (!m_EditScene || childIndex < 0) return 0;
    const Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    if (!obj || childIndex >= static_cast<int32_t>(obj->GetChildren().size()))
        return 0;
    const auto& child = obj->GetChildren()[static_cast<size_t>(childIndex)];
    return child ? child->GetSceneEntityId() : 0;
}

bool EditorSession::LoadPrefabSourceForInstance(
    const Engine::PrefabInstanceMetadata& metadata,
    Engine::Content::PrefabModel* model,
    std::string* error) const {
    if (!model) {
        if (error) *error = "Prefab source output is null";
        return false;
    }
    if (metadata.legacySourceIdentity ||
        metadata.sourcePath.empty() ||
        metadata.sourceDigest.size() != 16) {
        if (error) *error = "Prefab instance source identity is legacy/incomplete";
        return false;
    }
    const auto resolved = m_Root.Resolve(metadata.sourcePath);
    if (!resolved.ok) {
        if (error) *error = "Prefab source path rejected";
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!ReadAllBytesFromPath(resolved.path, &bytes)) {
        if (error) *error = "Prefab source bytes unreadable";
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    if (Engine::Content::ComputePrefabDigest(text) != metadata.sourceDigest) {
        if (error) *error = "Prefab source digest mismatch";
        return false;
    }
    const auto read = Engine::Content::ReadPrefab(text, &metadata.prefabGuid);
    if (!read.ok) {
        if (error) *error = read.error;
        return false;
    }
    *model = read.model;
    return true;
}

bool EditorSession::BuildPrefabApplyCandidateForInstance(
    uint64 rootEntityId, Engine::Content::PrefabApplyCandidateResult* out) const {
    if (!out) return false;
    *out = {};
    if (!m_EditScene || IsPlaying()) {
        out->error = "apply candidate: edit scene unavailable";
        return false;
    }
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata)) {
        out->error = "apply candidate: instance metadata not found";
        return false;
    }
    if (metadata.legacySourceIdentity || metadata.sourceRevision < 0) {
        out->error = "apply candidate: legacy/invalid source identity";
        return false;
    }
    if (metadata.overrides.empty()) {
        out->error = "apply candidate: no overrides";
        return false;
    }
    Engine::Content::PrefabModel source;
    std::string sourceError;
    if (!LoadPrefabSourceForInstance(metadata, &source, &sourceError)) {
        out->error = "apply candidate: " + sourceError;
        return false;
    }
    *out = Engine::Content::BuildPrefabApplyCandidate(
        source, metadata.overrides, metadata.localToScene,
        metadata.prefabGuid, metadata.sourceRevision);
    return out->ok;
}

namespace {

bool IsReconciliationSupportedComponent(const std::string& type) {
    return type == "Mesh" || type == "Camera" || type == "Collider" ||
           type == "RigidBody" || type == "Skinning";
}

bool ReadReconciliationSource(
    const Engine::Content::ProjectRoot& root,
    const std::string& path,
    const Engine::ResourceGUID& expectedGuid,
    const std::string& expectedDigest,
    int64_t projectRevision,
    Engine::Content::PrefabSourceSnapshot* out,
    std::string* error) {
    if (!out) return false;
    const auto resolved = root.Resolve(path);
    if (!resolved.ok) {
        if (error) *error = "reconciliation source path rejected";
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!ReadAllBytesFromPath(resolved.path, &bytes)) {
        if (error) *error = "reconciliation source bytes unreadable";
        return false;
    }
    const std::string text(bytes.begin(), bytes.end());
    const std::string digest = Engine::Content::ComputePrefabDigest(text);
    if (!expectedDigest.empty() && digest != expectedDigest) {
        if (error) *error = "reconciliation source digest mismatch";
        return false;
    }
    const auto read = Engine::Content::ReadPrefab(text, &expectedGuid);
    if (!read.ok) {
        if (error) *error = "reconciliation source invalid: " + read.error;
        return false;
    }
    out->prefabGuid = read.model.prefabGuid;
    out->projectRevision = projectRevision;
    out->contentDigest = digest;
    out->contentPath = path;
    out->model = read.model;
    return true;
}

bool BuildLocalReconciliationSnapshot(
    const Engine::Scene& scene,
    uint64_t rootEntityId,
    const Engine::PrefabInstanceMetadata& metadata,
    Engine::Content::PrefabLocalInstanceSnapshot* out,
    std::string* error) {
    if (!out) return false;
    if (rootEntityId == 0 || metadata.localToScene.empty()) {
        if (error) *error = "reconciliation local: empty instance mapping";
        return false;
    }
    const Engine::GameObject* root = scene.FindByEntityId(rootEntityId);
    if (!root) {
        if (error) *error = "reconciliation local: root missing";
        return false;
    }
    std::unordered_set<uint64> actualScenes;
    std::unordered_set<uint64> localIds;
    std::unordered_map<uint64, uint64> localByScene;
    std::unordered_set<uint64> mappedScenes;
    bool rootMapped = false;
    for (const auto& [localId, sceneId] : metadata.localToScene) {
        if (localId == 0 || sceneId == 0 || !localIds.insert(localId).second ||
            !mappedScenes.insert(sceneId).second) {
            if (error) *error = "reconciliation local: invalid/duplicate mapping";
            return false;
        }
        localByScene[sceneId] = localId;
    }
    std::function<void(const Engine::GameObject*)> collect =
        [&](const Engine::GameObject* node) {
            if (!node) return;
            actualScenes.insert(node->GetSceneEntityId());
            for (const auto& child : node->GetChildren())
                collect(child.get());
        };
    collect(root);
    if (actualScenes != mappedScenes) {
        if (error) *error = "reconciliation local: subtree/mapping mismatch";
        return false;
    }
    for (const auto& [sceneId, localId] : localByScene) {
        if (sceneId == rootEntityId) rootMapped = true;
    }
    if (!rootMapped) {
        if (error) *error = "reconciliation local: root is not mapped";
        return false;
    }

    out->rootEntityId = rootEntityId;
    out->localToScene = metadata.localToScene;
    out->overrides = metadata.overrides;
    out->syncState = metadata.syncState;
    out->entities.clear();
    out->entities.reserve(localByScene.size());
    for (const auto& [sceneId, localId] : localByScene) {
        const Engine::GameObject* object = scene.FindByEntityId(sceneId);
        if (!object) {
            if (error) *error = "reconciliation local: mapped entity missing";
            return false;
        }
        Engine::Content::PrefabLocalEntitySnapshot entity;
        entity.localId = localId;
        entity.sceneEntityId = sceneId;
        entity.name = object->GetName();
        if (sceneId != rootEntityId) {
            const Engine::GameObject* parent = object->GetParent();
            const auto parentIt = parent == nullptr
                ? localByScene.end()
                : localByScene.find(parent->GetSceneEntityId());
            if (parentIt == localByScene.end()) {
                if (error) *error = "reconciliation local: parent mapping missing";
                return false;
            }
            entity.parentLocalId = parentIt->second;
        }
        const auto& transform = object->GetTransform();
        entity.localTransform.position = transform.GetPosition();
        entity.localTransform.rotation = transform.GetRotationQuat();
        entity.localTransform.scale = transform.GetScale();
        object->ForEachComponent([&](const Engine::Component& component) {
            Engine::Content::PrefabLocalComponentSnapshot snapshot;
            const char* typeName = component.GetComponentTypeName();
            snapshot.type = typeName ? typeName : "(non-contract)";
            snapshot.supported = IsReconciliationSupportedComponent(snapshot.type);
            try {
                component.Serialize(snapshot.data);
            } catch (...) {
                snapshot.data = nlohmann::json::object();
                snapshot.supported = false;
            }
            entity.components.push_back(std::move(snapshot));
        });
        std::sort(entity.components.begin(), entity.components.end(),
                  [](const Engine::Content::PrefabLocalComponentSnapshot& a,
                     const Engine::Content::PrefabLocalComponentSnapshot& b) {
                      return a.type < b.type;
                  });
        out->entities.push_back(std::move(entity));
    }
    std::sort(out->entities.begin(), out->entities.end(),
              [](const Engine::Content::PrefabLocalEntitySnapshot& a,
                 const Engine::Content::PrefabLocalEntitySnapshot& b) {
                  return a.localId < b.localId;
              });
    return true;
}

bool ApplyPrefabFieldValue(Engine::GameObject* object,
                           const std::string& component,
                           const std::string& field,
                           const nlohmann::json& value,
                           std::string* error) {
    if (!object) {
        if (error) *error = "reconciliation value: entity missing";
        return false;
    }
    if (component == "Transform") {
        if (!value.is_number()) {
            if (error) *error = "reconciliation value: Transform is not numeric";
            return false;
        }
        auto& transform = object->GetTransform();
        Engine::Vec3 position = transform.GetPosition();
        Engine::Quat rotation = transform.GetRotationQuat();
        Engine::Vec3 scale = transform.GetScale();
        const float scalar = value.get<float>();
        if (field == "position.x") position.x = scalar;
        else if (field == "position.y") position.y = scalar;
        else if (field == "position.z") position.z = scalar;
        else if (field == "rotation.x") rotation.x = scalar;
        else if (field == "rotation.y") rotation.y = scalar;
        else if (field == "rotation.z") rotation.z = scalar;
        else if (field == "rotation.w") rotation.w = scalar;
        else if (field == "scale.x") scale.x = scalar;
        else if (field == "scale.y") scale.y = scalar;
        else if (field == "scale.z") scale.z = scalar;
        else {
            if (error) *error = "reconciliation value: unsupported Transform field";
            return false;
        }
        transform.SetPosition(position);
        transform.SetRotationQuat(rotation);
        transform.SetScale(scale);
        return true;
    }
    Engine::Component* comp = object->GetComponentByName(component);
    if (!comp) {
        if (error) *error = "reconciliation value: component missing";
        return false;
    }
    const int index = FindPropertyIndex(comp, field);
    if (index < 0) {
        if (error) *error = "reconciliation value: property missing";
        return false;
    }
    Engine::ComponentPropertyDesc desc;
    if (!comp->GetPropertyDesc(static_cast<size_t>(index), &desc) ||
        !desc.editable) {
        if (error) *error = "reconciliation value: property not editable";
        return false;
    }
    Engine::ComponentPropertyValue parsed;
    if (!JsonToPropertyValue(value, desc.type, &parsed) ||
        !comp->SetPropertyValue(static_cast<size_t>(index), parsed)) {
        if (error) *error = "reconciliation value: rejected";
        return false;
    }
    return true;
}

bool ApplyPrefabComponentPayload(Engine::GameObject* object,
                                 const std::string& component,
                                 const nlohmann::json& data,
                                 std::string* error) {
    if (!object || !data.is_object()) {
        if (error) *error = "reconciliation component: invalid payload";
        return false;
    }
    for (auto it = data.begin(); it != data.end(); ++it) {
        if (!ApplyPrefabFieldValue(object, component, it.key(), it.value(),
                                   error)) {
            return false;
        }
    }
    return true;
}

bool BuildPrefabGameObject(
    const std::string& name,
    const Engine::Content::PrefabLocalTransform& transform,
    const std::vector<Engine::Content::PrefabComponentData>& components,
    std::shared_ptr<Engine::GameObject>* out, std::string* error) {
    if (!out) return false;
    auto object = std::make_shared<Engine::GameObject>(name);
    object->GetTransform().SetPosition(transform.position);
    object->GetTransform().SetRotationQuat(transform.rotation);
    object->GetTransform().SetScale(transform.scale);
    for (const auto& data : components) {
        auto component = Engine::ComponentRegistryGo::Create(data.type);
        if (!component || !component->Deserialize(data.data) ||
            !object->Attach(component)) {
            if (error) {
                *error = "reconciliation component '" + data.type +
                         "' could not be instantiated";
            }
            return false;
        }
    }
    *out = std::move(object);
    return true;
}

} // namespace

bool EditorSession::GetPrefabInstanceJsonForEntity(uint64 entityId,
                                                   std::string* out) const {
    if (!out || !m_EditScene) return false;
    nlohmann::json doc = nlohmann::json::object();
    uint64_t rootId = 0;
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->FindPrefabInstanceForEntity(entityId, &rootId, &metadata)) {
        doc["isInstance"] = false;
        *out = doc.dump();
        return true;
    }
    uint64_t localId = 0;
    for (const auto& [local, scene] : metadata.localToScene) {
        if (scene == entityId) { localId = local; break; }
    }
    doc["isInstance"] = true;
    doc["rootSceneEntityId"] = rootId;
    doc["localId"] = localId;
    doc["prefabGuid"] = metadata.prefabGuid.ToHex();
    doc["sourceRevision"] = metadata.sourceRevision;
    doc["sourceDigest"] = metadata.sourceDigest;
    doc["sourcePath"] = metadata.sourcePath;
    doc["legacySourceIdentity"] = metadata.legacySourceIdentity;
    switch (metadata.syncState) {
    case Engine::Content::PrefabSyncState::Clean: doc["syncState"] = "clean"; break;
    case Engine::Content::PrefabSyncState::Overrides: doc["syncState"] = "overrides"; break;
    case Engine::Content::PrefabSyncState::ApplyPending: doc["syncState"] = "apply_pending"; break;
    case Engine::Content::PrefabSyncState::RecoveryRequired: doc["syncState"] = "recovery_required"; break;
    case Engine::Content::PrefabSyncState::Conflict: doc["syncState"] = "conflict"; break;
    }
    nlohmann::json overrides = nlohmann::json::array();
    for (const auto& record : metadata.overrides) {
        overrides.push_back({{"localId", record.localId},
                             {"component", record.component},
                             {"field", record.field},
                             {"value", record.value}});
    }
    doc["overrideCount"] = metadata.overrides.size();
    doc["overrides"] = std::move(overrides);
    doc["conflictCount"] = metadata.reconciliation.has_value()
        ? metadata.reconciliation->conflicts.size()
        : 0;
    if (metadata.reconciliation.has_value())
        doc["reconciliation"] =
            Engine::Content::PrefabReconciliationStateToJson(
                *metadata.reconciliation);
    *out = doc.dump();
    return true;
}

bool EditorSession::PublishPrefabApplyPhase1(uint64 rootEntityId) {
    Engine::Content::PrefabApplyCandidateResult candidate;
    if (!BuildPrefabApplyCandidateForInstance(rootEntityId, &candidate))
        return Fail("apply phase 1: " + candidate.error);
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("apply phase 1: instance metadata not found");

    const std::string recordPath = Engine::Content::PrefabApplyRecordPath(
        m_Root, metadata.prefabGuid, rootEntityId);
    if (recordPath.empty())
        return Fail("apply phase 1: recovery path invalid");
    std::error_code existsEc;
    if (std::filesystem::exists(Engine::Content::PathFromUtf8(recordPath),
                                existsEc)) {
        return Fail("apply phase 1: another Apply is pending for this instance");
    }

    std::string newPath;
    if (!DerivePrefabCandidatePath(metadata.sourcePath, metadata.sourceDigest,
                                   candidate.digest, &newPath))
        return Fail("apply phase 1: source path does not match Prefab layout");

    const auto resolved = m_Root.Resolve(newPath);
    if (!resolved.ok)
        return Fail("apply phase 1: candidate path rejected");
    std::filesystem::create_directories(
        std::filesystem::path(Engine::Content::PathFromUtf8(resolved.path))
            .parent_path(), existsEc);
    if (existsEc) return Fail("apply phase 1: cannot create Prefab directory");
    bool alreadyStaged = false;
    std::ifstream existing(Engine::Content::PathFromUtf8(resolved.path),
                           std::ios::binary);
    if (existing.good()) {
        std::string bytes((std::istreambuf_iterator<char>(existing)),
                          std::istreambuf_iterator<char>());
        if (bytes != candidate.bytes)
            return Fail("apply phase 1: candidate path collision");
        alreadyStaged = true;
    }
    if (!alreadyStaged) {
        const auto written = Engine::Content::WriteFileAtomic(
            resolved.path, candidate.bytes);
        if (!written.ok)
            return Fail("apply phase 1: cannot stage candidate Prefab");
    }

    Engine::Content::PrefabApplyRecord record;
    record.transactionId = MakeApplyTransactionId(rootEntityId);
    record.recordVersion = 1;
    record.state = Engine::Content::PrefabApplyState::Prepared;
    record.prefabGuid = metadata.prefabGuid;
    record.oldSourceRevision = metadata.sourceRevision;
    record.oldSourceDigest = metadata.sourceDigest;
    record.oldSourcePath = metadata.sourcePath;
    record.newDigest = candidate.digest;
    record.newPath = newPath;
    record.newSourceRevision = -1;
    record.instanceRootEntityId = rootEntityId;
    record.oldInstanceMetadata = PrefabInstanceMetadataJson(
        metadata, metadata.sourceRevision, metadata.sourceDigest,
        metadata.sourcePath, metadata.overrides, metadata.syncState);
    record.newInstanceMetadata = PrefabInstanceMetadataJson(
        metadata, -1, candidate.digest, newPath, {}, Engine::Content::PrefabSyncState::Clean);
    record.oldRevisionSnapshot = static_cast<int64_t>(m_Revisions.Revision());
    record.phase1Revision = -1;
    record.phase2Revision = -1;
    std::string recordError;
    if (!Engine::Content::WritePrefabApplyRecord(m_Root, record, &recordError))
        return Fail("apply phase 1: " + recordError);

    Engine::Content::ContentRegistry candidateRegistry = m_Reg;
    candidateRegistry.Unregister(metadata.prefabGuid);
    if (!candidateRegistry.RegisterExplicit(
            metadata.prefabGuid, newPath, Engine::Content::AssetType::Prefab)) {
        Engine::Content::RemovePrefabApplyRecord(
            m_Root, metadata.prefabGuid, rootEntityId, nullptr);
        return Fail("apply phase 1: registry rebind failed");
    }
    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene)) {
        Engine::Content::RemovePrefabApplyRecord(
            m_Root, metadata.prefabGuid, rootEntityId, nullptr);
        return Fail("apply phase 1: committed Scene unreadable");
    }

    const auto published = m_Revisions.Publish(
        m_Root, candidateRegistry.SerializeManifest(), committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) {
            record.state = Engine::Content::PrefabApplyState::RecoveryRequired;
            const bool recordWritten =
                Engine::Content::WritePrefabApplyRecord(
                    m_Root, record, nullptr);
            m_RecoveryRequired = true;
            return Fail("apply phase 1: " + published.diagnostic +
                        (recordWritten ? "" : " (record update failed)"));
        }
        Engine::Content::RemovePrefabApplyRecord(
            m_Root, metadata.prefabGuid, rootEntityId, nullptr);
        return Fail("apply phase 1: " + published.diagnostic);
    }

    record.state = Engine::Content::PrefabApplyState::PrefabPublished;
    record.phase1Revision = published.revision;
    record.newSourceRevision = published.revision;
    record.newInstanceMetadata["sourceRevision"] = published.revision;
    if (!Engine::Content::WritePrefabApplyRecord(m_Root, record, &recordError)) {
        m_RecoveryRequired = true;
        return Fail("apply phase 1: published but recovery record update failed: " +
                    recordError);
    }
    m_Reg = std::move(candidateRegistry);
    Engine::Log::Info("[EditorBridge] prefab apply phase 1: {} -> {} (revision {})",
                      metadata.sourcePath, newPath, published.revision);
    return true;
}

bool EditorSession::PublishPrefabApplyPhase2(uint64 rootEntityId) {
    if (!m_EditScene || IsPlaying())
        return Fail("apply phase 2: edit scene unavailable");
    Engine::PrefabInstanceMetadata liveMetadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &liveMetadata))
        return Fail("apply phase 2: instance metadata not found");
    const std::string recordPath = Engine::Content::PrefabApplyRecordPath(
        m_Root, liveMetadata.prefabGuid, rootEntityId);
    Engine::Content::PrefabApplyRecord record;
    std::string recordError;
    if (!Engine::Content::ReadPrefabApplyRecord(
            m_Root, liveMetadata.prefabGuid, rootEntityId, &record, &recordError))
        return Fail("apply phase 2: recovery record unavailable: " + recordError);
    if (record.state != Engine::Content::PrefabApplyState::PrefabPublished)
        return Fail("apply phase 2: recovery record is not PrefabPublished");
    if (record.phase1Revision < 1 ||
        m_Revisions.Revision() != record.phase1Revision)
        return Fail("apply phase 2: current revision is not Phase 1 revision");
    const auto candidateResolved = m_Root.Resolve(record.newPath);
    if (!candidateResolved.ok)
        return Fail("apply phase 2: Phase 1 candidate path rejected");
    std::vector<uint8_t> candidateBytes;
    if (!ReadAllBytesFromPath(candidateResolved.path, &candidateBytes))
        return Fail("apply phase 2: Phase 1 candidate bytes unreadable");
    const std::string candidateText(candidateBytes.begin(), candidateBytes.end());
    if (Engine::Content::ComputePrefabDigest(candidateText) != record.newDigest)
        return Fail("apply phase 2: Phase 1 candidate digest mismatch");
    const auto phase1Prefab = Engine::Content::ReadPrefab(
        candidateText, &liveMetadata.prefabGuid);
    if (!phase1Prefab.ok)
        return Fail("apply phase 2: Phase 1 candidate invalid: " + phase1Prefab.error);
    const auto currentAsset = m_Reg.SerializeManifest();
    const std::string registeredPath = m_Reg.ResolvePath(liveMetadata.prefabGuid);
    if (registeredPath != record.newPath)
        return Fail("apply phase 2: live registry is not the Phase 1 candidate");

    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene))
        return Fail("apply phase 2: committed Scene unreadable");
    nlohmann::json sceneJson;
    Engine::Content::SceneSnapshot snapshot;
    std::string sceneError;
    try {
        sceneJson = nlohmann::json::parse(committedScene);
    } catch (const std::exception& ex) {
        return Fail("apply phase 2: committed Scene JSON invalid: " +
                    std::string(ex.what()));
    }
    if (!Engine::Content::DeserializeSnapshot(sceneJson, snapshot, sceneError))
        return Fail("apply phase 2: committed Scene invalid: " + sceneError);
    Engine::Content::SerializedEntity* target = nullptr;
    for (auto& entity : snapshot.entities) {
        if (entity.entityId != rootEntityId) continue;
        if (target) return Fail("apply phase 2: duplicate instance anchor");
        target = &entity;
    }
    if (!target || !target->prefabInstance.has_value())
        return Fail("apply phase 2: Instance metadata missing in committed Scene");
    auto& serialized = *target->prefabInstance;
    if (serialized.prefabGuid != record.prefabGuid)
        return Fail("apply phase 2: committed Instance GUID mismatch");
    if (serialized.sourceDigest != record.oldSourceDigest ||
        serialized.sourcePath != record.oldSourcePath)
        return Fail("apply phase 2: committed Instance source is not Phase 1 source");
    serialized.sourceRevision = record.phase1Revision;
    serialized.sourceDigest = record.newDigest;
    serialized.sourcePath = record.newPath;
    serialized.legacySourceIdentity = false;
    serialized.overrides.clear();
    serialized.syncState = Engine::Content::PrefabSyncState::Clean;
    const std::string newScene = Engine::Content::SerializeSnapshot(snapshot).dump(2);

    const auto published = m_Revisions.Publish(
        m_Root, currentAsset, newScene);
    if (!published.ok) {
        if (published.recoveryRequired) {
            record.state = Engine::Content::PrefabApplyState::RecoveryRequired;
            Engine::Content::WritePrefabApplyRecord(m_Root, record, nullptr);
            m_RecoveryRequired = true;
        }
        return Fail("apply phase 2: " + published.diagnostic);
    }

    record.state = Engine::Content::PrefabApplyState::ScenePublished;
    record.phase2Revision = published.revision;
    record.newInstanceMetadata["sourceRevision"] = record.phase1Revision;
    if (!Engine::Content::WritePrefabApplyRecord(m_Root, record, &recordError)) {
        m_RecoveryRequired = true;
        return Fail("apply phase 2: published but recovery record update failed: " +
                    recordError);
    }

    Engine::PrefabInstanceMetadata newMetadata = liveMetadata;
    newMetadata.sourceRevision = record.phase1Revision;
    newMetadata.sourceDigest = record.newDigest;
    newMetadata.sourcePath = record.newPath;
    newMetadata.legacySourceIdentity = false;
    newMetadata.overrides.clear();
    newMetadata.syncState = Engine::Content::PrefabSyncState::Clean;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, newMetadata,
                                                 &recordError)) {
        m_RecoveryRequired = true;
        return Fail("apply phase 2: live metadata update failed: " + recordError);
    }
    m_SavedSceneJson = newScene;
    m_HasSavedScene = true;
    RefreshDirtyFromScene();
    Engine::Content::RemovePrefabApplyRecord(
        m_Root, liveMetadata.prefabGuid, rootEntityId, nullptr);
    Engine::Log::Info("[EditorBridge] prefab apply phase 2: instance {} (revision {})",
                      rootEntityId, published.revision);
    return true;
}

bool EditorSession::ApplyPrefabOverrides(uint64 rootEntityId) {
    if (!m_EditScene || IsPlaying())
        return Fail("apply: edit scene unavailable");
    if (m_RecoveryRequired)
        return Fail("apply: RecoveryRequired");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("apply: instance metadata not found");
    if (metadata.legacySourceIdentity)
        return Fail("apply: legacy source identity");
    if (metadata.overrides.empty())
        return Fail("apply: no overrides");
    if (metadata.syncState == Engine::Content::PrefabSyncState::RecoveryRequired ||
        metadata.syncState == Engine::Content::PrefabSyncState::Conflict)
        return Fail("apply: instance is not editable");
    if (!PublishPrefabApplyPhase1(rootEntityId)) return false;
    if (!PublishPrefabApplyPhase2(rootEntityId)) return false;
    Engine::Log::Info("[EditorBridge] prefab apply completed: root {}", rootEntityId);
    return true;
}

bool EditorSession::LoadPrefabReconciliationSources(
    uint64 rootEntityId, Engine::Content::PrefabReconciliationSources* out) {
    if (!out) return false;
    *out = {};
    if (!m_EditScene || IsPlaying())
        return Fail("reconciliation sources: edit scene unavailable");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("reconciliation sources: instance metadata not found");
    if (metadata.legacySourceIdentity)
        return Fail("reconciliation sources: legacy source identity");
    std::string sourceError;
    if (!LoadPrefabSourceForInstance(metadata, &out->base.model, &sourceError))
        return Fail("reconciliation sources: base unavailable: " + sourceError);
    out->base.prefabGuid = metadata.prefabGuid;
    out->base.projectRevision = metadata.sourceRevision;
    out->base.contentDigest = metadata.sourceDigest;
    out->base.contentPath = metadata.sourcePath;

    const std::string latestPath = m_Reg.ResolvePath(metadata.prefabGuid);
    if (latestPath.empty())
        return Fail("reconciliation sources: latest source not registered");
    if (!ReadReconciliationSource(m_Root, latestPath, metadata.prefabGuid, "",
                                  static_cast<int64_t>(m_Revisions.Revision()),
                                  &out->latest, &sourceError))
        return Fail("reconciliation sources: latest unavailable: " + sourceError);
    if (!BuildLocalReconciliationSnapshot(*m_EditScene, rootEntityId, metadata,
                                          &out->local, &sourceError))
        return Fail("reconciliation sources: local unavailable: " + sourceError);
    out->outOfDate = out->base.contentDigest != out->latest.contentDigest ||
                     out->base.contentPath != out->latest.contentPath;
    return true;
}

bool EditorSession::PreviewPrefabReconciliation(
    uint64 rootEntityId, Engine::Content::PrefabReconciliationPlan* out) {
    if (!out) return false;
    *out = {};
    Engine::Content::PrefabReconciliationSources sources;
    if (!LoadPrefabReconciliationSources(rootEntityId, &sources))
        return false;
    *out = Engine::Content::BuildPrefabReconciliation(
        sources.base, sources.latest, sources.local);
    return true;
}

bool EditorSession::PersistPrefabReconciliationConflicts(
    uint64 rootEntityId,
    const Engine::Content::PrefabReconciliationPlan& plan) {
    if (!m_EditScene || IsPlaying())
        return Fail("persist reconciliation conflicts: edit scene unavailable");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("persist reconciliation conflicts: metadata not found");
    if (metadata.legacySourceIdentity)
        return Fail("persist reconciliation conflicts: legacy source identity");
    if (metadata.syncState == Engine::Content::PrefabSyncState::ApplyPending ||
        metadata.syncState == Engine::Content::PrefabSyncState::RecoveryRequired)
        return Fail("persist reconciliation conflicts: instance state is not editable");
    if (plan.validationErrors.size() > 0)
        return Fail("persist reconciliation conflicts: plan validation failed");
    if (plan.hasConflicts && plan.conflicts.empty())
        return Fail("persist reconciliation conflicts: conflicts are missing");

    Engine::Content::PrefabReconciliationSources sources;
    if (!LoadPrefabReconciliationSources(rootEntityId, &sources))
        return false;

    if (plan.hasConflicts) {
        Engine::Content::PrefabReconciliationState state;
        state.latestSourceDigest = sources.latest.contentDigest;
        state.latestSourcePath = sources.latest.contentPath;
        state.conflicts = plan.conflicts;
        std::string stateError;
        if (!Engine::Content::ValidatePrefabReconciliationState(
                state, &stateError)) {
            return Fail("persist reconciliation conflicts: " + stateError);
        }
        metadata.reconciliation = std::move(state);
        metadata.syncState = Engine::Content::PrefabSyncState::Conflict;
    } else {
        metadata.reconciliation.reset();
        metadata.syncState = metadata.overrides.empty()
            ? Engine::Content::PrefabSyncState::Clean
            : Engine::Content::PrefabSyncState::Overrides;
    }
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, metadata,
                                                 &metadataError))
        return Fail("persist reconciliation conflicts: " + metadataError);
    MarkDirty();
    return true;
}

bool EditorSession::ResolvePrefabReconciliationConflict(
    uint64 rootEntityId, const std::string& conflictId,
    const std::string& action, const nlohmann::json& value) {
    if (!m_EditScene || IsPlaying())
        return Fail("resolve reconciliation conflict: edit scene unavailable");
    if (action != "take_source" && action != "keep_local" &&
        action != "manual_edit") {
        return Fail("resolve reconciliation conflict: invalid action");
    }
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("resolve reconciliation conflict: metadata not found");
    if (metadata.legacySourceIdentity)
        return Fail("resolve reconciliation conflict: legacy source identity");
    if (metadata.syncState == Engine::Content::PrefabSyncState::ApplyPending ||
        metadata.syncState == Engine::Content::PrefabSyncState::RecoveryRequired)
        return Fail("resolve reconciliation conflict: instance is not editable");

    Engine::Content::PrefabReconciliationSources sources;
    if (!LoadPrefabReconciliationSources(rootEntityId, &sources))
        return false;
    if (!metadata.reconciliation.has_value()) {
        const auto plan = Engine::Content::BuildPrefabReconciliation(
            sources.base, sources.latest, sources.local);
        const auto planIt = std::find_if(
            plan.conflicts.begin(), plan.conflicts.end(),
            [&](const Engine::Content::PrefabReconciliationConflict& conflict) {
                return conflict.id == conflictId;
            });
        if (planIt == plan.conflicts.end())
            return Fail("resolve reconciliation conflict: conflict not found");
        Engine::Content::PrefabReconciliationState state;
        state.latestSourceDigest = sources.latest.contentDigest;
        state.latestSourcePath = sources.latest.contentPath;
        state.conflicts = plan.conflicts;
        std::string stateError;
        if (!Engine::Content::ValidatePrefabReconciliationState(
                state, &stateError))
            return Fail("resolve reconciliation conflict: " + stateError);
        metadata.reconciliation = std::move(state);
        metadata.syncState = Engine::Content::PrefabSyncState::Conflict;
    } else {
        if (metadata.syncState != Engine::Content::PrefabSyncState::Conflict)
            return Fail("resolve reconciliation conflict: state is not conflict");
        if (metadata.reconciliation->latestSourceDigest !=
                sources.latest.contentDigest ||
            metadata.reconciliation->latestSourcePath !=
                sources.latest.contentPath) {
            return Fail("resolve reconciliation conflict: latest source changed");
        }
    }

    auto stateIt = std::find_if(
        metadata.reconciliation->conflicts.begin(),
        metadata.reconciliation->conflicts.end(),
        [&](const Engine::Content::PrefabReconciliationConflict& conflict) {
            return conflict.id == conflictId;
        });
    if (stateIt == metadata.reconciliation->conflicts.end())
        return Fail("resolve reconciliation conflict: conflict not found");
    const Engine::Content::PrefabReconciliationConflict conflict = *stateIt;

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("resolve prefab conflict")) return false;
    std::string failure;
    auto cancel = [&]() {
        if (autoTxn) CancelSceneEdit();
    };
    auto failTxn = [&](const std::string& message) {
        cancel();
        return Fail(message);
    };

    const auto mapIt = metadata.localToScene.find(conflict.localId);
    Engine::GameObject* object = mapIt == metadata.localToScene.end()
        ? nullptr
        : m_EditScene->FindByEntityId(mapIt->second);
    if ((conflict.type == Engine::Content::PrefabConflictType::Field ||
         conflict.type == Engine::Content::PrefabConflictType::EntityName ||
         conflict.type == Engine::Content::PrefabConflictType::EntityParent ||
         conflict.type == Engine::Content::PrefabConflictType::EntityDelete ||
         conflict.type == Engine::Content::PrefabConflictType::ComponentSet) &&
        !object) {
        return failTxn("resolve reconciliation conflict: mapped entity missing");
    }

    auto removeOverride = [&](const Engine::Content::PrefabOverrideRecord& record) {
        Engine::Content::RemovePrefabOverride(
            &metadata.overrides, record.localId, record.component, record.field);
    };
    auto applyField = [&](const nlohmann::json& target,
                         const bool applyToScene,
                         const bool persistOverride) -> bool {
        if (applyToScene &&
            !ApplyPrefabFieldValue(object, conflict.component, conflict.field,
                                   target, &failure)) {
            return failTxn("resolve reconciliation conflict: " + failure);
        }
        Engine::Content::PrefabOverrideRecord record;
        record.localId = conflict.localId;
        record.component = conflict.component;
        record.field = conflict.field;
        record.value = target;
        if (!persistOverride) {
            removeOverride(record);
            return true;
        }
        std::string overrideError;
        if (!Engine::Content::AddOrUpdatePrefabOverride(
                sources.latest.model, &metadata.overrides, record,
                &overrideError)) {
            return failTxn("resolve reconciliation conflict: " + overrideError);
        }
        return true;
    };

    switch (conflict.type) {
    case Engine::Content::PrefabConflictType::Field: {
        if (action == "take_source") {
            if (!applyField(conflict.latest, true, false)) return false;
        } else if (action == "keep_local") {
            if (!applyField(conflict.local, false, true)) return false;
        } else {
            if (value.is_null()) {
                return failTxn("resolve reconciliation conflict: manual value missing");
            }
            if (!applyField(value, true, true)) return false;
        }
        break;
    }
    case Engine::Content::PrefabConflictType::EntityName: {
        if (action == "keep_local") break;
        if (action == "take_source") {
            if (!conflict.latest.is_string())
                return failTxn("resolve reconciliation conflict: latest name invalid");
            object->SetName(conflict.latest.get<std::string>());
        } else {
            if (!value.is_string())
                return failTxn("resolve reconciliation conflict: manual name invalid");
            object->SetName(value.get<std::string>());
        }
        break;
    }
    case Engine::Content::PrefabConflictType::EntityParent: {
        if (action == "keep_local") break;
        nlohmann::json parent = action == "take_source" ? conflict.latest : value;
        if (parent.is_null()) {
            if (object->GetSceneEntityId() == rootEntityId)
                return failTxn("resolve reconciliation conflict: root parent is immutable");
            if (!m_EditScene->SetEntityParent(object->GetSceneEntityId(), 0,
                                              Engine::ReparentMode::KeepLocal,
                                              &failure))
                return failTxn("resolve reconciliation conflict: " + failure);
            break;
        }
        if (!parent.is_number_unsigned())
            return failTxn("resolve reconciliation conflict: parent Local ID invalid");
        const uint64_t parentLocal = parent.get<uint64_t>();
        const auto parentIt = metadata.localToScene.find(parentLocal);
        if (parentIt == metadata.localToScene.end())
            return failTxn("resolve reconciliation conflict: parent not mapped");
        if (!m_EditScene->SetEntityParent(object->GetSceneEntityId(),
                                          parentIt->second,
                                          Engine::ReparentMode::KeepLocal,
                                          &failure))
            return failTxn("resolve reconciliation conflict: " + failure);
        break;
    }
    case Engine::Content::PrefabConflictType::EntityDelete: {
        if (action == "keep_local") break;
        if (action != "take_source")
            return failTxn("resolve reconciliation conflict: manual delete is unsupported");
        if (object->GetSceneEntityId() == rootEntityId)
            return failTxn("resolve reconciliation conflict: root delete is unsupported");
        if (object->GetChildren().size() != 0)
            return failTxn("resolve reconciliation conflict: entity has local children");
        if (!m_EditScene->DeleteEntity(object->GetSceneEntityId()))
            return failTxn("resolve reconciliation conflict: entity delete failed");
        metadata.localToScene.erase(conflict.localId);
        for (auto it = metadata.overrides.begin(); it != metadata.overrides.end();) {
            if (it->localId == conflict.localId)
                it = metadata.overrides.erase(it);
            else ++it;
        }
        break;
    }
    case Engine::Content::PrefabConflictType::ComponentSet: {
        if (action == "keep_local") break;
        if (action == "manual_edit")
            return failTxn("resolve reconciliation conflict: manual component edit is unsupported");
        if (conflict.component.empty())
            return failTxn("resolve reconciliation conflict: component is empty");
        if (conflict.latest.is_null()) {
            object->RemoveComponentByName(conflict.component);
        } else {
            if (!conflict.latest.is_object())
                return failTxn("resolve reconciliation conflict: component payload invalid");
            if (!object->HasComponentByName(conflict.component)) {
                if (!object->AddComponentByName(conflict.component))
                    return failTxn("resolve reconciliation conflict: component add failed");
            }
            if (!ApplyPrefabComponentPayload(object, conflict.component,
                                             conflict.latest, &failure))
                return failTxn("resolve reconciliation conflict: " + failure);
        }
        for (auto it = metadata.overrides.begin(); it != metadata.overrides.end();) {
            if (it->localId == conflict.localId &&
                it->component == conflict.component)
                it = metadata.overrides.erase(it);
            else ++it;
        }
        break;
    }
    case Engine::Content::PrefabConflictType::EntityMissing: {
        return failTxn("resolve reconciliation conflict: entity missing requires built-in add");
    }
    case Engine::Content::PrefabConflictType::UnsupportedLocalEdit:
    case Engine::Content::PrefabConflictType::UnsupportedComponentChange: {
        if (action != "keep_local")
            return failTxn("resolve reconciliation conflict: unsupported conflict is keep-local only");
        break;
    }
    }

    std::string localError;
    Engine::Content::PrefabLocalInstanceSnapshot localSnapshot;
    if (!BuildLocalReconciliationSnapshot(*m_EditScene, rootEntityId,
                                          metadata, &localSnapshot, &localError))
        return failTxn("resolve reconciliation conflict: " + localError);
    if (!m_EditScene->ValidateComposition(&failure))
        return failTxn("resolve reconciliation conflict: " + failure);

    metadata.reconciliation->conflicts.erase(stateIt);
    if (metadata.reconciliation->conflicts.empty()) {
        metadata.reconciliation.reset();
        metadata.syncState = metadata.overrides.empty()
            ? Engine::Content::PrefabSyncState::Clean
            : Engine::Content::PrefabSyncState::Overrides;
    } else {
        metadata.syncState = Engine::Content::PrefabSyncState::Conflict;
    }
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, metadata,
                                                 &metadataError))
        return failTxn("resolve reconciliation conflict: " + metadataError);
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    return true;
}
bool EditorSession::GetPrefabReconciliationStatusJson(uint64 rootEntityId,
                                                       std::string* out) {
    if (!out || !m_EditScene)
        return Fail("reconciliation status: edit scene unavailable");
    *out = {};
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("reconciliation status: instance metadata not found");

    nlohmann::json json = nlohmann::json::object();
    json["isInstance"] = true;
    json["rootSceneEntityId"] = rootEntityId;
    json["prefabGuid"] = metadata.prefabGuid.ToHex();
    json["sourceRevision"] = metadata.sourceRevision;
    json["sourceDigest"] = metadata.sourceDigest;
    json["sourcePath"] = metadata.sourcePath;
    json["legacySourceIdentity"] = metadata.legacySourceIdentity;
    json["syncState"] = PrefabSyncStateText(metadata.syncState);
    json["overrideCount"] = metadata.overrides.size();
    json["persistedConflictCount"] = metadata.reconciliation.has_value()
        ? metadata.reconciliation->conflicts.size()
        : 0;
    json["conflictCount"] = metadata.reconciliation.has_value()
        ? metadata.reconciliation->conflicts.size()
        : 0;
    json["conflicts"] = nlohmann::json::array();
    if (metadata.reconciliation.has_value()) {
        for (const auto& conflict : metadata.reconciliation->conflicts)
            json["conflicts"].push_back(PrefabConflictJson(conflict));
        json["latestSourceDigest"] =
            metadata.reconciliation->latestSourceDigest;
        json["latestSourcePath"] = metadata.reconciliation->latestSourcePath;
    }

    bool sourceAvailable = false;
    Engine::Content::PrefabReconciliationSources sources;
    if (!metadata.legacySourceIdentity &&
        LoadPrefabReconciliationSources(rootEntityId, &sources)) {
        sourceAvailable = true;
        json["outOfDate"] = sources.outOfDate;
        json["latestSourceDigest"] = sources.latest.contentDigest;
        json["latestSourcePath"] = sources.latest.contentPath;
        const auto plan = Engine::Content::BuildPrefabReconciliation(
            sources.base, sources.latest, sources.local);
        json["planConflictCount"] = plan.conflicts.size();
        json["hasPendingPlanConflicts"] = plan.hasConflicts;
    } else {
        json["outOfDate"] = false;
        json["planConflictCount"] = 0;
        json["hasPendingPlanConflicts"] = false;
        if (!metadata.legacySourceIdentity)
            json["sourceUnavailableReason"] = GetLastError();
    }
    json["sourceUnavailable"] = !sourceAvailable;
    *out = json.dump();
    return true;
}

bool EditorSession::GetPrefabReconciliationPreviewJson(uint64 rootEntityId,
                                                        std::string* out) {
    if (!out) return false;
    *out = {};
    Engine::Content::PrefabReconciliationSources sources;
    if (!LoadPrefabReconciliationSources(rootEntityId, &sources))
        return false;
    const auto plan = Engine::Content::BuildPrefabReconciliation(
        sources.base, sources.latest, sources.local);
    nlohmann::json json = nlohmann::json::object();
    json["rootSceneEntityId"] = rootEntityId;
    json["baseSourceDigest"] = sources.base.contentDigest;
    json["baseSourcePath"] = sources.base.contentPath;
    json["latestSourceDigest"] = sources.latest.contentDigest;
    json["latestSourcePath"] = sources.latest.contentPath;
    json["latestSourceRevision"] = sources.latest.projectRevision;
    json["outOfDate"] = plan.outOfDate;
    json["plan"] = PrefabReconciliationPlanJson(plan);
    *out = json.dump();
    return true;
}
bool EditorSession::ExecutePrefabReconciliation(uint64 rootEntityId) {
    if (!m_EditScene || IsPlaying())
        return Fail("execute reconciliation: edit scene unavailable");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("execute reconciliation: metadata not found");
    if (metadata.legacySourceIdentity)
        return Fail("execute reconciliation: legacy source identity");
    if (metadata.reconciliation.has_value())
        return Fail("execute reconciliation: unresolved conflicts remain");
    if (metadata.syncState == Engine::Content::PrefabSyncState::ApplyPending ||
        metadata.syncState == Engine::Content::PrefabSyncState::RecoveryRequired)
        return Fail("execute reconciliation: instance is not editable");

    Engine::Content::PrefabReconciliationSources sources;
    if (!LoadPrefabReconciliationSources(rootEntityId, &sources))
        return false;
    const auto plan = Engine::Content::BuildPrefabReconciliation(
        sources.base, sources.latest, sources.local);
    if (!plan.validationErrors.empty())
        return Fail("execute reconciliation: plan validation failed");

    const auto latestEntity = [&](uint64_t localId) -> const Engine::Content::PrefabEntity* {
        for (const auto& entity : sources.latest.model.entities)
            if (entity.localId == localId) return &entity;
        return nullptr;
    };

    std::map<uint64_t, Engine::Content::PrefabEntity> additions;
    for (const auto& change : plan.structural) {
        if (change.kind != Engine::Content::PrefabStructuralChangeKind::AddEntity)
            continue;
        Engine::Content::PrefabEntity entity;
        entity.localId = change.localId;
        entity.name = change.name;
        if (change.parentLocalId != 0)
            entity.parentLocalId = change.parentLocalId;
        entity.localTransform = change.localTransform;
        entity.components = change.components;
        additions.emplace(entity.localId, std::move(entity));
    }
    for (const auto& conflict : plan.conflicts) {
        if (conflict.type != Engine::Content::PrefabConflictType::EntityMissing)
            continue;
        const auto* entity = latestEntity(conflict.localId);
        if (!entity)
            return Fail("execute reconciliation: missing entity has no Latest source");
        if (metadata.overrides.end() != std::find_if(
                metadata.overrides.begin(), metadata.overrides.end(),
                [&](const Engine::Content::PrefabOverrideRecord& record) {
                    return record.localId == conflict.localId;
                })) {
            return Fail("execute reconciliation: missing entity has stale overrides");
        }
        additions.emplace(entity->localId, *entity);
    }

    std::unordered_map<uint64_t, std::shared_ptr<Engine::GameObject>> newObjects;
    for (const auto& item : additions) {
        const auto& entity = item.second;
        if (metadata.localToScene.count(entity.localId))
            return Fail("execute reconciliation: new Local ID is already mapped");
        std::shared_ptr<Engine::GameObject> object;
        std::string buildError;
        if (!BuildPrefabGameObject(entity.name, entity.localTransform,
                                   entity.components, &object, &buildError))
            return Fail("execute reconciliation: " + buildError);
        newObjects.emplace(entity.localId, std::move(object));
    }

    std::vector<uint64_t> addRoots;
    for (const auto& item : additions) {
        const auto& entity = item.second;
        if (!entity.parentLocalId.has_value())
            return Fail("execute reconciliation: new entity requires a mapped parent");
        const auto parent = entity.parentLocalId.value();
        if (newObjects.count(parent)) {
            newObjects[parent]->AddChild(newObjects[entity.localId]);
            continue;
        }
        if (metadata.localToScene.find(parent) == metadata.localToScene.end())
            return Fail("execute reconciliation: new entity parent is not mapped");
        addRoots.push_back(entity.localId);
    }

    std::set<uint64_t> deleteIds;
    for (const auto& change : plan.structural) {
        if (change.kind == Engine::Content::PrefabStructuralChangeKind::DeleteEntity)
            deleteIds.insert(change.localId);
    }
    std::unordered_map<uint64_t, uint64_t> localParent;
    for (const auto& entity : sources.local.entities)
        if (entity.parentLocalId.has_value())
            localParent[entity.localId] = entity.parentLocalId.value();
    auto depth = [&](uint64_t localId) {
        uint64_t result = 0;
        auto it = localParent.find(localId);
        while (it != localParent.end() && result < 256) {
            ++result;
            it = localParent.find(it->second);
            if (it != localParent.end() && it->second == localId) break;
        }
        return result;
    };

    std::set<uint64_t> removedLocalIds;
    std::unordered_set<uint64_t> removedSceneIds;
    std::vector<uint64_t> deleteOrder(deleteIds.begin(), deleteIds.end());
    std::sort(deleteOrder.begin(), deleteOrder.end(),
              [&](uint64_t a, uint64_t b) {
                  const uint64_t da = depth(a);
                  const uint64_t db = depth(b);
                  if (da != db) return da > db;
                  return a < b;
              });

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("execute prefab reconciliation")) return false;
    auto cancel = [&]() {
        if (autoTxn) CancelSceneEdit();
    };
    auto failTxn = [&](const std::string& message) {
        cancel();
        return Fail(message);
    };

    for (const uint64_t localId : addRoots) {
        const auto parentIt = metadata.localToScene.find(
            additions[localId].parentLocalId.value());
        if (parentIt == metadata.localToScene.end())
            return failTxn("execute reconciliation: new entity parent disappeared");
        std::string addError;
        std::shared_ptr<Engine::GameObject> object = newObjects[localId];
        if (!m_EditScene->AddEntity(object, parentIt->second, &addError))
            return failTxn("execute reconciliation: add entity failed: " + addError);
    }
    for (const auto& item : newObjects) {
        const uint64_t sceneId = item.second->GetSceneEntityId();
        if (sceneId == 0)
            return failTxn("execute reconciliation: new entity has no Scene ID");
        metadata.localToScene[item.first] = sceneId;
    }

    for (const uint64_t localId : deleteOrder) {
        const auto mapIt = metadata.localToScene.find(localId);
        if (mapIt == metadata.localToScene.end()) continue;
        Engine::GameObject* object = m_EditScene->FindByEntityId(mapIt->second);
        if (!object) {
            removedLocalIds.insert(localId);
            continue;
        }
        if (object->GetSceneEntityId() == rootEntityId)
            return failTxn("execute reconciliation: root delete is unsupported");
        std::function<void(Engine::GameObject*)> collect =
            [&](Engine::GameObject* node) {
                if (!node) return;
                removedSceneIds.insert(node->GetSceneEntityId());
                for (const auto& child : node->GetChildren())
                    collect(child.get());
            };
        collect(object);
        for (const auto& [otherLocal, otherScene] : metadata.localToScene) {
            if (otherScene == object->GetSceneEntityId()) continue;
            if (removedSceneIds.count(otherScene)) {
                removedLocalIds.insert(otherLocal);
            }
        }
        removedLocalIds.insert(localId);
        if (!m_EditScene->DeleteEntity(object->GetSceneEntityId()))
            return failTxn("execute reconciliation: delete entity failed");
    }
    for (const uint64_t localId : removedLocalIds) {
        metadata.localToScene.erase(localId);
    }
    for (auto it = metadata.localToScene.begin(); it != metadata.localToScene.end();) {
        if (!m_EditScene->FindByEntityId(it->second))
            it = metadata.localToScene.erase(it);
        else ++it;
    }
    for (auto it = metadata.overrides.begin(); it != metadata.overrides.end();) {
        if (removedLocalIds.count(it->localId))
            it = metadata.overrides.erase(it);
        else ++it;
    }

    auto objectFor = [&](uint64_t localId) -> Engine::GameObject* {
        const auto it = metadata.localToScene.find(localId);
        return it == metadata.localToScene.end()
            ? nullptr
            : m_EditScene->FindByEntityId(it->second);
    };
    for (const auto& change : plan.structural) {
        if (change.kind == Engine::Content::PrefabStructuralChangeKind::AddEntity ||
            change.kind == Engine::Content::PrefabStructuralChangeKind::DeleteEntity)
            continue;
        Engine::GameObject* object = objectFor(change.localId);
        if (!object) {
            if (change.kind == Engine::Content::PrefabStructuralChangeKind::SetName ||
                change.kind == Engine::Content::PrefabStructuralChangeKind::SetParent ||
                change.kind == Engine::Content::PrefabStructuralChangeKind::AddComponent ||
                change.kind == Engine::Content::PrefabStructuralChangeKind::RemoveComponent)
                continue;
            return failTxn("execute reconciliation: structural entity missing");
        }
        if (change.kind == Engine::Content::PrefabStructuralChangeKind::SetName) {
            if (object->GetName() != change.name) object->SetName(change.name);
        } else if (change.kind == Engine::Content::PrefabStructuralChangeKind::SetParent) {
            uint64_t parentSceneId = 0;
            if (change.parentLocalId != 0) {
                const auto parentIt = metadata.localToScene.find(change.parentLocalId);
                if (parentIt == metadata.localToScene.end())
                    return failTxn("execute reconciliation: parent not mapped");
                parentSceneId = parentIt->second;
            }
            if (object->GetSceneEntityId() != rootEntityId &&
                m_EditScene->GetParentEntityId(object->GetSceneEntityId()) != parentSceneId) {
                std::string parentError;
                if (!m_EditScene->SetEntityParent(object->GetSceneEntityId(),
                                                  parentSceneId,
                                                  Engine::ReparentMode::KeepLocal,
                                                  &parentError))
                    return failTxn("execute reconciliation: " + parentError);
            }
        } else if (change.kind == Engine::Content::PrefabStructuralChangeKind::AddComponent) {
            if (object->HasComponentByName(change.component)) continue;
            auto component = Engine::ComponentRegistryGo::Create(change.component);
            if (!component || !component->Deserialize(change.data) ||
                !object->Attach(component))
                return failTxn("execute reconciliation: component add failed");
        } else if (change.kind == Engine::Content::PrefabStructuralChangeKind::RemoveComponent) {
            if (object->HasComponentByName(change.component))
                object->RemoveComponentByName(change.component);
            for (auto it = metadata.overrides.begin(); it != metadata.overrides.end();) {
                if (it->localId == change.localId &&
                    it->component == change.component)
                    it = metadata.overrides.erase(it);
                else ++it;
            }
        }
    }

    std::string fieldError;
    for (const auto& field : plan.fields) {
        if (field.decision == Engine::Content::PrefabReconciliationDecision::Conflict)
            continue;
        Engine::GameObject* object = objectFor(field.localId);
        if (!object) return failTxn("execute reconciliation: field entity missing");
        if (field.decision == Engine::Content::PrefabReconciliationDecision::TakeLatest) {
            if (!ApplyPrefabFieldValue(object, field.component, field.field,
                                       field.latest, &fieldError))
                return failTxn("execute reconciliation: " + fieldError);
        } else {
            Engine::Content::PrefabOverrideRecord record;
            record.localId = field.localId;
            record.component = field.component;
            record.field = field.field;
            record.value = field.local;
            if (!Engine::Content::AddOrUpdatePrefabOverride(
                    sources.latest.model, &metadata.overrides, record,
                    &fieldError))
                return failTxn("execute reconciliation: " + fieldError);
        }
    }
    for (const auto& conflict : plan.conflicts) {
        if (conflict.type != Engine::Content::PrefabConflictType::Field)
            continue;
        const bool localIsLatest = conflict.local == conflict.latest;
        const bool hasOverride = metadata.overrides.end() != std::find_if(
            metadata.overrides.begin(), metadata.overrides.end(),
            [&](const Engine::Content::PrefabOverrideRecord& record) {
                return record.localId == conflict.localId &&
                       record.component == conflict.component &&
                       record.field == conflict.field;
            });
        if (!localIsLatest && !hasOverride)
            return failTxn("execute reconciliation: unresolved field conflict remains");
    }

    std::unordered_set<uint64_t> removedOverrides;
    for (auto it = metadata.overrides.begin(); it != metadata.overrides.end();) {
        if (metadata.localToScene.find(it->localId) == metadata.localToScene.end()) {
            it = metadata.overrides.erase(it);
            continue;
        }
        if (removedLocalIds.count(it->localId)) {
            it = metadata.overrides.erase(it);
            continue;
        }
        nlohmann::json latestBase;
        std::string baseError;
        if (!Engine::Content::ReadPrefabOverrideBaseValue(
                sources.latest.model, it->localId, it->component, it->field,
                &latestBase, &baseError))
            return failTxn("execute reconciliation: " + baseError);
        if (latestBase == it->value)
            it = metadata.overrides.erase(it);
        else ++it;
    }
    Engine::Content::NormalizePrefabOverrides(&metadata.overrides);
    metadata.overrides = metadata.overrides;

    metadata.sourceRevision = static_cast<int64_t>(m_Revisions.Revision());
    metadata.sourceDigest = sources.latest.contentDigest;
    metadata.sourcePath = sources.latest.contentPath;
    metadata.legacySourceIdentity = false;
    metadata.reconciliation.reset();
    metadata.syncState = metadata.overrides.empty()
        ? Engine::Content::PrefabSyncState::Clean
        : Engine::Content::PrefabSyncState::Overrides;

    std::string mappingError;
    Engine::Content::PrefabLocalInstanceSnapshot localSnapshot;
    if (!BuildLocalReconciliationSnapshot(*m_EditScene, rootEntityId, metadata,
                                          &localSnapshot, &mappingError))
        return failTxn("execute reconciliation: " + mappingError);
    std::string compositionError;
    if (!m_EditScene->ValidateComposition(&compositionError))
        return failTxn("execute reconciliation: " + compositionError);
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, metadata,
                                                 &metadataError))
        return failTxn("execute reconciliation: " + metadataError);

    RealignBindings();
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    Emit(EV_ENTITY_MOVED, "root=" + std::to_string(rootEntityId) +
                              ";reconcile=source");
    return true;
}
bool EditorSession::RevertPrefabOverride(uint64 rootEntityId, uint64 localId,
                                         const std::string& component,
                                         const std::string& field) {
    if (!m_EditScene || IsPlaying())
        return Fail("revert override: edit scene unavailable");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("revert override: instance metadata not found");
    Engine::Content::PrefabModel source;
    std::string sourceError;
    if (!LoadPrefabSourceForInstance(metadata, &source, &sourceError))
        return Fail("revert override: " + sourceError);
    const auto it = std::find_if(
        metadata.overrides.begin(), metadata.overrides.end(),
        [&](const Engine::Content::PrefabOverrideRecord& record) {
            return record.localId == localId && record.component == component &&
                   record.field == field;
        });
    if (it == metadata.overrides.end())
        return Fail("revert override: matching override not found");
    nlohmann::json base;
    if (!Engine::Content::ReadPrefabOverrideBaseValue(
            source, localId, component, field, &base, &sourceError))
        return Fail("revert override: " + sourceError);
    const auto sceneIt = metadata.localToScene.find(localId);
    if (sceneIt == metadata.localToScene.end())
        return Fail("revert override: Local ID is not mapped");
    Engine::GameObject* object = m_EditScene->FindByEntityId(sceneIt->second);
    if (!object)
        return Fail("revert override: mapped entity not found");

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("revert override")) return false;
    auto cancel = [&]() {
        if (autoTxn) CancelSceneEdit();
    };

    if (component == "Transform") {
        auto& transform = object->GetTransform();
        Engine::Vec3 position = transform.GetPosition();
        Engine::Quat rotation = transform.GetRotationQuat();
        Engine::Vec3 scale = transform.GetScale();
        float value = 0.0f;
        if (!base.is_number()) {
            cancel();
            return Fail("revert override: Transform base is not numeric");
        }
        value = base.get<float>();
        if (field == "position.x") position.x = value;
        else if (field == "position.y") position.y = value;
        else if (field == "position.z") position.z = value;
        else if (field == "rotation.x") rotation.x = value;
        else if (field == "rotation.y") rotation.y = value;
        else if (field == "rotation.z") rotation.z = value;
        else if (field == "rotation.w") rotation.w = value;
        else if (field == "scale.x") scale.x = value;
        else if (field == "scale.y") scale.y = value;
        else if (field == "scale.z") scale.z = value;
        else {
            cancel();
            return Fail("revert override: unsupported Transform field");
        }
        transform.SetPosition(position);
        transform.SetRotationQuat(rotation);
        transform.SetScale(scale);
    } else {
        Engine::Component* componentObject =
            object->GetComponentByName(component);
        if (!componentObject) {
            cancel();
            return Fail("revert override: component not found");
        }
        int propertyIndex = FindPropertyIndex(componentObject, field);
        if (propertyIndex < 0) {
            cancel();
            return Fail("revert override: property not found");
        }
        Engine::ComponentPropertyDesc desc;
        if (!componentObject->GetPropertyDesc(
                static_cast<size_t>(propertyIndex), &desc)) {
            cancel();
            return Fail("revert override: property descriptor unavailable");
        }
        Engine::ComponentPropertyValue value;
        if (!JsonToPropertyValue(base, desc.type, &value) ||
            !componentObject->SetPropertyValue(
                static_cast<size_t>(propertyIndex), value)) {
            cancel();
            return Fail("revert override: base value rejected");
        }
    }

    metadata.overrides.erase(it);
    metadata.syncState = metadata.overrides.empty()
        ? Engine::Content::PrefabSyncState::Clean
        : Engine::Content::PrefabSyncState::Overrides;
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, metadata,
                                                 &metadataError)) {
        cancel();
        return Fail("revert override: " + metadataError);
    }
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    Emit(EV_COMPONENT_CHANGED,
         "root=" + std::to_string(rootEntityId) + ";revert=" + component +
             "." + field);
    return true;
}

bool EditorSession::RevertPrefabInstance(uint64 rootEntityId) {
    if (!m_EditScene || IsPlaying())
        return Fail("revert instance: edit scene unavailable");
    Engine::PrefabInstanceMetadata metadata;
    if (!m_EditScene->GetPrefabInstanceMetadata(rootEntityId, &metadata))
        return Fail("revert instance: metadata not found");
    if (metadata.overrides.empty())
        return Fail("revert instance: no overrides");
    Engine::Content::PrefabModel source;
    std::string sourceError;
    if (!LoadPrefabSourceForInstance(metadata, &source, &sourceError))
        return Fail("revert instance: " + sourceError);
    if (metadata.localToScene.size() != source.entities.size())
        return Fail("revert instance: Local ID mapping count mismatch");
    std::unordered_set<uint64> mappedScenes;
    for (const auto& [local, scene] : metadata.localToScene)
        mappedScenes.insert(scene);
    std::unordered_set<uint64> subtreeScenes;
    std::function<void(const Engine::GameObject*)> collectScenes =
        [&](const Engine::GameObject* node) {
            if (!node) return;
            subtreeScenes.insert(node->GetSceneEntityId());
            for (const auto& child : node->GetChildren())
                collectScenes(child.get());
        };
    collectScenes(m_EditScene->FindByEntityId(rootEntityId));
    if (subtreeScenes != mappedScenes)
        return Fail("revert instance: Scene subtree contains unmapped entities");

    std::vector<Engine::GameObject*> objects;
    objects.reserve(source.entities.size());
    for (const auto& entity : source.entities) {
        const auto mapped = metadata.localToScene.find(entity.localId);
        if (mapped == metadata.localToScene.end())
            return Fail("revert instance: source Local ID is unmapped");
        Engine::GameObject* object = m_EditScene->FindByEntityId(mapped->second);
        if (!object)
            return Fail("revert instance: mapped entity not found");
        std::unordered_set<std::string> sourceTypes;
        for (const auto& component : entity.components)
            sourceTypes.insert(component.type);
        std::unordered_set<std::string> currentTypes;
        object->ForEachComponent([&](const Engine::Component& component) {
            if (const char* type = component.GetComponentTypeName())
                currentTypes.insert(type);
        });
        if (sourceTypes != currentTypes)
            return Fail("revert instance: component set mismatch on local id " +
                        std::to_string(entity.localId));
        objects.push_back(object);
    }

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("revert prefab instance")) return false;
    auto cancel = [&]() {
        if (autoTxn) CancelSceneEdit();
    };

    for (size_t i = 0; i < source.entities.size(); ++i) {
        const auto& entity = source.entities[i];
        Engine::GameObject* object = objects[i];
        object->SetName(entity.name);
        auto& transform = object->GetTransform();
        transform.SetPosition(entity.localTransform.position);
        transform.SetRotationQuat(entity.localTransform.rotation);
        transform.SetScale(entity.localTransform.scale);
        for (const auto& componentData : entity.components) {
            Engine::Component* component =
                object->GetComponentByName(componentData.type);
            if (!component) {
                cancel();
                return Fail("revert instance: component missing");
            }
            if (!component->Deserialize(componentData.data)) {
                cancel();
                return Fail("revert instance: component data rejected");
            }
        }
    }

    metadata.overrides.clear();
    metadata.syncState = Engine::Content::PrefabSyncState::Clean;
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootEntityId, metadata,
                                                 &metadataError)) {
        cancel();
        return Fail("revert instance: " + metadataError);
    }
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    Emit(EV_COMPONENT_CHANGED,
         "root=" + std::to_string(rootEntityId) + ";revert=instance");
    return true;
}

bool EditorSession::GetEntityTransformById(uint64 entityId,
                                           float out9[9]) const {
    if (!out9 || !m_EditScene) return false;
    const Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    if (!obj) return false;
    const auto& t = obj->GetTransform();
    const Engine::Vec3 p = t.GetPosition();
    const Engine::Vec3 r = t.GetRotationEuler();
    const Engine::Vec3 s = t.GetScale();
    out9[0] = p.x; out9[1] = p.y; out9[2] = p.z;
    out9[3] = r.x; out9[4] = r.y; out9[5] = r.z;
    out9[6] = s.x; out9[7] = s.y; out9[8] = s.z;
    return true;
}

bool EditorSession::SetEntityTransformById(uint64 entityId,
                                           const float pos3[3],
                                           const float euler3[3],
                                           const float scale3[3]) {
    if (!pos3 || !euler3 || !scale3 || !m_EditScene || IsPlaying())
        return false;
    Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    if (!obj) return Fail("set transform: entity not found");
    for (int i = 0; i < 9; ++i) {
        const float v = i < 3 ? pos3[i] : (i < 6 ? euler3[i - 3]
                                                  : scale3[i - 6]);
        if (!std::isfinite(v)) return Fail("transform: non-finite value");
    }

    bool isInstance = false;
    uint64_t instanceRoot = 0;
    uint64_t localId = 0;
    Engine::PrefabInstanceMetadata instanceMeta;
    Engine::Content::PrefabModel sourceModel;
    if (m_EditScene->FindPrefabInstanceForEntity(entityId, &instanceRoot,
                                                  &instanceMeta)) {
        isInstance = true;
        for (const auto& [local, scene] : instanceMeta.localToScene)
            if (scene == entityId) { localId = local; break; }
        if (localId == 0)
            return Fail("set transform: instance local id not mapped");
        std::string sourceError;
        if (!LoadPrefabSourceForInstance(instanceMeta, &sourceModel,
                                          &sourceError))
            return Fail("set transform: " + sourceError);
    }

    const Engine::Vec3 oldPosition = obj->GetTransform().GetPosition();
    const Engine::Quat oldRotation = obj->GetTransform().GetRotationQuat();
    const Engine::Vec3 oldScale = obj->GetTransform().GetScale();
    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("transform")) return false;
    auto abandon = [&]() {
        m_SceneTxnActive = false;
        m_SceneTxnBefore.clear();
        m_SceneTxnLabel.clear();
    };
    auto& t = obj->GetTransform();
    t.SetPosition(pos3[0], pos3[1], pos3[2]);
    t.SetRotationEuler(Engine::Vec3(euler3[0], euler3[1], euler3[2]));
    t.SetScale(Engine::Vec3(scale3[0], scale3[1], scale3[2]));

    if (isInstance) {
        const Engine::Quat q = t.GetRotationQuat();
        const Engine::Vec3 p = t.GetPosition();
        const Engine::Vec3 s = t.GetScale();
        auto update = [&](const char* component, const char* field,
                          double value) {
            Engine::Content::PrefabOverrideRecord record;
            record.localId = localId;
            record.component = component;
            record.field = field;
            record.value = value;
            return Engine::Content::AddOrUpdatePrefabOverride(
                sourceModel, &instanceMeta.overrides, record, nullptr);
        };
        bool tracked = true;
        tracked = tracked && update("Transform", "position.x", p.x);
        tracked = tracked && update("Transform", "position.y", p.y);
        tracked = tracked && update("Transform", "position.z", p.z);
        tracked = tracked && update("Transform", "rotation.x", q.x);
        tracked = tracked && update("Transform", "rotation.y", q.y);
        tracked = tracked && update("Transform", "rotation.z", q.z);
        tracked = tracked && update("Transform", "rotation.w", q.w);
        tracked = tracked && update("Transform", "scale.x", s.x);
        tracked = tracked && update("Transform", "scale.y", s.y);
        tracked = tracked && update("Transform", "scale.z", s.z);
        instanceMeta.syncState = instanceMeta.overrides.empty()
            ? Engine::Content::PrefabSyncState::Clean
            : Engine::Content::PrefabSyncState::Overrides;
        std::string metadataError;
        if (!tracked || !m_EditScene->SetPrefabInstanceMetadata(
                            instanceRoot, instanceMeta, &metadataError)) {
            t.SetPosition(oldPosition);
            t.SetRotationQuat(oldRotation);
            t.SetScale(oldScale);
            if (autoTxn) abandon();
            return Fail("set transform: override tracking failed" +
                        (metadataError.empty() ? std::string()
                                               : ": " + metadataError));
        }
    }

    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    Emit(EV_ENTITY_MOVED, "id=" + std::to_string(entityId));
    return true;
}

bool EditorSession::RenameEntityById(uint64 entityId,
                                     const std::string& newName) {
    if (IsPlaying()) return Fail("rename entity ignored while playing");
    if (!m_EditScene) return Fail("rename entity: no project loaded");
    Engine::GameObject* obj = m_EditScene->FindByEntityId(entityId);
    if (!obj) return Fail("rename entity: not found");
    std::string cleaned = newName;
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    if (cleaned.empty() ||
        std::all_of(cleaned.begin(), cleaned.end(), isSpace))
        return Fail("rename entity: empty name rejected");
    if (obj->GetName() != cleaned && m_EditScene->FindObject(cleaned) != nullptr)
        return Fail("rename entity: duplicate name \"" + cleaned + "\"");
    const std::string old = obj->GetName();
    obj->SetName(cleaned);
    MarkDirty();
    Emit(EV_ENTITY_RENAMED,
         "id=" + std::to_string(entityId) + ";old=" + old + ";new=" + cleaned);
    return true;
}

bool EditorSession::DuplicateEntityById(uint64 entityId, bool includeSubtree,
                                        uint64* newRootId) {
    if (newRootId) *newRootId = 0;
    if (IsPlaying()) return Fail("duplicate entity ignored while playing");
    if (!m_EditScene) return Fail("duplicate entity: no project loaded");
    Engine::GameObject* source = m_EditScene->FindByEntityId(entityId);
    if (!source) return Fail("duplicate entity: not found");
    if (!newRootId) return Fail("duplicate entity: missing result");

    std::string error;
    std::function<bool(Engine::GameObject*)> checkBindings = [&](Engine::GameObject* node) -> bool {
        if (!node) return true;
        const auto it = m_BindingsById.find(node->GetSceneEntityId());
        if (it != m_BindingsById.end() &&
            (!it->second.spriteGuid.IsNull() || !it->second.scriptGuid.IsNull()))
            return Fail("duplicate entity: legacy sprite/script binding is not "
                        "supported by G3 policy");
        if (!includeSubtree) return true;
        for (const auto& child : node->GetChildren())
            if (!checkBindings(child.get())) return false;
        return true;
    };
    if (!checkBindings(source)) return false;

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("duplicate entity")) return false;

    auto abandon = [&]() {
        m_SceneTxnActive = false;
        m_SceneTxnBefore.clear();
        m_SceneTxnLabel.clear();
    };

    uint64 createdId = 0;
    if (!m_EditScene->DuplicateEntity(entityId, includeSubtree, &createdId,
                                       nullptr, &error)) {
        if (autoTxn) abandon();
        return Fail("duplicate entity failed: " + error);
    }

    RealignBindings();
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    *newRootId = createdId;
    Emit(EV_ENTITY_CREATED,
         "id=" + std::to_string(createdId) +
         ";source=" + std::to_string(entityId) +
         ";subtree=" + (includeSubtree ? "1" : "0"));
    return true;
}

int32_t EditorSession::CreatePrefabFromEntity(uint64 entityId,
                                              const char* assetName) {
    using AT = Engine::Content::AssetType;
    if (!m_EditScene) {
        Fail("create prefab: no project loaded");
        return -1;
    }
    Engine::GameObject* source = m_EditScene->FindByEntityId(entityId);
    if (!source) {
        Fail("create prefab: entity not found");
        return -1;
    }

    auto sanitizeStem = [](const std::string& raw) {
        std::string out;
        for (const char c : raw) {
            const bool ok = (c >= '0' && c <= '9') ||
                            (c >= 'A' && c <= 'Z') ||
                            (c >= 'a' && c <= 'z') || c == '_' || c == '-';
            if (ok) out += c;
            if (out.size() >= 48u) break;
        }
        return out.empty() ? std::string("prefab") : out;
    };

    std::unordered_map<uint64, Engine::Content::PrefabLegacyBinding> legacy;
    for (const auto& entry : m_BindingsById) {
        Engine::Content::PrefabLegacyBinding binding;
        binding.spriteGuid = entry.second.spriteGuid;
        binding.scriptGuid = entry.second.scriptGuid;
        if (!binding.spriteGuid.IsNull() || !binding.scriptGuid.IsNull())
            legacy[entry.first] = binding;
    }

    Engine::Content::PrefabCaptureOptions options;
    options.prefabGuid = Engine::ResourceGUID::Create();
    while (options.prefabGuid.IsNull() || m_Reg.ContainsGuid(options.prefabGuid))
        options.prefabGuid = Engine::ResourceGUID::Create();
    options.legacyBindings = &legacy;

    const auto capture = Engine::Content::CapturePrefabFromScene(
        *m_EditScene, entityId, options);
    if (!capture.ok) {
        Fail("create prefab: " + capture.error);
        return -1;
    }
    const auto written = Engine::Content::WritePrefab(capture.model);
    if (!written.ok) {
        Fail("create prefab: " + written.error);
        return -1;
    }

    const std::string rawName =
        (assetName && *assetName) ? assetName : source->GetName();
    const std::string stem = sanitizeStem(rawName);
    const std::string relPath =
        "assets/prefabs/" + stem + "." + written.digest + ".prefab.json";
    const auto resolved = m_Root.Resolve(relPath);
    if (!resolved.ok) {
        Fail("create prefab: path rejected: " + resolved.diagnostic);
        return -1;
    }

    std::error_code ec;
    std::filesystem::create_directories(
        Engine::Content::PathFromUtf8(resolved.path).parent_path(), ec);
    if (ec) {
        Fail("create prefab: cannot create assets/prefabs directory");
        return -1;
    }

    std::ifstream existing(Engine::Content::PathFromUtf8(resolved.path),
                           std::ios::binary);
    if (existing.good()) {
        std::string existingBytes((std::istreambuf_iterator<char>(existing)),
                                  std::istreambuf_iterator<char>());
        if (existingBytes != written.bytes) {
            Fail("create prefab: content-addressed path collision");
            return -1;
        }
    }

    const auto fileWritten =
        Engine::Content::WriteFileAtomic(resolved.path, written.bytes);
    if (!fileWritten.ok) {
        Fail("create prefab: cannot stage Prefab bytes");
        return -1;
    }

    Engine::Content::ContentRegistry candidate = m_Reg;
    if (!candidate.RegisterExplicit(options.prefabGuid, relPath, AT::Prefab)) {
        Fail("create prefab: registry registration failed");
        return -1;
    }

    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene)) {
        Fail("create prefab: committed scene unreadable");
        return -1;
    }
    const auto published = m_Revisions.Publish(
        m_Root, candidate.SerializeManifest(), committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) m_RecoveryRequired = true;
        Fail("create prefab: " + published.diagnostic);
        return -1;
    }

    m_Reg = std::move(candidate);
    int32_t idx = IndexOfGuid(m_AssetOrder, options.prefabGuid);
    if (idx < 0) {
        m_AssetOrder.push_back(options.prefabGuid);
        idx = static_cast<int32_t>(m_AssetOrder.size()) - 1;
    }

    const auto mirror = Engine::Content::WriteFileAtomic(
        m_ManifestPath, m_Reg.SerializeManifest());
    if (!mirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "create prefab: CommittedCleanupPending; legacy mirror update failed";
    }

    Engine::Log::Info("[EditorBridge] prefab created: {} ({}; revision {})",
                      relPath, options.prefabGuid.ToHex(), published.revision);
    Emit(EV_ASSET_IMPORTED,
         "idx=" + std::to_string(idx) +
             ";guid=" + options.prefabGuid.ToHex() +
             ";path=" + relPath + ";type=4");
    return idx;
}

bool EditorSession::InstantiatePrefab(int32_t assetIndex, uint64 parentEntityId,
                                      uint64* newRootId) {
    if (newRootId) *newRootId = 0;
    if (!m_EditScene) return Fail("instantiate prefab: no project loaded");
    if (IsPlaying()) return Fail("instantiate prefab ignored while playing");
    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry) ||
        entry.type != Engine::Content::AssetType::Prefab)
        return Fail("instantiate prefab: asset is not a Prefab");
    const auto resolved = m_Root.Resolve(entry.path);
    if (!resolved.ok)
        return Fail("instantiate prefab: path rejected");
    std::vector<uint8_t> bytes;
    if (!ReadAllBytesFromPath(resolved.path, &bytes))
        return Fail("instantiate prefab: bytes unreadable");
    const std::string text(bytes.begin(), bytes.end());
    const auto read = Engine::Content::ReadPrefab(text, &entry.guid);
    if (!read.ok) return Fail("instantiate prefab: " + read.error);
    if (parentEntityId != 0 &&
        !m_EditScene->FindByEntityId(parentEntityId))
        return Fail("instantiate prefab: parent entity not found");

    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("instantiate prefab")) return false;
    auto abandon = [&]() {
        m_SceneTxnActive = false;
        m_SceneTxnBefore.clear();
        m_SceneTxnLabel.clear();
    };

    std::unordered_map<uint64_t, std::shared_ptr<Engine::GameObject>> objects;
    std::string buildError;
    for (const auto& e : read.model.entities) {
        auto object = std::make_shared<Engine::GameObject>(e.name);
        object->GetTransform().SetPosition(e.localTransform.position);
        object->GetTransform().SetRotationQuat(e.localTransform.rotation);
        object->GetTransform().SetScale(e.localTransform.scale);
        for (const auto& c : e.components) {
            auto component = Engine::ComponentRegistryGo::Create(c.type);
            if (!component || !component->Deserialize(c.data) ||
                !object->Attach(component)) {
                buildError = "component '" + c.type + "' could not be instantiated";
                break;
            }
        }
        if (!buildError.empty()) break;
        objects.emplace(e.localId, std::move(object));
    }
    if (!buildError.empty()) {
        if (autoTxn) abandon();
        return Fail("instantiate prefab: " + buildError);
    }

    for (const auto& e : read.model.entities) {
        if (!e.parentLocalId.has_value()) continue;
        const auto parent = objects.find(*e.parentLocalId);
        const auto child = objects.find(e.localId);
        if (parent == objects.end() || child == objects.end()) {
            if (autoTxn) abandon();
            return Fail("instantiate prefab: Local ID parent missing");
        }
        parent->second->AddChild(child->second);
    }
    const auto rootIt = objects.find(read.model.rootLocalId);
    if (rootIt == objects.end()) {
        if (autoTxn) abandon();
        return Fail("instantiate prefab: root Local ID missing");
    }

    std::string addError;
    if (!m_EditScene->AddEntity(rootIt->second, parentEntityId, &addError)) {
        if (autoTxn) abandon();
        return Fail("instantiate prefab: " + addError);
    }
    const uint64_t rootSceneId = rootIt->second->GetSceneEntityId();
    Engine::PrefabInstanceMetadata metadata;
    metadata.prefabGuid = read.model.prefabGuid;
    metadata.sourceRevision = static_cast<int64_t>(m_Revisions.Revision());
    metadata.sourceDigest = Engine::Content::ComputePrefabDigest(text);
    metadata.sourcePath = entry.path;
    metadata.legacySourceIdentity = false;
    metadata.syncState = Engine::Content::PrefabSyncState::Clean;
    for (const auto& pair : objects)
        metadata.localToScene[pair.first] = pair.second->GetSceneEntityId();
    std::string metadataError;
    if (!m_EditScene->SetPrefabInstanceMetadata(rootSceneId, metadata,
                                                 &metadataError)) {
        m_EditScene->DeleteEntity(rootSceneId);
        if (autoTxn) abandon();
        return Fail("instantiate prefab: " + metadataError);
    }

    RealignBindings();
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    if (newRootId) *newRootId = rootSceneId;
    Emit(EV_ENTITY_CREATED,
         "id=" + std::to_string(rootSceneId) +
             ";prefab=" + read.model.prefabGuid.ToHex());
    return true;
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

std::string EditorSession::CaptureSceneJson() {
    if (!m_EditScene) return {};
    const auto snap = Engine::Content::CaptureSceneById(*m_EditScene, m_BindingsById);
    return Engine::Content::SerializeSnapshot(snap).dump(2);
}

bool EditorSession::ApplySceneJson(const std::string& json) {
    if (!m_EditScene) return false;
    nlohmann::json doc;
    Engine::Content::SceneSnapshot snap;
    std::string err;
    try {
        doc = nlohmann::json::parse(json);
    } catch (const std::exception& ex) {
        return Fail(std::string("scene transaction: JSON invalid: ") + ex.what());
    }
    if (!Engine::Content::DeserializeSnapshot(doc, snap, err))
        return Fail("scene transaction: snapshot invalid: " + err);
    auto scene = std::make_shared<Engine::Scene>("AvaloniaSession");
    auto instantiate = Engine::Content::InstantiateScene(
        snap, *scene, m_TexMgr, m_Reg,
        [this](const std::string& stored) {
            const auto r = m_Root.ResolveAny(stored);
            return r.ok ? r.path : std::string();
        });
    if (!instantiate.ok)
        return Fail("scene transaction: instantiate failed");
    m_EditScene = std::move(scene);
    m_Bindings = std::move(instantiate.bindings);
    m_BindingsById = std::move(instantiate.bindingsById);
    RealignBindings();
    return true;
}

void EditorSession::RefreshDirtyFromScene() {
    if (!m_EditScene) return;
    if (!m_HasSavedScene) {
        m_Dirty = true;
        return;
    }
    m_Dirty = CaptureSceneJson() != m_SavedSceneJson;
}

bool EditorSession::GetEntityTransform(int32_t index, float out9[9]) const {
    if (!out9 || !m_EditScene || index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    const auto& t = m_EditScene->GetObjects()[static_cast<size_t>(index)]
                        ->GetTransform();
    const Engine::Vec3 p = t.GetPosition();
    const Engine::Vec3 r = t.GetRotationEuler();
    const Engine::Vec3 s = t.GetScale();
    out9[0] = p.x; out9[1] = p.y; out9[2] = p.z;
    out9[3] = r.x; out9[4] = r.y; out9[5] = r.z;
    out9[6] = s.x; out9[7] = s.y; out9[8] = s.z;
    return true;
}

bool EditorSession::GetEditorViewProjection(float out16[16]) const {
    if (!out16 || !m_Application) return false;
    const auto *camera = m_Application->GetProduction3DCamera();
    if (!camera) return false;
    std::memcpy(out16, camera->GetViewProjectionMatrixPtr(),
                sizeof(float) * 16);
    return true;
}
bool EditorSession::SetEntityTransform(int32_t index, const float pos3[3],
                                       const float euler3[3],
                                       const float scale3[3]) {
    if (!pos3 || !euler3 || !scale3 || !m_EditScene || IsPlaying() ||
        index < 0 ||
        index >= static_cast<int32_t>(m_EditScene->GetObjectCount()))
        return false;
    for (int i = 0; i < 9; ++i) {
        const float v = i < 3 ? pos3[i] : (i < 6 ? euler3[i - 3]
                                                  : scale3[i - 6]);
        if (!std::isfinite(v)) return Fail("transform: non-finite value");
    }
    const bool autoTxn = !m_SceneTxnActive;
    if (autoTxn && !BeginSceneEdit("transform")) return false;
    auto& t = m_EditScene->GetObjects()[static_cast<size_t>(index)]
                  ->GetTransform();
    t.SetPosition(pos3[0], pos3[1], pos3[2]);
    t.SetRotationEuler(Engine::Vec3(euler3[0], euler3[1], euler3[2]));
    t.SetScale(Engine::Vec3(scale3[0], scale3[1], scale3[2]));
    MarkDirty();
    if (autoTxn && !CommitSceneEdit()) return false;
    Emit(EV_ENTITY_MOVED, "idx=" + std::to_string(index));
    return true;
}

bool EditorSession::BeginSceneEdit(const std::string& label) {
    if (!m_EditScene || IsPlaying()) return false;
    if (m_SceneTxnActive) return true;
    m_SceneTxnBefore = CaptureSceneJson();
    m_SceneTxnLabel = label;
    m_SceneTxnActive = true;
    return true;
}

bool EditorSession::CommitSceneEdit() {
    if (!m_SceneTxnActive) return false;
    const std::string after = CaptureSceneJson();
    if (after != m_SceneTxnBefore) {
        if (m_SceneHistory.size() >= 64) {
            m_SceneHistory.erase(m_SceneHistory.begin());
            if (m_SceneHistoryCursor > 0) --m_SceneHistoryCursor;
        }
        m_SceneHistory.resize(m_SceneHistoryCursor);
        m_SceneHistory.push_back({m_SceneTxnLabel, m_SceneTxnBefore, after});
        m_SceneHistoryCursor = m_SceneHistory.size();
    }
    m_SceneTxnActive = false;
    m_SceneTxnBefore.clear();
    m_SceneTxnLabel.clear();
    RefreshDirtyFromScene();
    return true;
}

bool EditorSession::CancelSceneEdit() {
    if (!m_SceneTxnActive) return false;
    const std::string before = m_SceneTxnBefore;
    m_SceneTxnActive = false;
    m_SceneTxnBefore.clear();
    m_SceneTxnLabel.clear();
    if (!before.empty() && !ApplySceneJson(before)) return false;
    RefreshDirtyFromScene();
    return true;
}

bool EditorSession::UndoSceneEdit() {
    if (m_SceneTxnActive || m_SceneHistoryCursor == 0) return false;
    --m_SceneHistoryCursor;
    const auto& entry = m_SceneHistory[m_SceneHistoryCursor];
    if (!ApplySceneJson(entry.before)) {
        ++m_SceneHistoryCursor;
        return false;
    }
    RefreshDirtyFromScene();
    return true;
}

bool EditorSession::RedoSceneEdit() {
    if (m_SceneTxnActive || m_SceneHistoryCursor >= m_SceneHistory.size())
        return false;
    const auto& entry = m_SceneHistory[m_SceneHistoryCursor];
    if (!ApplySceneJson(entry.after)) return false;
    ++m_SceneHistoryCursor;
    RefreshDirtyFromScene();
    return true;
}

bool EditorSession::CanUndoSceneEdit() const {
    return !m_SceneTxnActive && m_SceneHistoryCursor > 0;
}

bool EditorSession::CanRedoSceneEdit() const {
    return !m_SceneTxnActive &&
           m_SceneHistoryCursor < m_SceneHistory.size();
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
    m_BindingsById[obj->GetSceneEntityId()].spriteGuid = entry.guid;
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
    m_BindingsById[obj->GetSceneEntityId()].scriptGuid = entry.guid;
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

    bool isInstance = false;
    uint64_t instanceRoot = 0;
    uint64_t localId = 0;
    Engine::PrefabInstanceMetadata instanceMeta;
    Engine::Content::PrefabModel sourceModel;
    if (m_EditScene->FindPrefabInstanceForEntity(obj->GetSceneEntityId(),
                                                  &instanceRoot, &instanceMeta)) {
        isInstance = true;
        for (const auto& [local, scene] : instanceMeta.localToScene)
            if (scene == obj->GetSceneEntityId()) { localId = local; break; }
        if (localId == 0)
            return Fail("set component: instance local id not mapped");
        Engine::Content::PrefabOverrideFieldSpec fieldSpec;
        if (!Engine::Content::ResolvePrefabOverrideField(
                typeName, propName, &fieldSpec))
            return Fail("set component: field is not Override-capable: " +
                        typeName + "." + propName);
        std::string sourceError;
        if (!LoadPrefabSourceForInstance(instanceMeta, &sourceModel,
                                          &sourceError))
            return Fail("set component: " + sourceError);
    }

    Engine::ComponentPropertyValue oldValue;
    const bool hasOld = comp->GetPropertyValue(static_cast<size_t>(i), &oldValue);
    if (!comp->SetPropertyValue(static_cast<size_t>(i), v))
        return Fail("set component: write failed for '" + propName + "'");

    if (isInstance) {
        nlohmann::json value;
        switch (v.type) {
        case Engine::ComponentValueType::Bool: value = v.boolValue; break;
        case Engine::ComponentValueType::Int: value = v.intValue; break;
        case Engine::ComponentValueType::Float: value = v.floatValue; break;
        case Engine::ComponentValueType::String: value = v.stringValue; break;
        }
        Engine::Content::PrefabOverrideRecord record;
        record.localId = localId;
        record.component = typeName;
        record.field = propName;
        record.value = std::move(value);
        std::string overrideError;
        if (!Engine::Content::AddOrUpdatePrefabOverride(
                sourceModel, &instanceMeta.overrides, record, &overrideError)) {
            if (hasOld) comp->SetPropertyValue(static_cast<size_t>(i), oldValue);
            return Fail("set component: override tracking failed: " +
                        overrideError);
        }
        instanceMeta.syncState = instanceMeta.overrides.empty()
            ? Engine::Content::PrefabSyncState::Clean
            : Engine::Content::PrefabSyncState::Overrides;
        std::string metadataError;
        if (!m_EditScene->SetPrefabInstanceMetadata(
                instanceRoot, instanceMeta, &metadataError)) {
            if (hasOld) comp->SetPropertyValue(static_cast<size_t>(i), oldValue);
            return Fail("set component: metadata update failed: " +
                        metadataError);
        }
    }

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
    else if (type == 2) t = AT::Mesh;
    else { Fail("import asset: bad type " + std::to_string(type)); return -1; }

    if (t == AT::Mesh) {
        if (!reattachHex.empty()) {
            Fail("model import: reattach is not supported for Mesh in v1 "
                 "(GUID stability is guaranteed by the input digest; use a "
                 "normal reimport)");
            return -1;
        }
        if (m_EditScene) {
            return ImportModelAsset(input, WithForwardSlashesCopy(input),
                                    LooksAbsolutePath(WithForwardSlashesCopy(input)),
                                    std::string());
        }
        return -1;
    }
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

// ── P6-1B B3：模型导入提交链 ────────────────────────────────────────────
// 决策记录（B3 冻结语义）：
//   - 一个源文件 = 一个 Mesh asset（C-1 方案 a）；primitive spans 保留源序。
//   - Mesh/Material 资产文件按内容寻址命名（含 import digest8 / 材料 hash8），
//     重导永不覆写旧 revision 引用的物理字节。
//   - Material 槽文件路径确定性：stemDir/<base>.m<slot>.<mhash8>.material.json。
//   - Mesh GUID 稳定性：stemDir/<base>. 前缀的既有 Mesh 条目复用（词序最小路径）。
//   - 幂等快路径：meshPath 严格一致命中 → 复用既有 GUID，不产生新 revision。
//   - 单一 Publish 提交 Mesh + 全部 Material 组；失败不更新 mirror/事件。
//   - 外部源物理拷贝 = assets/imported/model/<base>_<abshash8>/<base>.<srchash8><ext>。
int32_t EditorSession::ImportModelAsset(const std::string& input,
                                        const std::string& fwd,
                                        bool absolute,
                                        const std::string& reattachHex,
                                        bool forceReimport) {
    using Engine::Content::AssetEntry;
    using Engine::Content::AssetType;
    using Engine::AssetImport::ImportResult;
    using Engine::AssetImport::ImportSeverity;

    if (!reattachHex.empty()) {
        Fail("model import: reattach is not supported in v1");
        return -1;
    }

    std::string targetRel;
    std::string sourceAbs;
    bool copySource = false;
    if (!absolute) {
        const auto normalized =
            Engine::Content::NormalizeProjectRelative(input);
        if (!normalized.ok) {
            Fail("model import: path '" + input + "' rejected: " +
                 Engine::Content::ToString(normalized.error));
            return -1;
        }
        targetRel = normalized.path;
        const auto resolved = m_Root.Resolve(targetRel);
        if (!resolved.ok) {
            Fail("model import: path '" + input +
                 "' rejected: not resolvable via project root: " +
                 resolved.diagnostic);
            return -1;
        }
        sourceAbs = resolved.path;
    } else {
        const auto inside = m_Root.ToProjectRelative(fwd);
        if (inside.ok) {
            targetRel = inside.path;
            const auto resolved = m_Root.Resolve(targetRel);
            if (!resolved.ok) {
                Fail("model import: path '" + input +
                     "' rejected: not resolvable via project root: " +
                     resolved.diagnostic);
                return -1;
            }
            sourceAbs = resolved.path;
        } else if (inside.error ==
                   Engine::Content::PathError::EscapesRoot) {
            copySource = true;
            sourceAbs = fwd;
        } else {
            Fail("model import: path '" + input + "' rejected: " +
                 Engine::Content::ToString(inside.error));
            return -1;
        }
    }

    std::vector<uint8_t> sourceBytes;
    if (!ReadAllBytesFromPath(sourceAbs, &sourceBytes) || sourceBytes.empty()) {
        Fail("model import: cannot read source bytes: " + sourceAbs);
        return -1;
    }
    if (sourceBytes.size() > Engine::AssetImport::kMaxSourceFileBytes) {
        Fail("model import: source exceeds kMaxSourceFileBytes");
        return -1;
    }

    // parse consumes bytes by move; keep a copy for the C-2 digest
    std::vector<uint8_t> digestSourceBytes = sourceBytes;
    const std::string srcDirAbs = ParentDirOf(sourceAbs);
    ImportResult parsed;
    const std::string ext = ExtOfLower(FileNameOf(fwd));
    if (ext == "obj") {
        parsed = Engine::AssetImport::ObjParser::ParseObj(std::move(sourceBytes),
                                                          srcDirAbs);
    } else if (ext == "gltf") {
        parsed = Engine::AssetImport::GltfParser::ParseGltfJson(
            std::move(sourceBytes), srcDirAbs);
    } else if (ext == "glb") {
        parsed = Engine::AssetImport::GltfParser::ParseGlb(std::move(sourceBytes),
                                                           srcDirAbs);
    } else {
        Fail("model import: unsupported mesh extension '" + ext + "'");
        return -1;
    }
    if (!parsed.ok) {
        std::string detail;
        for (const auto& dg : parsed.diagnostics) {
            if (dg.severity != ImportSeverity::Error) continue;
            if (!detail.empty()) detail += "; ";
            detail += std::string(dg.code) + " @" + dg.location;
            break;
        }
        Fail("model import: parse failed: " + detail);
        return -1;
    }
    for (const auto& wg : parsed.diagnostics) {
        if (wg.severity == ImportSeverity::Warning) {
            if (!m_Warnings.empty()) m_Warnings += "\n";
            m_Warnings += "model import warning: " + std::string(wg.code) +
                          " @" + wg.location + ": " + wg.message;
        }
    }
    if (parsed.mesh.materials.size() > 1) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "model import warning: source has " +
            std::to_string(parsed.mesh.materials.size()) +
            " materials; v1 renders the first material; ALL materials are "
            "registered (C-1 approved rule)";
    }

    // ── B4-D texture collection (digest inputs; preprocess deferred until
    // the idempotency fast path rules out a no-op re-import) ─────────────
    struct ModelTextureEntry
    {
        uint32_t imageIndex = 0;        // index into parsed.mesh.images
        std::string identity;           // normalized uri or glbimg<docIndex>
        bool embedded = false;          // GLB bufferView image
        Engine::AssetImport::TextureRole role =
            Engine::AssetImport::TextureRole::Unknown;
        std::vector<uint8_t> externalBytes; // owns external file bytes
    };
    std::vector<ModelTextureEntry> textureEntries;
    auto roleOfRef = [](int which)
        -> Engine::AssetImport::TextureRole {
        switch (which) {
            case 0: return Engine::AssetImport::TextureRole::BaseColor;
            case 1: return Engine::AssetImport::TextureRole::Normal;
            case 2: return Engine::AssetImport::TextureRole::Occlusion;
            case 3: return Engine::AssetImport::TextureRole::Emissive;
            default:
                return Engine::AssetImport::TextureRole::MetallicRoughness;
        }
    };
    auto collectTextureRef = [&](int which,
                                 const Engine::AssetImport::TextureReferenceIR&
                                     ref) -> bool {
        if (!ref.present || ref.imageIndex >= parsed.mesh.images.size())
            return true;
        const bool isColor =
            Engine::AssetImport::IsColorRole(roleOfRef(which));
        for (const auto& existing : textureEntries) {
            if (existing.imageIndex == ref.imageIndex &&
                Engine::AssetImport::IsColorRole(existing.role) == isColor)
                return true; // dedupe (imageIndex, class)
        }
        ModelTextureEntry entry;
        entry.imageIndex = ref.imageIndex;
        entry.role = roleOfRef(which);
        const auto& img = parsed.mesh.images[ref.imageIndex];
        entry.embedded =
            img.uri.empty() && parsed.mesh.imageBytes[ref.imageIndex].size() > 0;
        if (entry.embedded) {
            entry.identity =
                "glbimg" + std::to_string(img.sourceImageIndex);
        } else {
            entry.identity = LowerAsciiCopy(img.uri);
        }
        if (!entry.embedded) {
            const std::string texAbs =
                LooksAbsolutePath(img.uri)
                    ? img.uri
                    : (srcDirAbs.empty() ? img.uri : (srcDirAbs + "/" + img.uri));
            if (!ReadAllBytesFromPath(texAbs, &entry.externalBytes) ||
                entry.externalBytes.empty()) {
                Fail("model import: texture source unreadable: " + img.uri);
                return false;
            }
        }
        textureEntries.push_back(std::move(entry));
        return true;
    };
    for (uint32_t m = 0; m < parsed.mesh.materials.size(); ++m) {
        const auto& mat = parsed.mesh.materials[m];
        if (!collectTextureRef(0, mat.baseColor)) return -1;
        if (!collectTextureRef(1, mat.normal)) return -1;
        if (!collectTextureRef(2, mat.occlusion)) return -1;
        if (!collectTextureRef(3, mat.emissive)) return -1;
        if (!collectTextureRef(4, mat.metallicRoughness)) return -1;
    }

    Engine::AssetImport::ImportDigestInput digestIn;
    digestIn.sourcePathKey = LowerAsciiCopy(fwd);
    digestIn.sourceBytes = digestSourceBytes.data();
    digestIn.sourceSize = digestSourceBytes.size();
    digestIn.schema = Engine::AssetImport::DigestSchema::V2;
    std::vector<std::vector<uint8_t>> depStorage;
    for (const std::string& dep : parsed.mesh.dependencyFiles) {
        const std::string depAbs =
            LooksAbsolutePath(dep)
                ? dep
                : (srcDirAbs.empty() ? dep : (srcDirAbs + "/" + dep));
        depStorage.emplace_back();
        if (!ReadAllBytesFromPath(depAbs, &depStorage.back()) ||
            depStorage.back().empty()) {
            Fail("model import: digest dependency unreadable: " + dep);
            return -1;
        }
        Engine::AssetImport::DigestDependency dd;
        dd.pathKey = LowerAsciiCopy(dep);
        dd.bytes = depStorage.back().data();
        dd.size = depStorage.back().size();
        digestIn.dependencies.push_back(std::move(dd));
    }
    for (const auto& entry : textureEntries) {
        Engine::AssetImport::DigestTexture dt;
        dt.digestClass = Engine::AssetImport::IsColorRole(entry.role)
                             ? Engine::AssetImport::TextureDigestClass::Color
                             : Engine::AssetImport::TextureDigestClass::Data;
        dt.identity = entry.identity;
        if (entry.embedded) {
            dt.bytes = parsed.mesh.imageBytes[entry.imageIndex].data();
            dt.size = parsed.mesh.imageBytes[entry.imageIndex].size();
        } else {
            dt.bytes = entry.externalBytes.data();
            dt.size = entry.externalBytes.size();
        }
        digestIn.textures.push_back(std::move(dt));
    }
    const std::string digest =
        Engine::AssetImport::ComputeImportDigest(digestIn);
    const std::string digest8 = digest.substr(0, 8);

    // ── content-addressed asset naming ─────────────────────────────────
    const std::string baseCore = SanitizeName(StemOf(FileNameOf(fwd)));
    std::string stemDir;
    if (copySource) {
        const std::string folder =
            baseCore + "_" + Hex8(LowerAsciiCopy(sourceAbs));
        const std::string srcHash8 =
            Hex8(std::string(reinterpret_cast<const char*>(
                                 digestSourceBytes.data()),
                             digestSourceBytes.size()));
        const std::string extCanonical =
            ext == "obj" ? ".obj" : (ext == "gltf" ? ".gltf" : ".glb");
        stemDir = "assets/imported/model/" + folder;
        const std::string copyRel =
            stemDir + "/" + baseCore + "." + srcHash8 + extCanonical;
        const auto copyResolved = m_Root.Resolve(copyRel);
        if (!copyResolved.ok) {
            Fail("model import: cannot resolve copy target: " + copyRel +
                 ": " + copyResolved.diagnostic);
            return -1;
        }
        std::error_code ec;
        if (!fs::exists(fs::path(copyResolved.path), ec)) {
            fs::create_directories(
                fs::path(copyResolved.path).parent_path(), ec);
            if (ec) {
                Fail("model import: cannot create copy directory: " +
                     ec.message());
                return -1;
            }
            const auto copied = Engine::Content::CopyFileNoOverwrite(
                sourceAbs, copyResolved.path);
            if (!copied.ok &&
                copied.error != Engine::Content::AtomicFileError::TargetExists) {
                Fail("model import: copy failed: " +
                     std::string(Engine::Content::ToString(copied.error)) +
                     " " + copied.diagnostic);
                return -1;
            }
        }
        targetRel = copyRel;
    } else {
        const std::string parent = ParentDirOf(targetRel);
        stemDir = parent.empty()
                      ? ("assets/imported/model/" + baseCore + "_" +
                         Hex8(LowerAsciiCopy(sourceAbs)))
                      : parent;
    }
    parsed.mesh.sourceFile = targetRel;

    const std::string meshPath =
        stemDir + "/" + baseCore + "." + digest8 + ".mesh";
    const std::string meshBytes =
        Engine::AssetImport::SerializeMeshAssetJson(parsed.mesh);

    // ── B4 texture preprocess (after fast-path) → PNG outputs
    // content-addressed by normalized bytes; registers as Texture assets ──
    std::vector<std::string> texturePaths;      // registry path per entry
    std::vector<std::string> textureMetaBytes;  // sidecar meta json per entry
    for (auto& entry : textureEntries) {
        const std::vector<uint8_t>* bytesPtr =
            entry.embedded ? &parsed.mesh.imageBytes[entry.imageIndex]
                           : &entry.externalBytes;
        Engine::AssetImport::TexturePreprocessResult processed =
            Engine::AssetImport::PreprocessTexture(*bytesPtr, entry.role);
        if (!processed.ok) {
            Fail("model import: texture preprocess failed " + entry.identity +
                 ": " + processed.error);
            return -1;
        }
        const std::string pngHash = Hex8(std::string(
            reinterpret_cast<const char*>(processed.pngBytes.data()),
            processed.pngBytes.size()));
        const char* roleToken =
            entry.role == Engine::AssetImport::TextureRole::BaseColor
                ? "basecolor"
            : entry.role == Engine::AssetImport::TextureRole::Normal
                ? "normal"
            : entry.role == Engine::AssetImport::TextureRole::Occlusion
                ? "occlusion"
            : entry.role == Engine::AssetImport::TextureRole::Emissive
                ? "emissive"
                : "metallicroughness";
        const std::string sanit =
            SanitizeName(StemOf(FileNameOf(parsed.mesh.images[entry.imageIndex].uri.empty() ? ("img" + std::to_string(parsed.mesh.images[entry.imageIndex].sourceImageIndex)) : parsed.mesh.images[entry.imageIndex].uri)));
        const std::string pngPath =
            stemDir + "/tex/" + sanit + "." + roleToken + "." + pngHash +
            ".tex.png";
        nlohmann::json meta;
        meta["format"] = "texture-meta";
        meta["ruleVersion"] = 1;
        meta["role"] = roleToken;
        meta["colorspace"] =
            Engine::AssetImport::IsColorRole(entry.role) ? "sRGB" : "linear";
        meta["width"] = processed.width;
        meta["height"] = processed.height;
        meta["components"] = processed.components;
        meta["sourceIdentity"] = entry.identity;
        texturePaths.push_back(pngPath);
        textureMetaBytes.push_back(meta.dump());
    }
    // resolved asset paths → into material refs, BEFORE serializing materials
    for (auto& mat : parsed.mesh.materials) {
        auto resolve = [&](Engine::AssetImport::TextureReferenceIR& ref) {
            if (!ref.present || ref.imageIndex >= parsed.mesh.images.size())
                return;
            for (size_t i = 0; i < textureEntries.size(); ++i) {
                if (textureEntries[i].imageIndex != ref.imageIndex) continue;
                const bool colorRef =
                    (&ref == &mat.baseColor || &ref == &mat.emissive);
                const bool colorEntry = Engine::AssetImport::IsColorRole(
                    textureEntries[i].role);
                if (colorRef == colorEntry) {
                    ref.resolvedAssetPath = texturePaths[i];
                    break;
                }
            }
        };
        resolve(mat.baseColor);
        resolve(mat.normal);
        resolve(mat.occlusion);
        resolve(mat.emissive);
        resolve(mat.metallicRoughness);
    }

    std::vector<std::string> materialPaths;
    std::vector<std::string> materialBytes;
    for (uint32_t slot = 0; slot < parsed.mesh.materials.size(); ++slot) {
        std::string bytes = Engine::AssetImport::SerializeMaterialAssetJson(
            parsed.mesh.materials[slot], slot);
        const std::string mh = Hex8(bytes);
        materialPaths.push_back(stemDir + "/" + baseCore + ".m" +
                                std::to_string(slot) + "." + mh +
                                ".material.json");
        materialBytes.push_back(std::move(bytes));
    }

    // ── idempotency fast path (C-2): identical digest → identical
    // content-addressed mesh path → registry hit → same GUID, no revision ──
    if (!forceReimport) {
        for (const auto& entry : m_Reg.GetAllEntries()) {
            if (entry.type == AssetType::Mesh &&
                m_Root.KeysEqual(entry.path, meshPath)) {
                return IndexOfGuid(m_AssetOrder, entry.guid);
            }
        }
    }

    // ── staging writes BEFORE candidate/publish (never overwrites bytes
    //    referenced by a committed revision; names are content-addressed) ──
    auto writeAssetFile = [&](const std::string& rel,
                              const std::string& bytes) -> bool {
        const auto resolved = m_Root.Resolve(rel);
        if (!resolved.ok) {
            Fail("model import: cannot resolve staging path: " + rel +
                 ": " + resolved.diagnostic);
            return false;
        }
        std::error_code ec;
        fs::create_directories(fs::path(resolved.path).parent_path(), ec);
        if (ec) {
            Fail("model import: cannot create staging directory: " +
                 ec.message());
            return false;
        }
        const auto written = Engine::Content::WriteFileAtomic(
            resolved.path, bytes);
        if (!written.ok) {
            Fail("model import: staging write failed: " + rel + ": " +
                 std::string(Engine::Content::ToString(written.error)) +
                 " " + written.diagnostic);
            return false;
        }
        return true;
    };
    if (!writeAssetFile(meshPath, meshBytes)) return -1;
    for (size_t k = 0; k < materialPaths.size(); ++k) {
        if (!writeAssetFile(materialPaths[k], materialBytes[k])) return -1;
    }
    for (size_t k = 0; k < texturePaths.size(); ++k) {
        const auto& entry = textureEntries[k];
        const std::vector<uint8_t>* bytesPtr =
            entry.embedded ? &parsed.mesh.imageBytes[entry.imageIndex]
                           : &entry.externalBytes;
        Engine::AssetImport::TexturePreprocessResult processed =
            Engine::AssetImport::PreprocessTexture(*bytesPtr, entry.role);
        if (!writeAssetFile(texturePaths[k], std::string(
#if defined(_MSC_VER)
            reinterpret_cast<const char*>(processed.pngBytes.data()),
            processed.pngBytes.size()
#else
            processed.pngBytes.begin(), processed.pngBytes.end()
#endif
            ))) return -1;
        if (!writeAssetFile(texturePaths[k] + ".tex.meta.json",
                            textureMetaBytes[k])) return -1;
    }

    // ── candidate: reuse stable GUIDs via exact-path / stem-prefix lookups ──
    Engine::Content::ContentRegistry candidate = m_Reg;
    // B4: texture assets register first (exact content-addressed path →
    // Import keeps GUID stable for unchanged texture bytes).
    std::vector<Engine::ResourceGUID> textureGuids(texturePaths.size());
    for (size_t k = 0; k < texturePaths.size(); ++k) {
        textureGuids[k] = candidate.Import(texturePaths[k], AssetType::Texture);
        if (textureGuids[k].IsNull()) {
            Fail("model import: texture GUID allocation failed: " +
                 texturePaths[k]);
            return -1;
        }
    }
    const auto candidateEntries = candidate.GetAllEntries();
    auto findExact = [&](const std::string& path,
                         AssetType type) -> Engine::ResourceGUID {
        for (const auto& e : candidateEntries) {
            if (e.type == type && m_Root.KeysEqual(e.path, path))
                return e.guid;
        }
        return Engine::ResourceGUID();
    };
    auto findPrefixLease = [&](const std::string& prefix,
                               AssetType type) -> Engine::ResourceGUID {
        std::vector<std::string> matches;
        for (const auto& e : candidateEntries) {
            if (e.type == type && e.path.rfind(prefix, 0) == 0)
                matches.push_back(e.path);
        }
        if (matches.empty()) return Engine::ResourceGUID();
        std::sort(matches.begin(), matches.end());
        for (const auto& e : candidateEntries) {
            if (e.type == type && e.path == matches.front()) return e.guid;
        }
        return Engine::ResourceGUID();
    };

    Engine::ResourceGUID meshGuid =
        findExact(meshPath, AssetType::Mesh);
    if (meshGuid.IsNull())
        meshGuid = findPrefixLease(stemDir + "/" + baseCore + ".",
                                   AssetType::Mesh);
    bool meshRegistered = false;
    if (!meshGuid.IsNull()) {
        // C-2: keep the existing mesh GUID across revisions; rebind it from
        // its old content-addressed path to the new one.
        auto oldPath = ResolveRegisteredPath(candidate, meshGuid);
        if (!oldPath.empty() && !m_Root.KeysEqual(oldPath, meshPath))
            candidate.Unregister(meshGuid);
        meshRegistered =
            candidate.RegisterExplicit(meshGuid, meshPath, AssetType::Mesh);
    }
    if (!meshRegistered)
        meshGuid = candidate.Import(meshPath, AssetType::Mesh);
    if (meshGuid.IsNull()) {
        if (m_Warnings.size() > 0) m_Warnings = m_Warnings;
        Fail("model import: GUID allocation failed");
        return -1;
    }

    std::vector<Engine::ResourceGUID> materialGuids(materialPaths.size());
    for (size_t k = 0; k < materialPaths.size(); ++k) {
        Engine::ResourceGUID guid =
            findExact(materialPaths[k], AssetType::Material);
        if (guid.IsNull())
            guid = findPrefixLease(stemDir + "/" + baseCore + ".m" +
                                       std::to_string(k) + ".",
                                   AssetType::Material);
        if (!guid.IsNull()) {
            auto oldPath = ResolveRegisteredPath(candidate, guid);
            if (!oldPath.empty() &&
                !m_Root.KeysEqual(oldPath, materialPaths[k]))
                candidate.Unregister(guid);
            if (candidate.RegisterExplicit(guid, materialPaths[k],
                                           AssetType::Material)) {
                materialGuids[k] = guid;
                continue;
            }
        }
        materialGuids[k] =
            candidate.Import(materialPaths[k], AssetType::Material);
    }

    // ── single Publish for the Mesh + Material group ───────────────────
    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene)) {
        Fail("model import: committed scene unavailable");
        return -1;
    }
    const std::string manifestJson = candidate.SerializeManifest();
    const auto published =
        m_Revisions.Publish(m_Root, manifestJson, committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) {
            m_RecoveryRequired = true;
            Fail("model import: RecoveryRequired (commit state uncertain): " +
                 published.diagnostic);
            return -1;
        }
        Fail("model import: NotCommitted (revision publish failed): " +
             published.diagnostic);
        return -1;
    }
    m_Reg = std::move(candidate);

    auto ensureOrderPush = [&](const Engine::ResourceGUID& g) -> int32_t {
        int32_t idx = IndexOfGuid(m_AssetOrder, g);
        if (idx < 0) {
            m_AssetOrder.push_back(g);
            idx = static_cast<int32_t>(m_AssetOrder.size()) - 1;
        }
        return idx;
    };
    const int32_t meshIdx = ensureOrderPush(meshGuid);
    for (const auto& g : materialGuids) (void)ensureOrderPush(g);
    for (const auto& g : textureGuids) (void)ensureOrderPush(g);

    const auto mirror =
        Engine::Content::WriteFileAtomic(m_ManifestPath, manifestJson);
    if (!mirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings +=
            "import: CommittedCleanupPending; legacy manifest mirror "
            "update failed";
    }
    MarkDirty();
    Engine::Log::Info("[EditorBridge] model imported: {} (mesh; {} materials)",
                      meshPath, materialPaths.size());
    Emit(EV_ASSET_IMPORTED,
         "idx=" + std::to_string(meshIdx) + ";guid=" + meshGuid.ToHex() +
             ";path=" + meshPath +
             ";type=2;materials=" + std::to_string(materialPaths.size()) +
             ";textures=" + std::to_string(texturePaths.size()));
    return meshIdx;
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
        Engine::Content::CaptureSceneById(*m_EditScene, m_BindingsById);

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
        case AT::Texture:  return 0;
        case AT::Script:   return 1;
        case AT::Mesh:     return 2;
        case AT::Material: return 3;
        case AT::Prefab:   return 4;
    }
    return 2;
}

int32_t EditorSession::GetAssetStatus(int32_t index) const {
    Engine::Content::AssetEntry e;
    if (!AssetAt(index, &e)) return -1;
    const Engine::Content::PathResolution resolved = m_Root.Resolve(e.path);
    if (!resolved.ok) return 2;
    std::error_code ec;
    const std::filesystem::path p = Engine::Content::PathFromUtf8(resolved.path);
    if (!std::filesystem::exists(p, ec) || ec) return 1;
    const uint64_t size = std::filesystem::file_size(p, ec);
    if (ec || size == 0) return 1;
    return 0;
}

void EditorSession::LoadMaterialEdits() {
    m_EditedMaterials.clear();
    if (!m_Root.IsValid()) return;
    const auto p = Engine::Content::PathFromUtf8(
        m_Root.Path() + "/.engine-content/material-edits.json");
    std::error_code ec;
    if (!std::filesystem::exists(p, ec) || ec) return;
    try {
        std::ifstream f(p, std::ios::binary);
        nlohmann::json doc;
        f >> doc;
        if (!doc.is_object() || !doc.contains("edited") ||
            !doc["edited"].is_array())
            return;
        for (const auto& item : doc["edited"]) {
            if (!item.is_string()) continue;
            const Engine::ResourceGUID guid =
                Engine::ResourceGUID::FromHex(item.get<std::string>());
            if (!guid.IsNull()) m_EditedMaterials.insert(guid.ToHex());
        }
    } catch (...) {
        m_EditedMaterials.clear();
    }
}

bool EditorSession::SaveMaterialEdits() {
    nlohmann::json doc;
    doc["version"] = 1;
    doc["edited"] = nlohmann::json::array();
    for (const auto& guid : m_EditedMaterials) doc["edited"].push_back(guid);
    const auto dir = Engine::Content::PathFromUtf8(m_Root.Path() + "/.engine-content");
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return false;
    const auto written = Engine::Content::WriteFileAtomic(
        m_Root.Path() + "/.engine-content/material-edits.json", doc.dump(2));
    return written.ok;
}

bool EditorSession::GetMaterialJson(int32_t assetIndex, std::string* out) const {
    if (!out) return false;
    Engine::Content::AssetEntry e;
    if (!AssetAt(assetIndex, &e) ||
        e.type != Engine::Content::AssetType::Material)
        return false;
    const auto resolved = m_Root.Resolve(e.path);
    if (!resolved.ok) return false;
    std::vector<uint8_t> bytes;
    if (!ReadAllBytesFromPath(resolved.path, &bytes)) return false;
    out->assign(bytes.begin(), bytes.end());
    return true;
}

bool EditorSession::UpdateMaterial(int32_t assetIndex,
                                   const std::string& fieldsJson) {
    using AT = Engine::Content::AssetType;
    Engine::Content::AssetEntry entry;
    if (!AssetAt(assetIndex, &entry) || entry.type != AT::Material)
        return Fail("material edit: asset is not a Material");
    const auto resolved = m_Root.Resolve(entry.path);
    if (!resolved.ok)
        return Fail("material edit: path rejected");
    std::vector<uint8_t> currentBytes;
    if (!ReadAllBytesFromPath(resolved.path, &currentBytes))
        return Fail("material edit: committed bytes unreadable");

    std::string currentText(currentBytes.begin(), currentBytes.end());
    nlohmann::json doc;
    nlohmann::json patch;
    try {
        doc = nlohmann::json::parse(currentText);
        patch = nlohmann::json::parse(fieldsJson);
    } catch (const std::exception& ex) {
        return Fail(std::string("material edit: JSON invalid: ") + ex.what());
    }
    if (!doc.is_object() || !patch.is_object())
        return Fail("material edit: patch must be an object");

    auto finiteNumber = [](const nlohmann::json& value, float* out) {
        if (!value.is_number()) return false;
        const double d = value.get<double>();
        if (!std::isfinite(d)) return false;
        const float f = static_cast<float>(d);
        if (!std::isfinite(f)) return false;
        *out = f;
        return true;
    };
    auto floatArray = [&](const nlohmann::json& value, float* out,
                          size_t count) {
        if (!value.is_array() || value.size() != count) return false;
        for (size_t i = 0; i < count; ++i)
            if (!finiteNumber(value[i], &out[i])) return false;
        return true;
    };

    for (const auto& item : patch.items()) {
        const std::string& key = item.key();
        if (key == "baseColorFactor") {
            float values[4];
            if (!floatArray(item.value(), values, 4))
                return Fail("material edit: baseColorFactor invalid");
            doc["baseColorFactor"] =
                {values[0], values[1], values[2], values[3]};
        } else if (key == "metallicFactor") {
            float value = 0.0f;
            if (!finiteNumber(item.value(), &value))
                return Fail("material edit: metallicFactor invalid");
            doc["metallicFactor"] = value;
        } else if (key == "roughnessFactor") {
            float value = 0.0f;
            if (!finiteNumber(item.value(), &value))
                return Fail("material edit: roughnessFactor invalid");
            doc["roughnessFactor"] = value;
        } else if (key == "emissiveFactor") {
            float values[3];
            if (!floatArray(item.value(), values, 3))
                return Fail("material edit: emissiveFactor invalid");
            doc["emissiveFactor"] = {values[0], values[1], values[2]};
        } else if (key == "alphaCutoff") {
            float value = 0.0f;
            if (!finiteNumber(item.value(), &value) || value < 0.0f)
                return Fail("material edit: alphaCutoff invalid");
            doc["alphaCutoff"] = value;
        } else if (key == "alphaMode") {
            if (!item.value().is_string())
                return Fail("material edit: alphaMode invalid");
            const std::string mode = item.value().get<std::string>();
            if (mode != "OPAQUE" && mode != "MASK" && mode != "BLEND")
                return Fail("material edit: alphaMode invalid");
            doc["alphaMode"] = mode;
        } else {
            return Fail("material edit: unsupported field '" + key + "'");
        }
    }
    const std::string newText = doc.dump();
    std::vector<uint8_t> newBytes(newText.begin(), newText.end());
    if (!Engine::AssetImport::ReadMaterialAssetJson(newBytes).ok)
        return Fail("material edit: edited payload failed validation");
    if (newText == currentText) return true; // no-op, no revision

    const std::string suffix = ".material.json";
    if (entry.path.size() <= suffix.size() ||
        entry.path.compare(entry.path.size() - suffix.size(), suffix.size(),
                           suffix) != 0)
        return Fail("material edit: unsupported material path");
    const std::string withoutSuffix =
        entry.path.substr(0, entry.path.size() - suffix.size());
    const size_t lastDot = withoutSuffix.find_last_of('.');
    if (lastDot == std::string::npos)
        return Fail("material edit: unsupported material path");
    const std::string beforeHash = withoutSuffix.substr(0, lastDot);
    const size_t slotMarker = beforeHash.rfind(".m");
    if (slotMarker == std::string::npos)
        return Fail("material edit: unsupported material path");
    const std::string slotDigits = beforeHash.substr(slotMarker + 2);
    if (slotDigits.empty()) return Fail("material edit: unsupported material path");
    for (char c : slotDigits)
        if (c < '0' || c > '9')
            return Fail("material edit: unsupported material path");
    const std::string newPath =
        beforeHash + "." + Hex8(newText) + suffix;

    const auto newResolved = m_Root.Resolve(newPath);
    if (!newResolved.ok)
        return Fail("material edit: edited path rejected");
    const auto written = Engine::Content::WriteFileAtomic(newResolved.path,
                                                          newText);
    if (!written.ok)
        return Fail("material edit: could not stage edited material");

    Engine::Content::ContentRegistry candidate = m_Reg;
    candidate.Unregister(entry.guid);
    if (!candidate.RegisterExplicit(entry.guid, newPath, AT::Material))
        return Fail("material edit: registry rebind failed");
    std::string committedScene;
    if (!m_Revisions.ReadCommittedScene(m_ScenePath, &committedScene))
        return Fail("material edit: committed scene unreadable");
    const auto published = m_Revisions.Publish(
        m_Root, candidate.SerializeManifest(), committedScene);
    if (!published.ok) {
        if (published.recoveryRequired) m_RecoveryRequired = true;
        return Fail("material edit: " + published.diagnostic);
    }
    m_Reg = std::move(candidate);
    m_EditedMaterials.insert(entry.guid.ToHex());
    if (!SaveMaterialEdits()) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += "material edit: CouldNotPersistEditMarker";
    }
    const auto mirror = Engine::Content::WriteFileAtomic(
        m_ManifestPath, m_Reg.SerializeManifest());
    if (!mirror.ok) {
        if (!m_Warnings.empty()) m_Warnings += "\n";
        m_Warnings += "material edit: CommittedCleanupPending; legacy mirror update failed";
    }
    Engine::Log::Info("[EditorBridge] material edited: {} (revision {})",
                      newPath, published.revision);
    return true;
}

bool EditorSession::GetAssetDependencies(int32_t assetIndex,
                                         std::string* out) const {
    if (!out) return false;
    Engine::Content::AssetEntry asset;
    if (!AssetAt(assetIndex, &asset)) return false;
    const auto entries = m_Reg.GetAllEntries();
    std::vector<std::pair<std::string, std::string>> registryPairs;
    registryPairs.reserve(entries.size());
    for (const auto& e : entries)
        registryPairs.emplace_back(e.path,
                                   Engine::Content::ToString(e.type));
    auto guidOfPath = [&](const std::string& path) -> std::string {
        for (const auto& e : entries)
            if (e.path == path) return e.guid.ToHex();
        return {};
    };
    auto resolvedOf = [&](const std::string& path) {
        const auto r = m_Root.Resolve(path);
        if (!r.ok) return false;
        std::error_code ec;
        return std::filesystem::exists(Engine::Content::PathFromUtf8(r.path), ec) && !ec;
    };
    auto typeName = [](Engine::Content::AssetType t) {
        return std::string(Engine::Content::ToString(t));
    };
    nlohmann::json deps = nlohmann::json::array();
    auto addGuidDep = [&](const std::string& type, const std::string& guidHex,
                         const std::string& slot) {
        const auto guid = Engine::ResourceGUID::FromHex(guidHex);
        std::string path;
        for (const auto& entry : entries) {
            if (entry.guid == guid) { path = entry.path; break; }
        }
        nlohmann::json d;
        d["type"] = type;
        d["path"] = path;
        d["guid"] = guidHex;
        d["resolved"] = !path.empty() && resolvedOf(path);
        d["slot"] = slot;
        deps.push_back(std::move(d));
    };

    auto addDep = [&](const std::string& type, const std::string& path,
                      const std::string& slot, const std::string& via) {
        nlohmann::json d;
        d["type"] = type;
        d["path"] = path;
        d["guid"] = guidOfPath(path);
        d["resolved"] = resolvedOf(path);
        d["slot"] = slot;
        if (!via.empty()) d["via"] = via;
        deps.push_back(std::move(d));
    };

    auto readAsset = [&](const Engine::Content::AssetEntry& e,
                         std::vector<uint8_t>* bytes) {
        const auto r = m_Root.Resolve(e.path);
        return r.ok && ReadAllBytesFromPath(r.path, bytes);
    };

    if (asset.type == Engine::Content::AssetType::Mesh) {
        std::vector<uint8_t> bytes;
        if (!readAsset(asset, &bytes)) return false;
        try {
            const auto doc = nlohmann::json::parse(bytes.begin(), bytes.end());
            if (doc.contains("sourceFile") && doc["sourceFile"].is_string()) {
                const std::string source = doc["sourceFile"].get<std::string>();
                if (!source.empty()) addDep("Source", source, "source", "");
            }
            std::vector<std::string> names;
            if (doc.contains("materialNames") &&
                doc["materialNames"].is_array()) {
                for (const auto& n : doc["materialNames"])
                    if (n.is_string()) names.push_back(n.get<std::string>());
            }
            const auto slots = Engine::AssetImport::ResolveMaterialAssetPaths(
                asset.path, static_cast<uint32_t>(names.size()), registryPairs);
            if (slots.ok) {
                for (uint32_t slot = 0; slot < slots.slotPaths.size(); ++slot) {
                    const std::string& path = slots.slotPaths[slot];
                    if (path.empty()) continue;
                    addDep("Material", path, std::to_string(slot), "");
                    Engine::Content::AssetEntry mat;
                    mat.path = path;
                    mat.type = Engine::Content::AssetType::Material;
                    std::vector<uint8_t> mb;
                    if (readAsset(mat, &mb)) {
                        const auto mr =
                            Engine::AssetImport::ReadMaterialAssetJson(mb);
                        if (mr.ok) {
                            auto addTex = [&](const Engine::AssetImport::MaterialTextureRef& ref,
                                              const char* role) {
                                if (ref.present && !ref.assetPath.empty())
                                    addDep("Texture", ref.assetPath,
                                           std::string("m") + std::to_string(slot) +
                                               ":" + role,
                                           path);
                            };
                            addTex(mr.material.baseColor, "baseColorTexture");
                            addTex(mr.material.normal, "normalTexture");
                            addTex(mr.material.occlusion, "occlusionTexture");
                            addTex(mr.material.emissive, "emissiveTexture");
                            addTex(mr.material.metallicRoughness,
                                   "metallicRoughnessTexture");
                        }
                    }
                }
            }
        } catch (...) {
            return false;
        }
    } else if (asset.type == Engine::Content::AssetType::Material) {
        std::vector<uint8_t> bytes;
        if (!readAsset(asset, &bytes)) return false;
        const auto mr = Engine::AssetImport::ReadMaterialAssetJson(bytes);
        if (!mr.ok) return false;
        auto addTex = [&](const Engine::AssetImport::MaterialTextureRef& ref,
                          const char* role) {
            if (ref.present && !ref.assetPath.empty())
                addDep("Texture", ref.assetPath, role, "");
        };
        addTex(mr.material.baseColor, "baseColorTexture");
        addTex(mr.material.normal, "normalTexture");
        addTex(mr.material.occlusion, "occlusionTexture");
        addTex(mr.material.emissive, "emissiveTexture");
        addTex(mr.material.metallicRoughness, "metallicRoughnessTexture");
    } else if (asset.type == Engine::Content::AssetType::Prefab) {
        std::vector<uint8_t> bytes;
        if (!readAsset(asset, &bytes)) return false;
        const std::string text(bytes.begin(), bytes.end());
        const auto prefab = Engine::Content::ReadPrefab(text, &asset.guid);
        if (!prefab.ok) return false;
        std::unordered_set<std::string> seen;
        for (const auto& entity : prefab.model.entities) {
            for (const auto& component : entity.components) {
                if (component.type != "Mesh") continue;
                if (!component.data.contains("meshGuid") ||
                    !component.data["meshGuid"].is_string())
                    continue;
                const std::string meshGuid =
                    component.data["meshGuid"].get<std::string>();
                if (seen.insert(meshGuid).second)
                    addGuidDep("Mesh", meshGuid, "component");
            }
        }
    }

    nlohmann::json dependents = nlohmann::json::array();
    for (const auto& other : entries) {
        if (other.guid == asset.guid) continue;
        std::vector<uint8_t> bytes;
        if (!readAsset(other, &bytes)) continue;
        if (other.type == Engine::Content::AssetType::Mesh) {
            try {
                const auto doc = nlohmann::json::parse(bytes.begin(), bytes.end());
                std::vector<std::string> names;
                if (doc.contains("materialNames") && doc["materialNames"].is_array())
                    for (const auto& n : doc["materialNames"])
                        if (n.is_string()) names.push_back(n.get<std::string>());
                const auto slots = Engine::AssetImport::ResolveMaterialAssetPaths(
                    other.path, static_cast<uint32_t>(names.size()), registryPairs);
                if (slots.ok) {
                    for (uint32_t slot = 0; slot < slots.slotPaths.size(); ++slot) {
                        if (slots.slotPaths[slot] == asset.path) {
                            dependents.push_back(
                                {{"type", "Mesh"},
                                 {"path", other.path},
                                 {"guid", other.guid.ToHex()},
                                 {"slot", std::to_string(slot)}});
                        }
                    }
                }
            } catch (...) {
            }
        } else if (other.type == Engine::Content::AssetType::Material) {
            const auto mr = Engine::AssetImport::ReadMaterialAssetJson(bytes);
            if (!mr.ok) continue;
            auto check = [&](const Engine::AssetImport::MaterialTextureRef& ref,
                             const char* role) {
                if (ref.present && ref.assetPath == asset.path) {
                    dependents.push_back({{"type", "Material"},
                                          {"path", other.path},
                                          {"guid", other.guid.ToHex()},
                                          {"slot", role}});
                }
            };
            check(mr.material.baseColor, "baseColorTexture");
            check(mr.material.normal, "normalTexture");
            check(mr.material.occlusion, "occlusionTexture");
            check(mr.material.emissive, "emissiveTexture");
            check(mr.material.metallicRoughness, "metallicRoughnessTexture");
        } else if (other.type == Engine::Content::AssetType::Prefab) {
            const std::string text(bytes.begin(), bytes.end());
            const auto prefab = Engine::Content::ReadPrefab(text, &other.guid);
            if (!prefab.ok) continue;
            for (const auto& entity : prefab.model.entities) {
                for (const auto& component : entity.components) {
                    if (component.type != "Mesh" ||
                        !component.data.contains("meshGuid") ||
                        !component.data["meshGuid"].is_string())
                        continue;
                    if (component.data["meshGuid"].get<std::string>() !=
                        asset.guid.ToHex())
                        continue;
                    dependents.push_back({{"type", "Prefab"},
                                          {"path", other.path},
                                          {"guid", other.guid.ToHex()},
                                          {"slot", "component"}});
                }
            }
        }
    }

    nlohmann::json doc;
    doc["asset"] = {{"type", typeName(asset.type)},
                    {"path", asset.path},
                    {"guid", asset.guid.ToHex()}};
    doc["dependencies"] = deps;
    doc["dependents"] = dependents;
    *out = doc.dump();
    return true;
}

int32_t EditorSession::ReimportAsset(int32_t assetIndex,
                                     bool discardLocalEdits) {
    Engine::Content::AssetEntry asset;
    if (!AssetAt(assetIndex, &asset))
        return Fail("reimport: bad asset index"), -1;
    if (asset.type != Engine::Content::AssetType::Mesh)
        return Fail("reimport: only Mesh assets have a source file"), -1;
    const auto resolved = m_Root.Resolve(asset.path);
    if (!resolved.ok) return Fail("reimport: mesh path rejected"), -1;
    std::vector<uint8_t> bytes;
    if (!ReadAllBytesFromPath(resolved.path, &bytes))
        return Fail("reimport: mesh bytes unreadable"), -1;
    std::string sourceFile;
    std::vector<std::string> materialNames;
    try {
        const auto doc = nlohmann::json::parse(bytes.begin(), bytes.end());
        if (doc.contains("sourceFile") && doc["sourceFile"].is_string())
            sourceFile = doc["sourceFile"].get<std::string>();
        if (doc.contains("materialNames") && doc["materialNames"].is_array())
            for (const auto& n : doc["materialNames"])
                if (n.is_string()) materialNames.push_back(n.get<std::string>());
    } catch (...) {
        return Fail("reimport: mesh JSON invalid"), -1;
    }
    if (sourceFile.empty()) return Fail("reimport: source path missing"), -1;
    const auto sourceResolved = m_Root.Resolve(sourceFile);
    if (!sourceResolved.ok)
        return Fail("reimport: source path rejected"), -1;
    std::error_code ec;
    if (!std::filesystem::exists(
            Engine::Content::PathFromUtf8(sourceResolved.path), ec) || ec)
        return Fail("reimport: source file missing"), -1;

    const auto entries = m_Reg.GetAllEntries();
    std::vector<std::pair<std::string, std::string>> registryPairs;
    registryPairs.reserve(entries.size());
    for (const auto& e : entries)
        registryPairs.emplace_back(e.path,
                                   Engine::Content::ToString(e.type));
    const auto slots = Engine::AssetImport::ResolveMaterialAssetPaths(
        asset.path, static_cast<uint32_t>(materialNames.size()),
        registryPairs);
    std::vector<std::string> editedGuids;
    if (slots.ok) {
        for (const std::string& path : slots.slotPaths) {
            if (path.empty()) continue;
            for (const auto& e : entries) {
                if (e.path == path && m_EditedMaterials.count(e.guid.ToHex()))
                    editedGuids.push_back(e.guid.ToHex());
            }
        }
    }
    if (!editedGuids.empty() && !discardLocalEdits) {
        return Fail("reimport blocked: material " + editedGuids.front() +
                    " has local edits; use discard-local-edits"),
               -1;
    }
    const int32_t result = ImportModelAsset(sourceFile, sourceFile, false, "",
                                            discardLocalEdits);
    if (result < 0) return -1;
    if (discardLocalEdits && !editedGuids.empty()) {
        for (const auto& guid : editedGuids) m_EditedMaterials.erase(guid);
        if (!SaveMaterialEdits()) {
            if (!m_Warnings.empty()) m_Warnings += "\n";
            m_Warnings += "reimport: CouldNotPersistEditMarkerReset";
        }
    }
    return result;
}
bool EditorSession::AssetAt(int32_t index, Engine::Content::AssetEntry* out) const {
    if (!out || index < 0 ||
        index >= static_cast<int32_t>(m_AssetOrder.size()))
        return false;
    const Engine::ResourceGUID& guid = m_AssetOrder[static_cast<size_t>(index)];
    // 注意：GUID→条目用线性注册表查找（unordered_map 迭代序不可依赖）。
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
    m_Bindings.assign(m_EditScene->GetObjectCount(),
                      Engine::Content::EntityContentBinding{});
    for (size_t i = 0; i < m_EditScene->GetObjectCount(); ++i) {
        const auto& obj = m_EditScene->GetObjects()[i];
        if (!obj) continue;
        const auto it = m_BindingsById.find(obj->GetSceneEntityId());
        if (it != m_BindingsById.end()) m_Bindings[i] = it->second;
    }
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
