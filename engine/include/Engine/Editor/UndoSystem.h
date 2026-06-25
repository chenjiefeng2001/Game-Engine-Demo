#pragma once

/**
 * @file UndoSystem.h
 * @brief 工业级撤销/重做系统 — 命令模式 + 动作合并 + 状态快照
 *
 * 架构设计（参考 Unity/Unreal 的 Undo 系统）：
 *
 *   1. 命令模式 (Command Pattern)
 *      - 每个操作封装为一个 UndoCommand 对象
 *      - 包含 Execute() 正向执行 + Undo() 逆向恢复
 *
 *   2. 动作合并 (Command Grouping)
 *      - OpenGroup(label) / CloseGroup() 作用域
 *      - 滑块拖拽等连续微变动自动合并为一个组
 *
 *   3. 快照与增量 (Snapshot vs Delta)
 *      - 简单属性：记录属性路径 + 旧值/新值（增量）
 *      - 复杂修改：序列化对象完整快照，撤销时反序列化覆盖
 *
 *   4. 脏数据追踪 (Dirty Marking)
 *      - 撤销栈变化后自动标记受影响的场景/对象为 Dirty
 *
 *   5. 安全销毁重建
 *      - 被删除对象记录 GUID 和序列化数据
 *      - 撤销时按原 GUID 重建，引用关系不丢失
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stack>
#include <nlohmann/json.hpp>

namespace Engine {

    class GameObject;

    // ============================================================
    // 1. 基础命令接口
    // ============================================================
    class UndoCommand {
    public:
        virtual ~UndoCommand() = default;

        /** 命令描述（显示在 Edit → Undo 菜单中） */
        virtual const char* GetDescription() const = 0;

        /** 正向执行 */
        virtual void Execute() = 0;

        /** 逆向撤销 */
        virtual void Undo() = 0;

        /** 尝试与上一个命令合并（如连续拖拽滑块） */
        virtual bool TryMerge(const UndoCommand* previous) { return false; }

        /** 获取此命令影响到的对象 GUID 列表（用于脏标记） */
        virtual std::vector<uint64> GetAffectedObjectIDs() const { return {}; }
    };

    // ============================================================
    // 2. 属性修改命令（记录旧值/新值增量变化）
    // ============================================================
    class PropertyChangeCommand : public UndoCommand {
    public:
        PropertyChangeCommand(GameObject* target,
                              std::string propertyName,
                              nlohmann::json oldValue,
                              nlohmann::json newValue);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;
        bool TryMerge(const UndoCommand* previous) override;
        std::vector<uint64> GetAffectedObjectIDs() const override;

    private:
        uint64 m_ObjectID;
        std::string m_ObjectName;
        std::string m_PropertyName;
        nlohmann::json m_OldValue;
        nlohmann::json m_NewValue;

        // 查找对象指针（通过在场景中搜索 ID）
        GameObject* FindTarget() const;
    };

    // ============================================================
    // 3. 创建/删除对象命令
    // ============================================================
    class CreateGameObjectCommand : public UndoCommand {
    public:
        CreateGameObjectCommand(Scene* scene,
                                std::shared_ptr<GameObject> object,
                                GameObject* parent = nullptr);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;
        std::vector<uint64> GetAffectedObjectIDs() const override;

    private:
        Scene* m_Scene;
        uint64 m_ObjectID;
        std::string m_ObjectName;
        nlohmann::json m_SerializedData;
        uint64 m_ParentID = 0;
    };

    class DeleteGameObjectCommand : public UndoCommand {
    public:
        DeleteGameObjectCommand(Scene* scene, GameObject* object);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;
        std::vector<uint64> GetAffectedObjectIDs() const override;

    private:
        Scene* m_Scene;
        uint64 m_ObjectID;
        std::string m_ObjectName;
        nlohmann::json m_SerializedData;
        uint64 m_ParentID = 0;
        int m_SiblingIndex = -1;
    };

    // ============================================================
    // 4. 组件修改命令（增加/删除/修改组件）
    // ============================================================
    class AddComponentCommand : public UndoCommand {
    public:
        AddComponentCommand(GameObject* target,
                            const std::string& componentType,
                            nlohmann::json componentData);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;
        std::vector<uint64> GetAffectedObjectIDs() const override;

    private:
        uint64 m_ObjectID;
        std::string m_ComponentType;
        nlohmann::json m_ComponentData;
        size_t m_ComponentTypeId = 0;
    };

    class RemoveComponentCommand : public UndoCommand {
    public:
        RemoveComponentCommand(GameObject* target,
                               size_t componentTypeId,
                               const std::string& componentTypeName,
                               nlohmann::json componentData);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;
        std::vector<uint64> GetAffectedObjectIDs() const override;

    private:
        uint64 m_ObjectID;
        size_t m_ComponentTypeId;
        std::string m_ComponentTypeName;
        nlohmann::json m_ComponentData;
    };

    // ============================================================
    // 5. 完整对象快照命令（用于复杂修改）
    // ============================================================
    class SnapshotCommand : public UndoCommand {
    public:
        SnapshotCommand(const std::string& description,
                        std::function<void()> applySnapshot);

        const char* GetDescription() const override;
        void Execute() override;
        void Undo() override;

        void SetBeforeSnapshot(nlohmann::json before) { m_BeforeSnapshot = std::move(before); }
        void SetAfterSnapshot(nlohmann::json after) { m_AfterSnapshot = std::move(after); }

        /** 设置应用到场景的回调 */
        void SetApplyCallback(std::function<void(const nlohmann::json&)> cb) {
            m_ApplyCallback = std::move(cb);
        }

    private:
        std::string m_Description;
        nlohmann::json m_BeforeSnapshot;
        nlohmann::json m_AfterSnapshot;
        std::function<void(const nlohmann::json&)> m_ApplyCallback;
    };

    // ============================================================
    // 6. 撤销栈
    // ============================================================
    class UndoStack {
    public:
        UndoStack() = default;
        ~UndoStack() = default;

        UndoStack(const UndoStack&) = delete;
        UndoStack& operator=(const UndoStack&) = delete;

        // ── 栈操作 ──
        /** 推送一个命令到撤销栈 */
        void PushCommand(std::unique_ptr<UndoCommand> command);

        /** 撤销最近一次操作 */
        bool Undo();

        /** 重做最近一次撤销 */
        bool Redo();

        /** 清除所有记录 */
        void Clear();

        // ── 动作组（连续操作为一组） ──
        /** 开始一个动作组（例如：滑块拖拽的连续值变更） */
        void OpenGroup(const std::string& groupLabel);

        /** 结束当前动作组 */
        void CloseGroup();

        /** 当前是否在动作组内 */
        bool IsInGroup() const { return m_GroupLevel > 0; }

        // ── 查询 ──
        size_t GetUndoCount() const { return m_UndoStack.size(); }
        size_t GetRedoCount() const { return m_RedoStack.size(); }

        /** 获取下一次撤销操作的描述（用于菜单显示） */
        std::string GetUndoDescription() const;

        /** 获取下一次重做操作的描述（用于菜单显示） */
        std::string GetRedoDescription() const;

        // ── 脏标记 ──
        using DirtyCallback = std::function<void(const std::vector<uint64>& affectedIDs)>;
        void SetDirtyCallback(DirtyCallback cb) { m_DirtyCallback = std::move(cb); }

        /** 栈是否已被修改 */
        bool IsDirty() const { return m_Dirty; }
        void SetClean() { m_Dirty = false; }

        // ── 容量控制 ──
        void SetMaxCommands(size_t max) { m_MaxCommands = max; }
        size_t GetMaxCommands() const { return m_MaxCommands; }

    private:
        // 支持合并的命令栈
        void PushInternal(std::unique_ptr<UndoCommand> command);

        // 主撤销栈
        std::vector<std::unique_ptr<UndoCommand>> m_UndoStack;
        // 重做栈
        std::vector<std::unique_ptr<UndoCommand>> m_RedoStack;

        // 容量限制
        size_t m_MaxCommands = 1024;

        // 脏标记
        bool m_Dirty = false;
        DirtyCallback m_DirtyCallback;

        // 动作组
        int m_GroupLevel = 0;
        std::string m_GroupLabel;
        std::vector<std::unique_ptr<UndoCommand>> m_GroupCommands;
    };

    // ============================================================
    // 7. 撤销管理器（全局单例）
    // ============================================================
    class UndoManager {
    public:
        static UndoManager& Get();

        UndoManager(const UndoManager&) = delete;
        UndoManager& operator=(const UndoManager&) = delete;

        // ── 全局撤销栈 ──
        UndoStack& GetGlobalStack() { return m_GlobalStack; }
        const UndoStack& GetGlobalStack() const { return m_GlobalStack; }

        // ── 场景级撤销栈 ──
        UndoStack& GetSceneStack(const std::string& sceneName);
        void RemoveSceneStack(const std::string& sceneName);

        // ── 便捷方法 ──
        static void RecordPropertyChange(GameObject* target,
                                          const std::string& propName,
                                          const nlohmann::json& oldValue,
                                          const nlohmann::json& newValue);

        static void RecordCreateObject(Scene* scene,
                                        std::shared_ptr<GameObject> object,
                                        GameObject* parent = nullptr);

        static void RecordDeleteObject(Scene* scene, GameObject* object);

        static void RecordAddComponent(GameObject* target,
                                        const std::string& componentType,
                                        const nlohmann::json& componentData);

        static void RecordRemoveComponent(GameObject* target,
                                           size_t componentTypeId,
                                           const std::string& componentTypeName,
                                           const nlohmann::json& componentData);

        static void OpenGroup(const std::string& label);
        static void CloseGroup();
        static bool IsInGroup();

        // ── 当前的活跃场景栈名称 ──
        void SetActiveScene(const std::string& sceneName) { m_ActiveScene = sceneName; }
        const std::string& GetActiveScene() const { return m_ActiveScene; }

    private:
        UndoManager() = default;
        ~UndoManager() = default;

        UndoStack m_GlobalStack;
        std::unordered_map<std::string, UndoStack> m_SceneStacks;
        std::string m_ActiveScene;
    };

} // namespace Engine

// 便捷宏
#define RECORD_PROPERTY_CHANGE(obj, prop, oldVal, newVal) \
    Engine::UndoManager::RecordPropertyChange(obj, prop, oldVal, newVal)

#define RECORD_UNDO_GROUP(label) \
    Engine::UndoManager::OpenGroup(label)

#define RECORD_UNDO_GROUP_END() \
    Engine::UndoManager::CloseGroup()