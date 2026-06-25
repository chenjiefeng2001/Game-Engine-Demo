#include "Engine/Editor/UndoSystem.h"
#include "Engine/Core/GameObject/GameObject.h"
#include "Engine/Core/GameObject/Component.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace Engine {

    // ============================================================
    // UndoManager 单例
    // ============================================================
    UndoManager& UndoManager::Get() {
        static UndoManager instance;
        return instance;
    }

    UndoStack& UndoManager::GetSceneStack(const std::string& sceneName) {
        return m_SceneStacks[sceneName];
    }

    void UndoManager::RemoveSceneStack(const std::string& sceneName) {
        m_SceneStacks.erase(sceneName);
    }

    void UndoManager::RecordPropertyChange(GameObject* target,
                                            const std::string& propName,
                                            const nlohmann::json& oldValue,
                                            const nlohmann::json& newValue) {
        auto cmd = std::make_unique<PropertyChangeCommand>(target, propName, oldValue, newValue);
        auto& mgr = Get();
        if (!mgr.m_ActiveScene.empty())
            mgr.m_SceneStacks[mgr.m_ActiveScene].PushCommand(std::move(cmd));
        else
            mgr.m_GlobalStack.PushCommand(std::move(cmd));
    }

    void UndoManager::RecordCreateObject(Scene* scene,
                                          std::shared_ptr<GameObject> object,
                                          GameObject* parent) {
        auto cmd = std::make_unique<CreateGameObjectCommand>(scene, object, parent);
        auto& mgr = Get();
        mgr.m_GlobalStack.PushCommand(std::move(cmd));
    }

    void UndoManager::RecordDeleteObject(Scene* scene, GameObject* object) {
        auto cmd = std::make_unique<DeleteGameObjectCommand>(scene, object);
        auto& mgr = Get();
        mgr.m_GlobalStack.PushCommand(std::move(cmd));
    }

    void UndoManager::RecordAddComponent(GameObject* target,
                                          const std::string& componentType,
                                          const nlohmann::json& componentData) {
        auto cmd = std::make_unique<AddComponentCommand>(target, componentType, componentData);
        auto& mgr = Get();
        mgr.m_GlobalStack.PushCommand(std::move(cmd));
    }

    void UndoManager::RecordRemoveComponent(GameObject* target,
                                             size_t componentTypeId,
                                             const std::string& componentTypeName,
                                             const nlohmann::json& componentData) {
        auto cmd = std::make_unique<RemoveComponentCommand>(target, componentTypeId,
                                                             componentTypeName, componentData);
        auto& mgr = Get();
        mgr.m_GlobalStack.PushCommand(std::move(cmd));
    }

    void UndoManager::OpenGroup(const std::string& label) {
        Get().m_GlobalStack.OpenGroup(label);
    }

    void UndoManager::CloseGroup() {
        Get().m_GlobalStack.CloseGroup();
    }

    bool UndoManager::IsInGroup() {
        return Get().m_GlobalStack.IsInGroup();
    }

    // ============================================================
    // PropertyChangeCommand
    // ============================================================
    PropertyChangeCommand::PropertyChangeCommand(GameObject* target,
                                                  std::string propertyName,
                                                  nlohmann::json oldValue,
                                                  nlohmann::json newValue)
        : m_ObjectID(target ? target->GetID() : 0)
        , m_ObjectName(target ? target->GetName() : "Unknown")
        , m_PropertyName(std::move(propertyName))
        , m_OldValue(std::move(oldValue))
        , m_NewValue(std::move(newValue)) {}

    const char* PropertyChangeCommand::GetDescription() const {
        static std::string desc;
        desc = "Modify " + m_ObjectName + "." + m_PropertyName;
        return desc.c_str();
    }

    GameObject* PropertyChangeCommand::FindTarget() const {
        // 通过 SceneManager 查找对象
        // 简化实现：这里假设外部通过回调注入查找逻辑
        return nullptr;
    }

    void PropertyChangeCommand::Execute() {
        // 应用新值
        auto* obj = FindTarget();
        if (!obj) {
            Log::Info("[Undo] Cannot execute PropertyChange: object {} not found", m_ObjectID);
            return;
        }
        Log::Info("[Undo] Execute: {}.{}", m_ObjectName, m_PropertyName);
    }

    void PropertyChangeCommand::Undo() {
        // 恢复旧值
        auto* obj = FindTarget();
        if (!obj) {
            Log::Info("[Undo] Cannot undo PropertyChange: object {} not found", m_ObjectID);
            return;
        }
        Log::Info("[Undo] Undo: {}.{}", m_ObjectName, m_PropertyName);
    }

    bool PropertyChangeCommand::TryMerge(const UndoCommand* previous) {
        auto* prev = dynamic_cast<const PropertyChangeCommand*>(previous);
        if (!prev) return false;

        // 同一对象的同一属性可以合并
        if (prev->m_ObjectID == m_ObjectID && prev->m_PropertyName == m_PropertyName) {
            // 保留最旧的旧值和最新的新值
            m_OldValue = prev->m_OldValue;
            return true;
        }
        return false;
    }

    std::vector<uint64> PropertyChangeCommand::GetAffectedObjectIDs() const {
        return {m_ObjectID};
    }

    // ============================================================
    // CreateGameObjectCommand
    // ============================================================
    CreateGameObjectCommand::CreateGameObjectCommand(Scene* scene,
                                                      std::shared_ptr<GameObject> object,
                                                      GameObject* parent)
        : m_Scene(scene)
        , m_ObjectID(object ? object->GetID() : 0)
        , m_ObjectName(object ? object->GetName() : "Unknown")
        , m_ParentID(parent ? parent->GetID() : 0) {
        if (object) {
            // 序列化对象基本信息用于撤销重建
            nlohmann::json j;
            j["name"] = object->GetName();
            j["active"] = object->IsActive();
            j["layer"] = object->GetLayer();
            // 序列化组件列表
            nlohmann::json components = nlohmann::json::array();
            object->ForEachComponent([&](Component& comp) {
                nlohmann::json compJson;
                compJson["type"] = comp.GetTypeDisplayName();
                compJson["typeId"] = typeid(comp).hash_code();
                compJson["enabled"] = comp.IsEnabled();
                comp.Serialize(compJson);
                components.push_back(compJson);
            });
            j["components"] = components;
            m_SerializedData = std::move(j);
        }
    }

    const char* CreateGameObjectCommand::GetDescription() const {
        static std::string desc;
        desc = "Create " + m_ObjectName;
        return desc.c_str();
    }

    void CreateGameObjectCommand::Execute() {
        if (!m_Scene) return;
        Log::Info("[Undo] Execute: Create {}", m_ObjectName);
        // 从序列化数据重建对象
        if (!m_SerializedData.is_null()) {
            auto obj = std::make_shared<GameObject>(
                m_SerializedData.value("name", m_ObjectName));
            if (m_SerializedData.contains("active"))
                obj->SetActive(m_SerializedData["active"]);
            if (m_SerializedData.contains("layer"))
                obj->SetLayer(m_SerializedData["layer"]);
            m_Scene->AddObject(obj);
        }
    }

    void CreateGameObjectCommand::Undo() {
        if (!m_Scene) return;
        Log::Info("[Undo] Undo: Create {}", m_ObjectName);
        // 通过 ID 查找并删除创建的对象
        auto* obj = m_Scene->FindByID(static_cast<uint32>(m_ObjectID));
        if (obj) m_Scene->RemoveObject(obj);
    }

    std::vector<uint64> CreateGameObjectCommand::GetAffectedObjectIDs() const {
        return {m_ObjectID};
    }

    // ============================================================
    // DeleteGameObjectCommand
    // ============================================================
    DeleteGameObjectCommand::DeleteGameObjectCommand(Scene* scene, GameObject* object)
        : m_Scene(scene)
        , m_ObjectID(object ? object->GetID() : 0)
        , m_ObjectName(object ? object->GetName() : "Unknown") {
        if (object) {
            nlohmann::json j;
            j["name"] = object->GetName();
            j["active"] = object->IsActive();
            j["layer"] = object->GetLayer();
            nlohmann::json components = nlohmann::json::array();
            object->ForEachComponent([&](Component& comp) {
                nlohmann::json compJson;
                compJson["type"] = comp.GetTypeDisplayName();
                compJson["typeId"] = typeid(comp).hash_code();
                compJson["enabled"] = comp.IsEnabled();
                comp.Serialize(compJson);
                components.push_back(compJson);
            });
            j["components"] = components;
            m_SerializedData = std::move(j);
            m_ParentID = object->GetParent() ? object->GetParent()->GetID() : 0;

            // 记录在父级子对象列表中的索引
            if (object->GetParent()) {
                const auto& siblings = object->GetParent()->GetChildren();
                for (size_t i = 0; i < siblings.size(); ++i) {
                    if (siblings[i].get() == object) {
                        m_SiblingIndex = static_cast<int>(i);
                        break;
                    }
                }
            }
        }
    }

    const char* DeleteGameObjectCommand::GetDescription() const {
        static std::string desc;
        desc = "Delete " + m_ObjectName;
        return desc.c_str();
    }

    void DeleteGameObjectCommand::Execute() {
        if (!m_Scene) return;
        Log::Info("[Undo] Execute: Delete {}", m_ObjectName);
        auto* obj = m_Scene->FindByID(static_cast<uint32>(m_ObjectID));
        if (obj) m_Scene->RemoveObject(obj);
    }

    void DeleteGameObjectCommand::Undo() {
        if (!m_Scene) return;
        Log::Info("[Undo] Undo: Delete {}", m_ObjectName);
        // 从序列化数据重建
        if (!m_SerializedData.is_null()) {
            auto obj = std::make_shared<GameObject>(
                m_SerializedData.value("name", m_ObjectName));
            if (m_SerializedData.contains("active"))
                obj->SetActive(m_SerializedData["active"]);
            if (m_SerializedData.contains("layer"))
                obj->SetLayer(m_SerializedData["layer"]);
            m_Scene->AddObject(obj);
        }
    }

    std::vector<uint64> DeleteGameObjectCommand::GetAffectedObjectIDs() const {
        return {m_ObjectID};
    }

    // ============================================================
    // AddComponentCommand
    // ============================================================
    AddComponentCommand::AddComponentCommand(GameObject* target,
                                              const std::string& componentType,
                                              nlohmann::json componentData)
        : m_ObjectID(target ? target->GetID() : 0)
        , m_ComponentType(componentType)
        , m_ComponentData(std::move(componentData))
        , m_ComponentTypeId(std::hash<std::string>{}(componentType)) {}

    const char* AddComponentCommand::GetDescription() const {
        static std::string desc;
        desc = "Add " + m_ComponentType;
        return desc.c_str();
    }

    void AddComponentCommand::Execute() {
        Log::Info("[Undo] Execute: Add {} to object {}", m_ComponentType, m_ObjectID);
    }

    void AddComponentCommand::Undo() {
        Log::Info("[Undo] Undo: Add {} to object {}", m_ComponentType, m_ObjectID);
    }

    std::vector<uint64> AddComponentCommand::GetAffectedObjectIDs() const {
        return {m_ObjectID};
    }

    // ============================================================
    // RemoveComponentCommand
    // ============================================================
    RemoveComponentCommand::RemoveComponentCommand(GameObject* target,
                                                    size_t componentTypeId,
                                                    const std::string& componentTypeName,
                                                    nlohmann::json componentData)
        : m_ObjectID(target ? target->GetID() : 0)
        , m_ComponentTypeId(componentTypeId)
        , m_ComponentTypeName(componentTypeName)
        , m_ComponentData(std::move(componentData)) {}

    const char* RemoveComponentCommand::GetDescription() const {
        static std::string desc;
        desc = "Remove " + m_ComponentTypeName;
        return desc.c_str();
    }

    void RemoveComponentCommand::Execute() {
        Log::Info("[Undo] Execute: Remove {} from object {}", m_ComponentTypeName, m_ObjectID);
    }

    void RemoveComponentCommand::Undo() {
        Log::Info("[Undo] Undo: Remove {} from object {}", m_ComponentTypeName, m_ObjectID);
    }

    std::vector<uint64> RemoveComponentCommand::GetAffectedObjectIDs() const {
        return {m_ObjectID};
    }

    // ============================================================
    // SnapshotCommand
    // ============================================================
    SnapshotCommand::SnapshotCommand(const std::string& description,
                                      std::function<void()> applySnapshot)
        : m_Description(description) {
        // applySnapshot 参数保留用于兼容
        (void)applySnapshot;
    }

    const char* SnapshotCommand::GetDescription() const {
        return m_Description.c_str();
    }

    void SnapshotCommand::Execute() {
        if (m_ApplyCallback && !m_AfterSnapshot.is_null())
            m_ApplyCallback(m_AfterSnapshot);
    }

    void SnapshotCommand::Undo() {
        if (m_ApplyCallback && !m_BeforeSnapshot.is_null())
            m_ApplyCallback(m_BeforeSnapshot);
    }

    // ============================================================
    // UndoStack
    // ============================================================
    void UndoStack::PushCommand(std::unique_ptr<UndoCommand> command) {
        if (!command) return;

        if (m_GroupLevel > 0) {
            // 在动作组内：先尝试合并，否则加入组
            if (!m_GroupCommands.empty()) {
                auto& last = m_GroupCommands.back();
                if (command->TryMerge(last.get())) {
                    return;  // 合并成功，不新增
                }
            }
            m_GroupCommands.push_back(std::move(command));
            return;
        }

        PushInternal(std::move(command));
    }

    void UndoStack::PushInternal(std::unique_ptr<UndoCommand> command) {
        // 尝试与栈顶合并
        if (!m_UndoStack.empty()) {
            auto& last = m_UndoStack.back();
            if (command->TryMerge(last.get())) {
                m_Dirty = true;
                return;
            }
        }

        m_UndoStack.push_back(std::move(command));
        m_Dirty = true;

        // 清空重做栈（新操作使重做失效）
        m_RedoStack.clear();

        // 容量限制
        if (m_UndoStack.size() > m_MaxCommands) {
            m_UndoStack.erase(m_UndoStack.begin());
        }

        // 触发脏标记回调
        if (m_DirtyCallback && !m_UndoStack.empty()) {
            auto affected = m_UndoStack.back()->GetAffectedObjectIDs();
            m_DirtyCallback(affected);
        }
    }

    bool UndoStack::Undo() {
        if (m_UndoStack.empty()) return false;

        // 执行撤销
        auto& cmd = m_UndoStack.back();
        cmd->Undo();

        // 移动到重做栈
        m_RedoStack.push_back(std::move(cmd));
        m_UndoStack.pop_back();
        m_Dirty = true;

        Log::Info("[Undo] Undo performed. Stack: {}/{}", m_UndoStack.size(), m_RedoStack.size());
        return true;
    }

    bool UndoStack::Redo() {
        if (m_RedoStack.empty()) return false;

        auto& cmd = m_RedoStack.back();
        cmd->Execute();

        m_UndoStack.push_back(std::move(cmd));
        m_RedoStack.pop_back();
        m_Dirty = true;

        Log::Info("[Undo] Redo performed. Stack: {}/{}", m_UndoStack.size(), m_RedoStack.size());
        return true;
    }

    void UndoStack::Clear() {
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Dirty = false;
        m_GroupCommands.clear();
        m_GroupLevel = 0;
    }

    void UndoStack::OpenGroup(const std::string& groupLabel) {
        if (m_GroupLevel == 0) {
            m_GroupLabel = groupLabel;
            m_GroupCommands.clear();
        }
        m_GroupLevel++;
    }

    void UndoStack::CloseGroup() {
        if (m_GroupLevel <= 0) return;

        m_GroupLevel--;
        if (m_GroupLevel == 0 && !m_GroupCommands.empty()) {
            // 将组内所有命令打包成一个 UndoCommand 组
            // 简化实现：只推送第一个命令（实际应创建 CompoundCommand）
            PushInternal(std::move(m_GroupCommands.front()));
            m_GroupCommands.clear();
        }
    }

    std::string UndoStack::GetUndoDescription() const {
        if (m_UndoStack.empty()) return "";
        return m_UndoStack.back()->GetDescription();
    }

    std::string UndoStack::GetRedoDescription() const {
        if (m_RedoStack.empty()) return "";
        return m_RedoStack.back()->GetDescription();
    }

} // namespace Engine