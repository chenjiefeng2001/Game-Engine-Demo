/**
 * @file RenderGraph.cpp
 * @brief RenderGraph — 依赖调度 + 资源屏障生成 + 瞬态分配 + 执行
 *
 * v2.0 扩展：SceneRenderPass 快速路径 + Viewport 自动注入
 */

#include "Engine/Rendering/RenderGraph.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <queue>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

// ════════════════════════════════════════════════════════
// GPU 调试标记 + 时间戳分析器（全局实例）
// ════════════════════════════════════════════════════════
// 在 app 初始化时调用 profiler.Initialize()
// 然后在 RenderGraph 执行过程中自动调用 BeginPass/EndPass
static Engine::RHI::GPUProfiler* g_GPUProfiler = nullptr;

void SetRenderGraphGPUProfiler(Engine::RHI::GPUProfiler* profiler) {
    g_GPUProfiler = profiler;
}
Engine::RHI::GPUProfiler* GetRenderGraphGPUProfiler() {
    return g_GPUProfiler;
}

namespace {
    Engine::Logger s_Log("RenderGraph");
}

namespace Engine { namespace Rendering {

    // ── 构造 / 析构 ──

    RenderGraph::RenderGraph() = default;
    RenderGraph::~RenderGraph() = default;
    RenderGraph::RenderGraph(RenderGraph&&) noexcept = default;
    RenderGraph& RenderGraph::operator=(RenderGraph&&) noexcept = default;

    // ── RenderPassBuilder ──

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
        res.finalState   = RHI::ResourceState::ShaderResource;
        m_Pass.viewportX = 0; m_Pass.viewportY = 0;
        m_Pass.viewportW = static_cast<float>(w);
        m_Pass.viewportH = static_cast<float>(h);
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

    void RenderPassBuilder::ReadIndirectArgs(StringID name) {
        // 注册为 reads + indirectReads，确保 Barrier 生成时识别
        m_Pass.reads.push_back(name);
        m_Pass.indirectReads.push_back(name);
    }

    void RenderPassBuilder::SetViewport(float x, float y, float w, float h) {
        m_Pass.viewportX = x; m_Pass.viewportY = y;
        m_Pass.viewportW = w; m_Pass.viewportH = h;
    }

    // ── AddPass / AddScenePass ──

    void RenderGraph::AddPass(const std::string& name,
        std::function<void(RenderPassBuilder&)> setup,
        std::function<void(RHI::IRHICommandList&)> execute) {
        RenderPass pass;
        pass.name = name;
        pass.execute = std::move(execute);
        pass.passType = Type_Regular;
        RenderPassBuilder builder(pass, m_Resources);
        setup(builder);
        m_Passes.push_back(std::move(pass));
    }

    void RenderGraph::AddScenePass(const std::string& name,
        std::function<void(RenderPassBuilder&)> setup,
        const RHI::SceneExtraction* extraction,
        std::function<void(RHI::IRHICommandList&,
                           const RHI::SceneExtraction&,
                           const RHI::RenderPacket&)> drawPacket,
        uint32_t numSlices)
    {
        RenderPass pass;
        pass.name = name;
        pass.passType = Type_Scene;
        pass.extraction = extraction;
        pass.drawPacket = std::move(drawPacket);
        pass.numSlices = numSlices;
        pass.execute = [](RHI::IRHICommandList&) {};
        RenderPassBuilder builder(pass, m_Resources);
        setup(builder);
        m_Passes.push_back(std::move(pass));
    }

    // ── 编译步骤 ──

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

