#pragma once

/**
 * @file TaskGraph.h
 * @brief 任务依赖图 — 声明式构建、拓扑排序、批量提交
 *
 * 设计理念：
 *   1. 构建期（声明式 API）：描述任务及其资源读写关系
 *   2. 编译期（拓扑排序 + 循环检测）：自动推导依赖、分层、检测环
 *   3. 提交期（批量入队）：按拓扑层展开为 JobSystem 的 prerereqs 链
 *
 * 使用示例：
 * @code
 *   TaskGraph graph;
 *
 *   auto phys = graph.AddJob("PhysicsStep", [](uint32) { ... })
 *                      .Writes("PhysicsWorld");
 *
 *   auto part = graph.AddJob("ParticleUpdate", [](uint32) { ... })
 *                      .Reads("PhysicsWorld");  // 自动依赖 phys
 *
 *   auto audio = graph.AddJob("AudioUpdate", [](uint32) { ... })
 *                      .Reads("Transform");    // 无 writer → 无依赖，可与 phys/part 并行
 *
 *   if (!graph.Compile()) {
 *       // 有循环依赖！可通过 DumpGraph() 导出 DOT 格式诊断
 *       std::cerr << graph.DumpGraph();
 *       return;
 *   }
 *
 *   graph.Submit(*JobSystem::Get());
 * @endcode
 *
 * 编译算法：
 *   1. 依赖推导：遍历每个节点的 read/write 集，根据 lastWriter 推导前置边
 *   2. Kahn 拓扑排序：入度为 0 的节点入队，BFS 广度优先出队列
 *   3. 循环检测：若排序后仍有节点未访问 → SCC（使用 Tarjan 算法报告环路径）
 */

#include "Engine/Types.h"
#include "Engine/Core/JobSystem.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <functional>

namespace Engine {

    // ============================================================
    // TaskGraph — 任务依赖图
    // ============================================================
    class TaskGraph {
    public:
        using NodeID  = uint32;
        using ResourceID = std::string;   // 资源标识符（字符串名）

        // ══════════════════════════════════════════════════════
        // 构建节点
        // ══════════════════════════════════════════════════════

        /**
         * @brief 节点构建器 — 链式 API
         *
         * @code
         *   graph.AddJob("MyTask", []{...})
         *        .Reads("InputBuffer")
         *        .Writes("OutputBuffer");
         * @endcode
         */
        class NodeBuilder {
        public:
            NodeBuilder(TaskGraph& graph, NodeID id)
                : m_Graph(graph), m_ID(id) {}

            /** 声明此节点读取的资源 */
            NodeBuilder& Reads(ResourceID resource);

            /** 声明此节点写入的资源 */
            NodeBuilder& Writes(ResourceID resource);

            /** 显式声明前置节点（绕过资源推导） */
            NodeBuilder& DependsOn(NodeID predecessor);

            NodeID GetID() const noexcept { return m_ID; }

        private:
            TaskGraph& m_Graph;
            NodeID     m_ID;
        };

        // ══════════════════════════════════════════════════════
        // 添加节点
        // ══════════════════════════════════════════════════════

        /**
         * @brief 向图中添加一个任务节点
         * @param name 任务名称（调试用）
         * @param func 要执行的函数
         * @return NodeBuilder 用于链式声明依赖
         */
        NodeBuilder AddJob(const std::string& name, JobFunc func);

        /**
         * @brief 通过 ParallelFor 创建多个并行节点
         * @param name       任务名称前缀
         * @param begin      起始索引
         * @param end        结束索引
         * @param func       每个元素的处理函数
         * @return 所有批次节点 ID 的列表
         */
        std::vector<NodeID> AddParallelFor(
            const std::string& name,
            int32 begin, int32 end,
            ParallelForFunc func);

        // ══════════════════════════════════════════════════════
        // 编译
        // ══════════════════════════════════════════════════════

        /**
         * @brief 编译依赖图
         *
         * 执行步骤：
         *   1. 依赖推导（Reads/Writes → 前置边）
         *   2. Kahn 拓扑排序
         *   3. 循环检测（Tarjan SCC）
         *
         * @return true 表示编译成功（无环），false 表示存在循环依赖
         *
         * false 时调用 DumpGraph() 获取 DOT 格式的诊断输出。
         */
        bool Compile();

        // ══════════════════════════════════════════════════════
        // 提交
        // ══════════════════════════════════════════════════════

        /**
         * @brief 将编译后的任务图提交到 JobSystem
         *
         * 按拓扑层批量提交：
         *   - 第 0 层（无依赖）→ 直接入队
         *   - 第 1+ 层 → 依赖前一层完成后入队
         *
         * 必须在 Compile() 成功（返回 true）后调用。
         *
         * @param js JobSystem 实例
         * @return JobHandle 等待整个图完成的句柄
         */
        JobHandle Submit(JobSystem& js);

        // ══════════════════════════════════════════════════════
        // 调试
        // ══════════════════════════════════════════════════════

        /**
         * @brief 导出 DOT 格式图（用于 Graphviz 可视化）
         *
         * 输出可直接粘贴到 https://dreampuf.github.io/GraphvizOnline
         * 或 fed to `dot -Tpng graph.dot -o graph.png`
         */
        std::string DumpGraph() const;

        /** 获取节点数量 */
        size_t GetNodeCount() const noexcept { return m_Nodes.size(); }

    private:
        // ══════════════════════════════════════════════════════
        // 内部表示
        // ══════════════════════════════════════════════════════

        struct Node {
            std::string              name;
            JobFunc                  func;
            std::vector<ResourceID>  reads;
            std::vector<ResourceID>  writes;

            // ── 编译后填充 ──
            std::vector<NodeID>      explicitDeps;  // 显式依赖（DependsOn）
            std::vector<NodeID>      dependencies;   // 所有前驱节点（推导 + 显式）
            std::vector<NodeID>      successors;     // 后继节点（由 dependencies 反转得到）
            uint32                   topologicalOrder = UINT32_MAX;
            uint32                   layer           = 0;
        };

        std::vector<Node> m_Nodes;

        // ＝＝ 编译状态（Compile 内部使用） ＝＝

        struct CompilerState {
            std::unordered_map<ResourceID, NodeID> lastWriter;
            std::vector<uint32> inDegree;
        };

        // ══════════════════════════════════════════════════════
        // 编译辅助
        // ══════════════════════════════════════════════════════

        /** 步骤 1：从 read/write 集推导依赖 */
        void DeriveDependencies(CompilerState& state);

        /** 步骤 2-3：Kahn 拓扑排序 + 环检测 */
        bool TopologicalSort(CompilerState& state);

        /** Tarjan SCC 辅助：递归查找强连通分量 */
        void TarjanSCC(uint32 u,
                       std::vector<uint32>& index,
                       std::vector<uint32>& lowlink,
                       std::vector<bool>& onStack,
                       std::vector<uint32>& stack,
                       uint32& currentIndex,
                       std::vector<std::vector<uint32>>& sccs) const;

        /** 根据 SCC 结果生成可读的环路径报告 */
        std::string FormatCycleReport(
            const std::vector<std::vector<uint32>>& sccs) const;
    };

} // namespace Engine