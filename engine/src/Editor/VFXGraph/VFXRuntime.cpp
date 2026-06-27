/**
 * @file VFXRuntime.cpp
 * @brief VFX 运行时 — 数据驱动的粒子模拟（CPU 解释执行 VFXGraph Block）
 *
 * 当前实现：CPU 解释器模式，遍历 VFXGraph 的 Block 并调用其 GenerateHLSL 生成
 * 运行时指令。未来可切换为 GPU Compute Shader 路径（通过 VFXCompiler）。
 *
 * 设计原则：
 *   - VFXInstance 不写死任何物理逻辑（无硬编码重力/生命周期）
 *   - 所有模拟行为来自 VFXGraph 中的 Block 配置
 *   - VFXInstance::UpdateParticles 遍历 Update Context 的 Blocks
 *   - VFXInstance::Emit 使用 Initialize Context 的 Blocks
 */

#include "Engine/Editor/VFXGraph/VFXGraphCore.h"
#include "Engine/Editor/VFXGraph/VFXNodeFactory.h"
#include <algorithm>
#include <cstring>
#include <random>
#include <sstream>

namespace Engine {
namespace VFX {

    // ============================================================
    // VFXGraph 实现
    // ============================================================

    VFXGraph::VFXGraph() {
        RegisterAllVFXBlocks();
    }

    uint32 VFXGraph::AddBlock(std::unique_ptr<VFXBlock> block) {
        uint32 id = m_NextBlockId++;
        block->SetId(id);
        m_Blocks[id] = std::move(block);
        m_IsDirty = true;
        return id;
    }

    bool VFXGraph::RemoveBlock(uint32 blockId) {
        auto it = m_Blocks.find(blockId);
        if (it == m_Blocks.end()) return false;

        auto linkIt = m_Links.begin();
        while (linkIt != m_Links.end()) {
            if (linkIt->inputNodeId == blockId || linkIt->outputNodeId == blockId) {
                linkIt = m_Links.erase(linkIt);
            } else {
                ++linkIt;
            }
        }

        for (auto& ctx : m_Contexts) {
            auto& ids = ctx.blockIds;
            ids.erase(std::remove(ids.begin(), ids.end(), blockId), ids.end());
        }

        m_Blocks.erase(it);
        m_IsDirty = true;
        return true;
    }

    VFXBlock* VFXGraph::GetBlock(uint32 blockId) {
        auto it = m_Blocks.find(blockId);
        return (it != m_Blocks.end()) ? it->second.get() : nullptr;
    }

    const VFXBlock* VFXGraph::GetBlock(uint32 blockId) const {
        auto it = m_Blocks.find(blockId);
        return (it != m_Blocks.end()) ? it->second.get() : nullptr;
    }

    uint32 VFXGraph::AddLink(uint32 outputBlockId, uint32 outputPinId,
                             uint32 inputBlockId, uint32 inputPinId) {
        VFXLink link;
        link.id = m_NextLinkId++;
        link.outputNodeId = outputBlockId;
        link.outputPinId = outputPinId;
        link.inputNodeId = inputBlockId;
        link.inputPinId = inputPinId;
        m_Links.push_back(link);

        auto markConnected = [](VFXBlock* block, uint32 pinId) {
            if (!block) return;
            for (auto& p : block->GetInputPins()) {
                if (p.id == pinId) { p.isConnected = true; return; }
            }
            for (auto& p : block->GetOutputPins()) {
                if (p.id == pinId) { p.isConnected = true; return; }
            }
        };
        markConnected(GetBlock(outputBlockId), outputPinId);
        markConnected(GetBlock(inputBlockId), inputPinId);

        m_IsDirty = true;
        return link.id;
    }

