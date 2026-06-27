#pragma once

/**
 * @file VFXNodes.h
 * @brief VFX Block 节点实现 — Spawn/Init/Update/Output/Utility 五大类
 *
 * 每个 Block 定义了：
 *   1. 输入/输出引脚接口
 *   2. GPU Compute Shader 代码生成（GenerateHLSL）
 *   3. 参数属性声明
 *   4. 内联编辑控件
 *
 * 注意：所有实现均为内联，以避免需要单独的 .cpp 文件。
 * 遵循 RHI 抽象层原则：不直接使用任何具体 API（OpenGL/Vulkan/D3D12）
 */

#include "VFXGraphCore.h"
#include <imgui.h>
#include <string>
#include <sstream>
#include <algorithm>
#include <cmath>

namespace Engine {
namespace VFX {

    // ═══════════════════════════════════════════════════════════
    // Spawn Context Blocks
    // ═══════════════════════════════════════════════════════════

    // ── Spawn Burst: 单次爆发 ──
    class SpawnBurstBlock : public VFXBlock {
    public:
        SpawnBurstBlock(uint32 id)
            : VFXBlock(id, "Spawn Burst", BlockCategory::Spawn_Burst) {
            AddInputPin("Burst Count", VFXPinType::Float).f[0] = 10;
            AddOutputPin("Spawned Count", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// SpawnBurst: emit burstCount particles\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "uint _SpawnBurstCount = " + std::to_string(m_BurstCount) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<SpawnBurstBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragInt("Count", (int*)&m_BurstCount, 1, 1, 10000);
            ImGui::DragFloat("Delay", &m_Delay, 0.01f, 0, 10);
        }

    private:
        uint32 m_BurstCount = 10;
        float  m_Delay = 0.0f;
    };

    // ── Spawn Rate: 持续发射 ──
    class SpawnRateBlock : public VFXBlock {
    public:
        SpawnRateBlock(uint32 id)
            : VFXBlock(id, "Spawn Rate", BlockCategory::Spawn_Rate) {
            AddInputPin("Rate", VFXPinType::Float).f[0] = 100;
            AddOutputPin("Emitted Count", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// SpawnRate: emit rate * dt particles\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _SpawnRate = " + std::to_string(m_Rate) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<SpawnRateBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Rate (per sec)", &m_Rate, 1.0f, 0, 100000);
        }

    private:
        float m_Rate = 100.0f;
    };

    // ═══════════════════════════════════════════════════════════
    // Initialize Context Blocks
    // ═══════════════════════════════════════════════════════════

    // ── Init Life: 生命周期 ──
    class InitLifeBlock : public VFXBlock {
    public:
        InitLifeBlock(uint32 id)
            : VFXBlock(id, "Init Life", BlockCategory::Init_Life) {
            AddOutputPin("LifeTime", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitLife: set lifetime\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _InitLife = " + std::to_string(m_LifeTime) + ";\n"
                   "float _InitLifeRandom = " + std::to_string(m_Randomness) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitLifeBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Life Time", &m_LifeTime, 0.1f, 0.1f, 100);
            ImGui::DragFloat("Randomness", &m_Randomness, 0.01f, 0, 1);
        }

    private:
        float m_LifeTime = 2.0f;
        float m_Randomness = 0.5f;
    };

    // ── Init Position: 初始位置 ──
    class InitPositionBlock : public VFXBlock {
    public:
        InitPositionBlock(uint32 id)
            : VFXBlock(id, "Init Position", BlockCategory::Init_Position) {
            AddOutputPin("Position", VFXPinType::Float3);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitPosition: set initial position\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "int _InitPosMode = " + std::to_string(m_ShapeMode) + ";\n"
                   "float _InitPosRadius = " + std::to_string(m_Radius) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitPositionBlock>(newId);
        }
        void DrawInlineContent() override {
            const char* modes[] = {"Point", "Sphere", "Box", "Cone"};
            ImGui::Combo("Shape", &m_ShapeMode, modes, IM_ARRAYSIZE(modes));
            ImGui::DragFloat("Radius", &m_Radius, 0.1f, 0, 100);
        }

    private:
        int m_ShapeMode = 0;
        float m_Radius = 1.0f;
        float m_BoxSize[3] = {1,1,1};
        bool m_SpreadAlongAxis = false;
        int m_Axis = 1;
    };

