/**
 * @file VFXRuntime.cpp
 * @brief VFX 运行时 — 数据驱动的粒子模拟 + 工业级 JSON 序列化
 */

#include "Engine/Editor/VFXGraph/VFXGraphCore.h"
#include "Engine/Editor/VFXGraph/VFXNodeFactory.h"
#include <algorithm>
#include <cstring>
#include <random>
#include <sstream>
#include <nlohmann/json.hpp>

namespace Engine {
namespace VFX {

    using json = nlohmann::json;

    VFXGraph::VFXGraph() { RegisterAllVFXBlocks(); }

    // ── 创建默认模板 ──
    void VFXGraph::CreateDefaultTemplate() {
        Clear();
        GetOrCreateContext(ContextType::Spawn);
        GetOrCreateContext(ContextType::Initialize);
        GetOrCreateContext(ContextType::Update);
        GetOrCreateContext(ContextType::Output);
        auto& factoryList = GetVFXBlockFactoryList();
        for (auto& entry : factoryList) {
            if (entry.category == BlockCategory::Spawn_Rate) {
                uint32 id = AddBlock(entry.factory(0));
                GetContext(ContextType::Spawn)->blockIds.push_back(id);
                break;
            }
        }
        MarkDirty(false);
    }

    // ── 根据类别创建 Block（反序列化用） ──
    std::unique_ptr<VFXBlock> VFXGraph::CreateBlockByCategory(BlockCategory category, uint32 id) const {
        auto& factoryList = GetVFXBlockFactoryList();
        for (auto& entry : factoryList) {
            if (entry.category == category) {
                return entry.factory(id);
            }
        }
        return nullptr;
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
            if (linkIt->inputNodeId == blockId || linkIt->outputNodeId == blockId)
                linkIt = m_Links.erase(linkIt);
            else ++linkIt;
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
            for (auto& p : block->GetInputPins()) if (p.id == pinId) { p.isConnected = true; return; }
            for (auto& p : block->GetOutputPins()) if (p.id == pinId) { p.isConnected = true; return; }
        };
        markConnected(GetBlock(outputBlockId), outputPinId);
        markConnected(GetBlock(inputBlockId), inputPinId);
        m_IsDirty = true;
        return link.id;
    }

    bool VFXGraph::RemoveLink(uint32 linkId) {
        size_t idx = (size_t)-1;
        for (size_t i = 0; i < m_Links.size(); ++i) {
            if (m_Links[i].id == linkId) { idx = i; break; }
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
            for (auto& p : block->GetInputPins()) if (p.id == pinId) { p.isConnected = false; return; }
            for (auto& p : block->GetOutputPins()) if (p.id == pinId) { p.isConnected = false; return; }
        };
        resetIfUnused(m_Links[idx].outputNodeId, m_Links[idx].outputPinId);
        resetIfUnused(m_Links[idx].inputNodeId, m_Links[idx].inputPinId);
        m_Links.erase(m_Links.begin() + idx);
        m_IsDirty = true;
        return true;
    }

    // 其余 VFXGraph 方法保持不变（RemoveLinkByPins, CanConnectPins, ReplaceLink, GetOrCreateContext, GetContext 等）
    bool VFXGraph::RemoveLinkByPins(uint32 outputBlockId, uint32 outputPinId, uint32 inputBlockId, uint32 inputPinId) {
        for (size_t i = 0; i < m_Links.size(); ++i) {
            auto& l = m_Links[i];
            if (l.outputNodeId == outputBlockId && l.outputPinId == outputPinId &&
                l.inputNodeId == inputBlockId && l.inputPinId == inputPinId)
                return RemoveLink(l.id);
        }
        return false;
    }

    bool VFXGraph::CanConnectPins(const VFXPin& outPin, const VFXPin& inPin, std::string& outReason) const {
        if (outPin.direction == inPin.direction) { outReason = "Same direction."; return false; }
        if (outPin.direction != PinDirection::Output) { outReason = "Source must be output."; return false; }
        if (inPin.direction != PinDirection::Input) { outReason = "Target must be input."; return false; }
        if (outPin.nodeId == inPin.nodeId) { outReason = "Self-connect."; return false; }
        if (outPin.type != inPin.type && outPin.type != VFXPinType::DynamicVector &&
            inPin.type != VFXPinType::DynamicVector && outPin.type != VFXPinType::ParticleData &&
            inPin.type != VFXPinType::ParticleData) {
            outReason = "Type mismatch."; return false;
        }
        return true;
    }

