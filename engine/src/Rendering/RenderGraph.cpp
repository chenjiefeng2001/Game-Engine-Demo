/**
 * @file RenderGraph.cpp
 * @brief RenderGraph — 依赖调度 + 资源屏障生成 + 瞬态分配 + 执行
 */

#include "Engine/Rendering/RenderGraph.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <queue>
#include <algorithm>
#include <cstring>

namespace {
    Engine::Logger s_Log("RenderGraph");
}

namespace Engine { namespace Rendering {

    // ════════════════════════════════════════════════════════
    // RenderGraph 构造 / 析构
    // ════════════════════════════════════════════════════════

    RenderGraph::RenderGraph() = default;
    RenderGraph::~RenderGraph() = default;

    RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

    // ════════════════════════════════════════════════════════
    // RenderPassBuilder — 资源声明
    // ════════════════════════════════════════════════════════

    void RenderPassBuilder::CreateTexture(StringID name, uint32_t w, uint32_t h,
                                           RHI::Format fmt, bool isTransient) {
        m_Pass.writes.push_back(name);
        RGResource res;
        res.name    = name;
        res.type    = RGResource::Texture;
        res.width   = w;
        res.height  = h;
        res.format  = fmt;
        res.creatingPass = static_cast<uint32_t>(m_Resources.size());
        res.isTransient  = isTransient;
        res.initialState = RHI::ResourceState::Undefined;
        res.currentState = RHI::ResourceState::Undefined;
        res.finalState   = RHI::ResourceState::ShaderResource;  // Pass 读后通常作为 SRV
        m_Resources[name.Value()] = res;
    }

    void RenderPassBuilder::CreateBuffer(StringID name, uint32_t size, bool isTransient) {
        m_Pass.writes.push_back(name);
        RGResource res;
        res.name    = name;
        res.type    = RGResource::Buffer;
        res.width   = size;
        res.format  = RHI::Format::Unknown;
        res.creatingPass = static_cast<uint32_t>(m_Resources.size());
        res.isTransient  = isTransient;
        res.initialState = RHI::ResourceState::Undefined;
        res.currentState = RHI::ResourceState::Undefined;
        res.finalState   = RHI::ResourceState::Common;
        m_Resources[name.Value()] = res;
    }

    void RenderPassBuilder::ReadTexture(StringID name) { m_Pass.reads.push_back(name); }
    void RenderPassBuilder::WriteTexture(StringID name) { m_Pass.writes.push_back(name); }

    // ════════════════════════════════════════════════════════
    // AddPass
    // ════════════════════════════════════════════════════════

    void RenderGraph::AddPass(const std::string& name,
        std::function<void(RenderPassBuilder&)> setup,
        std::function<void(RHI::IRHICommandList&)> execute) {
        RenderPass pass;
        pass.name = name;
        pass.execute = std::move(execute);
        RenderPassBuilder builder(pass, m_Resources);
        setup(builder);
        m_Passes.push_back(std::move(pass));
    }

    // ════════════════════════════════════════════════════════
    // DeriveDependencies — 通过生产者-消费者关系推导依赖
    // ════════════════════════════════════════════════════════

    void RenderGraph::DeriveDependencies() {
        std::unordered_map<uint64_t, uint32_t> lastWriter;
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            auto& pass = m_Passes[passIdx];
            for (auto& readName : pass.reads) {
                auto it = lastWriter.find(readName.Value());
                if (it != lastWriter.end() && it->second != passIdx) {
                    if (std::find(pass.dependencies.begin(), pass.dependencies.end(), it->second)
                        == pass.dependencies.end()) {
                        pass.dependencies.push_back(it->second);
                    }
                }
            }
            for (auto& writeName : pass.writes) {
                auto it = lastWriter.find(writeName.Value());
                if (it != lastWriter.end() && it->second != passIdx) {
                    if (std::find(pass.dependencies.begin(), pass.dependencies.end(), it->second)
                        == pass.dependencies.end()) {
                        pass.dependencies.push_back(it->second);
                    }
                }
                lastWriter[writeName.Value()] = passIdx;
            }
        }
    }

    // ════════════════════════════════════════════════════════
    // TopologicalSort — Kahn 算法
    // ════════════════════════════════════════════════════════

