#include "Engine/Core/Resources/AssetPipeline.h"
#include "Engine/Core/FileSystem.h"
#include <iostream>
#include <algorithm>
#include <queue>
#include <set>
#include <chrono>
#include <cstring>

namespace Engine {

    // ============================================================
    // 内置规则匹配
    // ============================================================

    RuleMatch TextureValidationRule::GetMatch() const {
        return {
            {"png", "jpg", "jpeg", "bmp", "tga", "dds", "hdr", "ktx"},
            {ResourceType::Texture},
            false
        };
    }

    RuleMatch TexturePreprocessRule::GetMatch() const {
        return {
            {"png", "jpg", "jpeg", "bmp", "tga", "dds", "hdr", "ktx"},
            {ResourceType::Texture},
            false
        };
    }

    RuleMatch AudioValidationRule::GetMatch() const {
        return {
            {"wav", "ogg", "mp3", "flac"},
            {ResourceType::AudioClip},
            false
        };
    }

    RuleMatch ShaderCompileRule::GetMatch() const {
        return {
            {"glsl", "vert", "frag", "comp"},
            {ResourceType::Shader},
            false
        };
    }

    // ============================================================
    // TextureValidationRule::Execute
    // ============================================================

    bool TextureValidationRule::Execute(PipelineContext& ctx) {
        if (ctx.onProgress)
            ctx.onProgress(0.0f, "Validating texture: " + ctx.entry->path);

        // 使用 stb_image 的头信息验证（仅检查文件头）
        // 实际项目中应调用 stb_image 的 stbi_info_from_file
        std::string resolved = FileSystem::ResolvePath(ctx.entry->path);
        auto data = FileSystem::ReadFile(resolved);

        if (data.empty()) {
            ctx.metadata["validation_error"] = "File is empty or unreadable";
            return false;
        }

        // 简单的魔数检查
        const uint8* buf = data.data();
        size_t len = data.size();
        bool validMagic = false;

        // PNG: 89 50 4E 47
        if (len >= 4 && buf[0] == 0x89 && buf[1] == 'P' && buf[2] == 'N' && buf[3] == 'G')
            validMagic = true;
        // JPEG: FF D8 FF
        else if (len >= 3 && buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF)
            validMagic = true;
        // BMP: 42 4D
        else if (len >= 2 && buf[0] == 'B' && buf[1] == 'M')
            validMagic = true;
        // DDS: 44 44 53 20
        else if (len >= 4 && buf[0] == 'D' && buf[1] == 'D' && buf[2] == 'S' && buf[3] == ' ')
            validMagic = true;
        // HDR: 23 3F 52 41 44 49 41 4E 43 45 0A  ( #?RADIANCE\n )
        else if (len >= 11 && buf[0] == '#' && buf[1] == '?' &&
                 std::memcmp(buf + 2, "RADIANCE", 8) == 0)
            validMagic = true;
        // KTX: AB 4B 54 58 20
        else if (len >= 4 && buf[0] == 0xAB && buf[1] == 'K' && buf[2] == 'T' && buf[3] == 'X')
            validMagic = true;
        else {
            ctx.metadata["validation_error"] = "Unknown or invalid image format";
            return false;
        }

        ctx.metadata["validation_passed"] = "true";
        ctx.metadata["file_size"] = std::to_string(data.size());

        if (ctx.onProgress)
            ctx.onProgress(1.0f, "Texture validated: " + ctx.entry->path);

        return true;
    }

    // ============================================================
    // TexturePreprocessRule::Execute
    // ============================================================

    bool TexturePreprocessRule::Execute(PipelineContext& ctx) {
        if (ctx.onProgress)
            ctx.onProgress(0.0f, "Preprocessing texture: " + ctx.entry->path);

        // 该规则为占位——在实际引擎中，此处应调用 stb_image 解码
        // 然后以 RGBA8 PNG 格式写入中间目录
        //
        // 示例流程：
        //   1. stbi_load(sourcePath, &w, &h, &ch, 4) → RGBA8
        //   2. 使用 stb_image_write 将像素数据写入 intermediateDir/xxx.png
        //   3. ctx.generatedFiles.push_back(outputPath)

        std::string stem = ctx.entry->fileName;
        // 去掉扩展名
        auto dotPos = stem.find_last_of('.');
        if (dotPos != std::string::npos)
            stem = stem.substr(0, dotPos);

        std::string outputPath = ctx.intermediateDir + "/" + stem + ".rgba8.png";
        ctx.generatedFiles.push_back(outputPath);
        ctx.metadata["preprocessed"] = outputPath;

        if (ctx.onProgress)
            ctx.onProgress(1.0f, "Texture preprocessed: " + stem);

        return true;
    }