    // ── Init Velocity: 初始速度 ──
    class InitVelocityBlock : public VFXBlock {
    public:
        InitVelocityBlock(uint32 id)
            : VFXBlock(id, "Init Velocity", BlockCategory::Init_Velocity) {
            AddOutputPin("Velocity", VFXPinType::Float3);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitVelocity\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _InitSpeed = " + std::to_string(m_Speed) + ";\n"
                   "float _InitSpeedRandom = " + std::to_string(m_RandomSpeed) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitVelocityBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Speed", &m_Speed, 0.1f, 0, 100);
            ImGui::DragFloat("Random", &m_RandomSpeed, 0.1f, 0, 100);
            const char* dirs[] = {"From Center", "Random", "Axis Aligned"};
            ImGui::Combo("Direction", &m_DirectionMode, dirs, IM_ARRAYSIZE(dirs));
        }

    private:
        float m_Speed = 5.0f;
        float m_RandomSpeed = 2.0f;
        int m_DirectionMode = 0;
        float m_Direction[3] = {0,1,0};
    };

    // ── Init Color: 初始颜色 ──
    class InitColorBlock : public VFXBlock {
    public:
        InitColorBlock(uint32 id)
            : VFXBlock(id, "Init Color", BlockCategory::Init_Color) {
            AddOutputPin("Color", VFXPinType::Color);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitColor\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float4 _InitColor = float4(" +
                std::to_string(m_Color[0]) + "," + std::to_string(m_Color[1]) + "," +
                std::to_string(m_Color[2]) + "," + std::to_string(m_Color[3]) + ");\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitColorBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::ColorEdit4("Color", m_Color, ImGuiColorEditFlags_AlphaBar);
        }

    private:
        float m_Color[4] = {1,1,1,1};
        float m_ColorRandom[4] = {0,0,0,0};
    };

    // ── Init Size: 初始大小 ──
    class InitSizeBlock : public VFXBlock {
    public:
        InitSizeBlock(uint32 id)
            : VFXBlock(id, "Init Size", BlockCategory::Init_Size) {
            AddOutputPin("Size", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitSize\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _InitSize = " + std::to_string(m_Size) + ";\n"
                   "float _InitSizeRandom = " + std::to_string(m_RandomSize) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitSizeBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Size", &m_Size, 0.1f, 0, 100);
            ImGui::DragFloat("Random", &m_RandomSize, 0.01f, 0, 1);
        }

    private:
        float m_Size = 1.0f;
        float m_RandomSize = 0.3f;
    };

    // ── Init Rotation: 初始旋转 ──
    class InitRotationBlock : public VFXBlock {
    public:
        InitRotationBlock(uint32 id)
            : VFXBlock(id, "Init Rotation", BlockCategory::Init_Rotation) {
            AddOutputPin("Rotation", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// InitRotation\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _InitRotation = " + std::to_string(m_Rotation) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<InitRotationBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Rotation (deg)", &m_Rotation, 1, 0, 360);
            ImGui::DragFloat("Random (deg)", &m_RandomRotation, 1, 0, 360);
        }

    private:
        float m_Rotation = 0.0f;
        float m_RandomRotation = 360.0f;
    };

    // ═══════════════════════════════════════════════════════════
    // Update Context Blocks
    // ═══════════════════════════════════════════════════════════

    // ── Gravity: 重力 ──
    class GravityBlock : public VFXBlock {
    public:
        GravityBlock(uint32 id)
            : VFXBlock(id, "Gravity", BlockCategory::Update_Gravity) {
            AddInputPin("Scale", VFXPinType::Float).f[0] = 1.0f;
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return std::string("p.velocity.y += ") + std::to_string(m_Gravity * m_Scale) + " * dt;\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _Gravity = " + std::to_string(m_Gravity) + ";\n"
                   "float _GravityScale = " + std::to_string(m_Scale) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<GravityBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Gravity", &m_Gravity, 0.1f, -50, 50);
            ImGui::DragFloat("Scale", &m_Scale, 0.1f, 0, 10);
        }

    private:
        float m_Gravity = -9.8f;
        float m_Scale = 1.0f;
    };

    // ── Drag: 空气阻力 ──
    class DragBlock : public VFXBlock {
    public:
        DragBlock(uint32 id)
            : VFXBlock(id, "Drag", BlockCategory::Update_Drag) {}
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "p.velocity *= (1.0 - " + std::to_string(m_Drag) + " * dt);\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _Drag = " + std::to_string(m_Drag) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<DragBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Drag Coeff", &m_Drag, 0.01f, 0, 1);
        }

    private:
        float m_Drag = 0.1f;
    };

