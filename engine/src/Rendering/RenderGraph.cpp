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
#include <functional>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>

// ════════════════════════════════════════════════════════
// GPU 时间戳分析器（T-4，全局实例）
// ════════════════════════════════════════════════════════
// 调用方在 app 初始化时创建/Initialize，并在 GPU 完成后 Resolve；
// RenderGraph 只负责在 Pass 边界 BeginSegment/EndSegment。
static Engine::RHI::IGpuTimestampProfiler* g_GpuTimestampProfiler = nullptr;

void SetRenderGraphGpuTimestampProfiler(
    Engine::RHI::IGpuTimestampProfiler* profiler) {
    g_GpuTimestampProfiler = profiler;
}
Engine::RHI::IGpuTimestampProfiler* GetRenderGraphGpuTimestampProfiler() {
    return g_GpuTimestampProfiler;
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
    void RenderPassBuilder::ImportTexture(StringID name, ::Engine::RHI::IRHITexture* texture,
                                          uintptr_t renderTargetFbo) {
        m_Pass.reads.push_back(name);
        RGResource res;
        res.name = name;
        res.type = RGResource::Texture;
        res.rhiTexture = texture;
        res.isTransient = false;
        res.renderTargetFbo = renderTargetFbo;
        m_Resources[name.Value()] = res;
    }
    void RenderPassBuilder::ImportBuffer(StringID name, ::Engine::RHI::IRHIBuffer* buffer) {
        m_Pass.reads.push_back(name);
        RGResource res;
        res.name = name;
        res.type = RGResource::Buffer;
        res.rhiBuffer = buffer;
        res.isTransient = false;
        m_Resources[name.Value()] = res;
    }

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
        m_Compiled = false;
        m_ExecutionOrder.clear();
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
        m_Compiled = false;
        m_ExecutionOrder.clear();
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
        std::unordered_map<uint64_t, std::vector<uint32_t>> writers;
        std::unordered_map<uint64_t, std::vector<uint32_t>> readers;
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            for (auto& readName : m_Passes[passIdx].reads) {
                auto& vec = readers[readName.Value()];
                if (std::find(vec.begin(), vec.end(), passIdx) == vec.end()) vec.push_back(passIdx);
            }
            for (auto& writeName : m_Passes[passIdx].writes) {
                auto& vec = writers[writeName.Value()];
                if (std::find(vec.begin(), vec.end(), passIdx) == vec.end()) vec.push_back(passIdx);
            }
        }
        auto isReader = [&](uint64_t res, uint32_t p) {
            auto it = readers.find(res);
            return it != readers.end() && std::find(it->second.begin(), it->second.end(), p) != it->second.end();
        };
        auto isWriter = [&](uint64_t res, uint32_t p) {
            auto it = writers.find(res);
            return it != writers.end() && std::find(it->second.begin(), it->second.end(), p) != it->second.end();
        };
        auto addDep = [&](uint32_t from, uint32_t to) {
            if (from == to) return;
            auto& deps = m_Passes[from].dependencies;
            if (std::find(deps.begin(), deps.end(), to) == deps.end()) deps.push_back(to);
        };
        for (uint64_t res : [&](){ std::vector<uint64_t> v; for (auto& kv : writers) v.push_back(kv.first); for (auto& kv : readers) if (std::find(v.begin(),v.end(),kv.first)==v.end()) v.push_back(kv.first); return v; }()) {
            auto wit = writers.find(res);
            auto rit = readers.find(res);
            std::vector<uint32_t> wlist = (wit != writers.end()) ? wit->second : std::vector<uint32_t>{};
            std::vector<uint32_t> rlist = (rit != readers.end()) ? rit->second : std::vector<uint32_t>{};
            for (uint32_t r : rlist) {
                if (isWriter(res, r)) continue;
                for (uint32_t w : wlist) {
                    if (w < r) addDep(r, w);
                }
            }
            for (uint32_t r : rlist) {
                if (!isWriter(res, r)) continue;
                for (uint32_t w : wlist) {
                    if (w > r) addDep(r, w);
                }
            }
            for (uint32_t w : wlist) {
                if (isReader(res, w)) continue;
                for (uint32_t w2 : wlist) {
                    if (w2 < w && !isReader(res, w2)) addDep(w, w2);
                }
            }
            for (uint32_t w : wlist) {
                if (isReader(res, w)) continue;
                for (uint32_t r : rlist) {
                    if (isWriter(res, r)) continue;
                    if (r < w) addDep(w, r);
                }
            }
        }
    }

    bool RenderGraph::TopologicalSort() {
        uint32_t passCount = static_cast<uint32_t>(m_Passes.size());
        std::vector<uint32_t> remainingDeps(passCount, 0);
        for (uint32_t i = 0; i < passCount; ++i)
            remainingDeps[i] = static_cast<uint32_t>(m_Passes[i].dependencies.size());
        std::priority_queue<uint32_t, std::vector<uint32_t>, std::greater<uint32_t>> q;
        for (uint32_t i = 0; i < passCount; ++i)
            if (remainingDeps[i] == 0) { q.push(i); m_Passes[i].layer = 0; }
        uint32_t visitCount = 0;
        while (!q.empty()) {
            uint32_t u = q.top(); q.pop();
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
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            auto& res = const_cast<RGResource&>(kv.second);
            res.currentState = res.initialState;
        }
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            auto& res = const_cast<RGResource&>(kv.second);
            if (res.firstUse == UINT32_MAX) continue;
            RHI::ResourceState previousState = res.initialState;
            for (uint32_t passIdx = res.firstUse; passIdx <= res.lastUse; ++passIdx) {
                if (passIdx >= m_Passes.size()) break;
                auto& pass = m_Passes[passIdx];
                RHI::ResourceState requiredState = DeriveRequiredState(pass, res);
                if (requiredState != previousState &&
                    previousState != RHI::ResourceState::Undefined &&
                    ((res.type == RGResource::Buffer && (res.rhiBuffer != nullptr || !res.isTransient)) ||
                     (res.type == RGResource::Texture && (res.rhiTexture != nullptr || !res.isTransient)))) {
                    Barrier barrier;
                    barrier.passIndex = passIdx;
                    barrier.desc.type = RHI::ResourceBarrierDesc::Type::Transition;
                    barrier.desc.buffer = (res.type == RGResource::Buffer) ? res.rhiBuffer : nullptr;
                    barrier.desc.texture = (res.type == RGResource::Texture) ? res.rhiTexture : nullptr;
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
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            auto& res = const_cast<RGResource&>(kv.second);
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
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            auto& res = const_cast<RGResource&>(kv.second);
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
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            auto& res = const_cast<RGResource&>(kv.second);
            if (res.isTransient && res.transientIndex != -1 &&
                res.transientIndex < transientHeap.GetRequestCount()) {
                auto alloc = transientHeap.GetAllocation(res.transientIndex);
                if (alloc.valid && res.type == RGResource::Buffer) res.rhiBuffer = alloc.buffer;
            }
        }
        GenerateBarriers();
        m_ExecutionOrder.clear();
        for (uint32_t i = 0; i < m_Passes.size(); ++i) {
            for (uint32_t j = 0; j < m_Passes.size(); ++j) {
                if (m_Passes[j].topologicalOrder == i) { m_ExecutionOrder.push_back(j); break; }
            }
        }
        m_CompileCount++;
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

    bool RenderGraph::Execute(RHI::IRHIDevice& device,
                               RHI::IRHICommandQueue& queue)
    {
        if (!m_Compiled) { s_Log.Error("RenderGraph::Execute: not compiled"); return false; }

        auto cmdList = device.CreateCommandList(RHI::CommandListType::Direct);
        if (!cmdList) { s_Log.Error("RenderGraph::Execute: failed to create command list"); return false; }

        auto* profiler = g_GpuTimestampProfiler;
        const bool profile = profiler && profiler->IsAvailable();
        if (profile) profiler->BeginFrame();

        cmdList->Begin();
        for (size_t __i = 0; __i < (m_ExecutionOrder.empty() ? m_Passes.size() : m_ExecutionOrder.size()); ++__i) {
            const uint32_t passIdx = m_ExecutionOrder.empty() ? static_cast<uint32_t>(__i) : m_ExecutionOrder[__i];
            auto& pass = m_Passes[passIdx];
            for (const auto& barrier : m_Barriers)
                if (barrier.passIndex == passIdx)
                    cmdList->ResourceBarrier(1, &barrier.desc);
            InjectViewportAndScissor(*cmdList, pass);

            for (auto& writeName : pass.writes) {
                auto it = m_Resources.find(writeName.Value());
                if (it != m_Resources.end() && it->second.renderTargetFbo != 0) {
                    cmdList->BindNativeFramebuffer(it->second.renderTargetFbo);
                    break;
                }
            }

            const uint32_t segment = profile
                ? profiler->BeginSegment(*cmdList,
                      pass.name.empty() ? "pass" : pass.name.c_str())
                : UINT32_MAX;
            if (pass.passType == Type_Scene && pass.extraction && pass.drawPacket) {
                const auto& packets = pass.extraction->packets;
                for (size_t i = 0; i < packets.size(); ++i)
                    pass.drawPacket(*cmdList, *pass.extraction, packets[i]);
            } else if (pass.execute) {
                pass.execute(*cmdList);
            }
            if (profile && segment != UINT32_MAX)
                profiler->EndSegment(*cmdList, segment);
        }
        cmdList->End();

        RHI::IRHICommandList* submitLists[] = { cmdList.get() };
        queue.ExecuteCommandLists(1, submitLists);
        return true;
    }

    // ── ExecuteParallel 多线程 ──

    bool RenderGraph::ExecuteParallel(RHI::IRHIDevice& device,
                                       RHI::IRHICommandQueue& queue,
                                       JobSystem& js)
    {
        if (!m_Compiled) { s_Log.Error("RenderGraph::ExecuteParallel: not compiled"); return false; }

        bool hasScenePass = false;
        for (auto& p : m_Passes)
            if (p.passType == Type_Scene) { hasScenePass = true; break; }

        if (!hasScenePass) {
            auto* profiler = g_GpuTimestampProfiler;
            const bool profile = profiler && profiler->IsAvailable();
            if (profile) profiler->BeginFrame();

            uint32_t maxLayer = 0;
            for (const auto& p : m_Passes)
                if (p.layer > maxLayer) maxLayer = p.layer;
            std::vector<std::unique_ptr<RHI::IRHICommandList>> layerCmdLists(maxLayer + 1);
            for (uint32_t layer = 0; layer <= maxLayer; ++layer) {
                layerCmdLists[layer] = device.CreateCommandList(RHI::CommandListType::Direct);
                if (layerCmdLists[layer]) layerCmdLists[layer]->Begin();
            }
            for (size_t __i = 0; __i < (m_ExecutionOrder.empty() ? m_Passes.size() : m_ExecutionOrder.size()); ++__i) {
                const uint32_t passIdx = m_ExecutionOrder.empty() ? static_cast<uint32_t>(__i) : m_ExecutionOrder[__i];
                const auto& p = m_Passes[passIdx];
                for (const auto& barrier : m_Barriers)
                    if (barrier.passIndex == passIdx && layerCmdLists[p.layer])
                        layerCmdLists[p.layer]->ResourceBarrier(1, &barrier.desc);
                const uint32_t segment = (profile && layerCmdLists[p.layer])
                    ? profiler->BeginSegment(*layerCmdLists[p.layer],
                          p.name.empty() ? "pass" : p.name.c_str())
                    : UINT32_MAX;
                for (auto& writeName : p.writes) {
                    auto it = m_Resources.find(writeName.Value());
                    if (it != m_Resources.end() && it->second.renderTargetFbo != 0) {
                        layerCmdLists[p.layer]->BindNativeFramebuffer(it->second.renderTargetFbo);
                        break;
                    }
                }
                if (p.execute && layerCmdLists[p.layer])
                    p.execute(*layerCmdLists[p.layer]);
                if (profile && segment != UINT32_MAX && layerCmdLists[p.layer])
                    profiler->EndSegment(*layerCmdLists[p.layer], segment);
            }
            for (auto& cmdList : layerCmdLists)
                if (cmdList) cmdList->End();
            for (auto& cmdList : layerCmdLists) {
                if (cmdList) {
                    RHI::IRHICommandList* submitLists[] = { cmdList.get() };
                    queue.ExecuteCommandLists(1, submitLists);
                }
            }
            return true;
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
        return true;
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
        for (const auto& kv : m_Resources) {
            (void)kv.first;
            const auto& res = kv.second;
            oss << "  R_" << res.name.Value()
                << " [shape=box,label=\"Resource(" << res.name.Value()
                << ")\\n(Life: " << res.firstUse << "-" << res.lastUse
                << ", Transient: " << (res.isTransient ? "Y" : "N") << ")\"];\n";
        }
        oss << "}\n";
        return oss.str();
    }

}} // Engine::Rendering