    // ============================================================
    // AudioValidationRule::Execute
    // ============================================================

    bool AudioValidationRule::Execute(PipelineContext& ctx) {
        if (ctx.onProgress)
            ctx.onProgress(0.0f, "Validating audio: " + ctx.entry->path);

        std::string resolved = FileSystem::ResolvePath(ctx.entry->path);
        auto data = FileSystem::ReadFile(resolved);

        if (data.empty()) {
            ctx.metadata["validation_error"] = "File is empty or unreadable";
            return false;
        }

        // 检查音频文件魔数
        const uint8* buf = data.data();
        size_t len = data.size();
        bool validMagic = false;

        // WAV: 52 49 46 46 (RIFF)
        if (len >= 4 && buf[0] == 'R' && buf[1] == 'I' && buf[2] == 'F' && buf[3] == 'F')
            validMagic = true;
        // Ogg: 4F 67 67 53 (OggS)
        else if (len >= 4 && buf[0] == 'O' && buf[1] == 'g' && buf[2] == 'g' && buf[3] == 'S')
            validMagic = true;
        // MP3: ID3 或 FF FB
        else if (len >= 3 &&
                 (buf[0] == 'I' && buf[1] == 'D' && buf[2] == '3') ||
                 (buf[0] == 0xFF && (buf[1] & 0xE0) == 0xE0))
            validMagic = true;
        // FLAC: 66 4C 61 43 (fLaC)
        else if (len >= 4 && buf[0] == 'f' && buf[1] == 'L' && buf[2] == 'a' && buf[3] == 'C')
            validMagic = true;
        else {
            ctx.metadata["validation_error"] = "Unknown or invalid audio format";
            return false;
        }

        ctx.metadata["validation_passed"] = "true";

        if (ctx.onProgress)
            ctx.onProgress(1.0f, "Audio validated: " + ctx.entry->path);

        return true;
    }

    // ============================================================
    // ShaderCompileRule::Execute
    // ============================================================

    bool ShaderCompileRule::Execute(PipelineContext& ctx) {
        if (ctx.onProgress)
            ctx.onProgress(0.0f, "Compiling shader: " + ctx.entry->path);

        // 占位：实际的 GLSL→SPIR-V 编译需要调用 glslangValidator
        // 或 shaderc 库。在此仅模拟编译过程。
        //
        // 真实实现：
        //   1. 读取 GLSL 源文件
        //   2. 调用 shaderc::Compiler 编译为 SPIR-V
        //   3. 写入 .spv 文件到输出目录

        std::string stem = ctx.entry->fileName;
        auto dotPos = stem.find_last_of('.');
        if (dotPos != std::string::npos)
            stem = stem.substr(0, dotPos);

        std::string outputPath = ctx.outputDir + "/" + stem + ".spv";
        ctx.generatedFiles.push_back(outputPath);
        ctx.metadata["compiled_spv"] = outputPath;

        // 模拟编译成功
        if (ctx.onProgress)
            ctx.onProgress(1.0f, "Shader compiled: " + stem);

        return true;
    }

    bool ShaderCompileRule::NeedsRebuild(PipelineContext& ctx) {
        std::string resolved = FileSystem::ResolvePath(ctx.entry->path);

        // 检查源文件是否存在
        if (!FileSystem::Exists(resolved))
            return false;

        // 检查是否有对应的 .spv 输出
        std::string stem = ctx.entry->fileName;
        auto dotPos = stem.find_last_of('.');
        if (dotPos != std::string::npos)
            stem = stem.substr(0, dotPos);
        std::string spvPath = ctx.outputDir + "/" + stem + ".spv";
        std::string resolvedSpv = FileSystem::ResolvePath(spvPath);

        if (!FileSystem::Exists(resolvedSpv))
            return true; // 没有输出，需要编译

        // 比较时间戳
        int64 srcTime = FileSystem::GetLastWriteTime(resolved);
        int64 dstTime = FileSystem::GetLastWriteTime(resolvedSpv);
        return srcTime > dstTime; // 源文件更新，需要重新编译
    }