    // ── Turbulence: 湍流 ──
    class TurbulenceBlock : public VFXBlock {
    public:
        TurbulenceBlock(uint32 id)
            : VFXBlock(id, "Turbulence", BlockCategory::Update_Turbulence) {}
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// Turbulence: noise-based velocity perturbation\n"
                   "float _noise = sin(p.position.x * 12.9898 + p.position.z * 78.233 + _VFXTime * " +
                   std::to_string(m_Frequency) + ") * " + std::to_string(m_Strength) + " * dt;\n"
                   "p.velocity.x += _noise; p.velocity.z += _noise;\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _TurbStrength = " + std::to_string(m_Strength) + ";\n"
                   "float _TurbFreq = " + std::to_string(m_Frequency) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<TurbulenceBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Strength", &m_Strength, 0.1f, 0, 100);
            ImGui::DragFloat("Frequency", &m_Frequency, 0.1f, 0, 100);
        }

    private:
        float m_Strength = 1.0f;
        float m_Frequency = 1.0f;
    };

    // ── Color Over Life: 颜色随生命周期变化 ──
    class ColorOverLifeBlock : public VFXBlock {
    public:
        ColorOverLifeBlock(uint32 id)
            : VFXBlock(id, "Color Over Life", BlockCategory::Update_Color) {}
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "p.color = lerp(float4(" +
                std::to_string(m_StartColor[0]) + "," + std::to_string(m_StartColor[1]) + "," +
                std::to_string(m_StartColor[2]) + "," + std::to_string(m_StartColor[3]) + "), float4(" +
                std::to_string(m_EndColor[0]) + "," + std::to_string(m_EndColor[1]) + "," +
                std::to_string(m_EndColor[2]) + "," + std::to_string(m_EndColor[3]) + "), t);\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float4 _ColorStart = float4(" +
                std::to_string(m_StartColor[0]) + "," + std::to_string(m_StartColor[1]) + "," +
                std::to_string(m_StartColor[2]) + "," + std::to_string(m_StartColor[3]) + ");\n"
                "float4 _ColorEnd = float4(" +
                std::to_string(m_EndColor[0]) + "," + std::to_string(m_EndColor[1]) + "," +
                std::to_string(m_EndColor[2]) + "," + std::to_string(m_EndColor[3]) + ");\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<ColorOverLifeBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::ColorEdit4("Start Color", m_StartColor, ImGuiColorEditFlags_AlphaBar);
            ImGui::ColorEdit4("End Color", m_EndColor, ImGuiColorEditFlags_AlphaBar);
        }

    private:
        float m_StartColor[4] = {1,1,1,1};
        float m_EndColor[4] = {0.5f,0.5f,1,0};
    };

    // ── Size Over Life: 大小随生命周期变化 ──
    class SizeOverLifeBlock : public VFXBlock {
    public:
        SizeOverLifeBlock(uint32 id)
            : VFXBlock(id, "Size Over Life", BlockCategory::Update_Size) {}
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "p.size = lerp(" + std::to_string(m_StartSize) + ", " +
                   std::to_string(m_EndSize) + ", t);\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "float _SizeStart = " + std::to_string(m_StartSize) + ";\n"
                   "float _SizeEnd = " + std::to_string(m_EndSize) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<SizeOverLifeBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Start Size", &m_StartSize, 0.1f, 0, 100);
            ImGui::DragFloat("End Size", &m_EndSize, 0.1f, 0, 100);
        }

    private:
        float m_StartSize = 1.0f;
        float m_EndSize = 0.1f;
    };

    // ═══════════════════════════════════════════════════════════
    // Output Context Blocks
    // ═══════════════════════════════════════════════════════════

    // ── Quad Billboard: 面片 ──
    class QuadOutputBlock : public VFXBlock {
    public:
        QuadOutputBlock(uint32 id)
            : VFXBlock(id, "Quad Billboard", BlockCategory::Output_Quad) {
            AddInputPin("Texture", VFXPinType::Texture2D);
            AddInputPin("Blend Mode", VFXPinType::Float).f[0] = 0;
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// QuadOutput: billboard rendering\n";
        }
        std::string GeneratePropertyDecl() const override {
            return "int _BlendMode = " + std::to_string(m_BlendMode) + ";\n"
                   "bool _SoftParticles = " + (m_SoftParticles ? std::string("true") : std::string("false")) + ";\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<QuadOutputBlock>(newId);
        }
        void DrawInlineContent() override {
            const char* blends[] = {"Alpha Blend", "Additive", "Multiply"};
            ImGui::Combo("Blend", &m_BlendMode, blends, IM_ARRAYSIZE(blends));
            ImGui::Checkbox("Soft Particles", &m_SoftParticles);
        }

    private:
        std::string m_TexturePath;
        int m_BlendMode = 0;
        bool m_SoftParticles = true;
        bool m_DepthWrite = false;
    };

    // ── Mesh: 网格粒子 ──
    class MeshOutputBlock : public VFXBlock {
    public:
        MeshOutputBlock(uint32 id)
            : VFXBlock(id, "Mesh", BlockCategory::Output_Mesh) {
            AddInputPin("Mesh", VFXPinType::Float); // placeholder
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// MeshOutput: mesh particle rendering\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<MeshOutputBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::Text("Mesh: %s", m_MeshPath.empty() ? "(none)" : m_MeshPath.c_str());
        }

