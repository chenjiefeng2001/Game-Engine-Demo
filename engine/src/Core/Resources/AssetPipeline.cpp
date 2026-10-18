#include "Engine/Core/Resources/AssetPipeline.h"
#include "Engine/Core/FileSystem.h"
#include "Engine/Core/Log.h"
#include <algorithm>

namespace {
    Engine::Logger s_Log("AssetPipeline");
}
#include <queue>
#include <set>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iterator>
// ShaderStageType 住在与后端无关的 RenderResources 里；stage 推断必须在
// ENGINE_HAS_VULKAN 守卫**之外**可用，否则「不认识扩展名」与「没有 Vulkan 后端」
// 这两种完全不同的原因会被混为一谈。
#include "Engine/Core/RenderResources/ShaderStage.h"
#if defined(ENGINE_HAS_VULKAN)
#include "Engine/Vulkan/ShaderRegistry.h"
#endif

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

        std::string stem = ctx.entry->fileName;
        auto dotPos = stem.find_last_of('.');
        if (dotPos != std::string::npos)
            stem = stem.substr(0, dotPos);

        // ── stage 推断：不猜 ──────────────────────────────────────────────
        // 无法识别的扩展名不猜，避免把顶点着色器当成片元着色器登记后静默产出
        // 错误的管线。识别不出时本规则**不适用**，因此返回 true 但**不产出任何
        // 文件**——此前正是在这一步之前就 push_back 了 .spv 路径，于是报告里出现
        // 一个从未被编译、也从未被写出的"产物"。
        const std::string ext = [&] {
            const auto dot = ctx.entry->fileName.find_last_of('.');
            return dot == std::string::npos
                ? std::string()
                : ctx.entry->fileName.substr(dot + 1);
        }();
        ShaderStageType stage;
        if      (ext == "vert" || ext == "vs")   stage = ShaderStageType::Vertex;
        else if (ext == "frag" || ext == "fs")   stage = ShaderStageType::Fragment;
        else if (ext == "comp" || ext == "cs")   stage = ShaderStageType::Compute;
        else if (ext == "geom" || ext == "gs")   stage = ShaderStageType::Geometry;
        else if (ext == "tesc" || ext == "tcs")  stage = ShaderStageType::TessControl;
        else if (ext == "tese" || ext == "tes")  stage = ShaderStageType::TessEvaluation;
        else {
            if (ctx.onProgress) {
                ctx.onProgress(1.0f, "Shader stage unknown for " + ctx.entry->fileName +
                                     " (extension '" + ext + "'); nothing was compiled");
            }
            return true;
        }

        // ctx.sourcePath 由 Process 解析过；缺省时退回 entry->path，保持单点真相。
        const std::string sourcePath = FileSystem::ResolvePath(
            ctx.sourcePath.empty() ? ctx.entry->path : ctx.sourcePath);
        std::ifstream src(sourcePath, std::ios::binary);
        if (!src) {
            if (ctx.onProgress)
                ctx.onProgress(1.0f, "Shader source unreadable: " + sourcePath);
            // 源文件读不到是**失败**：NeedsRebuild 对缺失源文件返回 false（当作
            // 「不需要重建」），于是这里也返回 true 就形成了一条静默跳过。
            return false;
        }
        const std::string glsl((std::istreambuf_iterator<char>(src)),
                               std::istreambuf_iterator<char>());

        // ── SPIR-V：编译、登记、真正落盘 ───────────────────────────────────
        // ShaderRegistry 位于 engine/src/Vulkan，仅在 Vulkan_FOUND 时编译，
        // 故必须用 ENGINE_HAS_VULKAN 守卫，否则无 Vulkan 的构建会链接失败。
        //
        // 副作用边界（重要）：**SPIR-V 编译器目前就住在 Vulkan 后端里**
        // （engine/src/Vulkan/ShaderCompiler.cpp 的 ShaderCompiler 是文件私有类）。
        // 因此本规则在「有 shaderc 但无 Vulkan」的构建里无法产出 .spv，只能
        // 如实报告。把它提到与后端无关的位置是后续一项独立工作。
