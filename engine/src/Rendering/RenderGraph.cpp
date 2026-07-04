#include "Engine/Rendering/RenderGraph.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Core/Log.h"
#include <sstream>
#include <queue>
#include <algorithm>

namespace Engine { namespace Rendering {

    void RenderPassBuilder::CreateTexture(StringID name, uint32_t w, uint32_t h, RHI::Format fmt) {
        m_Pass.writes.push_back(name);
        RGResource res;
        res.name = name; res.type = RGResource::Texture;
        res.width = w; res.height = h; res.format = fmt;
        res.creatingPass = static_cast<uint32_t>(m_Resources.size());
        m_Resources[name.Value()] = res;
    }

    void RenderPassBuilder::CreateBuffer(StringID name, uint32_t size) {
        m_Pass.writes.push_back(name);
        RGResource res;
        res.name = name; res.type = RGResource::Buffer; res.width = size;
        m_Resources[name.Value()] = res;
    }

    void RenderPassBuilder::ReadTexture(StringID name) { m_Pass.reads.push_back(name); }
    void RenderPassBuilder::WriteTexture(StringID name) { m_Pass.writes.push_back(name); }

    void RenderGraph::AddPass(const std::string& name,
        std::function<void(RenderPassBuilder&)> setup,
        std::function<void(RHI::IRHICommandList&)> execute) {
        RenderPass pass;
        pass.name = name; pass.execute = std::move(execute);
        RenderPassBuilder builder(pass, m_Resources);
        setup(builder);
        m_Passes.push_back(std::move(pass));
    }

    void RenderGraph::DeriveDependencies() {
        std::unordered_map<uint64_t, uint32_t> lastWriter;
        for (uint32_t passIdx = 0; passIdx < m_Passes.size(); ++passIdx) {
            auto& pass = m_Passes[passIdx];
            for (auto& readName : pass.reads) {
                auto it = lastWriter.find(readName.Value());
                if (it != lastWriter.end() && it->second != passIdx) {
                    if (std::find(pass.dependencies.begin(), pass.dependencies.end(), it->second) == pass.dependencies.end())
                        pass.dependencies.push_back(it->second);
                }
            }
            for (auto& writeName : pass.writes) {
                auto it = lastWriter.find(writeName.Value());
                if (it != lastWriter.end() && it->second != passIdx) {
                    if (std::find(pass.dependencies.begin(), pass.dependencies.end(), it->second) == pass.dependencies.end())
                        pass.dependencies.push_back(it->second);
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
                        if (--remainingDeps[v] == 0) { m_Passes[v].layer = m_Passes[u].layer + 1; q.push(v); }
                    }
                }
            }
        }
        if (visitCount != passCount) return false;
        return true;
    }

    bool RenderGraph::Compile() {
        if (m_Passes.empty()) return true;
        for (auto& p : m_Passes) { p.dependencies.clear(); p.topologicalOrder = UINT32_MAX; p.layer = 0; }
        DeriveDependencies();
        if (!TopologicalSort()) return false;
        ComputeResourceLifetimes();
        m_Compiled = true;
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

    std::string RenderGraph::DumpGraph() const {
        std::ostringstream oss;
        oss << "digraph RenderGraph {\n  rankdir=TB;\n";
        for (uint32_t i = 0; i < m_Passes.size(); ++i)
            oss << "  N" << i << " [label=\"" << m_Passes[i].name << "\\n(L=" << m_Passes[i].layer << ")\"];\n";
        for (uint32_t i = 0; i < m_Passes.size(); ++i)
            for (uint32_t dep : m_Passes[i].dependencies)
                oss << "  N" << dep << " -> N" << i << ";\n";
        oss << "}\n";
        return oss.str();
    }

}} // Engine::Rendering