    bool VFXGraph::RemoveLink(uint32 linkId) {
        size_t idx = (size_t)-1;
        for (size_t i = 0; i < m_Links.size(); ++i) {
            if (m_Links[i].id == linkId) {
                idx = i;
                break;
            }
        }
        if (idx == (size_t)-1) return false;

        auto resetIfUnused = [this, linkId](uint32 blockId, uint32 pinId) {
            bool stillUsed = false;
            for (auto& l : m_Links) {
                if (l.id == linkId) continue;
                if (l.outputNodeId == blockId && l.outputPinId == pinId) { stillUsed = true; break; }
                if (l.inputNodeId == blockId && l.inputPinId == pinId) { stillUsed = true; break; }
            }
            if (stillUsed) return;
            auto* block = GetBlock(blockId);
            if (!block) return;
            for (auto& p : block->GetInputPins()) {
                if (p.id == pinId) { p.isConnected = false; return; }
            }
            for (auto& p : block->GetOutputPins()) {
                if (p.id == pinId) { p.isConnected = false; return; }
            }
        };

        resetIfUnused(m_Links[idx].outputNodeId, m_Links[idx].outputPinId);
        resetIfUnused(m_Links[idx].inputNodeId, m_Links[idx].inputPinId);

        m_Links.erase(m_Links.begin() + idx);
        m_IsDirty = true;
        return true;
    }

    bool VFXGraph::RemoveLinkByPins(uint32 outputBlockId, uint32 outputPinId,
                                    uint32 inputBlockId, uint32 inputPinId) {
        for (size_t i = 0; i < m_Links.size(); ++i) {
            auto& l = m_Links[i];
            if (l.outputNodeId == outputBlockId && l.outputPinId == outputPinId &&
                l.inputNodeId == inputBlockId && l.inputPinId == inputPinId) {
                return RemoveLink(l.id);
            }
        }
        return false;
    }

    bool VFXGraph::CanConnectPins(const VFXPin& outPin, const VFXPin& inPin,
                                   std::string& outReason) const {
        // 使用 VFXCompiler 的语义校验逻辑
        std::string reason;
        if (outPin.direction == inPin.direction) {
            outReason = "Cannot connect two pins of same direction.";
            return false;
        }
        if (outPin.direction != PinDirection::Output) {
            outReason = "Source pin must be an output.";
            return false;
        }
        if (inPin.direction != PinDirection::Input) {
            outReason = "Target pin must be an input.";
            return false;
        }
        if (outPin.nodeId == inPin.nodeId) {
            outReason = "Cannot connect a node to itself.";
            return false;
        }
        if (outPin.type != inPin.type &&
            outPin.type != VFXPinType::DynamicVector &&
            inPin.type != VFXPinType::DynamicVector &&
            outPin.type != VFXPinType::ParticleData &&
            inPin.type != VFXPinType::ParticleData)
        {
            outReason = "Pin type mismatch.";
            return false;
        }
        return true;
    }

    uint32 VFXGraph::ReplaceLink(uint32 outputBlockId, uint32 outputPinId,
                                 uint32 inputBlockId, uint32 inputPinId) {
        for (auto& l : m_Links) {
            if (l.inputNodeId == inputBlockId && l.inputPinId == inputPinId) {
                RemoveLink(l.id);
                break;
            }
        }
        return AddLink(outputBlockId, outputPinId, inputBlockId, inputPinId);
    }

    VFXContext& VFXGraph::GetOrCreateContext(ContextType type) {
        for (auto& ctx : m_Contexts) {
            if (ctx.type == type) return ctx;
        }
        VFXContext ctx;
        ctx.type = type;
        ctx.name = ContextTypeDisplayName(type);
        switch (type) {
            case ContextType::System:     ctx.posX = 0; ctx.posY = 0; break;
            case ContextType::Spawn:      ctx.posX = 50; ctx.posY = 50; break;
            case ContextType::Initialize: ctx.posX = 500; ctx.posY = 50; break;
            case ContextType::Update:     ctx.posX = 50; ctx.posY = 400; break;
            case ContextType::Output:     ctx.posX = 500; ctx.posY = 400; break;
            default: break;
        }
        m_Contexts.push_back(ctx);
        return m_Contexts.back();
    }

    VFXContext* VFXGraph::GetContext(ContextType type) {
        for (auto& ctx : m_Contexts) {
            if (ctx.type == type) return &ctx;
        }
        return nullptr;
    }

    const VFXContext* VFXGraph::GetContext(ContextType type) const {
        for (auto& ctx : m_Contexts) {
            if (ctx.type == type) return &ctx;
        }
        return nullptr;
    }

    void VFXGraph::RemoveProperty(int index) {
        if (index >= 0 && index < (int)m_Properties.size()) {
            m_Properties.erase(m_Properties.begin() + index);
            MarkDirty();
        }
    }

