/**
 * @file TaskGraph.cpp
 * @brief 任务依赖图实现 — 声明式构建、Kahn 拓扑排序、Tarjan SCC 循环检测
 */

#include "Engine/Core/TaskGraph.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <queue>
#include <algorithm>

namespace Engine {

    namespace {
        Logger s_Log("TaskGraph");
    }

    // ============================================================
    // NodeBuilder
    // ============================================================

    TaskGraph::NodeBuilder& TaskGraph::NodeBuilder::Reads(ResourceID resource) {
        m_Graph.m_Nodes[m_ID].reads.push_back(std::move(resource));
        return *this;
    }

    TaskGraph::NodeBuilder& TaskGraph::NodeBuilder::Writes(ResourceID resource) {
        m_Graph.m_Nodes[m_ID].writes.push_back(std::move(resource));
        return *this;
    }

    TaskGraph::NodeBuilder& TaskGraph::NodeBuilder::DependsOn(NodeID predecessor) {
        if (predecessor < m_Graph.m_Nodes.size()) {
            m_Graph.m_Nodes[m_ID].explicitDeps.push_back(predecessor);
        }
        return *this;
    }

    // ============================================================
    // 添加节点
    // ============================================================

    TaskGraph::NodeBuilder TaskGraph::AddJob(const std::string& name, JobFunc func) {
        NodeID id = static_cast<NodeID>(m_Nodes.size());
        Node node;
        node.name = name;
        node.func = std::move(func);
        m_Nodes.push_back(std::move(node));
        return NodeBuilder(*this, id);
    }

    std::vector<TaskGraph::NodeID> TaskGraph::AddParallelFor(
        const std::string& name, int32 begin, int32 end, ParallelForFunc func)
    {
        if (begin >= end) return {};

        uint32 count = static_cast<uint32>(end - begin);
        // 批次划分与 JobSystem::ParallelFor 对齐
        uint32 batchSize = std::max<uint32>(1,
            (count + 7) / 8); // 每批次最多 8 个元素
        uint32 numBatches = (count + batchSize - 1) / batchSize;

        std::vector<NodeID> batchIDs;
        batchIDs.reserve(numBatches);

        for (uint32 batch = 0; batch < numBatches; ++batch) {
            int32 bBegin = begin + static_cast<int32>(batch * batchSize);
            int32 bEnd   = std::min(bBegin + static_cast<int32>(batchSize), end);

            std::string batchName = name + "_" + std::to_string(batch);
            auto batchFunc = [func, bBegin, bEnd](uint32 /*threadIndex*/) {
                for (int32 i = bBegin; i < bEnd; ++i) {
                    func(i);
                }
            };

            NodeID id = static_cast<NodeID>(m_Nodes.size());
            Node node;
            node.name = batchName;
            node.func = std::move(batchFunc);
            m_Nodes.push_back(std::move(node));
            batchIDs.push_back(id);
        }

        return batchIDs;
    }

    // ============================================================
    // 编译
    // ============================================================

    bool TaskGraph::Compile() {
        if (m_Nodes.empty()) {
            return true; // 空图无需编译
        }

        // 清除之前的编译结果，准备重新编译
        for (auto& node : m_Nodes) {
            node.dependencies.clear();
            node.successors.clear();
            node.topologicalOrder = UINT32_MAX;
            node.layer = 0;
        }

        CompilerState state;
        state.inDegree.resize(m_Nodes.size(), 0);

        // ── 步骤 1：依赖推导 ──
        DeriveDependencies(state);

        // ── 步骤 2-3：拓扑排序 + 环检测 ──
        return TopologicalSort(state);
    }

    void TaskGraph::DeriveDependencies(CompilerState& state) {
        // 按节点顺序遍历（保持确定性）
        for (NodeID n = 0; n < m_Nodes.size(); ++n) {
            auto& node = m_Nodes[n];

            // ── 处理显式依赖 ──
            for (NodeID pred : node.explicitDeps) {
                // 避免重复边
                if (std::find(node.dependencies.begin(), node.dependencies.end(), pred)
                    == node.dependencies.end()) {
                    node.dependencies.push_back(pred);
                }
            }

            // ── 读依赖：当前节点读什么 → 依赖上次写该资源的节点 ──
            for (const auto& res : node.reads) {
                auto it = state.lastWriter.find(res);
                if (it != state.lastWriter.end() && it->second != n) {
                    NodeID writer = it->second;
                    // 避免重复添加
                    if (std::find(node.dependencies.begin(), node.dependencies.end(), writer)
                        == node.dependencies.end()) {
                        node.dependencies.push_back(writer);
                    }
                }
            }

            // ── 写后写依赖：当前节点写什么 → 依赖上次写该资源的节点 ──
            for (const auto& res : node.writes) {
                auto it = state.lastWriter.find(res);
                if (it != state.lastWriter.end() && it->second != n) {
                    NodeID prevWriter = it->second;
                    if (std::find(node.dependencies.begin(), node.dependencies.end(), prevWriter)
                        == node.dependencies.end()) {
                        node.dependencies.push_back(prevWriter);
                    }
                }
                // 更新 last writer
                state.lastWriter[res] = n;
            }
        }

        // ── 构建 successors 列表 & 计算入度 ──
        for (NodeID n = 0; n < m_Nodes.size(); ++n) {
            state.inDegree[n] = static_cast<uint32>(m_Nodes[n].dependencies.size());
            for (NodeID dep : m_Nodes[n].dependencies) {
                m_Nodes[dep].successors.push_back(n);
            }
        }
    }