#if defined(ENGINE_HAS_VULKAN)
        // ShaderRegistry 位于 Engine::RHI，而本文件在 Engine 命名空间内，需显式限定。
        if (!RHI::ShaderRegistry::Get().RegisterSource(stem, stage, glsl)) {
            if (ctx.onProgress)
                ctx.onProgress(1.0f, "Shader compile failed: " + stem);
            // 此前编译失败只打印一行，然后照样 return true，于是「GLSL 写错了」
            // 会得到 success=true **加上**一个 .spv 条目。
            return false;
        }

        // 真正写出 .spv。注册表是唯一的 SPIR-V 出处（Find 是公开的唯一取词途径），
        // 因此落盘的就是刚刚交给 vkCreateShaderModule 的那份字节。
        const std::vector<uint32_t>* words =
            RHI::ShaderRegistry::Get().Find(StringID::Runtime(stem), stage);
        if (!words || words->empty()) {
            if (ctx.onProgress)
                ctx.onProgress(1.0f, "Shader compiled but absent from the registry: " + stem);
            return false;
        }

        const std::string outputPath = ctx.outputDir + "/" + stem + ".spv";
        // WriteFile 自己会建父目录，但显式建一次能让「目录建不出来」以明确原因
        // 失败，而不是退化成写入失败。Process 只创建 outputDir/.intermediate，
        // 从不创建 outputDir 本身。
        if (!FileSystem::CreateDirectory(ctx.outputDir)) {
            if (ctx.onProgress)
                ctx.onProgress(1.0f, "Cannot create shader output directory: " + ctx.outputDir);
            return false;
        }

        // SPIR-V 是 32 位字流；逐字拷贝而不是把 vector<uint32> 当字节数组，
        // 这样不依赖宿主字节序假设。
        std::vector<uint8> bytes;
        bytes.reserve(words->size() * sizeof(uint32_t));
        for (uint32_t word : *words) {
            bytes.push_back(static_cast<uint8>(word & 0xFFu));
            bytes.push_back(static_cast<uint8>((word >> 8) & 0xFFu));
            bytes.push_back(static_cast<uint8>((word >> 16) & 0xFFu));
            bytes.push_back(static_cast<uint8>((word >> 24) & 0xFFu));
        }
        if (!FileSystem::WriteFile(outputPath, bytes)) {
            if (ctx.onProgress)
                ctx.onProgress(1.0f, "Cannot write SPIR-V: " + outputPath);
            return false;
        }

        // 只有文件真的存在才登记为产物。
        ctx.generatedFiles.push_back(outputPath);
        ctx.metadata["compiled_spv"] = outputPath;
        if (ctx.onProgress)
            ctx.onProgress(1.0f, "Shader compiled and written: " + outputPath);
#else
        if (ctx.onProgress) {
            ctx.onProgress(1.0f, "No Vulkan backend in this build: no SPIR-V produced for " + stem);
        }
#endif

        return true;
    }

    bool ShaderCompileRule::NeedsRebuild(PipelineContext& ctx) {
        std::string resolved = FileSystem::ResolvePath(
            ctx.sourcePath.empty() ? ctx.entry->path : ctx.sourcePath);

        // 源文件不存在时返回 true（需要重建），而不是 false。
        // Process 把 !NeedsRebuild 当作「跳过这条规则」，于是此前这条路径让整个
        // 编译**静默跳过**：源文件被删掉或路径写错，既不编译也不报错。
        if (!FileSystem::Exists(resolved))
            return true;

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
            s_Log.Warn("Rule already registered: {}, replacing.", rule->GetName());
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

        s_Log.Info("BuildAll: {} succeeded, {} failed in {}s", report.succeeded, report.failed, report.totalElapsedSeconds);

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

        s_Log.Info("BuildDirty: {} succeeded, {} failed in {}s", report.succeeded, report.failed, report.totalElapsedSeconds);

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