    // ============================================================
    // 规则管理
    // ============================================================

    void AssetPipeline::RegisterRule(std::shared_ptr<AssetRule> rule) {
        if (!rule) return;

        // 避免重复注册同名规则
        auto it = std::find_if(m_Rules.begin(), m_Rules.end(),
            [&](const std::shared_ptr<AssetRule>& r) {
                return std::strcmp(r->GetName(), rule->GetName()) == 0;
            });
        if (it != m_Rules.end()) {
            std::cerr << "[AssetPipeline] Rule already registered: "
                      << rule->GetName() << ", replacing." << std::endl;
            *it = rule;
            return;
        }

        m_Rules.push_back(std::move(rule));
        // 按阶段排序
        std::sort(m_Rules.begin(), m_Rules.end(),
            [](const auto& a, const auto& b) {
                return static_cast<uint8>(a->GetStage()) <
                       static_cast<uint8>(b->GetStage());
            });
    }

    void AssetPipeline::UnregisterRule(const std::string& name) {
        auto it = std::remove_if(m_Rules.begin(), m_Rules.end(),
            [&](const std::shared_ptr<AssetRule>& r) {
                return name == r->GetName();
            });
        m_Rules.erase(it, m_Rules.end());
    }

    std::vector<std::shared_ptr<AssetRule>>
    AssetPipeline::GetMatchingRules(const AssetEntry& entry) const {
        std::vector<std::shared_ptr<AssetRule>> matched;

        for (const auto& rule : m_Rules) {
            const auto& match = rule->GetMatch();

            // 检查类型匹配
            bool typeMatch = match.matchAllExtensions;
            if (!typeMatch) {
                for (auto rt : match.types) {
                    if (rt == entry.type) {
                        typeMatch = true;
                        break;
                    }
                }
            }

            if (!typeMatch) continue;

            // 检查扩展名匹配
            bool extMatch = match.matchAllExtensions;
            if (!extMatch) {
                for (const auto& ext : match.extensions) {
                    if (ext == entry.extension) {
                        extMatch = true;
                        break;
                    }
                }
            }

            if (!extMatch) continue;

            matched.push_back(rule);
        }

        return matched;
    }

    // ============================================================
    // 单资产处理
    // ============================================================

    PipelineResult AssetPipeline::Process(const AssetEntry& entry,
                                          const std::string& outputDir,
                                          std::atomic<bool>* cancelFlag) {
        PipelineResult result;
        auto startTime = std::chrono::steady_clock::now();

        // 获取匹配规则
        auto rules = GetMatchingRules(entry);
        if (rules.empty()) {
            result.success = true;
            result.lastStage = PipelineStage::None;
            result.elapsedSeconds = 0.0;
            return result;
        }

        // 准备上下文
        PipelineContext ctx;
        ctx.entry = &entry;
        ctx.sourcePath = FileSystem::ResolvePath(entry.path);
        ctx.outputDir = outputDir;
        ctx.intermediateDir = outputDir + "/.intermediate";
        ctx.cancelFlag = cancelFlag;

        // 确保中间目录存在
        FileSystem::CreateDirectory(ctx.intermediateDir);

        // 按阶段执行每条规则
        for (const auto& rule : rules) {
            if (cancelFlag && cancelFlag->load(std::memory_order_acquire)) {
                result.errorMessage = "Cancelled";
                result.lastStage = rule->GetStage();
                return result;
            }

            // 检查是否需要重新处理（增量构建）
            if (!rule->NeedsRebuild(ctx)) {
                // 已是最新，跳过
                result.lastStage = rule->GetStage();
                continue;
            }

            bool ruleOk = RunRule(*rule, ctx).success;
            if (!ruleOk) {
                result.success = false;
                result.errorMessage = "Rule '" + std::string(rule->GetName()) +
                                      "' failed for " + entry.path;
                result.lastStage = rule->GetStage();
                auto endTime = std::chrono::steady_clock::now();
                result.elapsedSeconds = std::chrono::duration<double>(
                    endTime - startTime).count();

                m_FailedCount++;
                return result;
            }

            result.lastStage = rule->GetStage();
        }

        // 收集输出文件
        result.outputFiles = std::move(ctx.generatedFiles);
        result.success = true;

        auto endTime = std::chrono::steady_clock::now();
        result.elapsedSeconds = std::chrono::duration<double>(
            endTime - startTime).count();

        m_ProcessedCount++;
        return result;
    }

