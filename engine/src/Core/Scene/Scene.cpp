#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/Serializer.h"
#include "Engine/Core/RHI/IRenderQueue.h"
#include "Engine/Core/Physics/PhysicsComponent.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/GameObject/ComponentRegistry_Go.h"
#include "Engine/Core/Content/PrefabOverrideModel.h"
#include <nlohmann/json.hpp>
#include "Engine/Core/Log.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <unordered_set>
#include <cmath>
#include <cstring>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Engine {

    // ──────────────────────────────────────────────
    // 构造 / 析构
    // ──────────────────────────────────────────────

    Scene::Scene()
        : m_Name("Scene") {
    }

    Scene::Scene(std::string name)
        : m_Name(std::move(name)) {
    }

    Scene::~Scene() {
        OnDestroy();
        m_Objects.clear();
    }

    // ──────────────────────────────────────────────
    // 对象管理
    // ──────────────────────────────────────────────
    bool Scene::RegisterEntityTree(const std::shared_ptr<GameObject>& obj,
                                   std::string* error) {
        if (!obj) {
            if (error) *error = "null entity";
            return false;
        }
        std::unordered_set<uint64> explicitIds;
        std::function<bool(const std::shared_ptr<GameObject>&)> preflight =
            [&](const std::shared_ptr<GameObject>& node) {
                if (!node) return false;
                const uint64 id = node->GetSceneEntityId();
                if (id != 0) {
                    if (m_EntityById.count(id) || !explicitIds.insert(id).second) {
                        if (error) *error = "duplicate scene entity id";
                        return false;
                    }
                }
                for (const auto& child : node->GetChildren())
                    if (!preflight(child)) return false;
                return true;
            };
        const uint64 assigned = obj->GetSceneEntityId();
        uint64 id = assigned;
        if (id == 0) {
            while (m_NextEntityId != 0 &&
                   m_NextEntityId != std::numeric_limits<uint64>::max() &&
                   (m_EntityById.count(m_NextEntityId) ||
                    explicitIds.count(m_NextEntityId))) {
                ++m_NextEntityId;
            }
            if (m_NextEntityId == 0 ||
                m_NextEntityId == std::numeric_limits<uint64>::max()) {
                if (error) *error = "scene entity id exhausted";
                return false;
            }
            id = m_NextEntityId++;
        } else if (id >= m_NextEntityId) {
            if (id == std::numeric_limits<uint64>::max()) {
                if (error) *error = "scene entity id exhausted";
                return false;
            }
            m_NextEntityId = id + 1;
        }
        auto inserted = m_EntityById.emplace(id, obj.get());
        if (!inserted.second) {
            if (error) *error = "duplicate scene entity id";
            return false;
        }
        obj->SetSceneEntityId(id);
        for (const auto& child : obj->GetChildren()) {
            if (!RegisterEntityTree(child, error)) return false;
        }
        return true;
    }

    void Scene::AddObject(std::shared_ptr<GameObject> obj) {
        if (!obj) return;
        uint64 parentId = 0;
        if (obj->GetParent()) {
            const uint64 candidate = obj->GetParent()->GetSceneEntityId();
            if (candidate != 0 && m_EntityById.count(candidate) != 0)
                parentId = candidate;
        }
        std::string error;
        if (!AddEntity(std::move(obj), parentId, &error)) {
            Engine::Log::Error("[Scene] AddObject rejected: {}", error);
        }
    }

    bool Scene::AddEntity(std::shared_ptr<GameObject> obj, uint64 parentId,
                          std::string* error) {
        if (!obj) {
            if (error) *error = "null entity";
            return false;
        }
        if (parentId != 0 && m_EntityById.count(parentId) == 0) {
            if (error) *error = "parent entity not found";
            return false;
        }
        if (!RegisterEntityTree(obj, error)) return false;
        const bool isRoot = std::find(m_Objects.begin(), m_Objects.end(), obj) !=
                            m_Objects.end();
        if (!isRoot && obj->GetParent() == nullptr)
            m_Objects.push_back(obj);
        if (parentId == 0) {
            if (obj->GetParent() != nullptr)
                return SetEntityParent(obj->GetSceneEntityId(), 0,
                                       ReparentMode::KeepLocal, error);
            return true;
        }
        return SetEntityParent(obj->GetSceneEntityId(), parentId,
                               ReparentMode::KeepLocal, error);
    }

    bool Scene::RemoveObject(GameObject* obj) {
        if (!obj) return false;

        std::vector<GameObject*> subtree;
        std::function<void(GameObject*)> collect = [&](GameObject* node) {
            if (!node) return;
            subtree.push_back(node);
            for (const auto& child : node->GetChildren())
                collect(child.get());
        };
        collect(obj);
        for (auto it = m_PrefabInstances.begin(); it != m_PrefabInstances.end();) {
            bool remove = false;
            for (GameObject* node : subtree) {
                if (it->first == node->GetSceneEntityId()) { remove = true; break; }
                for (const auto& entry : it->second.localToScene) {
                    if (entry.second == node->GetSceneEntityId()) {
                        remove = true; break;
                    }
                }
                if (remove) break;
            }
            if (remove) it = m_PrefabInstances.erase(it); else ++it;
        }
        for (GameObject* node : subtree) {
            m_PrefabInstances.erase(node->GetSceneEntityId());
            m_EntityById.erase(node->GetSceneEntityId());
        }

        std::shared_ptr<GameObject> owned;
        auto it = std::find_if(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) { return o.get() == obj; });
        if (it != m_Objects.end()) {
            owned = *it;
            m_Objects.erase(it);
        } else {
            GameObject* parent = obj->GetParent();
            if (parent) {
                for (const auto& c : parent->GetChildren())
                    if (c.get() == obj) { owned = c; break; }
                if (owned) parent->RemoveChild(obj);
            }
        }
        if (!owned) return false;

        owned->OnDestroy();
        return true;
    }

    bool Scene::DeleteEntity(uint64 entityId) {
        GameObject* obj = FindByEntityId(entityId);
        return obj ? RemoveObject(obj) : false;
    }

    void Scene::Clear() {
        OnDestroy();
        m_Objects.clear();
        m_PrefabInstances.clear();
    }

    bool Scene::Contains(GameObject* obj) const {
        if (!obj) return false;
        return std::any_of(m_Objects.begin(), m_Objects.end(),
            [obj](const std::shared_ptr<GameObject>& o) { return o.get() == obj; });
    }

    size_t Scene::GetTotalObjectCount() const {
        size_t count = 0;
        std::function<void(const std::vector<std::shared_ptr<GameObject>>&)> recCount;
        recCount = [&](const auto& list) {
            for (auto& obj : list) {
                ++count;
                recCount(obj->GetChildren());
            }
        };
        recCount(m_Objects);
        return count;
    }

    // ──────────────────────────────────────────────
    // 查找
    // ──────────────────────────────────────────────

    namespace detail {
        GameObject* findRecursive(const std::string& name,
                                  const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (auto& obj : objects) {
                if (obj->GetName() == name) return obj.get();
                auto* found = findRecursive(name, obj->GetChildren());
                if (found) return found;
            }
            return nullptr;
        }

        const GameObject* findRecursiveConst(
                const std::string& name,
                const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (const auto& obj : objects) {
                if (obj->GetName() == name) return obj.get();
                auto* found = findRecursiveConst(name, obj->GetChildren());
                if (found) return found;
            }
            return nullptr;
        }
        GameObject* findByIDRecursive(uint32 id,
                const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (auto& obj : objects) {
                if (obj->GetID() == id) return obj.get();
                auto* found = findByIDRecursive(id, obj->GetChildren());
                if (found) return found;
            }
            return nullptr;
        }

        const GameObject* findByIDRecursiveConst(uint32 id,
                const std::vector<std::shared_ptr<GameObject>>& objects) {
            for (const auto& obj : objects) {
                if (obj->GetID() == id) return obj.get();
                auto* found = findByIDRecursiveConst(id, obj->GetChildren());
                if (found) return found;
            }
            return nullptr;
        }
    } // namespace detail

    GameObject* Scene::FindObject(const std::string& name) {
        return detail::findRecursive(name, m_Objects);
    }

    const GameObject* Scene::FindObject(const std::string& name) const {
        return detail::findRecursiveConst(name, m_Objects);
    }

    GameObject* Scene::FindByID(uint32 id) {
        return detail::findByIDRecursive(id, m_Objects);
    }

    const GameObject* Scene::FindByID(uint32 id) const {
        return detail::findByIDRecursiveConst(id, m_Objects);
    }
    GameObject* Scene::FindByEntityId(uint64 entityId) {
        const auto it = m_EntityById.find(entityId);
        return it == m_EntityById.end() ? nullptr : it->second;
    }

    const GameObject* Scene::FindByEntityId(uint64 entityId) const {
        const auto it = m_EntityById.find(entityId);
        return it == m_EntityById.end() ? nullptr : it->second;
    }

    uint64 Scene::GetParentEntityId(uint64 entityId) const {
        const GameObject* obj = FindByEntityId(entityId);
        return obj && obj->GetParent() ? obj->GetParent()->GetSceneEntityId() : 0;
    }

    std::vector<GameObject*> Scene::GetAllEntities() const {
        std::vector<GameObject*> out;
        std::unordered_set<GameObject*> visited;
        std::function<void(GameObject*)> visit = [&](GameObject* obj) {
            if (!obj || !visited.insert(obj).second) return;
            out.push_back(obj);
            for (const auto& child : obj->GetChildren())
                visit(child.get());
        };
        for (const auto& root : m_Objects) visit(root.get());
        return out;
    }
    namespace {
        bool IsDuplicateComponentAllowed(const char* typeName) {
            if (!typeName) return false;
            const std::string type(typeName);
            if (type != "Mesh" && type != "Camera" && type != "Collider" &&
                type != "RigidBody" && type != "Skinning")
                return false;
            return ComponentRegistryGo::IsRegistered(type);
        }

        std::string MakeDuplicateName(const Scene& scene, const std::string& base) {
            const std::string stem = base.empty() ? "Entity" : base;
            for (int i = 1; i < 100000; ++i) {
                const std::string candidate = stem + "_Copy" +
                    (i == 1 ? std::string() : "_" + std::to_string(i));
                if (!scene.FindObject(candidate)) return candidate;
            }
            return stem + "_Copy";
        }
    }

    bool Scene::DuplicateEntity(uint64 sourceId, bool includeSubtree,
                                uint64* newRootId,
                                std::unordered_map<uint64, uint64>* idMap,
                                std::string* error) {
        auto fail = [&](const std::string& message) {
            if (error) *error = message;
            return false;
        };
        if (sourceId == 0) return fail("source entity id is zero");
        GameObject* source = FindByEntityId(sourceId);
        if (!source) return fail("source entity not found");
        if (!ValidateComposition()) return fail("scene composition is invalid");

        struct ClonePair {
            GameObject* source = nullptr;
            std::shared_ptr<GameObject> clone;
        };
        std::vector<ClonePair> pairs;
        std::vector<GameObject*> created;

        std::function<bool(GameObject*, bool, std::shared_ptr<GameObject>&)> build =
            [&](GameObject* src, bool isRoot, std::shared_ptr<GameObject>& out) {
                if (!src) return fail("null entity in subtree");
                const std::string name = isRoot
                    ? MakeDuplicateName(*this, src->GetName())
                    : src->GetName();
                auto dst = std::make_shared<GameObject>(name);
                dst->GetTransform().SetPosition(src->GetTransform().GetPosition());
                dst->GetTransform().SetRotationQuat(
                    src->GetTransform().GetRotationQuat());
                dst->GetTransform().SetScale(src->GetTransform().GetScale());
                dst->SetActive(src->IsActive());
                dst->SetLayer(src->GetLayer());
                dst->SetLayerMask(src->GetLayerMask());

                bool ok = true;
                src->ForEachComponent([&](const Component& component) {
                    if (!ok) return;
                    const char* typeName = component.GetComponentTypeName();
                    if (!IsDuplicateComponentAllowed(typeName)) {
                        ok = fail("duplicate: unsupported component '" +
                                  std::string(typeName ? typeName : "(non-contract)") +
                                  "' on entity '" + src->GetName() + "'");
                        return;
                    }
                    std::shared_ptr<Component> copy =
                        ComponentRegistryGo::Create(typeName);
                    if (!copy) {
                        ok = fail("duplicate: component factory failed for '" +
                                  std::string(typeName) + "'");
                        return;
                    }
                    nlohmann::json data;
                    try {
                        component.Serialize(data);
                        if (!copy->Deserialize(data)) {
                            ok = fail("duplicate: component data rejected for '" +
                                      std::string(typeName) + "' on entity '" +
                                      src->GetName() + "'");
                            return;
                        }
                    } catch (const std::exception& ex) {
                        ok = fail("duplicate: component copy threw for '" +
                                  std::string(typeName) + "': " + ex.what());
                        return;
                    }
                    if (!dst->Attach(copy)) {
                        ok = fail("duplicate: component attach failed for '" +
                                  std::string(typeName) + "'");
                    }
                });
                if (!ok) return false;

                if (includeSubtree) {
                    for (const auto& child : src->GetChildren()) {
                        std::shared_ptr<GameObject> childClone;
                        if (!build(child.get(), false, childClone)) return false;
                        dst->AddChild(childClone);
                    }
                }
                out = std::move(dst);
                created.push_back(out.get());
                pairs.push_back({src, out});
                return true;
            };

        std::shared_ptr<GameObject> rootClone;
        if (!build(source, true, rootClone)) return false;

        auto allocateId = [&](uint64& id) {
            while (m_NextEntityId != 0 &&
                   m_NextEntityId != std::numeric_limits<uint64>::max() &&
                   m_EntityById.count(m_NextEntityId)) {
                ++m_NextEntityId;
            }
            if (m_NextEntityId == 0 ||
                m_NextEntityId == std::numeric_limits<uint64>::max())
                return false;
            id = m_NextEntityId++;
            return true;
        };
        for (GameObject* node : created) {
            uint64 id = 0;
            if (!allocateId(id)) return fail("duplicate: scene entity id exhausted");
            node->SetSceneEntityId(id);
        }

        std::vector<uint64> inserted;
        for (GameObject* node : created) {
            const uint64 id = node->GetSceneEntityId();
            if (!m_EntityById.emplace(id, node).second) {
                for (uint64 rollback : inserted) m_EntityById.erase(rollback);
                return fail("duplicate: scene entity id collision");
            }
            inserted.push_back(id);
        }

        GameObject* sourceParent = source->GetParent();
        if (sourceParent) {
            sourceParent->AddChild(rootClone);
        } else {
            m_Objects.push_back(rootClone);
        }
        rootClone->MarkTransformDirtyRecursive();

        if (newRootId) *newRootId = rootClone->GetSceneEntityId();
        if (idMap) {
            idMap->clear();
            for (const auto& pair : pairs)
                (*idMap)[pair.source->GetSceneEntityId()] =
                    pair.clone->GetSceneEntityId();
        }
        return true;
    }


    bool Scene::SetEntityParent(uint64 childId, uint64 parentId,
                                ReparentMode mode, std::string* error) {
        auto fail = [&](const char* message) {
            if (error) *error = message;
            return false;
        };
        if (childId == 0) return fail("child entity id is zero");
        if (childId == parentId) return fail("entity cannot parent itself");
        GameObject* child = FindByEntityId(childId);
        if (!child) return fail("child entity not found");
        GameObject* parent = parentId == 0 ? nullptr : FindByEntityId(parentId);
        if (parentId != 0 && !parent) return fail("parent entity not found");
        for (GameObject* p = parent; p; p = p->GetParent())
            if (p == child) return fail("reparent would create a cycle");

        GameObject* oldParent = child->GetParent();
        if (oldParent == parent) return true;

        Vec3 newPosition;
        Quat newRotation;
        Vec3 newScale;
        if (mode == ReparentMode::KeepWorld) {
            const Mat4 oldWorld = child->GetTransform().GetWorldMatrix();
            glm::mat4 parentWorld(1.0f);
            if (parent)
                std::memcpy(&parentWorld,
                            parent->GetTransform().GetWorldMatrix().Data(),
                            sizeof(float) * 16);
            const float determinant = glm::determinant(glm::mat3(parentWorld));
            if (!std::isfinite(determinant) || std::fabs(determinant) < 1e-8f)
                return fail("parent world matrix is singular");
            const glm::mat4 inverse = glm::inverse(parentWorld);
            const float* inverseData = glm::value_ptr(inverse);
            for (int i = 0; i < 16; ++i)
                if (!std::isfinite(inverseData[i]))
                    return fail("parent world inverse is non-finite");
            const glm::mat4 local = inverse * glm::make_mat4(oldWorld.Data());
            Mat4 localMat;
            std::memcpy(localMat.Data(), &local, sizeof(float) * 16);
            if (!TransformComponent::TryDecomposeMatrix(
                    localMat, &newPosition, &newRotation, &newScale))
                return fail("keep-world local transform is not representable");
        }

        std::shared_ptr<GameObject> shared;
        if (oldParent) {
            for (const auto& candidate : oldParent->GetChildren())
                if (candidate.get() == child) { shared = candidate; break; }
            if (!shared) return fail("child parent/children are inconsistent");
            oldParent->RemoveChild(child);
        } else {
            const auto it = std::find_if(m_Objects.begin(), m_Objects.end(),
                [child](const std::shared_ptr<GameObject>& o) {
                    return o.get() == child;
                });
            if (it == m_Objects.end())
                return fail("child is not attached to the scene");
            shared = *it;
            m_Objects.erase(it);
        }

        if (parent) {
            parent->AddChild(shared);
        } else {
            child->SetParent(nullptr);
            m_Objects.push_back(shared);
        }

        if (mode == ReparentMode::KeepWorld) {
            TransformComponent& transform = child->GetTransform();
            transform.SetPosition(newPosition);
            transform.SetRotationQuat(newRotation);
            transform.SetScale(newScale);
        }
        child->MarkTransformDirtyRecursive();
        return true;
    }

    bool Scene::SetPrefabInstanceMetadata(uint64 rootId,
                                        const PrefabInstanceMetadata& metadata,
                                        std::string* error) {
        auto fail = [&](const std::string& message) {
            if (error) *error = message;
            return false;
        };
        if (rootId == 0 || !FindByEntityId(rootId))
            return fail("Prefab instance root not found");
        if (metadata.prefabGuid.IsNull())
            return fail("Prefab instance GUID is zero");
        if (metadata.sourceRevision < 0)
            return fail("Prefab instance source revision is negative");
        if (metadata.localToScene.empty())
            return fail("Prefab instance Local ID map is empty");

        std::unordered_set<uint64> sceneIds;
        bool rootMapped = false;
        for (const auto& [localId, sceneId] : metadata.localToScene) {
            if (localId == 0 || sceneId == 0)
                return fail("Prefab instance map contains zero");
            const GameObject* entity = FindByEntityId(sceneId);
            if (!entity)
                return fail("Prefab instance map targets missing entity");
            if (!sceneIds.insert(sceneId).second)
                return fail("Prefab instance map has duplicate Scene ID");
            const GameObject* cursor = entity;
            bool underRoot = false;
            while (cursor) {
                if (cursor->GetSceneEntityId() == rootId) {
                    underRoot = true;
                    break;
                }
                cursor = cursor->GetParent();
            }
            if (!underRoot)
                return fail("Prefab instance map escapes its root");
            if (sceneId == rootId) rootMapped = true;
        }
        if (!rootMapped)
            return fail("Prefab instance map does not contain its root");

        if (metadata.legacySourceIdentity) {
            if (!metadata.sourceDigest.empty() || !metadata.sourcePath.empty())
                return fail("legacy Prefab instance has source identity fields");
            if (!metadata.overrides.empty())
                return fail("legacy Prefab instance cannot have overrides");
        } else {
            if (metadata.sourceDigest.size() != 16)
                return fail("Prefab instance sourceDigest must be 16 hex");
            for (const char c : metadata.sourceDigest)
                if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
                    return fail("Prefab instance sourceDigest is invalid");
            if (metadata.sourcePath.empty())
                return fail("Prefab instance sourcePath is empty");
            if (metadata.sourcePath[0] == '/' || metadata.sourcePath[0] == '\\' ||
                metadata.sourcePath.find("..") != std::string::npos)
                return fail("Prefab instance sourcePath is unsafe");
        }

        std::unordered_set<std::string> overrideIds;
        for (const auto& record : metadata.overrides) {
            std::string overrideError;
            if (!ValidatePrefabOverrideRecord(record, &overrideError))
                return fail(overrideError);
            if (metadata.localToScene.find(record.localId) ==
                metadata.localToScene.end())
                return fail("Prefab override localId is not mapped");
            const std::string key = std::to_string(record.localId) + "|" +
                                    record.component + "|" + record.field;
            if (!overrideIds.insert(key).second)
                return fail("duplicate Prefab override identity");
        }
        if (metadata.overrides.empty() &&
            (metadata.syncState == Content::PrefabSyncState::Overrides ||
             metadata.syncState == Content::PrefabSyncState::ApplyPending)) {
            return fail("Prefab sync state expects overrides");
        }
        if (!metadata.overrides.empty() &&
            metadata.syncState == Content::PrefabSyncState::Clean) {
            return fail("Prefab sync state does not reflect overrides");
        }
        if (metadata.reconciliation.has_value()) {
            std::string reconciliationError;
            if (!Content::ValidatePrefabReconciliationState(
                    *metadata.reconciliation, &reconciliationError)) {
                return fail("Prefab reconciliation is invalid: " +
                            reconciliationError);
            }
            if (metadata.syncState != Content::PrefabSyncState::Conflict)
                return fail("Prefab reconciliation requires conflict state");
        } else if (metadata.syncState == Content::PrefabSyncState::Conflict) {
            return fail("Prefab conflict state has no reconciliation records");
        }

        m_PrefabInstances[rootId] = metadata;
        NormalizePrefabOverrides(&m_PrefabInstances[rootId].overrides);
        return true;
    }
    bool Scene::FindPrefabInstanceForEntity(uint64 entityId, uint64* rootId,
                                           PrefabInstanceMetadata* out) const {
        if (rootId) *rootId = 0;
        if (out) *out = {};
        const GameObject* entity = FindByEntityId(entityId);
        if (!entity) return false;
        for (const GameObject* cursor = entity; cursor;
             cursor = cursor->GetParent()) {
            const uint64 id = cursor->GetSceneEntityId();
            const auto it = m_PrefabInstances.find(id);
            if (it == m_PrefabInstances.end()) continue;
            if (rootId) *rootId = id;
            if (out) *out = it->second;
            return true;
        }
        return false;
    }

    bool Scene::GetPrefabInstanceMetadata(uint64 rootId,
                                          PrefabInstanceMetadata* out) const {
        if (!out) return false;
        const auto it = m_PrefabInstances.find(rootId);
        if (it == m_PrefabInstances.end()) return false;
        *out = it->second;
        return true;
    }

    bool Scene::RemovePrefabInstanceMetadata(uint64 rootId) {
        return m_PrefabInstances.erase(rootId) != 0;
    }

    bool Scene::ValidateComposition(std::string* error) const {
        std::unordered_set<uint64> ids;
        std::unordered_set<GameObject*> visited;
        auto fail = [&](const char* message) {
            if (error) *error = message;
            return false;
        };
        for (const auto& [id, obj] : m_EntityById) {
            if (id == 0 || !obj) return fail("invalid entity map entry");
            if (!ids.insert(id).second) return fail("duplicate entity id");
        }
        std::function<bool(GameObject*, GameObject*)> visit =
            [&](GameObject* obj, GameObject* expectedParent) {
                if (!obj) return fail("null entity");
                if (!visited.insert(obj).second) return fail("hierarchy cycle");
                if (obj->GetSceneEntityId() == 0) return fail("zero entity id");
                if (m_EntityById.find(obj->GetSceneEntityId()) == m_EntityById.end())
                    return fail("entity missing from id map");
                if (obj->GetParent() != expectedParent)
                    return fail("parent/children mismatch");
                std::unordered_set<GameObject*> children;
                for (const auto& child : obj->GetChildren()) {
                    if (!child || !children.insert(child.get()).second)
                        return fail("duplicate child");
                    if (!visit(child.get(), obj)) return false;
                }
                return true;
            };
        for (const auto& root : m_Objects) {
            if (root->GetParent() != nullptr) return fail("root has a parent");
            if (!visit(root.get(), nullptr)) return false;
        }
        if (visited.size() != m_EntityById.size())
            return fail("orphan entity in id map");
        return true;
    }


    // ──────────────────────────────────────────────
    // 生命周期（非活跃场景直接跳过）
    // ──────────────────────────────────────────────

    void Scene::OnCreate() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->OnCreate();
            std::function<void(GameObject&)> recCreate =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnCreate();
                        recCreate(*c);
                    }
                };
            recCreate(*obj);
        }
    }

    void Scene::Update(float32 dt) {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->Update(dt);
        }
    }

    void Scene::Render() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->Render();
            std::function<void(GameObject&)> recRender =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->Render();
                        recRender(*c);
                    }
                };
            recRender(*obj);
        }
    }

    void Scene::OnDestroy() {
        for (auto& obj : m_Objects) {
            obj->OnDestroy();
            std::function<void(GameObject&)> recDestroy =
                [&](GameObject& o) {
                    for (auto& c : o.GetChildren()) {
                        c->OnDestroy();
                        recDestroy(*c);
                    }
                };
            recDestroy(*obj);
        }
    }

    // ──────────────────────────────────────────────
    // 物理同步
    // ──────────────────────────────────────────────

    namespace detail {
        static void PostPhysicsUpdateRecursive(GameObject& obj) {
            if (obj.HasPhysics()) {
                Vec2 pos;
                float32 angle;
                obj.GetPhysics().SyncPhysicsToTransform(pos, angle);
                obj.GetTransform().SetPosition(pos.x, pos.y, 0.0f);
                obj.GetTransform().SetRotation(0.0f, 0.0f, angle);
            }
            for (auto& child : obj.GetChildren()) {
                PostPhysicsUpdateRecursive(*child);
            }
        }
    } // namespace detail

    void Scene::PostPhysicsUpdate() {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            detail::PostPhysicsUpdateRecursive(*obj);
        }
    }

    void Scene::CollectRenderCommands(IRenderQueue& queue) {
        if (!m_Active) return;
        for (auto& obj : m_Objects) {
            obj->CollectRenderCommands(queue);
            std::function<void(GameObject&)> collectRecursive =
                [&](GameObject& obj2) {
                    for (auto& child : obj2.GetChildren()) {
                        child->CollectRenderCommands(queue);
                        collectRecursive(*child);
                    }
                };
            collectRecursive(*obj);
        }
    }

    // ──────────────────────────────────────────────
    // 序列化/反序列化
    // ──────────────────────────────────────────────

    bool Scene::SaveToFile(const std::string& filePath) const {
        return JsonSerializer::SaveToFile(*this, filePath);
    }

    bool Scene::LoadFromFile(const std::string& filePath) {
        return JsonSerializer::LoadFromFile(*this, filePath);
    }

} // namespace Engine