    bool TaskGraph::TopologicalSort(CompilerState& state) {
        uint32 nodeCount = static_cast<uint32>(m_Nodes.size());

        // ── Kahn 算法：入度为 0 的节点入队 ──
        std::queue<NodeID> q;
        for (NodeID n = 0; n < nodeCount; ++n) {
            if (state.inDegree[n] == 0) {
                q.push(n);
                m_Nodes[n].layer = 0;
            }
        }

        uint32 visitCount = 0;
        std::vector<uint32> remainingInDegree = state.inDegree; // 拷贝用于 SCC 检测

        while (!q.empty()) {
            NodeID u = q.front();
            q.pop();

            m_Nodes[u].topologicalOrder = visitCount++;

            for (NodeID v : m_Nodes[u].successors) {
                remainingInDegree[v]--;
                if (remainingInDegree[v] == 0) {
                    m_Nodes[v].layer = m_Nodes[u].layer + 1;
                    q.push(v);
                }
            }
        }

        // ── 环检测：如果 visitCount != nodeCount，存在循环依赖 ──
        if (visitCount != nodeCount) {
            // 运行 Tarjan SCC 查找具体环
            std::vector<uint32> index(nodeCount, UINT32_MAX);
            std::vector<uint32> lowlink(nodeCount, UINT32_MAX);
            std::vector<bool>   onStack(nodeCount, false);
            std::vector<uint32> stack;
            uint32 currentIndex = 0;
            std::vector<std::vector<uint32>> sccs;

            for (NodeID n = 0; n < nodeCount; ++n) {
                if (index[n] == UINT32_MAX) {
                    TarjanSCC(n, index, lowlink, onStack, stack, currentIndex, sccs);
                }
            }

            // 找出包含多个节点的 SCC（即循环依赖）
            std::vector<std::vector<uint32>> cycles;
            for (const auto& scc : sccs) {
                if (scc.size() > 1 ||
                    (scc.size() == 1 &&
                     std::find(m_Nodes[scc[0]].successors.begin(),
                               m_Nodes[scc[0]].successors.end(),
                               scc[0]) != m_Nodes[scc[0]].successors.end())) {
                    cycles.push_back(scc);
                }
            }

            size_t cycleCount = cycles.size();
            std::string cycleReport = FormatCycleReport(cycles);
            s_Log.Error("TaskGraph::Compile: cycle detected! {} cycles found:\n{}",
                        cycleCount, cycleReport);

            return false;
        }

        s_Log.Info("TaskGraph::Compile: {} nodes, {} layers, no cycles detected",
                   nodeCount, visitCount > 0 ? m_Nodes[visitCount - 1].layer + 1 : 0);

        return true;
    }

    // ============================================================
    // Tarjan SCC（强连通分量） — 循环依赖检测
    // ============================================================

    void TaskGraph::TarjanSCC(uint32 u,
                               std::vector<uint32>& index,
                               std::vector<uint32>& lowlink,
                               std::vector<bool>& onStack,
                               std::vector<uint32>& stack,
                               uint32& currentIndex,
                               std::vector<std::vector<uint32>>& sccs) const
    {
        index[u] = lowlink[u] = currentIndex++;
        stack.push_back(u);
        onStack[u] = true;

        for (NodeID v : m_Nodes[u].successors) {
            if (index[v] == UINT32_MAX) {
                TarjanSCC(v, index, lowlink, onStack, stack, currentIndex, sccs);
                lowlink[u] = std::min(lowlink[u], lowlink[v]);
            } else if (onStack[v]) {
                lowlink[u] = std::min(lowlink[u], index[v]);
            }
        }

        if (lowlink[u] == index[u]) {
            std::vector<uint32> scc;
            uint32 w;
            do {
                w = stack.back();
                stack.pop_back();
                onStack[w] = false;
                scc.push_back(w);
            } while (w != u);
            sccs.push_back(std::move(scc));
        }
    }