    RHI::ResourceState RenderGraph::DeriveRequiredState(
        const RenderPass& pass, const RGResource& res)
    {
        // 1. 检查 IndirectReads 显式声明 → 使用 INDIRECT_ARGUMENT 状态
        for (const auto& irName : pass.indirectReads) {
            if (irName.Value() == res.name.Value()) {
                return RHI::ResourceState::IndirectArgument;
            }
        }

        bool reads = false, writes = false;
        for (const auto& rName : pass.reads) {
            if (rName.Value() == res.name.Value()) { reads = true; break; }
        }
        if (!reads) {
            for (const auto& wName : pass.writes) {
                if (wName.Value() == res.name.Value()) { writes = true; break; }
            }
        }
        if (reads && writes) return RHI::ResourceState::UnorderedAccess;
        if (writes) {
            if (res.format == RHI::Format::D32_Float ||
                res.format == RHI::Format::D24_UNorm_S8_UInt ||
                res.format == RHI::Format::D16_UNorm ||
                res.format == RHI::Format::D32_Float_S8_UInt)
                return RHI::ResourceState::DepthStencil;
            return RHI::ResourceState::RenderTarget;
        }
        if (reads || pass.writes.empty()) return RHI::ResourceState::ShaderResource;
        return RHI::ResourceState::Common;
    }

    void RenderGraph::GenerateBarriers() {
        m_Barriers.clear();
        if (m_Passes.empty()) return;
        for (auto& [key, res] : m_Resources) {
            (void)key;
            res.currentState = res.initialState;
        }
        for (auto& [key, res] : m_Resources) {
            (void)key;
            if (res.firstUse == UINT32_MAX) continue;
            RHI::ResourceState previousState = res.initialState;
            for (uint32_t passIdx = res.firstUse; passIdx <= res.lastUse; ++passIdx) {
                if (passIdx >= m_Passes.size()) break;
                auto& pass = m_Passes[passIdx];
                RHI::ResourceState requiredState = DeriveRequiredState(pass, res);
                if (requiredState != previousState &&
                    previousState != RHI::ResourceState::Undefined) {
                    Barrier barrier;
                    barrier.passIndex = passIdx;
                    barrier.desc.type = RHI::ResourceBarrierDesc::Type::Transition;
                    barrier.desc.buffer = (res.type == RGResource::Buffer) ?
                        reinterpret_cast<RHI::IRHIBuffer*>(static_cast<uintptr_t>(res.name.Value())) : nullptr;
                    barrier.desc.texture = (res.type == RGResource::Texture) ?
                        reinterpret_cast<RHI::IRHITexture*>(static_cast<uintptr_t>(res.name.Value())) : nullptr;
                    barrier.desc.stateBefore = previousState;
                    barrier.desc.stateAfter = requiredState;
                    m_Barriers.push_back(barrier);
                }
                previousState = requiredState;
            }
            res.currentState = previousState;
        }
    }

    bool RenderGraph::Compile(TransientHeap& transientHeap) {
        if (m_Passes.empty()) { m_Compiled = true; return true; }
        for (auto& p : m_Passes) {
            p.dependencies.clear();
            p.topologicalOrder = UINT32_MAX;
            p.layer = 0;
        }
        for (auto& [key, res] : m_Resources) {
            (void)key;
            res.firstUse = UINT32_MAX; res.lastUse = 0;
            res.currentState = res.initialState;
            res.transientIndex = -1;
        }
        DeriveDependencies();
        if (!TopologicalSort()) {
            s_Log.Error("RenderGraph::Compile: cyclic dependency detected");
            return false;
        }
        ComputeResourceLifetimes();
        transientHeap.Reset();
        for (auto& [key, res] : m_Resources) {
            (void)key;
            if (res.isTransient && res.firstUse != UINT32_MAX) {
                TransientAllocRequest req;
                req.size = res.width * res.height * RHI::FormatSize(res.format);
                req.alignment = 256;
                req.format = res.format;
                req.isTexture = (res.type == RGResource::Texture);
                req.width = res.width;
                req.height = res.height;
                req.firstUsePass = res.firstUse;
                req.lastUsePass = res.lastUse;
                res.transientIndex = transientHeap.RegisterRequest(req);
            }
        }
        if (!transientHeap.Compile()) {
            s_Log.Error("RenderGraph::Compile: transient heap allocation failed");
            return false;
        }
        GenerateBarriers();
        m_Compiled = true;
        return true;
    }