    PipelineResult AssetPipeline::RunRule(AssetRule& rule, PipelineContext& ctx) {
        PipelineResult result;

        auto startTime = std::chrono::steady_clock::now();
        bool ok = rule.Execute(ctx);
        auto endTime = std::chrono::steady_clock::now();

        result.success = ok;
        result.lastStage = rule.GetStage();
        result.elapsedSeconds = std::chrono::duration<double>(
            endTime - startTime).count();

        if (!ok) {
            auto it = ctx.metadata.find("validation_error");
            if (it != ctx.metadata.end())
                result.errorMessage = it->second;
        }

        return result;
    }

    // ============================================================
    // 依赖图
    // ============================================================

    DepGraph AssetPipeline::BuildDependencyGraph(const AssetDatabase& db) const {
        DepGraph graph;
        const auto& allEntries = db.GetAllEntries();

        // 建立 GUID → 节点索引的映射
        std::unordered_map<std::string, size_t> guidToNode;
        guidToNode.reserve(allEntries.size());

        // 创建节点
        for (size_t i = 0; i < allEntries.size(); ++i) {
            DepGraphNode node;
            node.entry = &allEntries[i];
            guidToNode[allEntries[i].guid] = graph.nodes.size();
            graph.nodes.push_back(std::move(node));
        }

        // 创建边
        for (size_t i = 0; i < allEntries.size(); ++i) {
            const auto& entry = allEntries[i];
            size_t fromIndex = guidToNode[entry.guid];

            for (const auto& depGuid : entry.dependencies) {
                auto it = guidToNode.find(depGuid);
                if (it == guidToNode.end()) continue; // 损坏的引用

                size_t toIndex = it->second;
                DepGraphEdge edge;
                edge.fromGuid = entry.guid;
                edge.toGuid   = depGuid;

                graph.edges.push_back(std::move(edge));
                size_t edgeIndex = graph.edges.size() - 1;

                graph.nodes[fromIndex].outgoing.push_back(edgeIndex);
                graph.nodes[toIndex].incoming.push_back(edgeIndex);
            }
        }

        // BFS 计算拓扑深度
        std::queue<size_t> q;
        for (size_t i = 0; i < graph.nodes.size(); ++i) {
            if (graph.nodes[i].incoming.empty()) {
                graph.nodes[i].depth = 0;
                q.push(i);
            }
        }

        while (!q.empty()) {
            size_t idx = q.front(); q.pop();
            for (size_t edgeIdx : graph.nodes[idx].outgoing) {
                size_t toNode = guidToNode[graph.edges[edgeIdx].toGuid];
                uint32 newDepth = graph.nodes[idx].depth + 1;
                if (newDepth > graph.nodes[toNode].depth) {
                    graph.nodes[toNode].depth = newDepth;
                    q.push(toNode);
                }
            }
        }

        return graph;
    }

    std::pair<std::vector<const AssetEntry*>,
              std::vector<const AssetEntry*>>
    AssetPipeline::GetDependencyChain(const AssetDatabase& db,
                                       const std::string& guid) const {
        // upstream: 依赖此资产的条目（引用者）
        // downstream: 此资产依赖的条目（被引用者）
        std::vector<const AssetEntry*> upstream, downstream;

        const auto* entry = db.FindByGuid(guid);
        if (!entry) return {upstream, downstream};

        const auto& allEntries = db.GetAllEntries();

        // 上游：谁引用了这个资产
        for (const auto& e : allEntries) {
            for (const auto& dep : e.dependencies) {
                if (dep == guid) {
                    upstream.push_back(&e);
                    break;
                }
            }
        }

        // 下游：这个资产引用了谁
        for (const auto& depGuid : entry->dependencies) {
            const auto* depEntry = db.FindByGuid(depGuid);
            if (depEntry)
                downstream.push_back(depEntry);
        }

        return {upstream, downstream};
    }