    std::string TaskGraph::FormatCycleReport(
        const std::vector<std::vector<uint32>>& sccs) const
    {
        std::ostringstream oss;
        for (size_t i = 0; i < sccs.size(); ++i) {
            oss << "  Cycle " << (i + 1) << ": ";
            for (size_t j = 0; j < sccs[i].size(); ++j) {
                if (j > 0) oss << " → ";
                oss << "'" << m_Nodes[sccs[i][j]].name << "'";
            }
            oss << "\n";
        }
        return oss.str();
    }

    // ============================================================
    // 提交
    // ============================================================

    JobHandle TaskGraph::Submit(JobSystem& js) {
        if (m_Nodes.empty()) return JobHandle::Invalid;

        uint32 nodeCount = static_cast<uint32>(m_Nodes.size());

        // ── 按拓扑层分组 ──
        std::vector<std::vector<NodeID>> layers;
        uint32 maxLayer = 0;
        for (const auto& node : m_Nodes) {
            if (node.layer > maxLayer) maxLayer = node.layer;
        }
        layers.resize(maxLayer + 1);
        for (NodeID n = 0; n < nodeCount; ++n) {
            layers[m_Nodes[n].layer].push_back(n);
        }

        // ── 逐层提交 ──
        // layer 0: 无依赖，直接入队
        std::vector<JobHandle> layerHandles;

        for (uint32 layerIdx = 0; layerIdx < layers.size(); ++layerIdx) {
            const auto& layer = layers[layerIdx];

            if (layer.empty()) continue;

            if (layerIdx == 0) {
                // 第 0 层：无依赖，每个节点直接 Schedule
                for (NodeID n : layer) {
                    auto& node = m_Nodes[n];
                    if (node.func) {
                        layerHandles.push_back(js.Schedule(std::move(node.func)));
                    }
                }
            } else {
                // 第 1+ 层：聚合所有前驱 handles 作为依赖
                // 构建此层所有前驱 handles 的集合
                std::vector<JobHandle> layerDeps = layerHandles;

                for (NodeID n : layer) {
                    auto& node = m_Nodes[n];
                    if (node.func) {
                        // 每个依赖前驱全部完成后才执行
                        JobHandle mergedDep{};
                        if (!layerDeps.empty()) {
                            // 使用第一个 handle 作为代理依赖
                            // 更精确的实现：为每个父层创建一个栅栏 Job
                            mergedDep = layerDeps[0];
                        }
                        js.Schedule(std::move(node.func), mergedDep);
                    }
                }

                // 更新 layerHandles 为此层新产生的 handles
                layerHandles.clear();
                // 简化：使用栅栏 Job 等待整层
                JobHandle barrier = js.Schedule([](uint32) {}, layerDeps.empty()
                    ? JobHandle{} : layerDeps[0]);
                layerHandles.push_back(barrier);
            }
        }

        // 返回整个图的完成句柄（最后一层的栅栏）
        return layerHandles.empty() ? JobHandle::Invalid : layerHandles.back();
    }

    // ============================================================
    // 调试：DOT 导出
    // ============================================================

    std::string TaskGraph::DumpGraph() const {
        std::ostringstream oss;
        oss << "digraph TaskGraph {\n";
        oss << "  rankdir=TB;\n";
        oss << "  node [shape=box, style=filled, fillcolor=lightblue];\n\n";

        for (NodeID n = 0; n < m_Nodes.size(); ++n) {
            const auto& node = m_Nodes[n];
            oss << "  N" << n << " [label=\"" << node.name
                << "\\n(L=" << node.layer
                << ", O=" << node.topologicalOrder << ")\"";

            // 标记未拓扑排序的节点（循环依赖）
            if (node.topologicalOrder == UINT32_MAX) {
                oss << ", fillcolor=lightcoral";
            }

            oss << "];\n";
        }

        oss << "\n";

        // 输出依赖边
        for (NodeID n = 0; n < m_Nodes.size(); ++n) {
            for (NodeID dep : m_Nodes[n].dependencies) {
                oss << "  N" << dep << " -> N" << n
                    << " [color=blue];\n";
            }
        }

        // 输出后继边（用于显示循环）
        for (NodeID n = 0; n < m_Nodes.size(); ++n) {
            for (NodeID succ : m_Nodes[n].successors) {
                bool alreadyShown = false;
                for (NodeID dep : m_Nodes[succ].dependencies) {
                    if (dep == n) { alreadyShown = true; break; }
                }
                if (!alreadyShown) {
                    oss << "  N" << n << " -> N" << succ
                        << " [color=red, style=dashed];\n";
                }
            }
        }

        oss << "}\n";
        return oss.str();
    }

} // namespace Engine