    // ── 内部辅助 ──

    void RenderGraph::InjectViewportAndScissor(RHI::IRHICommandList& cmdList,
                                                const RenderPass& pass) {
        if (pass.viewportW > 0 && pass.viewportH > 0) {
            RHI::Viewport vp;
            vp.x = pass.viewportX; vp.y = pass.viewportY;
            vp.width = pass.viewportW; vp.height = pass.viewportH;
            cmdList.SetViewport(vp);
            RHI::Rect rect;
            rect.x = static_cast<int32>(pass.viewportX);
            rect.y = static_cast<int32>(pass.viewportY);
            rect.width = static_cast<int32>(pass.viewportW);
            rect.height = static_cast<int32>(pass.viewportH);
            cmdList.SetScissorRect(rect);
        }
    }

    // ── Execute 单线程 ──

    void RenderGraph::Execute(RHI::IRHIDevice& device,
                               RHI::IRHICommandQueue& queue)
    {
        if (!m_Compiled) { s_Log.Error("RenderGraph::Execute: not compiled"); return; }

        auto cmdList = device.CreateCommandList(RHI::CommandListType::Direct);
        if (!cmdList) { s_Log.Error("RenderGraph::Execute: failed to create command list"); return; }

        cmdList->Begin();
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            auto& pass = m_Passes[passIdx];
            for (const auto& barrier : m_Barriers)
                if (barrier.passIndex == passIdx)
                    cmdList->ResourceBarrier(1, &barrier.desc);
            InjectViewportAndScissor(*cmdList, pass);

            if (pass.passType == Type_Scene && pass.extraction && pass.drawPacket) {
                const auto& packets = pass.extraction->packets;
                for (size_t i = 0; i < packets.size(); ++i)
                    pass.drawPacket(*cmdList, *pass.extraction, packets[i]);
            } else if (pass.execute) {
                pass.execute(*cmdList);
            }
        }
        cmdList->End();

        RHI::IRHICommandList* submitLists[] = { cmdList.get() };
        queue.ExecuteCommandLists(1, submitLists);
        s_Log.Info("RenderGraph::Execute: {} passes, {} barriers",
                   m_Passes.size(), m_Barriers.size());
    }

    // ── ExecuteParallel 多线程 ──

    void RenderGraph::ExecuteParallel(RHI::IRHIDevice& device,
                                       RHI::IRHICommandQueue& queue,
                                       JobSystem& js)
    {
        if (!m_Compiled) { s_Log.Error("RenderGraph::ExecuteParallel: not compiled"); return; }

        bool hasScenePass = false;
        for (auto& p : m_Passes)
            if (p.passType == Type_Scene) { hasScenePass = true; break; }

        if (!hasScenePass) {
            uint32_t maxLayer = 0;
            for (const auto& p : m_Passes)
                if (p.layer > maxLayer) maxLayer = p.layer;
            std::vector<std::unique_ptr<RHI::IRHICommandList>> layerCmdLists(maxLayer + 1);
            for (uint32_t layer = 0; layer <= maxLayer; ++layer) {
                layerCmdLists[layer] = device.CreateCommandList(RHI::CommandListType::Direct);
                if (layerCmdLists[layer]) layerCmdLists[layer]->Begin();
            }
            for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
                const auto& p = m_Passes[passIdx];
                for (const auto& barrier : m_Barriers)
                    if (barrier.passIndex == passIdx && layerCmdLists[p.layer])
                        layerCmdLists[p.layer]->ResourceBarrier(1, &barrier.desc);
                if (p.execute && layerCmdLists[p.layer])
                    p.execute(*layerCmdLists[p.layer]);
            }
            for (auto& cmdList : layerCmdLists)
                if (cmdList) cmdList->End();
            for (auto& cmdList : layerCmdLists) {
                if (cmdList) {
                    RHI::IRHICommandList* submitLists[] = { cmdList.get() };
                    queue.ExecuteCommandLists(1, submitLists);
                }
            }
            return;
        }