    bool RenderGraph::TopologicalSort() {
        uint32_t passCount = static_cast<uint32_t>(m_Passes.size());
        std::vector<uint32_t> remainingDeps(passCount, 0);
        for (uint32_t i = 0; i < passCount; ++i)
            remainingDeps[i] = static_cast<uint32_t>(m_Passes[i].dependencies.size());

        std::queue<uint32_t> q;
        for (uint32_t i = 0; i < passCount; ++i)
            if (remainingDeps[i] == 0) { q.push(i); m_Passes[i].layer = 0; }

        uint32_t visitCount = 0;
        while (!q.empty()) {
            uint32_t u = q.front(); q.pop();
            m_Passes[u].topologicalOrder = visitCount++;
            for (uint32_t v = 0; v < passCount; ++v) {
                for (uint32_t dep : m_Passes[v].dependencies) {
                    if (dep == u) {
                        if (--remainingDeps[v] == 0) {
                            m_Passes[v].layer = m_Passes[u].layer + 1;
                            q.push(v);
                        }
                    }
                }
            }
        }
        if (visitCount != passCount) return false;
        return true;
    }

    // ════════════════════════════════════════════════════════
    // ComputeResourceLifetimes — 首次/末次使用
    // ════════════════════════════════════════════════════════

    void RenderGraph::ComputeResourceLifetimes() {
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            for (auto& rName : m_Passes[passIdx].reads) {
                auto it = m_Resources.find(rName.Value());
                if (it != m_Resources.end()) {
                    if (it->second.firstUse == UINT32_MAX) it->second.firstUse = passIdx;
                    it->second.lastUse = std::max(it->second.lastUse, passIdx);
                }
            }
            for (auto& rName : m_Passes[passIdx].writes) {
                auto it = m_Resources.find(rName.Value());
                if (it != m_Resources.end()) {
                    if (it->second.firstUse == UINT32_MAX) it->second.firstUse = passIdx;
                    it->second.lastUse = std::max(it->second.lastUse, passIdx);
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════
    // DeriveRequiredState — 从 Pass 的访问模式推导所需状态
    // ════════════════════════════════════════════════════════

    RHI::ResourceState RenderGraph::DeriveRequiredState(
        const RenderPass& pass, const RGResource& res)
    {
        // 检查 Pass 对资源的访问模式
        bool reads  = false;
        bool writes = false;

        for (const auto& rName : pass.reads) {
            if (rName.Value() == res.name.Value()) {
                reads = true;
                break;
            }
        }
        if (!reads) {
            for (const auto& wName : pass.writes) {
                if (wName.Value() == res.name.Value()) {
                    writes = true;
                    break;
                }
            }
        }

        // 如果 Pass 同时读写 → 可能是 UAV
        if (reads && writes) {
            return RHI::ResourceState::UnorderedAccess;
        }

        // 仅写入 → RenderTarget/DepthStencil
        if (writes) {
            if (res.format == RHI::Format::D32_Float ||
                res.format == RHI::Format::D24_UNorm_S8_UInt ||
                res.format == RHI::Format::D16_UNorm ||
                res.format == RHI::Format::D32_Float_S8_UInt) {
                return RHI::ResourceState::DepthStencil;
            }
            return RHI::ResourceState::RenderTarget;
        }

        // 仅读取 → ShaderResource
        if (reads || pass.writes.empty()) {
            return RHI::ResourceState::ShaderResource;
        }

        return RHI::ResourceState::Common;
    }

    // ════════════════════════════════════════════════════════
    // GenerateBarriers — 自动屏障生成
    // ════════════════════════════════════════════════════════

    void RenderGraph::GenerateBarriers() {
        m_Barriers.clear();

        if (m_Passes.empty()) return;

        // 追踪每个资源的当前状态
        // 初始化：设置所有资源的 currentState = initialState
        for (auto& [key, res] : m_Resources) {
            (void)key;
            res.currentState = res.initialState;
        }

        // 对每个资源，遍历使用它的 Pass
        for (auto& [key, res] : m_Resources) {
            (void)key;
            if (res.firstUse == UINT32_MAX) continue;  // 未被使用

            RHI::ResourceState previousState = res.initialState;

            for (uint32_t passIdx = res.firstUse; passIdx <= res.lastUse; ++passIdx) {
                if (passIdx >= m_Passes.size()) break;

                auto& pass = m_Passes[passIdx];
                RHI::ResourceState requiredState = DeriveRequiredState(pass, res);

                // 如果状态发生转换 → 需要在 Pass 执行前插入屏障
                if (requiredState != previousState &&
                    previousState != RHI::ResourceState::Undefined) {

                    Barrier barrier;
                    barrier.passIndex = passIdx;

                    barrier.desc.type        = RHI::ResourceBarrierDesc::Type::Transition;
                    barrier.desc.buffer      = (res.type == RGResource::Buffer) ?
                        reinterpret_cast<RHI::IRHIBuffer*>(static_cast<uintptr_t>(res.name.Value())) : nullptr;
                    barrier.desc.texture     = (res.type == RGResource::Texture) ?
                        reinterpret_cast<RHI::IRHITexture*>(static_cast<uintptr_t>(res.name.Value())) : nullptr;
                    barrier.desc.stateBefore = previousState;
                    barrier.desc.stateAfter  = requiredState;

                    m_Barriers.push_back(barrier);

                    s_Log.Debug("Barrier: pass[{}] state {} → {}",
                                passIdx,
                                static_cast<int>(previousState),
                                static_cast<int>(requiredState));
                }

                previousState = requiredState;
            }

            // 如果 finalState 与最后一个状态不同 → 添加结尾转换
            if (res.finalState != previousState &&
                res.finalState != RHI::ResourceState::Undefined) {
                // 最后一个使用 Pass 之后转换到 finalState
                // 由 RenderGraph 在 Pass 执行后处理
                // 这里延迟到 Execute 阶段处理
            }

            res.currentState = previousState;
        }

        s_Log.Info("RenderGraph: generated {} barriers for {} resources",
                   (uint64_t)m_Barriers.size(), (uint64_t)m_Resources.size());
    }

    // ════════════════════════════════════════════════════════
    // Compile — 完整编译管线
    // ════════════════════════════════════════════════════════

    bool RenderGraph::Compile(TransientHeap& transientHeap) {
        if (m_Passes.empty()) {
            m_Compiled = true;
            return true;
        }

        // 1. 清除旧状态
        for (auto& p : m_Passes) {
            p.dependencies.clear();
            p.topologicalOrder = UINT32_MAX;
            p.layer = 0;
        }
        for (auto& [key, res] : m_Resources) {
            (void)key;
            res.firstUse = UINT32_MAX;
            res.lastUse  = 0;
            res.currentState = res.initialState;
            res.transientIndex = -1;
        }

        // 2. 推导依赖
        DeriveDependencies();

        // 3. 拓扑排序
        if (!TopologicalSort()) {
            s_Log.Error("RenderGraph::Compile: cyclic dependency detected");
            return false;
        }

        // 4. 计算资源生命周期
        ComputeResourceLifetimes();

        // 5. 注册瞬态资源到 TransientHeap
        transientHeap.Reset();
        for (auto& [key, res] : m_Resources) {
            (void)key;
            if (res.isTransient && res.firstUse != UINT32_MAX) {
                TransientAllocRequest req;
                req.size         = res.width * res.height * RHI::FormatSize(res.format);
                req.alignment    = 256;
                req.format       = res.format;
                req.isTexture    = (res.type == RGResource::Texture);
                req.width        = res.width;
                req.height       = res.height;
                req.firstUsePass = res.firstUse;
                req.lastUsePass  = res.lastUse;
                res.transientIndex = transientHeap.RegisterRequest(req);
            }
        }

        // 6. 编译 TransientHeap
        if (!transientHeap.Compile()) {
            s_Log.Error("RenderGraph::Compile: transient heap allocation failed");
            return false;
        }

        // 7. 生成屏障
        GenerateBarriers();

        m_Compiled = true;
        return true;
    }

    // ════════════════════════════════════════════════════════
    // Execute — 执行编译后的 RenderGraph
    // ════════════════════════════════════════════════════════

    void RenderGraph::Execute(RHI::IRHIDevice& device,
                               RHI::IRHICommandQueue& queue)
    {
        if (!m_Compiled) {
            s_Log.Error("RenderGraph::Execute: not compiled");
            return;
        }

        // 创建单个命令列表（主线程）
        auto cmdList = device.CreateCommandList(RHI::CommandListType::Direct);
        if (!cmdList) {
            s_Log.Error("RenderGraph::Execute: failed to create command list");
            return;
        }

        cmdList->Begin();

        // 对每个 Pass（按拓扑顺序）
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            // 找出需要在此 Pass 前插入的屏障
            bool hasBarriers = false;
            for (const auto& barrier : m_Barriers) {
                if (barrier.passIndex == passIdx) {
                    if (!hasBarriers) {
                        hasBarriers = true;
                    }
                    cmdList->ResourceBarrier(1, &barrier.desc);
                }
            }

            // 执行 Pass
            if (m_Passes[passIdx].execute) {
                m_Passes[passIdx].execute(*cmdList);
            }
        }

        cmdList->End();

        // 提交到队列执行
        ::Engine::RHI::IRHICommandList* submitLists[] = { cmdList.get() };
        queue.ExecuteCommandLists(1, submitLists);

        s_Log.Info("RenderGraph::Execute: {} passes, {} barriers executed",
                   m_Passes.size(), m_Barriers.size());
    }

    // ════════════════════════════════════════════════════════
    // ExecuteParallel — 多线程并行执行
    // ════════════════════════════════════════════════════════

    void RenderGraph::ExecuteParallel(RHI::IRHIDevice& device,
                                       RHI::IRHICommandQueue& queue,
                                       JobSystem& js)
    {
        if (!m_Compiled) {
            s_Log.Error("RenderGraph::ExecuteParallel: not compiled");
            return;
        }

        // 按 layer 分组，同 layer 的 Pass 可以并行录制
        // 每个 worker 持有一个独立的 CommandList
        uint32_t maxLayer = 0;
        for (const auto& pass : m_Passes) {
            if (pass.layer > maxLayer) maxLayer = pass.layer;
        }

        // 为每个 layer 创建一个命令列表
        // 不同 layer 之间需要同步
        std::vector<std::unique_ptr<RHI::IRHICommandList>> layerCmdLists;
        layerCmdLists.resize(maxLayer + 1);

        for (uint32_t layer = 0; layer <= maxLayer; ++layer) {
            layerCmdLists[layer] = device.CreateCommandList(RHI::CommandListType::Direct);
            if (layerCmdLists[layer]) {
                layerCmdLists[layer]->Begin();
            }
        }

        // 当前正在录制的命令列表（按 layer 索引）
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            const auto& pass = m_Passes[passIdx];

            // 检查是否有屏障需要在此 Pass 前插入
            for (const auto& barrier : m_Barriers) {
                if (barrier.passIndex == passIdx) {
                    if (layerCmdLists[pass.layer]) {
                        layerCmdLists[pass.layer]->ResourceBarrier(1, &barrier.desc);
                    }
                }
            }

            // 执行 Pass（录制到对应 layer 的命令列表）
            if (pass.execute && layerCmdLists[pass.layer]) {
                pass.execute(*layerCmdLists[pass.layer]);
            }
        }

        // 结束所有命令列表
        for (auto& cmdList : layerCmdLists) {
            if (cmdList) cmdList->End();
        }

        // 按顺序提交：先 layer 0，再 layer 1，...
        for (auto& cmdList : layerCmdLists) {
            if (cmdList) {
                ::Engine::RHI::IRHICommandList* submitLists[] = { cmdList.get() };
                queue.ExecuteCommandLists(1, submitLists);
            }
        }

        s_Log.Info("RenderGraph::ExecuteParallel: {} passes in {} layers",
                   m_Passes.size(), maxLayer + 1);
    }

    // ════════════════════════════════════════════════════════
    // DumpGraph — DOT 调试输出
    // ════════════════════════════════════════════════════════

    std::string RenderGraph::DumpGraph() const {
        std::ostringstream oss;
        oss << "digraph RenderGraph {\n  rankdir=TB;\n";
        for (uint32_t i = 0; i < m_Passes.size(); ++i)
            oss << "  N" << i << " [label=\"" << m_Passes[i].name
                << "\\n(L=" << m_Passes[i].layer << ")\"];\n";
        for (uint32_t i = 0; i < m_Passes.size(); ++i)
            for (uint32_t dep : m_Passes[i].dependencies)
                oss << "  N" << dep << " -> N" << i << ";\n";

        // 添加资源节点
        for (const auto& [key, res] : m_Resources) {
            (void)key;
            oss << "  R_" << res.name.Value() << " [shape=box,label=\""
                << "Resource(" << res.name.Value() << ")"
                << "\\n(Life: " << res.firstUse << "-" << res.lastUse
                << ", Transient: " << (res.isTransient ? "Y" : "N") << ")\"];\n";
        }
        oss << "}\n";
        return oss.str();
    }

}} // Engine::Rendering