    void VFXGraph::RemoveLinksConnectedToPin(uint32 pinId) {
        for (size_t i = m_Links.size(); i-- > 0; ) {
            if (m_Links[i].outputPinId == pinId || m_Links[i].inputPinId == pinId) {
                RemoveLink(m_Links[i].id);
            }
        }
    }

    uint32 VFXGraph::ReplaceLink(uint32 outputBlockId, uint32 outputPinId, uint32 inputBlockId, uint32 inputPinId) {
        for (auto& l : m_Links) {
            if (l.inputNodeId == inputBlockId && l.inputPinId == inputPinId) { RemoveLink(l.id); break; }
        }
        return AddLink(outputBlockId, outputPinId, inputBlockId, inputPinId);
    }

    VFXContext& VFXGraph::GetOrCreateContext(ContextType type) {
        for (auto& ctx : m_Contexts) if (ctx.type == type) return ctx;
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

    VFXContext& VFXGraph::CreateNewContext(ContextType type) {
        VFXContext ctx;
        ctx.type = type;
        ctx.name = ContextTypeDisplayName(type);
        ctx.sizeX = 200.0f;
        ctx.sizeY = 150.0f;
        ctx.visible = true;
        m_Contexts.push_back(ctx);
        MarkDirty();
        return m_Contexts.back();
    }

    void VFXGraph::RemoveBlockFromAllContexts(uint32 blockId) {
        for (auto& ctx : m_Contexts) {
            auto it = std::find(ctx.blockIds.begin(), ctx.blockIds.end(), blockId);
            if (it != ctx.blockIds.end()) {
                ctx.blockIds.erase(it);
            }
        }
    }

    bool VFXGraph::RemoveContext(ContextType type) {
        for (size_t i = 0; i < m_Contexts.size(); ++i) {
            if (m_Contexts[i].type == type) {
                m_Contexts.erase(m_Contexts.begin() + i);
                MarkDirty();
                return true;
            }
        }
        return false;
    }

    VFXContext* VFXGraph::GetContext(ContextType type) {
        for (auto& ctx : m_Contexts) if (ctx.type == type) return &ctx;
        return nullptr;
    }
    const VFXContext* VFXGraph::GetContext(ContextType type) const {
        for (auto& ctx : m_Contexts) if (ctx.type == type) return &ctx;
        return nullptr;
    }

    void VFXGraph::RemoveProperty(int index) {
        if (index >= 0 && index < (int)m_Properties.size()) {
            m_Properties.erase(m_Properties.begin() + index); MarkDirty();
        }
    }

    bool VFXGraph::HasPathTo(uint32 fromBlockId, uint32 toBlockId, std::unordered_set<uint32>& visited) const {
        if (fromBlockId == toBlockId) return true;
        if (visited.count(fromBlockId)) return false;
        visited.insert(fromBlockId);
        for (auto& link : m_Links)
            if (link.outputNodeId == fromBlockId && HasPathTo(link.inputNodeId, toBlockId, visited)) return true;
        return false;
    }

    std::vector<uint32> VFXGraph::TopologicalSort(std::vector<VFXCompileError>* outErrors) const {
        std::unordered_map<uint32, int> inDegree;
        for (auto& [id, block] : m_Blocks) if (block->IsEnabled()) inDegree[id] = 0;
        for (auto& link : m_Links) if (inDegree.count(link.inputNodeId) && inDegree.count(link.outputNodeId)) inDegree[link.inputNodeId]++;
        std::vector<uint32> zeroInDegree, sorted;
        for (auto& [id, deg] : inDegree) if (deg == 0) zeroInDegree.push_back(id);
        while (!zeroInDegree.empty()) {
            uint32 nodeId = zeroInDegree.back(); zeroInDegree.pop_back(); sorted.push_back(nodeId);
            for (auto& link : m_Links) {
                if (link.outputNodeId == nodeId && inDegree.count(link.inputNodeId))
                    if (--inDegree[link.inputNodeId] == 0) zeroInDegree.push_back(link.inputNodeId);
            }
        }
        if (sorted.size() < inDegree.size() && outErrors) {
            outErrors->push_back({VFXCompileError::Error, 0, 0, "Graph contains cycles."});
        }
        return sorted;
    }

    std::vector<VFXCompileError> VFXGraph::Validate() const {
        std::vector<VFXCompileError> errors;
        for (auto& ctx : m_Contexts) {
            if (ctx.blockIds.empty() && ctx.type != ContextType::System)
                errors.push_back({VFXCompileError::Warning, 0, 0, "Context " + std::string(ContextTypeName(ctx.type)) + " is empty."});
        }
        return errors;
    }

    void VFXGraph::Clear() {
        m_Blocks.clear(); m_Links.clear(); m_Contexts.clear(); m_Properties.clear();
        m_NextBlockId = 1; m_NextLinkId = 1; m_IsDirty = true;
    }

    // ═══════════════════════════════════════════════════════════
    // 工业级 JSON 序列化（使用 nlohmann/json）
    // ═══════════════════════════════════════════════════════════

    std::string VFXGraph::ToJSON() const {
        json root;
        root["capacity"] = m_ParticleCapacity;
        root["duration"] = m_Duration;
        root["looping"] = m_Looping;
        root["prewarm"] = m_Prewarm;

        for (auto& prop : m_Properties) {
            json p;
            p["name"] = prop.name; p["type"] = (int)prop.type;
            p["value"] = {prop.f[0], prop.f[1], prop.f[2], prop.f[3]};
            root["properties"].push_back(p);
        }

        for (auto& [id, block] : m_Blocks) {
            json b;
            b["id"] = id; b["name"] = block->GetName();
            b["category"] = (int)block->GetCategory();
            b["pos"] = {block->GetPosX(), block->GetPosY()};
            b["enabled"] = block->IsEnabled();
            for (auto& pin : block->GetInputPins()) {
                if (!pin.isConnected)
                    b["params"][pin.name] = {pin.f[0], pin.f[1], pin.f[2], pin.f[3]};
            }
            root["blocks"].push_back(b);
        }

        for (auto& ctx : m_Contexts) {
            json c;
            c["type"] = (int)ctx.type;
            c["pos"] = {ctx.posX, ctx.posY};
            c["size"] = {ctx.sizeX, ctx.sizeY};
            c["blocks"] = ctx.blockIds;
            root["contexts"].push_back(c);
        }

        for (auto& link : m_Links) {
            root["links"].push_back({{"outNode", link.outputNodeId}, {"outPin", link.outputPinId},
                                      {"inNode", link.inputNodeId}, {"inPin", link.inputPinId}});
        }

        return root.dump(4);
    }

    bool VFXGraph::FromJSON(const std::string& data) {
        try {
            json root = json::parse(data, nullptr, false);
            if (root.is_discarded()) return false;
            Clear();

            // ── 使用 value() 提供默认值，防止字段缺失崩溃 ──
            m_ParticleCapacity = root.value("capacity", kDefaultParticleCapacity);
            m_Duration = root.value("duration", 5.0f);
            m_Looping = root.value("looping", true);
            m_Prewarm = root.value("prewarm", false);

            // ── Properties ──
            if (root.contains("properties") && root["properties"].is_array()) {
                for (auto& p : root["properties"]) {
                    if (!p.contains("name") || !p.contains("type")) continue;
                    VFXBlackboardProperty prop;
                    prop.name = p["name"].get<std::string>();
                    prop.type = static_cast<VFXPinType>(p["type"].get<int>());
                    if (p.contains("value") && p["value"].is_array()) {
                        auto& v = p["value"];
                        size_t count = std::min<size_t>(4, v.size());
                        for (size_t i = 0; i < count; ++i) prop.f[i] = v[i].get<float>();
                    }
                    m_Properties.push_back(prop);
                }
            }

            // ── Blocks ──
            uint32 maxId = 0;
            if (root.contains("blocks") && root["blocks"].is_array()) {
                for (auto& b : root["blocks"]) {
                    if (!b.contains("id") || !b.contains("category")) continue;

                    uint32 bId = b["id"].get<uint32>();
                    BlockCategory bCat = static_cast<BlockCategory>(b.value("category", 0));
                    if (bId > maxId) maxId = bId;

                    auto block = CreateBlockByCategory(bCat, bId);
                    if (!block) continue;

                    if (b.contains("pos") && b["pos"].is_array() && b["pos"].size() >= 2) {
                        block->SetPosition(b["pos"][0].get<float>(), b["pos"][1].get<float>());
                    }
                    if (b.contains("enabled")) {
                        block->SetEnabled(b["enabled"].get<bool>());
                    }

                    // 安全恢复引脚参数
                    if (b.contains("params")) {
                        auto& params = b["params"];
                        for (auto& pin : block->GetInputPins()) {
                            if (params.contains(pin.name) && params[pin.name].is_array()) {
                                auto& v = params[pin.name];
                                size_t count = std::min<size_t>(4, v.size());
                                for (size_t i = 0; i < count; ++i)
                                    pin.f[i] = v[i].get<float>();
                            }
                        }
                    }
                    m_Blocks[bId] = std::move(block);
                }
            }

            // ── 同步 NextBlockId（防止新创建的 Block ID 冲突） ──
            if (maxId >= m_NextBlockId) {
                m_NextBlockId = maxId + 1;
            }

            // ── Links ──
            if (root.contains("links") && root["links"].is_array()) {
                for (auto& l : root["links"]) {
                    if (!l.contains("outNode") || !l.contains("inNode")) continue;
                    uint32 outN = l["outNode"].get<uint32>();
                    uint32 outP = l.value("outPin", 0u);
                    uint32 inN  = l["inNode"].get<uint32>();
                    uint32 inP  = l.value("inPin", 0u);
                    AddLink(outN, outP, inN, inP);
                }
            }

            // ── Contexts ──
            if (root.contains("contexts") && root["contexts"].is_array()) {
                for (auto& c : root["contexts"]) {
                    if (!c.contains("type")) continue;
                    auto& ctx = GetOrCreateContext(static_cast<ContextType>(c["type"].get<int>()));

                    if (c.contains("pos") && c["pos"].is_array() && c["pos"].size() >= 2) {
                        ctx.posX = c["pos"][0].get<float>();
                        ctx.posY = c["pos"][1].get<float>();
                    }
                    if (c.contains("size") && c["size"].is_array() && c["size"].size() >= 2) {
                        ctx.sizeX = c["size"][0].get<float>();
                        ctx.sizeY = c["size"][1].get<float>();
                    }
                    if (c.contains("blocks") && c["blocks"].is_array()) {
                        ctx.blockIds = c["blocks"].get<std::vector<uint32>>();
                    }
                }
            }

            m_IsDirty = false;
            return true;

        } catch (const json::exception& e) {
            // 防御性编程：捕获所有 JSON 异常，防止程序崩溃
            printf("VFXGraph::FromJSON error: %s\n", e.what());
            return false;
        }
    }

    bool VFXGraph::SaveToFile(const std::string& path) const {
        std::string j = ToJSON();
        FILE* f = fopen(path.c_str(), "w");
        if (!f) return false;
        fwrite(j.c_str(), 1, j.size(), f);
        fclose(f);
        return true;
    }

    bool VFXGraph::LoadFromFile(const std::string& path) {
        FILE* f = fopen(path.c_str(), "r");
        if (!f) return false;
        fseek(f, 0, SEEK_END); long size = ftell(f);
        fseek(f, 0, SEEK_SET);
        std::string jsonStr(size, '\0');
        fread(&jsonStr[0], 1, size, f);
        fclose(f);
        return FromJSON(jsonStr);
    }

    // ═══════════════════════════════════════════════════════════
    // VFXInstance — 数据驱动
    // ═══════════════════════════════════════════════════════════

    VFXInstance::VFXInstance() : m_Particles(kDefaultParticleCapacity) {}
    VFXInstance::~VFXInstance() = default;

    void VFXInstance::Clear() {
        m_AliveCount = 0; m_EmitAccum = 0.0f;
        std::memset(m_Particles.data(), 0, m_Particles.size() * sizeof(ParticleData));
    }

    void VFXInstance::Emit(uint32 count) {
        if (!m_Graph) return;
        const VFXContext* initCtx = m_Graph->GetContext(ContextType::Initialize);
        float lifeTime = 2.0f, speed = 5.0f, size = 1.0f, color[4] = {1,1,1,1};
        for (uint32 i = 0; i < count && m_AliveCount < m_MaxCapacity; ++i) {
            uint32 idx = m_AliveCount++;
            auto& p = m_Particles[idx];
            p.position[0] = p.position[1] = p.position[2] = 0;
            p.velocity[0] = p.velocity[2] = 0; p.velocity[1] = speed;
            p.color[0] = color[0]; p.color[1] = color[1]; p.color[2] = color[2]; p.color[3] = color[3];
            p.size = size; p.lifetime = lifeTime; p.age = 0.0f; p.alive = 1;
        }
    }

    void VFXInstance::UpdateParticles(float dt) {
        if (!m_Graph) return;
        const VFXContext* updateCtx = m_Graph->GetContext(ContextType::Update);
        for (uint32 i = 0; i < m_AliveCount; ) {
            auto& p = m_Particles[i];
            if (!p.alive) { ++i; continue; }
            p.age += dt;
            if (p.age >= p.lifetime) {
                p.alive = 0;
                if (i < m_AliveCount - 1) m_Particles[i] = m_Particles[m_AliveCount - 1];
                --m_AliveCount; continue;
            }
            if (updateCtx) {
                for (uint32 bid : updateCtx->blockIds) {
                    auto* block = m_Graph->GetBlock(bid);
                    if (!block || !block->IsEnabled()) continue;
                    switch (block->GetCategory()) {
                        case BlockCategory::Update_Gravity: p.velocity[1] += -9.8f * dt; break;
                        case BlockCategory::Update_Drag:
                            p.velocity[0] *= (1.0f - 0.1f * dt);
                            p.velocity[1] *= (1.0f - 0.1f * dt);
                            p.velocity[2] *= (1.0f - 0.1f * dt); break;
                        case BlockCategory::Update_Color: {
                            float t = p.age / p.lifetime;
                            p.color[0] = 1.0f - t * 0.5f; p.color[1] = 1.0f - t * 0.5f;
                            p.color[2] = 1.0f; p.color[3] = 1.0f - t; break;
                        }
                        case BlockCategory::Update_Size: p.size = 1.0f - (p.age / p.lifetime) * 0.9f; break;
                        case BlockCategory::Update_Turbulence: {
                            float n = sinf(p.position[0] * 12.9898f + p.position[2] * 78.233f + m_TotalTime * 1.0f) * dt;
                            p.velocity[0] += n; p.velocity[2] += n; break;
                        }
                        default: break;
                    }
                }
            }
            p.position[0] += p.velocity[0] * dt;
            p.position[1] += p.velocity[1] * dt;
            p.position[2] += p.velocity[2] * dt;
            ++i;
        }
    }

    void VFXInstance::OnUpdate(float dt) {
        if (!m_Playing || m_Paused || !m_Graph) return;
        m_TotalTime += dt;
        float emitRate = 100.0f;
        const VFXContext* spawnCtx = m_Graph->GetContext(ContextType::Spawn);
        if (spawnCtx) {
            for (uint32 bid : spawnCtx->blockIds) {
                auto* block = m_Graph->GetBlock(bid);
                if (block && block->IsEnabled() && block->GetCategory() == BlockCategory::Spawn_Rate)
                    emitRate = 100.0f;
            }
        }
        m_EmitAccum += emitRate * dt;
        if (m_EmitAccum >= 1.0f) {
            uint32 toEmit = (uint32)m_EmitAccum; m_EmitAccum -= (float)toEmit; Emit(toEmit);
        }
        UpdateParticles(dt);
    }

    // ═══════════════════════════════════════════════════════════
    // VFXManager
    // ═══════════════════════════════════════════════════════════

    VFXManager& VFXManager::Get() { static VFXManager instance; return instance; }

    uint32 VFXManager::Spawn(std::shared_ptr<VFXGraph> graph) {
        auto inst = std::make_unique<VFXInstance>(); inst->SetGraph(graph); inst->Play();
        uint32 id = m_NextId++; m_Instances[id] = std::move(inst); return id;
    }

    void VFXManager::Despawn(uint32 id) { m_Instances.erase(id); }
    VFXInstance* VFXManager::Get(uint32 id) { auto it = m_Instances.find(id); return (it != m_Instances.end()) ? it->second.get() : nullptr; }
    void VFXManager::OnUpdate(float dt) { for (auto& [id, inst] : m_Instances) inst->OnUpdate(dt); }
    void VFXManager::Clear() { m_Instances.clear(); }

}} // namespace Engine::VFX