        // ScenePass 多线程分片录制
        auto mainCmd = device.CreateCommandList(RHI::CommandListType::Direct);
        mainCmd->Begin();

        struct SliceInfo {
            RenderPass* pass;
            uint32_t packetStart;
            uint32_t packetEnd;
            std::unique_ptr<RHI::IRHICommandList> cmdList;
        };
        std::vector<SliceInfo> slices;

        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            auto& p = m_Passes[passIdx];
            if (p.passType != Type_Scene || !p.extraction) {
                for (const auto& barrier : m_Barriers)
                    if (barrier.passIndex == passIdx)
                        mainCmd->ResourceBarrier(1, &barrier.desc);
                InjectViewportAndScissor(*mainCmd, p);
                if (p.execute) p.execute(*mainCmd);
                continue;
            }

            const auto& packets = p.extraction->packets;
            if (packets.empty()) continue;

            uint32_t sliceCount = std::max(1u, p.numSlices);
            uint32_t total = static_cast<uint32_t>(packets.size());
            uint32_t sliceSize = (total + sliceCount - 1) / sliceCount;

            for (const auto& barrier : m_Barriers)
                if (barrier.passIndex == passIdx)
                    mainCmd->ResourceBarrier(1, &barrier.desc);
            InjectViewportAndScissor(*mainCmd, p);

            for (uint32_t s = 0; s < sliceCount; ++s) {
                uint32_t start = s * sliceSize;
                uint32_t end = std::min(start + sliceSize, total);
                if (start >= end) break;
                auto sliceCmd = device.CreateCommandList(RHI::CommandListType::Direct);
                sliceCmd->Begin();
                slices.push_back({&p, start, end, std::move(sliceCmd)});
            }
        }

        std::atomic<uint32_t> sliceDone{0};
        for (auto& slice : slices) {
            js.Schedule([&slice, &sliceDone](uint32_t) {
                if (!slice.pass->drawPacket) return;
                for (uint32_t i = slice.packetStart; i < slice.packetEnd; ++i) {
                    slice.pass->drawPacket(*slice.cmdList, *slice.pass->extraction,
                                            slice.pass->extraction->packets[i]);
                }
                slice.cmdList->End();
                sliceDone.fetch_add(1, std::memory_order_release);
            });
        }

        while (sliceDone.load(std::memory_order_acquire) < slices.size())
            std::this_thread::yield();

        mainCmd->End();

        RHI::IRHICommandList* mainList = mainCmd.get();
        queue.ExecuteCommandLists(1, &mainList);
        for (auto& slice : slices) {
            RHI::IRHICommandList* list = slice.cmdList.get();
            queue.ExecuteCommandLists(1, &list);
        }

        s_Log.Info("RenderGraph::ExecuteParallel: {} passes, {} slices",
                   m_Passes.size(), slices.size());
    }

    // ── DumpGraph ──

    std::string RenderGraph::DumpGraph() const {
        std::ostringstream oss;
        oss << "digraph RenderGraph {\n  rankdir=TB;\n";
        for (uint32_t i = 0; i < m_Passes.size(); ++i) {
            oss << "  N" << i << " [label=\""
                << m_Passes[i].name
                << ((m_Passes[i].passType == Type_Scene) ? " (Scene)" : "")
                << "\\n(L=" << m_Passes[i].layer << ")\"];\n";
        }
        for (uint32_t i = 0; i < m_Passes.size(); ++i)
            for (uint32_t dep : m_Passes[i].dependencies)
                oss << "  N" << dep << " -> N" << i << ";\n";
        for (const auto& [key, res] : m_Resources) {
            (void)key;
            oss << "  R_" << res.name.Value()
                << " [shape=box,label=\"Resource(" << res.name.Value()
                << ")\\n(Life: " << res.firstUse << "-" << res.lastUse
                << ", Transient: " << (res.isTransient ? "Y" : "N") << ")\"];\n";
        }
        oss << "}\n";
        return oss.str();
    }

}} // Engine::Rendering