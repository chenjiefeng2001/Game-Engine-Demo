#pragma once
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/StringID.h"
#include "Engine/Core/TaskGraph.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <queue>

namespace Engine { namespace Rendering {

    struct RGResource {
        StringID  name;
        enum Type { Texture, Buffer } type = Texture;
        uint32_t  width  = 0;
        uint32_t  height = 0;
        uint32_t  depth  = 1;
        ::Engine::RHI::Format format = ::Engine::RHI::Format::RGBA8_UNorm;
        uint32_t  firstUse = UINT32_MAX;
        uint32_t  lastUse  = 0;
        uint32_t  creatingPass = UINT32_MAX;
        ::Engine::RHI::ResourceState initialState = ::Engine::RHI::ResourceState::Undefined;
    };

    struct RenderPass {
        std::string name;
        std::vector<StringID> reads;
        std::vector<StringID> writes;
        std::function<void(::Engine::RHI::IRHICommandList&)> execute;
        std::vector<uint32_t> dependencies;
        uint32_t topologicalOrder = UINT32_MAX;
        uint32_t layer = 0;
    };

    class RenderPassBuilder {
    public:
        explicit RenderPassBuilder(RenderPass& pass,
                                   std::unordered_map<uint64_t, RGResource>& resources)
            : m_Pass(pass), m_Resources(resources) {}
        void CreateTexture(StringID name, uint32_t w, uint32_t h, ::Engine::RHI::Format fmt);
        void CreateBuffer(StringID name, uint32_t size);
        void ReadTexture(StringID name);
        void WriteTexture(StringID name);
        RenderPass& GetPass() noexcept { return m_Pass; }
    private:
        RenderPass& m_Pass;
        std::unordered_map<uint64_t, RGResource>& m_Resources;
    };

    class RenderGraph {
    public:
        void AddPass(const std::string& name,
                     std::function<void(RenderPassBuilder&)> setup,
                     std::function<void(::Engine::RHI::IRHICommandList&)> execute);
        bool Compile();
        void Execute(::Engine::RHI::IRHIDevice& device, ::Engine::RHI::IRHICommandQueue& queue);
        void ExecuteParallel(::Engine::RHI::IRHIDevice& device, ::Engine::RHI::IRHICommandQueue& queue,
                             class JobSystem& js);
        std::string DumpGraph() const;
        size_t GetPassCount() const noexcept { return m_Passes.size(); }
    private:
        void DeriveDependencies();
        bool TopologicalSort();
        void ComputeResourceLifetimes();
        std::vector<::Engine::RHI::ResourceBarrierDesc> GenerateBarriers(uint32_t, uint32_t) const;
        std::vector<RenderPass> m_Passes;
        std::unordered_map<uint64_t, RGResource> m_Resources;
        bool m_Compiled = false;
    };

}} // Engine::Rendering