    // ============================================================
    // 循环检测（DFS）
    // ============================================================

    std::vector<CycleInfo> AssetPipeline::DetectCycles(const AssetDatabase& db) const {
        const auto& allEntries = db.GetAllEntries();
        std::unordered_map<std::string, std::vector<std::string>> adjList;

        // 构建邻接表
        for (const auto& entry : allEntries) {
            for (const auto& dep : entry.dependencies) {
                adjList[entry.guid].push_back(dep);
            }
        }

        std::vector<CycleInfo> cycles;
        std::unordered_set<std::string> visited;
        std::unordered_set<std::string> inStack;
        std::vector<std::string> path;

        std::function<bool(const std::string&)> dfs =
            [&](const std::string& guid) -> bool {
                visited.insert(guid);
                inStack.insert(guid);
                path.push_back(guid);

                auto it = adjList.find(guid);
                if (it != adjList.end()) {
                    for (const auto& neighbor : it->second) {
                        if (inStack.count(neighbor)) {
                            // 找到循环
                            CycleInfo cycle;
                            auto pos = std::find(path.begin(), path.end(), neighbor);
                            for (auto p = pos; p != path.end(); ++p) {
                                cycle.cycleGuids.push_back(*p);
                                const auto* e = db.FindByGuid(*p);
                                cycle.cyclePaths.push_back(e ? e->path : "?");
                            }
                            cycles.push_back(std::move(cycle));
                            return false; // 继续检测其他循环
                        }
                        if (!visited.count(neighbor)) {
                            if (!dfs(neighbor)) {
                                // 继续但不停止
                            }
                        }
                    }
                }

                path.pop_back();
                inStack.erase(guid);
                return true;
            };

        for (const auto& entry : allEntries) {
            if (!visited.count(entry.guid)) {
                dfs(entry.guid);
            }
        }

        return cycles;
    }

    // ============================================================
    // 拓扑排序（Kahn 算法）
    // ============================================================

    std::vector<std::string> AssetPipeline::TopologicalSort(const AssetDatabase& db) const {
        const auto& allEntries = db.GetAllEntries();

        // 构建入度表
        std::unordered_map<std::string, size_t> inDegree;
        std::unordered_map<std::string, std::vector<std::string>> outEdges;

        for (const auto& entry : allEntries) {
            if (!inDegree.count(entry.guid))
                inDegree[entry.guid] = 0;

            for (const auto& dep : entry.dependencies) {
                outEdges[dep].push_back(entry.guid);
                inDegree[entry.guid]++;
            }
        }

        // 入度为 0 的节点入队
        std::queue<std::string> q;
        for (const auto& [guid, deg] : inDegree) {
            if (deg == 0) q.push(guid);
        }

        std::vector<std::string> result;
        while (!q.empty()) {
            auto guid = q.front(); q.pop();
            result.push_back(guid);

            auto it = outEdges.find(guid);
            if (it != outEdges.end()) {
                for (const auto& next : it->second) {
                    if (--inDegree[next] == 0)
                        q.push(next);
                }
            }
        }

        // 如果 result 长度小于总节点数，说明有循环依赖
        // 此时将剩余节点附加到末尾（不完美但可用）
        if (result.size() < inDegree.size()) {
            for (const auto& [guid, _] : inDegree) {
                if (std::find(result.begin(), result.end(), guid) == result.end())
                    result.push_back(guid);
            }
        }

        // 反转：依赖者在前，被依赖者在后
        // 这样处理时先处理被依赖者（叶子节点）
        std::reverse(result.begin(), result.end());

        return result;
    }

    // ============================================================
    // 批量构建
    // ============================================================

