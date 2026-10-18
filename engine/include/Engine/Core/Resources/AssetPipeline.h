#pragma once

/**
 * @file AssetPipeline.h
 * @brief 资产调节管道 — 定义资源处理规则、依赖关系分析与批处理构建
 *
 * 设计要点：
 *   - PipelineStage：管道阶段（验证→预处理→编译→优化→打包）
 *   - AssetRule：每种资源类型的处理规则（输入类型→转换→输出类型）
 *   - DependencyGraph：依赖图构建、拓扑排序、循环检测
 *   - AssetPipeline：规则注册、单资产处理、批量构建
 *
 * 使用示例：
 * @code
 *   AssetPipeline pipeline;
 *
 *   // 注册规则
 *   pipeline.RegisterRule(std::make_shared<TextureCompressRule>());
 *   pipeline.RegisterRule(std::make_shared<AudioEncodeRule>());
 *
 *   // 处理单个资产
 *   PipelineResult result = pipeline.Process(entry);
 *
 *   // 批量处理（按拓扑序）
 *   auto report = pipeline.BuildAll(db);
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/AssetDatabase.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <chrono>

namespace Engine {

    // ============================================================
    // 管道阶段
    // ============================================================
    enum class PipelineStage : uint8 {
        None        = 0,
        Validation  = 1,   // 校验源文件完整性
        Preprocess  = 2,   // 预处理（格式检测、元数据提取）
        Compile     = 3,   // 编译/转换（如纹理压缩、音频编码）
        Optimize    = 4,   // 优化（如 LOD 生成、mipmap）
        Package     = 5,   // 打包（最终输出）
        COUNT
    };

    inline const char* PipelineStageName(PipelineStage stage) {
        switch (stage) {
            case PipelineStage::Validation:  return "Validation";
            case PipelineStage::Preprocess:  return "Preprocess";
            case PipelineStage::Compile:     return "Compile";
            case PipelineStage::Optimize:    return "Optimize";
            case PipelineStage::Package:     return "Package";
            default:                         return "None";
        }
    }

    // ============================================================
    // 规则匹配条件
    // ============================================================
    struct RuleMatch {
        std::vector<std::string> extensions;    // 匹配的扩展名（如 "png"、"wav"）
        std::vector<ResourceType> types;         // 匹配的资源类型
        bool matchAllExtensions = false;         // 是否匹配所有扩展名
    };

    // ============================================================
    // 管道上下文（单次 Process 调用）
    // ============================================================
    struct PipelineContext {
        const AssetEntry* entry = nullptr;       // 正在处理的资产条目
        std::string sourcePath;                  // 源文件绝对路径
        std::string outputDir;                   // 输出目录
        std::string intermediateDir;             // 临时文件目录

        // 运行时数据
        std::unordered_map<std::string, std::string> metadata;   // 中间元数据
        std::vector<std::string> generatedFiles;                  // 本次生成的文件

        // 进度回调
        std::function<bool(float progress, const std::string& message)> onProgress;

        // 是否取消
        std::atomic<bool>* cancelFlag = nullptr;
    };

    // ============================================================
    // 管道结果
    // ============================================================
    struct PipelineResult {
        bool success = false;
        std::string errorMessage;
        std::vector<std::string> outputFiles;    // 输出的文件列表
        PipelineStage lastStage = PipelineStage::None;
        double elapsedSeconds = 0.0;
    };

    // ============================================================
    // 资产处理规则基类
    // ============================================================
    class AssetRule {
    public:
        virtual ~AssetRule() = default;

        /** 规则名称（如 "TextureCompress_BC7"） */
        virtual const char* GetName() const noexcept = 0;

        /** 规则描述 */
        virtual const char* GetDescription() const noexcept = 0;

        /** 所属管道阶段 */
        virtual PipelineStage GetStage() const noexcept = 0;

        /** 匹配条件 */
        virtual RuleMatch GetMatch() const = 0;

        /**
         * @brief 执行规则
         * @param ctx 管道上下文
         * @return 是否成功
         *
         * 应处理 ctx.entry 指向的资产，并将输出文件路径
         * 附加到 ctx.generatedFiles。
         */
        virtual bool Execute(PipelineContext& ctx) = 0;

        /**
         * @brief 检查资产是否需要重新处理（增量构建）
         * @param ctx 管道上下文
         * @return true 表示需要重新处理
         *
         * 默认实现：始终返回 true（总是重新处理）。
         * 重写可实现基于时间戳或哈希的增量检查。
         */
        virtual bool NeedsRebuild(PipelineContext& ctx) { return true; }
    };

    // ============================================================
    // 依赖图
    // ============================================================
    struct DepGraphEdge {
        std::string fromGuid;       // 依赖者 GUID
        std::string toGuid;         // 被依赖者 GUID
        bool        isOptional = false;  // 是否为可选依赖
    };

    struct DepGraphNode {
        const AssetEntry* entry = nullptr;
        std::vector<size_t> outgoing;   // 出边索引
        std::vector<size_t> incoming;   // 入边索引
        uint32 depth = 0;               // 拓扑深度（根节点为 0）
    };

    struct DepGraph {
        std::vector<DepGraphNode> nodes;
        std::vector<DepGraphEdge> edges;

        /** 是否为空 */
        bool Empty() const { return nodes.empty(); }
    };

    // ============================================================
    // 循环检测结果
    // ============================================================
    struct CycleInfo {
        std::vector<std::string> cycleGuids;    // 构成循环的 GUID 链
        std::vector<std::string> cyclePaths;    // 对应的路径
        bool HasCycle() const { return !cycleGuids.empty(); }
    };

    // ============================================================
    // 批量构建报告
    // ============================================================
    struct BuildReportEntry {
        std::string guid;
        std::string path;
        bool        success = false;
        std::string error;
        double      elapsedSeconds = 0.0;
        PipelineStage lastStage = PipelineStage::None;
    };

    struct BuildReport {
        std::vector<BuildReportEntry> entries;
        size_t succeeded = 0;
        size_t failed    = 0;
        double totalElapsedSeconds = 0.0;

        bool IsAllSucceeded() const { return failed == 0; }
    };

    // ============================================================
    // 资产调节管道
    // ============================================================
    class AssetPipeline {
    public:
        AssetPipeline() = default;
        ~AssetPipeline() = default;

        AssetPipeline(const AssetPipeline&) = delete;
        AssetPipeline& operator=(const AssetPipeline&) = delete;

        // ── 规则管理 ──

        /** 注册一条处理规则 */
        void RegisterRule(std::shared_ptr<AssetRule> rule);

        /** 移除指定名称的规则 */
        void UnregisterRule(const std::string& name);

        /** 获取所有已注册规则 */
        const std::vector<std::shared_ptr<AssetRule>>& GetRules() const { return m_Rules; }

        /** 获取匹配指定资产的所有规则（按阶段排序） */
        std::vector<std::shared_ptr<AssetRule>> GetMatchingRules(const AssetEntry& entry) const;

        // ── 单资产处理 ──

        /**
         * @brief 对单个资产运行管道
         * @param entry 资产条目
         * @param outputDir 输出目录
         * @param cancelFlag 可选取消标志
         * @return 处理结果
         */
        PipelineResult Process(const AssetEntry& entry,
                               const std::string& outputDir,
                               std::atomic<bool>* cancelFlag = nullptr);

        // ── 批量构建 ──

        /**
         * @brief 对数据库中的所有资产执行批量构建
         * @param db 资产数据库
         * @param outputDir 输出目录
         * @param concurrency 最大并发数（0 = 顺序执行）
         * @return 构建报告
         *
         * 会按拓扑顺序处理（先处理依赖资产，再处理依赖者）。
         */
        BuildReport BuildAll(const AssetDatabase& db,
                             const std::string& outputDir,
                             uint32 concurrency = 0);

        /**
         * @brief 增量构建（仅处理变更或缺失输出的资产）
         * @param db 资产数据库
         * @param outputDir 输出目录
         * @return 构建报告
         */
        BuildReport BuildDirty(const AssetDatabase& db,
                               const std::string& outputDir);

        // ── 依赖图 ──

        /**
         * @brief 构建完整的依赖图
         * @param db 资产数据库
         * @return 依赖图
         */
        DepGraph BuildDependencyGraph(const AssetDatabase& db) const;

        /**
         * @brief 获取指定资产的上下游依赖
         * @param db 资产数据库
         * @param guid 资产 GUID
         * @return 上下游节点列表（first=上游依赖者，second=下游被依赖者）
         */
        std::pair<std::vector<const AssetEntry*>,
                  std::vector<const AssetEntry*>>
        GetDependencyChain(const AssetDatabase& db, const std::string& guid) const;

        /**
         * @brief 检测依赖循环
         * @param db 资产数据库
         * @return 所有检测到的循环
         */
        std::vector<CycleInfo> DetectCycles(const AssetDatabase& db) const;

        // ── 查询 ──

        /** 获取管道状态摘要 */
        struct PipelineSummary {
            size_t totalRules       = 0;
            size_t totalAssets      = 0;
            size_t processedAssets  = 0;
            size_t failedAssets     = 0;
            size_t pendingAssets    = 0;
        };
        PipelineSummary GetSummary() const;

    private:
        // ── 内部辅助 ──

        /** 按拓扑序对资产 GUID 排序 */
        std::vector<std::string> TopologicalSort(const AssetDatabase& db) const;

        /** 执行单个规则并计时 */
        PipelineResult RunRule(AssetRule& rule, PipelineContext& ctx);

        // ── 数据 ──
        std::vector<std::shared_ptr<AssetRule>> m_Rules;

        // 简单处理计数（用于 GetSummary）
        mutable std::atomic<size_t> m_ProcessedCount{0};
        mutable std::atomic<size_t> m_FailedCount{0};
    };

    // ============================================================
    // 内置规则（常用）
    // ============================================================

    /**
     * @brief 纹理验证规则
     * 检查纹理文件是否可被 stb_image 正确解码
     */
    class TextureValidationRule : public AssetRule {
    public:
        const char* GetName() const noexcept override { return "TextureValidation"; }
        const char* GetDescription() const noexcept override {
            return "Validate texture files with stb_image";
        }
        PipelineStage GetStage() const noexcept override { return PipelineStage::Validation; }
        RuleMatch GetMatch() const override;
        bool Execute(PipelineContext& ctx) override;
    };

    /**
     * @brief 纹理预处理规则
     * 将各种格式的纹理统一为 RGBA8 PNG 中间格式
     */
    class TexturePreprocessRule : public AssetRule {
    public:
        const char* GetName() const noexcept override { return "TexturePreprocess"; }
        const char* GetDescription() const noexcept override {
            return "Preprocess textures to RGBA8 PNG intermediate";
        }
        PipelineStage GetStage() const noexcept override { return PipelineStage::Preprocess; }
        RuleMatch GetMatch() const override;
        bool Execute(PipelineContext& ctx) override;
    };

    /**
     * @brief 音频验证规则
     * 检查音频文件是否可被 stb_vorbis / dr_wav 正确解码
     */
    class AudioValidationRule : public AssetRule {
    public:
        const char* GetName() const noexcept override { return "AudioValidation"; }
        const char* GetDescription() const noexcept override {
            return "Validate audio files with stb_vorbis/dr_wav";
        }
        PipelineStage GetStage() const noexcept override { return PipelineStage::Validation; }
        RuleMatch GetMatch() const override;
        bool Execute(PipelineContext& ctx) override;
    };

    /**
     * @brief 着色器编译规则
     * 将 GLSL 源文件编译为 SPIR-V 中间格式
     */
    class ShaderCompileRule : public AssetRule {
    public:
        const char* GetName() const noexcept override { return "ShaderCompile"; }
        const char* GetDescription() const noexcept override {
            return "Compile GLSL shaders to SPIR-V";
        }
        PipelineStage GetStage() const noexcept override { return PipelineStage::Compile; }
        RuleMatch GetMatch() const override;
        bool Execute(PipelineContext& ctx) override;
        bool NeedsRebuild(PipelineContext& ctx) override;
    };

} // namespace Engine
