#pragma once
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/RenderPacket.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/RHI/GPUProfiler.h"
#include "Engine/Core/StringID.h"
#include "Engine/Core/TaskGraph.h"
#include "Engine/Core/JobSystem.h"
#include "Engine/Rendering/TransientHeap.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <queue>

namespace Engine { namespace Rendering {

    // ============================================================
    // RGResource — RenderGraph 内的虚拟资源
    // ============================================================
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

        // 状态追踪
        ::Engine::RHI::ResourceState initialState    = ::Engine::RHI::ResourceState::Undefined;
        ::Engine::RHI::ResourceState currentState    = ::Engine::RHI::ResourceState::Undefined;
        ::Engine::RHI::ResourceState finalState      = ::Engine::RHI::ResourceState::Common;

        // 瞬态分配索引（-1 = 外部资源）
        int32_t   transientIndex = -1;

        // 是否为帧内瞬态资源
        bool      isTransient = false;

        // Viewport/Scissor 自动追踪
        float viewportMinX = 0.0f, viewportMinY = 0.0f;
        float viewportMaxX = 0.0f, viewportMaxY = 0.0f;
    };

    // ── Pass 类型标签 ──
    enum PassType : uint8 {
        Type_Regular = 0,
        Type_Scene   = 1,   // ScenePass: 消费 SceneExtraction 快照
    };

    // ============================================================
    // RenderPass — 单个 Pass
    // ============================================================
    struct RenderPass {
        PassType passType = Type_Regular;

        std::string name;
        std::vector<StringID> reads;
        std::vector<StringID> writes;
        std::function<void(::Engine::RHI::IRHICommandList&)> execute;
        std::vector<uint32_t> dependencies;
        uint32_t topologicalOrder = UINT32_MAX;
        uint32_t layer = 0;

        // Viewport/Scissor（由 Build 阶段自动设置）
        float viewportX = 0, viewportY = 0;
        float viewportW = 0, viewportH = 0;

        // ── Indirect Draw 依赖 ──
        // 显式声明此 Pass 读取哪些 Buffer 作为间接绘制参数
        // 这些 buffer 需要在 Barrier 中转换为 INDIRECT_ARGUMENT 状态
        std::vector<StringID> indirectReads;

        // ── SceneRenderPass 特有字段（仅当 passType == Type_Scene 时有效） ──
        const RHI::SceneExtraction* extraction = nullptr;
        std::function<void(::Engine::RHI::IRHICommandList&,
                           const RHI::SceneExtraction&,
                           const RHI::RenderPacket&)> drawPacket;
        uint32_t numSlices = 1;
    };

    // ============================================================
    // Barrier — 单个资源屏障
    // ============================================================
    struct Barrier {
        uint32_t passIndex = UINT32_MAX;
        ::Engine::RHI::ResourceBarrierDesc desc;
    };

    // ============================================================
    // Pass 访问模式
    // ============================================================
    struct PassResourceAccess {
        StringID name;
        enum AccessType { Read, Write, ReadWrite } access = Read;
    };

    // ============================================================
    // RenderPassBuilder
    // ============================================================
    class RenderPassBuilder {
    public:
        explicit RenderPassBuilder(RenderPass& pass,
                                   std::unordered_map<uint64_t, RGResource>& resources)
            : m_Pass(pass), m_Resources(resources) {}
        void CreateTexture(StringID name, uint32_t w, uint32_t h,
                           ::Engine::RHI::Format fmt,
                           bool isTransient = true);
        void CreateBuffer(StringID name, uint32_t size,
                          bool isTransient = true);
        void ReadTexture(StringID name);
        void WriteTexture(StringID name);
        void ReadIndirectArgs(StringID name);
        void SetViewport(float x, float y, float w, float h);
        RenderPass& GetPass() noexcept { return m_Pass; }
    private:
        RenderPass& m_Pass;
        std::unordered_map<uint64_t, RGResource>& m_Resources;
    };

    // ============================================================
    // RenderGraph
    // ============================================================
    class RenderGraph {
    public:
        RenderGraph();
        ~RenderGraph();

        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;
        RenderGraph(RenderGraph&&) noexcept;
        RenderGraph& operator=(RenderGraph&&) noexcept;

        // ── Pass 注册 ──

        /** @brief 添加普通 Pass（无场景数据） */
        void AddPass(const std::string& name,
                     std::function<void(RenderPassBuilder&)> setup,
                     std::function<void(::Engine::RHI::IRHICommandList&)> execute);

        /** @brief 添加场景 Pass（消费 SceneExtraction 快照） */
        void AddScenePass(const std::string& name,
                          std::function<void(RenderPassBuilder&)> setup,
                          const RHI::SceneExtraction* extraction,
                          std::function<void(::Engine::RHI::IRHICommandList&,
                                             const RHI::SceneExtraction&,
                                             const RHI::RenderPacket&)> drawPacket,
                          uint32_t numSlices = 1);

        // ── 编译 ──

        /** @brief 编译 RenderGraph（依赖推导 + 排序 + 屏障 + 瞬态分配） */
        bool Compile(TransientHeap& transientHeap);

        /**
         * @brief 重置 RenderGraph（清空所有 Pass 和资源）
         * 在窗口 Resize 或交换链重建前调用。
         */
        void Reset() noexcept {
            m_Compiled = false;
            m_Passes.clear();
            m_Resources.clear();
            m_Barriers.clear();
        }

        /** @brief 是否已编译 */
        bool IsCompiled() const noexcept { return m_Compiled; }

        // ── 执行 ──

        /** @brief 单线程执行 */
        void Execute(::Engine::RHI::IRHIDevice& device,
                     ::Engine::RHI::IRHICommandQueue& queue);

        /** @brief 多线程并行执行（使用 JobSystem） */
        void ExecuteParallel(::Engine::RHI::IRHIDevice& device,
                             ::Engine::RHI::IRHICommandQueue& queue,
                             JobSystem& js);

        // ── 调试 ──

        /** @brief 导出 DOT 图 */
        std::string DumpGraph() const;

    private:
        // ── 内部编译步骤 ──
        void DeriveDependencies();
        bool TopologicalSort();
        void ComputeResourceLifetimes();
        ::Engine::RHI::ResourceState DeriveRequiredState(
            const RenderPass& pass, const RGResource& res);
        void GenerateBarriers();

        // ── 内部执行步骤 ──
        void ExecutePass(::Engine::RHI::IRHICommandList& cmdList, uint32_t passIdx);
        void InjectViewportAndScissor(::Engine::RHI::IRHICommandList& cmdList,
                                      const RenderPass& pass);

        // ── 状态 ──
        bool m_Compiled = false;
        std::vector<RenderPass> m_Passes;
        std::unordered_map<uint64_t, RGResource> m_Resources;
        std::vector<Barrier> m_Barriers;
    };

}} // namespace Engine::Rendering