    private:
        std::string m_MeshPath;
    };

    // ═══════════════════════════════════════════════════════════
    // Utility Blocks
    // ═══════════════════════════════════════════════════════════

    // ── Constant: 常量 ──
    class VFXConstantBlock : public VFXBlock {
    public:
        VFXConstantBlock(uint32 id, int dataType = 0)
            : VFXBlock(id, "Constant", BlockCategory::Utility_Constant), m_DataType(dataType) {
            AddOutputPin("Value", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            std::string typeName = (m_DataType == 0) ? "float" :
                                   (m_DataType == 1) ? "float2" :
                                   (m_DataType == 2) ? "float3" : "float4";
            return typeName + "(" +
                std::to_string(m_Value[0]) + "," + std::to_string(m_Value[1]) + "," +
                std::to_string(m_Value[2]) + "," + std::to_string(m_Value[3]) + ")";
        }
        std::string GeneratePropertyDecl() const override {
            int dim = (m_DataType <= 2) ? (m_DataType + 1) : 4;
            std::string typeName = (dim == 1) ? "float" : (dim == 2) ? "float2" : (dim == 3) ? "float3" : "float4";
            return typeName + " _Constant = " + typeName + "(" +
                std::to_string(m_Value[0]) + "," + std::to_string(m_Value[1]) + "," +
                std::to_string(m_Value[2]) + "," + std::to_string(m_Value[3]) + ");\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<VFXConstantBlock>(newId, m_DataType);
        }
        void DrawInlineContent() override {
            const char* types[] = {"Float", "Float2", "Float3", "Float4", "Color"};
            ImGui::Combo("Type", &m_DataType, types, IM_ARRAYSIZE(types));
            switch (m_DataType) {
                case 0: ImGui::DragFloat("##v", &m_Value[0], 0.01f); break;
                case 1: ImGui::DragFloat2("##v", m_Value, 0.01f); break;
                case 2: ImGui::DragFloat3("##v", m_Value, 0.01f); break;
                case 3: ImGui::DragFloat4("##v", m_Value, 0.01f); break;
                case 4: ImGui::ColorEdit4("##c", m_Value, ImGuiColorEditFlags_AlphaBar); break;
            }
        }

        int GetDataType() const { return m_DataType; }
        void SetDataType(int dt) { m_DataType = dt; }

    private:
        int m_DataType = 0;
        float m_Value[4] = {1,1,1,1};
    };

    // ── Random: 随机 ──
    class RandomBlock : public VFXBlock {
    public:
        RandomBlock(uint32 id)
            : VFXBlock(id, "Random", BlockCategory::Utility_Random) {
            AddOutputPin("Value", VFXPinType::DynamicVector);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "// Random: hash-based random value\n";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<RandomBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragInt("Dimension", &m_Dimension, 1, 1, 4);
            ImGui::DragFloat2("Min", m_Min, 0.01f);
            ImGui::DragFloat2("Max", m_Max, 0.01f);
        }

    private:
        int m_Dimension = 3;
        float m_Min[4] = {0,0,0,0};
        float m_Max[4] = {1,1,1,1};
    };

    // ── Sample Curve: 曲线采样 ──
    class SampleCurveBlock : public VFXBlock {
    public:
        SampleCurveBlock(uint32 id)
            : VFXBlock(id, "Sample Curve", BlockCategory::Utility_Curve) {
            AddInputPin("Time", VFXPinType::Float).f[0] = 0;
            AddOutputPin("Value", VFXPinType::Float);
        }
        std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const override {
            (void)pinOutputs;
            return "lerp(" + std::to_string(m_Value0) + ", " + std::to_string(m_Value1) +
                   ", smoothstep(" + std::to_string(m_Time0) + ", " + std::to_string(m_Time1) + ", t))";
        }
        std::unique_ptr<VFXBlock> Clone(uint32 newId) const override {
            return std::make_unique<SampleCurveBlock>(newId);
        }
        void DrawInlineContent() override {
            ImGui::DragFloat("Value0", &m_Value0, 0.01f);
            ImGui::DragFloat("Value1", &m_Value1, 0.01f);
            ImGui::DragFloat("Time0", &m_Time0, 0.01f, 0, 1);
            ImGui::DragFloat("Time1", &m_Time1, 0.01f, 0, 1);
        }

    private:
        float m_Value0 = 0.0f;
        float m_Value1 = 1.0f;
        float m_Time0 = 0.0f;
        float m_Time1 = 1.0f;
    };

}} // namespace Engine::VFX