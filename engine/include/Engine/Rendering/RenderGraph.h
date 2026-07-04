#pragma once
#include "Engine/Core/RHI/IRHICommandList.h"
#include "Engine/Core/RHI/IRHIDevice.h"
#include "Engine/Core/RHI/RHITypes.h"
#include "Engine/Core/RHI/PSODesc.h"
#include "Engine/Core/StringID.h"
#include "Engine/Core/TaskGraph.h"
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

        // 瞬态分配索引（-1 = 外部资源，不需要瞬态分配）
        int32_t   transientIndex = -1;

        // 是否为帧内瞬态资源（自动分配/释放）
        bool      isTransient = false;
    };

    // ============================================================
    // RenderPass — 单个 Pass
    // ============================================================
    struct RenderPass {
        std::string name;
        std::vector<StringID> reads;
        std::vector<StringID> writes;
        std::function<void(::Engine::RHI::IRHICommandList&)> execute;
        std::vector<uint32_t> dependencies;
        uint32_t topologicalOrder = UINT32_MAX;
        uint32_t layer = 0;
    };

    // ============================================================
    // Pass 访问模式（用于推导屏障）
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

        // 禁用拷贝
        RenderGraph(const RenderGraph&) = delete;
        RenderGraph& operator=(const RenderGraph&) = delete;

        // 允许移动
        RenderGraph(RenderGraph&&) noexcept;
        RenderGraph& operator=(RenderGraph&&) noexcept;

        /**
         * @brief 添加一个渲染 Pass
         *
         * @param name    Pass 名称（调试用）
         * @param setup   设置回调（声明资源的读写关系）
         * @param execute 执行回调（录制 GPU 命令）
         */
        void AddPass(const std::string& name,
                     std::function<void(RenderPassBuilder&)> setup,
                     std::function<void(::Engine::RHI::IRHICommandList&)> execute);

        /**
         * @brief 编译 RenderGraph
         *
         * 此方法执行：
         *   1. 依赖推导 (DeriveDependencies)
         *   2. 拓扑排序 (TopologicalSort)
         *   3. 资源生命周期计算 (ComputeResourceLifetimes)
         *   4. 瞬态资源分配 (TransientHeap::RegisterRequest + Compile)
         *   5. 屏障生成 (GenerateBarriers)
         *
         * @return true 表示编译成功
         */
        bool Compile(TransientHeap& transientHeap);

        /**
         * @brief 执行编译后的 RenderGraph
         *
         * 对每个 Pass：
         *   1. 提交所有输入屏障 (ResourceBarrier)
         *   2. 执行 Pass 的录制回调
         *   3. 提交所有输出屏障
         *
         * @param device RHI 设备
         * @param queue  RHI 命令队列
         */
        void Execute(::Engine::RHI::IRHIDevice& device,
                     ::Engine::RHI::IRHICommandQueue& queue);

        /**
         * @brief 多线程并行执行
         *
         * 如果多个 Pass 的 layer 相同（无依赖），在多个
         * CommandList 上并行录制。
         */
        void ExecuteParallel(::Engine::RHI::IRHIDevice& device,
                             ::Engine::RHI::IRHICommandQueue& queue,
                             class JobSystem& js);

        /** 导出 DOT 图（调试用） */
        std::string DumpGraph() const;

        /** Pass 数量 */
        size_t GetPassCount() const noexcept { return m_Passes.size(); }

        /** 资源数量 */
        size_t GetResourceCount() const noexcept { return m_Resources.size(); }

        /** 获取屏障总数 */
        size_t GetBarrierCount() const noexcept { return m_Barriers.size(); }

    private:
        // ── 编译阶段 ──

        /** 根据读写关系推导 Pass 间的依赖 */
        void DeriveDependencies();

        /** 拓扑排序（Kahn 算法） */
        bool TopologicalSort();

        /** 计算每个资源的首次/末次使用 Pass */
        void ComputeResourceLifetimes();

        /**
         * @brief 生成所有必需的资源屏障
         *
         * 每个 barrier 连接两个连续的 Pass，
         * 将资源从上一个 Pass 遗留的状态转换为当前 Pass 需要的状态。
         */
        void GenerateBarriers();

        /**
         * @brief 根据 Pass 对资源的访问模式推导所需状态
         */
        static ::Engine::RHI::ResourceState DeriveRequiredState(
            const RenderPass& pass, const RGResource& res);

        // ── 数据 ──
        std::vector<RenderPass> m_Passes;
        std::unordered_map<uint64_t, RGResource> m_Resources;
        bool m_Compiled = false;

        // ── 屏障结果 ──
        struct Barrier {
            uint32_t                  passIndex;   ///< 在此 Pass 之前插入屏障
            ::Engine::RHI::ResourceBarrierDesc desc;
        };
        std::vector<Barrier> m_Barriers;
    };

}} // Engine::Rendering