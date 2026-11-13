#include "Engine/Core/Content/PrefabApplyRecord.h"

#include "Engine/Core/Content/AtomicFile.h"
#include "Engine/Core/Content/RevisionStore.h"
#include "Engine/Core/Content/Utf8Path.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>

namespace Engine::Content {

namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool IsHex16(const std::string& text) {
    if (text.size() != 16) return false;
    for (const char c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
            return false;
    return true;
}

bool IsSafeProjectPath(const std::string& path) {
    if (path.empty() || path[0] == '/' || path[0] == '\\') return false;
    if (path.find('\0') != std::string::npos || path.find('\\') != std::string::npos)
        return false;
    if (path.find("..") != std::string::npos) return false;
    if (path.rfind("assets/", 0) != 0) return false;
    if (path.find(".prefab.json") == std::string::npos) return false;
    return true;
}

bool IsValidTransactionId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id)
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
              (c >= 'a' && c <= 'z') || c == '-' || c == '_'))
            return false;
    return true;
}

nlohmann::json CanonicalBody(const PrefabApplyRecord& record) {
    nlohmann::json json = nlohmann::json::object();
    json["transactionId"] = record.transactionId;
    json["recordVersion"] = record.recordVersion;
    json["state"] = ToString(record.state);
    json["prefabGuid"] = record.prefabGuid.ToHex();
    json["oldSourceRevision"] = record.oldSourceRevision;
    json["oldSourceDigest"] = record.oldSourceDigest;
    json["oldSourcePath"] = record.oldSourcePath;
    json["newDigest"] = record.newDigest;
    json["newPath"] = record.newPath;
    json["newSourceRevision"] = record.newSourceRevision;
    json["instanceRootEntityId"] = record.instanceRootEntityId;
    json["oldInstanceMetadata"] = record.oldInstanceMetadata;
    json["newInstanceMetadata"] = record.newInstanceMetadata;
    json["oldRevisionSnapshot"] = record.oldRevisionSnapshot;
    json["phase1Revision"] = record.phase1Revision;
    json["phase2Revision"] = record.phase2Revision;
    return json;
}

bool ReadFileBytes(const std::string& path, std::string* out) {
    if (!out) return false;
    std::ifstream in(PathFromUtf8(path), std::ios::binary);
    if (!in.is_open()) return false;
    *out = std::string(std::istreambuf_iterator<char>(in),
                       std::istreambuf_iterator<char>());
    return in.good() || in.eof();
}

} // namespace

const char* ToString(PrefabApplyState state) {
    switch (state) {
    case PrefabApplyState::Prepared: return "prepared";
    case PrefabApplyState::PrefabPublished: return "prefab_published";
    case PrefabApplyState::ScenePublished: return "scene_published";
    case PrefabApplyState::RecoveryRequired: return "recovery_required";
    }
    return "recovery_required";
}

bool ParsePrefabApplyState(const std::string& text, PrefabApplyState* out) {
    if (!out) return false;
    if (text == "prepared") { *out = PrefabApplyState::Prepared; return true; }
    if (text == "prefab_published") {
        *out = PrefabApplyState::PrefabPublished; return true;
    }
    if (text == "scene_published") {
        *out = PrefabApplyState::ScenePublished; return true;
    }
    if (text == "recovery_required") {
        *out = PrefabApplyState::RecoveryRequired; return true;
    }
    return false;
}