    BuildReport AssetPipeline::BuildAll(const AssetDatabase& db,
                                        const std::string& outputDir,
                                        uint32 concurrency) {
        BuildReport report;
        auto startTime = std::chrono::steady_clock::now();

        // 获取拓扑排序
        auto order = TopologicalSort(db);
        report.entries.reserve(order.size());

        if (concurrency == 0) {
            // 顺序执行
            for (const auto& guid : order) {
                const auto* entry = db.FindByGuid(guid);
                if (!entry) continue;

                auto result = Process(*entry, outputDir);

                BuildReportEntry bre;
                bre.guid = entry->guid;
                bre.path = entry->path;
                bre.success = result.success;
                bre.error = result.errorMessage;
                bre.elapsedSeconds = result.elapsedSeconds;
                bre.lastStage = result.lastStage;

                report.entries.push_back(std::move(bre));
                if (bre.success)
                    report.succeeded++;
                else
                    report.failed++;
            }
        } else {
            // 并发执行（简化：使用简单的线程池）
            std::atomic<size_t> nextIndex{0};
            std::vector<std::thread> workers;
            std::mutex reportMutex;

            for (uint32 i = 0; i < concurrency; ++i) {
                workers.emplace_back([&]() {
                    while (true) {
                        size_t idx = nextIndex.fetch_add(1);
                        if (idx >= order.size()) break;

                        const auto* entry = db.FindByGuid(order[idx]);
                        if (!entry) continue;

                        auto result = Process(*entry, outputDir);

                        BuildReportEntry bre;
                        bre.guid = entry->guid;
                        bre.path = entry->path;
                        bre.success = result.success;
                        bre.error = result.errorMessage;
                        bre.elapsedSeconds = result.elapsedSeconds;
                        bre.lastStage = result.lastStage;

                        {
                            std::lock_guard<std::mutex> lock(reportMutex);
                            report.entries.push_back(std::move(bre));
                            if (bre.success)
                                report.succeeded++;
                            else
                                report.failed++;
                        }
                    }
                });
            }

            for (auto& w : workers)
                if (w.joinable()) w.join();
        }

        auto endTime = std::chrono::steady_clock::now();
        report.totalElapsedSeconds = std::chrono::duration<double>(
            endTime - startTime).count();

        std::cout << "[AssetPipeline] BuildAll: " << report.succeeded
                  << " succeeded, " << report.failed << " failed in "
                  << report.totalElapsedSeconds << "s" << std::endl;

        return report;
    }

    BuildReport AssetPipeline::BuildDirty(const AssetDatabase& db,
                                           const std::string& outputDir) {
        BuildReport report;
        auto startTime = std::chrono::steady_clock::now();

        const auto& allEntries = db.GetAllEntries();

        for (const auto& entry : allEntries) {
            auto rules = GetMatchingRules(entry);
            if (rules.empty()) continue;

            // 检查是否有任何规则需要重构建
            bool needsRebuild = false;

            PipelineContext ctx;
            ctx.entry = &entry;
            ctx.sourcePath = FileSystem::ResolvePath(entry.path);
            ctx.outputDir = outputDir;
            ctx.intermediateDir = outputDir + "/.intermediate";

            for (const auto& rule : rules) {
                if (rule->NeedsRebuild(ctx)) {
                    needsRebuild = true;
                    break;
                }
            }

            if (!needsRebuild) continue;

            auto result = Process(entry, outputDir);

            BuildReportEntry bre;
            bre.guid = entry.guid;
            bre.path = entry.path;
            bre.success = result.success;
            bre.error = result.errorMessage;
            bre.elapsedSeconds = result.elapsedSeconds;
            bre.lastStage = result.lastStage;

            report.entries.push_back(std::move(bre));
            if (bre.success)
                report.succeeded++;
            else
                report.failed++;
        }

        auto endTime = std::chrono::steady_clock::now();
        report.totalElapsedSeconds = std::chrono::duration<double>(
            endTime - startTime).count();

        std::cout << "[AssetPipeline] BuildDirty: " << report.succeeded
                  << " succeeded, " << report.failed << " failed in "
                  << report.totalElapsedSeconds << "s" << std::endl;

        return report;
    }

    // ============================================================
    // 摘要
    // ============================================================

    AssetPipeline::PipelineSummary AssetPipeline::GetSummary() const {
        PipelineSummary summary;
        summary.totalRules = m_Rules.size();
        summary.processedAssets = m_ProcessedCount.load();
        summary.failedAssets = m_FailedCount.load();
        return summary;
    }

} // namespace Engine