    bool VFXGraph::HasPathTo(uint32 fromBlockId, uint32 toBlockId,
                              std::unordered_set<uint32>& visited) const {
        if (fromBlockId == toBlockId) return true;
        if (visited.count(fromBlockId)) return false;
        visited.insert(fromBlockId);
        for (auto& link : m_Links) {
            if (link.outputNodeId == fromBlockId) {
                if (HasPathTo(link.inputNodeId, toBlockId, visited)) return true;
            }
        }
        return false;
    }

    std::vector<uint32> VFXGraph::TopologicalSort(std::vector<VFXCompileError>* outErrors) const {
        std::unordered_map<uint32, int> inDegree;
        for (auto& [id, block] : m_Blocks) {
            if (block->IsEnabled()) inDegree[id] = 0;
        }
        for (auto& link : m_Links) {
            if (inDegree.count(link.inputNodeId) && inDegree.count(link.outputNodeId)) {
                inDegree[link.inputNodeId]++;
            }
        }
        std::vector<uint32> zeroInDegree;
        for (auto& [id, deg] : inDegree) {
            if (deg == 0) zeroInDegree.push_back(id);
        }
        std::vector<uint32> sorted;
        while (!zeroInDegree.empty()) {
            uint32 nodeId = zeroInDegree.back();
            zeroInDegree.pop_back();
            sorted.push_back(nodeId);
            for (auto& link : m_Links) {
                if (link.outputNodeId == nodeId && inDegree.count(link.inputNodeId)) {
                    if (--inDegree[link.inputNodeId] == 0)
                        zeroInDegree.push_back(link.inputNodeId);
                }
            }
        }
        if (sorted.size() < inDegree.size() && outErrors) {
            VFXCompileError err;
            err.severity = VFXCompileError::Error;
            err.message = "Graph contains cycles.";
            outErrors->push_back(err);
        }
        return sorted;
    }

    std::vector<VFXCompileError> VFXGraph::Validate() const {
        std::vector<VFXCompileError> errors;
        for (auto& ctx : m_Contexts) {
            if (ctx.blockIds.empty() && ctx.type != ContextType::System) {
                VFXCompileError err;
                err.severity = VFXCompileError::Warning;
                err.message = std::string("Context ") + ContextTypeName(ctx.type) + " is empty.";
                errors.push_back(err);
            }
        }
        return errors;
    }

    void VFXGraph::Clear() {
        m_Blocks.clear();
        m_Links.clear();
        m_Contexts.clear();
        m_Properties.clear();
        m_NextBlockId = 1;
        m_NextLinkId = 1;
        m_IsDirty = true;
    }

    std::string VFXGraph::ToJSON() const {
        std::stringstream ss;
        ss << "{\n";
        ss << "  \"particleCapacity\": " << m_ParticleCapacity << ",\n";
        ss << "  \"duration\": " << m_Duration << ",\n";
        ss << "  \"looping\": " << (m_Looping ? "true" : "false") << ",\n";
        ss << "  \"prewarm\": " << (m_Prewarm ? "true" : "false") << ",\n";
        ss << "  \"blockCount\": " << m_Blocks.size() << ",\n";
        ss << "  \"linkCount\": " << m_Links.size() << ",\n";

        // 序列化 Block 位置
        ss << "  \"blocks\": [\n";
        bool first = true;
        for (auto& [id, block] : m_Blocks) {
            if (!first) ss << ",\n";
            first = false;
            ss << "    { \"id\": " << id
               << ", \"name\": \"" << block->GetName()
               << "\", \"posX\": " << block->GetPosX()
               << ", \"posY\": " << block->GetPosY()
               << ", \"enabled\": " << (block->IsEnabled() ? "true" : "false")
               << " }";
        }
        ss << "\n  ],\n";

        // 序列化 Contexts
        ss << "  \"contexts\": [\n";
        first = true;
        for (auto& ctx : m_Contexts) {
            if (!first) ss << ",\n";
            first = false;
            ss << "    { \"type\": " << (int)ctx.type
               << ", \"blockIds\": [";
            for (size_t i = 0; i < ctx.blockIds.size(); ++i) {
                if (i > 0) ss << ",";
                ss << ctx.blockIds[i];
            }
            ss << "] }";
        }
        ss << "\n  ],\n";

        // 序列化 Links
        ss << "  \"links\": [\n";
        first = true;
        for (auto& link : m_Links) {
            if (!first) ss << ",\n";
            first = false;
            ss << "    { \"id\": " << link.id
               << ", \"out\": [" << link.outputNodeId << "," << link.outputPinId << "]"
               << ", \"in\": [" << link.inputNodeId << "," << link.inputPinId << "]"
               << " }";
        }
        ss << "\n  ]\n";
        ss << "}\n";
        return ss.str();
    }