bool ValidatePrefabApplyRecord(const PrefabApplyRecord& record,
                               std::string* error) {
    if (record.recordVersion != 1)
        return Fail(error, "unsupported Prefab apply record version");
    if (!IsValidTransactionId(record.transactionId))
        return Fail(error, "invalid Prefab apply transactionId");
    if (record.prefabGuid.IsNull())
        return Fail(error, "Prefab apply GUID is zero");
    if (record.oldSourceRevision < 0)
        return Fail(error, "Prefab apply old source revision is negative");
    if (!IsSafeProjectPath(record.oldSourcePath))
        return Fail(error, "Prefab apply old source path is unsafe");
    if (!IsHex16(record.oldSourceDigest))
        return Fail(error, "Prefab apply old source digest is invalid");
    if (!IsHex16(record.newDigest))
        return Fail(error, "Prefab apply new digest is invalid");
    if (!IsSafeProjectPath(record.newPath))
        return Fail(error, "Prefab apply new path is unsafe");
    if (record.instanceRootEntityId == 0)
        return Fail(error, "Prefab apply instance root is zero");
    if (record.oldRevisionSnapshot < 0)
        return Fail(error, "Prefab apply old revision snapshot is negative");
    if (!record.oldInstanceMetadata.is_object() ||
        !record.newInstanceMetadata.is_object())
        return Fail(error, "Prefab apply instance metadata must be objects");
    if (record.newSourceRevision < -1)
        return Fail(error, "Prefab apply new source revision is invalid");
    if (record.phase1Revision < -1 || record.phase2Revision < -1)
        return Fail(error, "Prefab apply phase revision is invalid");
    switch (record.state) {
    case PrefabApplyState::Prepared:
        if (record.phase1Revision != -1 || record.phase2Revision != -1)
            return Fail(error, "prepared record has published phases");
        break;
    case PrefabApplyState::PrefabPublished:
        if (record.phase1Revision < 1 || record.phase2Revision != -1)
            return Fail(error, "prefab_published record phase state is invalid");
        break;
    case PrefabApplyState::ScenePublished:
        if (record.phase1Revision < 1 || record.phase2Revision < 1)
            return Fail(error, "scene_published record phase state is invalid");
        break;
    case PrefabApplyState::RecoveryRequired:
        break;
    }
    return true;
}

bool ComputePrefabApplyRecordIntegrity(
    const PrefabApplyRecord& record, std::string* hash, std::string* error) {
    if (!hash) return Fail(error, "Prefab apply integrity output is null");
    if (!ValidatePrefabApplyRecord(record, error)) return false;
    *hash = RevisionStore::HashBytes(CanonicalBody(record).dump(2));
    return true;
}

bool SerializePrefabApplyRecord(const PrefabApplyRecord& record,
                                std::string* bytes, std::string* error) {
    if (!bytes) return Fail(error, "Prefab apply bytes output is null");
    if (!ValidatePrefabApplyRecord(record, error)) return false;
    std::string hash;
    if (!ComputePrefabApplyRecordIntegrity(record, &hash, error)) return false;
    nlohmann::json json = CanonicalBody(record);
    json["integrityHash"] = hash;
    *bytes = json.dump(2);
    return true;
}

bool ParsePrefabApplyRecord(std::string_view bytes,
                            PrefabApplyRecord* out, std::string* error) {
    if (!out) return Fail(error, "Prefab apply record output is null");
    *out = {};
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(bytes.begin(), bytes.end());
    } catch (const std::exception& ex) {
        return Fail(error, std::string("Prefab apply record JSON invalid: ") + ex.what());
    }
    if (!json.is_object()) return Fail(error, "Prefab apply record root is not an object");
    static const std::set<std::string> keys = {
        "transactionId", "recordVersion", "state", "prefabGuid",
        "oldSourceRevision", "oldSourceDigest", "oldSourcePath", "newDigest",
        "newPath", "newSourceRevision", "instanceRootEntityId",
        "oldInstanceMetadata", "newInstanceMetadata", "oldRevisionSnapshot",
        "phase1Revision", "phase2Revision", "integrityHash"};
    for (auto it = json.begin(); it != json.end(); ++it)
        if (keys.find(it.key()) == keys.end())
            return Fail(error, "Prefab apply record has unknown field '" + it.key() + "'");
    for (const auto& key : keys)
        if (!json.contains(key)) return Fail(error, "Prefab apply record is missing '" + key + "'");

    PrefabApplyRecord record;
    try {
        record.transactionId = json["transactionId"].get<std::string>();
        record.recordVersion = json["recordVersion"].get<int32_t>();
        if (!ParsePrefabApplyState(json["state"].get<std::string>(), &record.state))
            return Fail(error, "Prefab apply state is invalid");
        record.prefabGuid = ResourceGUID::FromHex(json["prefabGuid"].get<std::string>());
        record.oldSourceRevision = json["oldSourceRevision"].get<int64_t>();
        record.oldSourceDigest = json["oldSourceDigest"].get<std::string>();
        record.oldSourcePath = json["oldSourcePath"].get<std::string>();
        record.newDigest = json["newDigest"].get<std::string>();
        record.newPath = json["newPath"].get<std::string>();
        record.newSourceRevision = json["newSourceRevision"].get<int64_t>();
        record.instanceRootEntityId = json["instanceRootEntityId"].get<uint64_t>();
        record.oldInstanceMetadata = json["oldInstanceMetadata"];
        record.newInstanceMetadata = json["newInstanceMetadata"];
        record.oldRevisionSnapshot = json["oldRevisionSnapshot"].get<int64_t>();
        record.phase1Revision = json["phase1Revision"].get<int64_t>();
        record.phase2Revision = json["phase2Revision"].get<int64_t>();
        record.integrityHash = json["integrityHash"].get<std::string>();
    } catch (const std::exception& ex) {
        return Fail(error, std::string("Prefab apply record field type invalid: ") + ex.what());
    }
    if (!IsHex16(record.integrityHash))
        return Fail(error, "Prefab apply integrity hash is invalid");
    if (!ValidatePrefabApplyRecord(record, error)) return false;
    std::string computed;
    if (!ComputePrefabApplyRecordIntegrity(record, &computed, error)) return false;
    if (computed != record.integrityHash)
        return Fail(error, "Prefab apply integrity hash mismatch");
    record.integrityValid = true;
    *out = std::move(record);
    return true;
}

