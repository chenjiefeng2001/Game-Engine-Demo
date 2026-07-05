#pragma once

/**
 * @file VFXNodeFactory.h
 * @brief VFX Block 工厂 — 注册表 + 创建函数
 *
 * 遵循抽象工厂原则，所有 Block 通过工厂创建，编辑器不直接实例化具体 Block。
 */

#include "VFXGraphCore.h"
#include "VFXNodes.h"
#include <functional>
#include <vector>

namespace Engine {
namespace VFX {

    struct VFXBlockFactoryEntry {
        const char* name;
        BlockCategory category;
        const char* group;
        std::function<std::unique_ptr<VFXBlock>(uint32)> factory;
    };

    // 注册表
    inline std::vector<VFXBlockFactoryEntry>& GetVFXBlockFactoryList() {
        static std::vector<VFXBlockFactoryEntry> s_FactoryList;
        return s_FactoryList;
    }

    inline void RegisterVFXBlock(const VFXBlockFactoryEntry& entry) {
        GetVFXBlockFactoryList().push_back(entry);
    }

    // ── 注册所有 Block ──
    inline void RegisterAllVFXBlocks() {
        static bool s_Registered = false;
        if (s_Registered) return;
        s_Registered = true;

        auto& list = GetVFXBlockFactoryList();

        // Spawn
        list.push_back({"Spawn Burst",     BlockCategory::Spawn_Burst,     "Spawn",  [](uint32 id) { return std::make_unique<SpawnBurstBlock>(id); }});
        list.push_back({"Spawn Rate",      BlockCategory::Spawn_Rate,      "Spawn",  [](uint32 id) { return std::make_unique<SpawnRateBlock>(id); }});

        // Initialize
        list.push_back({"Init Life",       BlockCategory::Init_Life,       "Initialize", [](uint32 id) { return std::make_unique<InitLifeBlock>(id); }});
        list.push_back({"Init Position",   BlockCategory::Init_Position,   "Initialize", [](uint32 id) { return std::make_unique<InitPositionBlock>(id); }});
        list.push_back({"Init Velocity",   BlockCategory::Init_Velocity,   "Initialize", [](uint32 id) { return std::make_unique<InitVelocityBlock>(id); }});
        list.push_back({"Init Color",      BlockCategory::Init_Color,      "Initialize", [](uint32 id) { return std::make_unique<InitColorBlock>(id); }});
        list.push_back({"Init Size",       BlockCategory::Init_Size,       "Initialize", [](uint32 id) { return std::make_unique<InitSizeBlock>(id); }});
        list.push_back({"Init Rotation",   BlockCategory::Init_Rotation,   "Initialize", [](uint32 id) { return std::make_unique<InitRotationBlock>(id); }});

        // Update
        list.push_back({"Gravity",         BlockCategory::Update_Gravity,     "Update", [](uint32 id) { return std::make_unique<GravityBlock>(id); }});
        list.push_back({"Drag",            BlockCategory::Update_Drag,        "Update", [](uint32 id) { return std::make_unique<DragBlock>(id); }});
        list.push_back({"Turbulence",      BlockCategory::Update_Turbulence,  "Update", [](uint32 id) { return std::make_unique<TurbulenceBlock>(id); }});
        list.push_back({"Color Over Life", BlockCategory::Update_Color,       "Update", [](uint32 id) { return std::make_unique<ColorOverLifeBlock>(id); }});
        list.push_back({"Size Over Life",  BlockCategory::Update_Size,        "Update", [](uint32 id) { return std::make_unique<SizeOverLifeBlock>(id); }});

        // Output
        list.push_back({"Quad Billboard",  BlockCategory::Output_Quad,    "Output", [](uint32 id) { return std::make_unique<QuadOutputBlock>(id); }});
        list.push_back({"Mesh",            BlockCategory::Output_Mesh,    "Output", [](uint32 id) { return std::make_unique<MeshOutputBlock>(id); }});

        // Utility
        list.push_back({"Constant",        BlockCategory::Utility_Constant, "Utility", [](uint32 id) { return std::make_unique<VFXConstantBlock>(id); }});
        list.push_back({"Random",          BlockCategory::Utility_Random,   "Utility", [](uint32 id) { return std::make_unique<RandomBlock>(id); }});
        list.push_back({"Sample Curve",    BlockCategory::Utility_Curve,    "Utility", [](uint32 id) { return std::make_unique<SampleCurveBlock>(id); }});
    }

    /// 创建一个默认 VFX 图（含示例 Block）
    inline std::unique_ptr<VFXGraph> CreateDefaultVFXGraph() {
        auto graph = std::make_unique<VFXGraph>();

        // 添加 Spawn Rate + Init 示例
        graph->AddBlock(std::make_unique<SpawnRateBlock>(0));
        graph->AddBlock(std::make_unique<InitLifeBlock>(0));
        graph->AddBlock(std::make_unique<InitPositionBlock>(0));
        graph->AddBlock(std::make_unique<InitVelocityBlock>(0));
        graph->AddBlock(std::make_unique<QuadOutputBlock>(0));

        // 添加 Update 模块
        graph->AddBlock(std::make_unique<GravityBlock>(0));
        graph->AddBlock(std::make_unique<ColorOverLifeBlock>(0));
        graph->AddBlock(std::make_unique<SizeOverLifeBlock>(0));

        return graph;
    }

}} // namespace Engine::VFX