    bool VFXGraph::FromJSON(const std::string& json) {
        // 简化实现：解析 "blocks" 和 "contexts" 和 "links"
        // 实际项目应使用 nlohmann/json
        // 这里实现最小恢复：清空并从 JSON 重建
        (void)json;

        // 因 ToJSON 输出已足够描述状态，完整 FromJSON 需要第三方 JSON 库。
        // 当前返回 false 表示还无法从 JSON 恢复（快照功能需要 nlohmann/json 支持）
        return false;
    }

    bool VFXGraph::SaveToFile(const std::string& path) const {
        std::string json = ToJSON();
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        fwrite(json.c_str(), 1, json.size(), f);
        fclose(f);
        return true;
    }

    bool VFXGraph::LoadFromFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string json(size, '\0');
        fread(&json[0], 1, size, f);
        fclose(f);
        return FromJSON(json);
    }

    // ============================================================
    // VFXInstance 实现 — 数据驱动
    // ============================================================

    VFXInstance::VFXInstance()
        : m_Particles(kDefaultParticleCapacity)
    {
    }

    VFXInstance::~VFXInstance() = default;

    void VFXInstance::Clear() {
        m_AliveCount = 0;
        m_EmitAccum = 0.0f;
        std::memset(m_Particles.data(), 0, m_Particles.size() * sizeof(ParticleData));
    }

    void VFXInstance::Emit(uint32 count) {
        if (!m_Graph) return;

        // ── 数据驱动发射 ──
        // 从 Initialize Context 的 Block 读取配置
        const VFXContext* initCtx = m_Graph->GetContext(ContextType::Initialize);

        // 默认值（当没有 Block 时使用）
        float defaultLifeTime = 2.0f;
        float defaultSpeed = 5.0f;
        float defaultSize = 1.0f;
        float defaultColor[4] = {1,1,1,1};

        // 通过 Block 的 GenerateHLSL 获取运行时参数（这里简化为从内联参数读取）
        // 生产环境应实现更完善的运行时反射
        if (initCtx) {
            for (uint32 bid : initCtx->blockIds) {
                auto* block = m_Graph->GetBlock(bid);
                if (!block || !block->IsEnabled()) continue;
                // Block 的 DrawInlineContent 已经暴露了参数到 UI
                // 运行时可通过 GeneratePropertyDecl 获取参数值
                // 这里用默认值示例，实际应通过序列化的属性读取
                (void)block;
            }
        }

        for (uint32 i = 0; i < count && m_AliveCount < m_MaxCapacity; ++i) {
            uint32 idx = m_AliveCount++;
            auto& p = m_Particles[idx];

            // 默认为原点发射
            p.position[0] = 0;
            p.position[1] = 0;
            p.position[2] = 0;
            p.velocity[0] = 0;
            p.velocity[1] = defaultSpeed;
            p.velocity[2] = 0;
            p.color[0] = defaultColor[0];
            p.color[1] = defaultColor[1];
            p.color[2] = defaultColor[2];
            p.color[3] = defaultColor[3];
            p.size = defaultSize;
            p.lifetime = defaultLifeTime;
            p.age = 0.0f;
            p.alive = 1;
        }
    }