std::string PrefabApplyRecordPath(const ProjectRoot& root,
                                  const ResourceGUID& prefabGuid,
                                  uint64_t instanceRootEntityId) {
    if (!root.IsValid() || prefabGuid.IsNull() || instanceRootEntityId == 0)
        return {};
    return root.Path() + "/.engine-content/prefab-apply/" +
           prefabGuid.ToHex() + "." + std::to_string(instanceRootEntityId) + ".json";
}

bool WritePrefabApplyRecord(const ProjectRoot& root,
                            const PrefabApplyRecord& record,
                            std::string* error) {
    if (!ValidatePrefabApplyRecord(record, error)) return false;
    if (record.prefabGuid.IsNull() || record.instanceRootEntityId == 0)
        return Fail(error, "Prefab apply record identity is invalid");
    const std::string path = PrefabApplyRecordPath(
        root, record.prefabGuid, record.instanceRootEntityId);
    if (path.empty()) return Fail(error, "Prefab apply record path is invalid");
    std::string bytes;
    if (!SerializePrefabApplyRecord(record, &bytes, error)) return false;
    std::error_code ec;
    const auto parent = std::filesystem::path(PathFromUtf8(path)).parent_path();
    std::filesystem::create_directories(parent, ec);
    if (ec) return Fail(error, "Prefab apply record directory creation failed");
    const AtomicFileResult written = WriteFileAtomic(path, bytes);
    if (!written.ok)
        return Fail(error, "Prefab apply record write failed: " +
                           std::string(ToString(written.error)));
    return true;
}

bool ReadPrefabApplyRecord(const ProjectRoot& root,
                           const ResourceGUID& prefabGuid,
                           uint64_t instanceRootEntityId,
                           PrefabApplyRecord* out, std::string* error) {
    if (!out) return Fail(error, "Prefab apply record output is null");
    const std::string path = PrefabApplyRecordPath(root, prefabGuid,
                                                    instanceRootEntityId);
    if (path.empty()) return Fail(error, "Prefab apply record path is invalid");
    std::string bytes;
    if (!ReadFileBytes(path, &bytes))
        return Fail(error, "Prefab apply record is unreadable");
    if (!ParsePrefabApplyRecord(bytes, out, error)) return false;
    if (out->prefabGuid != prefabGuid || out->instanceRootEntityId != instanceRootEntityId)
        return Fail(error, "Prefab apply record identity mismatch");
    return true;
}

bool RemovePrefabApplyRecord(const ProjectRoot& root,
                             const ResourceGUID& prefabGuid,
                             uint64_t instanceRootEntityId,
                             std::string* error) {
    const std::string path = PrefabApplyRecordPath(root, prefabGuid,
                                                    instanceRootEntityId);
    if (path.empty()) return Fail(error, "Prefab apply record path is invalid");
    std::error_code ec;
    std::filesystem::remove(PathFromUtf8(path), ec);
    if (ec) return Fail(error, "Prefab apply record removal failed");
    return true;
}

} // namespace Engine::Content