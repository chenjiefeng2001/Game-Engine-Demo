#pragma once

/**
 * @file VFXUndoManager.h
 * @brief VFX 撤销/重做管理器 — Command 模式实现
 *
 * 设计原则：
 *   - 所有修改 VFXGraph 的操作必须通过 UndoManager::Execute() 包装
 *   - 每个命令在执行前保存 JSON 快照，执行后保存快照
 *   - 撤销 = 从快照恢复，重做 = 重新执行
 *   - 支持组合操作（GroupAction）：拖拽移动、批量删除等
 *
 * 使用方式：
 *   auto& undo = VFXUndoManager::Get();
 *   undo.BeginGroup("Add Block");
 *   graph.AddBlock(...);
 *   undo.EndGroup();
 */

#include "VFXGraphCore.h"
#include <string>
#include <vector>
#include <stack>
#include <functional>

namespace Engine {
namespace VFX {

    // ============================================================
    // 撤销命令
    // ============================================================
    struct VFXUndoCommand {
        enum Type : uint8 {
            AddBlock,
            RemoveBlock,
            MoveBlock,
            AddLink,
            RemoveLink,
            EditProperty,
            GroupAction
        };

        Type type;
        std::string jsonBefore;   // 操作前 VFXGraph 完整 JSON 快照
        std::string jsonAfter;    // 操作后 VFXGraph 完整 JSON 快照
        uint32 blockId = 0;
        uint32 linkId = 0;
        std::string description;
    };

    // ============================================================
    // 撤销管理器（单例）
    // ============================================================
    class VFXUndoManager {
    public:
        static VFXUndoManager& Get() {
            static VFXUndoManager instance;
            return instance;
        }

        /// 记录操作前快照（在 Execute 前调用）
        void SnapshotBefore(VFXGraph* graph, VFXUndoCommand::Type type,
                            const std::string& desc = "") {
            if (!graph || m_GroupDepth > 0) return;
            VFXUndoCommand cmd;
            cmd.type = type;
            cmd.description = desc;
            cmd.jsonBefore = graph->ToJSON();
            m_PendingCmd = cmd;
        }

        /// 记录操作后快照并提交（在 Execute 后调用）
        void SnapshotAfter(VFXGraph* graph, uint32 blockId = 0, uint32 linkId = 0) {
            if (!graph || m_GroupDepth > 0) return;
            if (m_PendingCmd.jsonBefore.empty()) return;

            m_PendingCmd.jsonAfter = graph->ToJSON();
            m_PendingCmd.blockId = blockId;
            m_PendingCmd.linkId = linkId;

            // 如果前后没有变化，不提交
            if (m_PendingCmd.jsonBefore == m_PendingCmd.jsonAfter) return;

            // 推入撤销栈，清空重做栈
            m_UndoStack.push_back(std::move(m_PendingCmd));
            m_PendingCmd = {};

            // 清空重做栈（新操作使所有重做失效）
            m_RedoStack.clear();

            // 限制栈大小
            constexpr size_t kMaxUndo = 100;
            if (m_UndoStack.size() > kMaxUndo) {
                m_UndoStack.erase(m_UndoStack.begin());
            }
        }

        /// 开始组合操作（如拖拽移动）
        void BeginGroup(const std::string& desc = "Group Action") {
            if (m_GroupDepth == 0) {
                m_GroupCmd = VFXUndoCommand{};
                m_GroupCmd.type = VFXUndoCommand::GroupAction;
                m_GroupCmd.description = desc;
            }
            m_GroupDepth++;
        }

        /// 结束组合操作
        void EndGroup(VFXGraph* graph) {
            if (m_GroupDepth <= 0) return;
            m_GroupDepth--;
            if (m_GroupDepth == 0 && graph) {
                m_GroupCmd.jsonAfter = graph->ToJSON();
                if (!m_GroupCmd.jsonBefore.empty() &&
                    m_GroupCmd.jsonBefore != m_GroupCmd.jsonAfter) {
                    m_UndoStack.push_back(std::move(m_GroupCmd));
                    m_RedoStack.clear();
                }
                m_GroupCmd = {};
            }
        }

        /// 撤销
        bool Undo(VFXGraph* graph) {
            if (!graph || m_UndoStack.empty()) return false;

            VFXUndoCommand cmd = std::move(m_UndoStack.back());
            m_UndoStack.pop_back();

            // 保存当前状态到重做栈
            cmd.jsonAfter = graph->ToJSON();
            m_RedoStack.push_back(std::move(cmd));

            // 从 jsonBefore 恢复
            return graph->FromJSON(m_RedoStack.back().jsonBefore);
        }

        /// 重做
        bool Redo(VFXGraph* graph) {
            if (!graph || m_RedoStack.empty()) return false;

            VFXUndoCommand cmd = std::move(m_RedoStack.back());
            m_RedoStack.pop_back();

            // 保存当前状态到撤销栈
            cmd.jsonBefore = graph->ToJSON();
            m_UndoStack.push_back(std::move(cmd));

            // 从 jsonAfter 恢复
            return graph->FromJSON(m_UndoStack.back().jsonAfter);
        }

        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }

        const std::string& GetUndoDescription() const {
            static std::string empty;
            return m_UndoStack.empty() ? empty : m_UndoStack.back().description;
        }
        const std::string& GetRedoDescription() const {
            static std::string empty;
            return m_RedoStack.empty() ? empty : m_RedoStack.back().description;
        }

        void Clear() {
            m_UndoStack.clear();
            m_RedoStack.clear();
            m_PendingCmd = {};
            m_GroupCmd = {};
            m_GroupDepth = 0;
        }

    private:
        VFXUndoManager() = default;

        std::vector<VFXUndoCommand> m_UndoStack;
        std::vector<VFXUndoCommand> m_RedoStack;
        VFXUndoCommand m_PendingCmd;
        VFXUndoCommand m_GroupCmd;
        int m_GroupDepth = 0;
    };

    // ============================================================
    // 便捷 RAII 包装器：自动调用 SnapshotBefore/SnapshotAfter
    // ============================================================
    class ScopedVFXUndo {
    public:
        ScopedVFXUndo(VFXGraph* graph, VFXUndoCommand::Type type,
                      const std::string& desc = "")
            : m_Graph(graph) {
            auto& mgr = VFXUndoManager::Get();
            mgr.SnapshotBefore(graph, type, desc);
        }
        ~ScopedVFXUndo() {
            if (m_Graph) {
                auto& mgr = VFXUndoManager::Get();
                mgr.SnapshotAfter(m_Graph);
            }
        }
        ScopedVFXUndo(const ScopedVFXUndo&) = delete;
        ScopedVFXUndo& operator=(const ScopedVFXUndo&) = delete;

    private:
        VFXGraph* m_Graph;
    };

}} // namespace Engine::VFX