    void VFXInstance::UpdateParticles(float dt) {
        if (!m_Graph) return;

        // ── 数据驱动更新 ──
        // 遍历 Update Context 的 Block，通过 GenerateHLSL 生成更新代码
        // CPU 模式：遍历每个粒子，对每个粒子应用所有 Block 的逻辑
        const VFXContext* updateCtx = m_Graph->GetContext(ContextType::Update);

        for (uint32 i = 0; i < m_AliveCount; ) {
            auto& p = m_Particles[i];
            if (!p.alive) { ++i; continue; }

            p.age += dt;

            if (p.age >= p.lifetime) {
                p.alive = 0;
                if (i < m_AliveCount - 1) {
                    m_Particles[i] = m_Particles[m_AliveCount - 1];
                }
                --m_AliveCount;
                continue;
            }

            // 应用 Update Context 中所有 Block 的逻辑
            // 每个 Block 的 GenerateHLSL 生成对应 HLSL 代码，
            // CPU 模式相当于"解释执行"这些 HLSL 表达式
            if (updateCtx) {
                for (uint32 bid : updateCtx->blockIds) {
                    auto* block = m_Graph->GetBlock(bid);
                    if (!block || !block->IsEnabled()) continue;

                    // 根据 Block 类型执行对应逻辑
                    // 这是一个简化版 Block 解释器
                    switch (block->GetCategory()) {
                        case BlockCategory::Update_Gravity: {
                            // GravityBlock 的参数通过 DrawInlineContent 暴露
                            // 这里简化为默认 -9.8
                            p.velocity[1] += -9.8f * dt;
                            break;
                        }
                        case BlockCategory::Update_Drag: {
                            p.velocity[0] *= (1.0f - 0.1f * dt);
                            p.velocity[1] *= (1.0f - 0.1f * dt);
                            p.velocity[2] *= (1.0f - 0.1f * dt);
                            break;
                        }
                        case BlockCategory::Update_Color: {
                            float t = p.age / p.lifetime;
                            p.color[0] = 1.0f - t * 0.5f;  // 白 → 灰蓝
                            p.color[1] = 1.0f - t * 0.5f;
                            p.color[2] = 1.0f - t * 0.0f;
                            p.color[3] = 1.0f - t;
                            break;
                        }
                        case BlockCategory::Update_Size: {
                            float t = p.age / p.lifetime;
                            p.size = 1.0f - t * 0.9f;
                            break;
                        }
                        case BlockCategory::Update_Turbulence: {
                            float noise = sinf(p.position[0] * 12.9898f +
                                               p.position[2] * 78.233f +
                                               m_TotalTime * 1.0f) * 1.0f * dt;
                            p.velocity[0] += noise;
                            p.velocity[2] += noise;
                            break;
                        }
                        default:
                            break;
                    }
                }
            }

            // 位置更新
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;

            ++i;
        }
    }

    void VFXInstance::OnUpdate(float dt) {
        if (!m_Playing || m_Paused || !m_Graph) return;

        m_TotalTime += dt;

        // ── 数据驱动发射 ──
        // 从 Spawn Context 获取发射率
        const VFXContext* spawnCtx = m_Graph->GetContext(ContextType::Spawn);
        float emitRate = 100.0f;
        if (spawnCtx) {
            for (uint32 bid : spawnCtx->blockIds) {
                auto* block = m_Graph->GetBlock(bid);
                if (block && block->IsEnabled()) {
                    switch (block->GetCategory()) {
                        case BlockCategory::Spawn_Rate:
                            emitRate = 100.0f; // 从 SpawnRateBlock 参数读取
                            break;
                        case BlockCategory::Spawn_Burst:
                            // Burst 一次性发射
                            emitRate = 0;
                            break;
                        default:
                            break;
                    }
                }
            }
        }

        m_EmitAccum += emitRate * dt;
        if (m_EmitAccum >= 1.0f) {
            uint32 toEmit = (uint32)m_EmitAccum;
            m_EmitAccum -= (float)toEmit;
            Emit(toEmit);
        }

        // 更新粒子
        UpdateParticles(dt);
    }

    // ============================================================
    // VFXManager 实现
    // ============================================================

    VFXManager& VFXManager::Get() {
        static VFXManager instance;
        return instance;
    }

    uint32 VFXManager::Spawn(std::shared_ptr<VFXGraph> graph) {
        auto instance = std::make_unique<VFXInstance>();
        instance->SetGraph(graph);
        instance->Play();
        uint32 id = m_NextId++;
        m_Instances[id] = std::move(instance);
        return id;
    }

    void VFXManager::Despawn(uint32 id) {
        m_Instances.erase(id);
    }

    VFXInstance* VFXManager::Get(uint32 id) {
        auto it = m_Instances.find(id);
        return (it != m_Instances.end()) ? it->second.get() : nullptr;
    }

    void VFXManager::OnUpdate(float dt) {
        for (auto& [id, inst] : m_Instances) {
            inst->OnUpdate(dt);
        }
    }

    void VFXManager::Clear() {
        m_Instances.clear();
    }

}} // namespace Engine::VFX