#pragma once

/**
 * @file VFXGraphCore.h
 * @brief VFX 图核心数据结构 — GPU 粒子系统节点图编辑器核心
 *
 * 对标 Unity VFX Graph / Unreal Niagara 的节点式视觉特效编辑系统。
 * 基于生命周期上下文（Context）和模块块（Block）架构范式。
 *
 * 设计原则：
 *   - 上下文（Context）是五大生命周期容器（Spawn/Init/Update/Output/System）
 *   - 块（Block）是可拖入容器的数学/物理/渲染模块
 *   - 引脚连接实现数据流，块内参数通过属性面板编辑
 */

#include "Engine/Types.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <glm/glm.hpp>

namespace Engine {
namespace VFX {

    // ============================================================
    // 粒子容量
    // ============================================================
    constexpr uint32 kDefaultParticleCapacity = 65536;

    // ============================================================
    // 引脚方向
    // ============================================================
    enum class PinDirection : uint8 {
        Input,
        Output
    };

    // ============================================================
    // VFX 数据类型（带颜色语义编码）
    // ============================================================
    enum class VFXPinType : uint8 {
        Float,          // 蓝 - 标量
        Float2,         // 黄绿 - vec2
        Float3,         // 黄 - vec3
        Float4,         // 粉 - vec4
        Color,          // 粉 - RGBA (拾色器)
        Texture2D,      // 橙 - 2D纹理
        Texture3D,      // 深橙 - 3D纹理/矢量场
        Boolean,        // 红 - 布尔
        ParticleData,   // 紫 - 粒子数据流（特殊类型，仅用于隐式连接）
        DynamicVector,  // 灰 - 动态推断
        Count
    };

    inline const char* VFXPinTypeName(VFXPinType t) {
        switch (t) {
            case VFXPinType::Float:        return "float";
            case VFXPinType::Float2:       return "float2";
            case VFXPinType::Float3:       return "float3";
            case VFXPinType::Float4:       return "float4";
            case VFXPinType::Color:        return "float4";
            case VFXPinType::Texture2D:    return "Texture2D";
            case VFXPinType::Texture3D:    return "Texture3D";
            case VFXPinType::Boolean:      return "bool";
            case VFXPinType::ParticleData: return "ParticleData";
            case VFXPinType::DynamicVector:return "float4";
            default:                       return "float";
        }
    }

    // ============================================================
    // 引脚定义
    // ============================================================
    struct VFXPin {
        uint32      id;
        std::string name;
        PinDirection direction;
        VFXPinType  type;
        uint32      nodeId;
        bool        isConnected = false;

        // 默认值
        union { float f[4] = {0,0,0,0}; bool b; };
        std::string texturePath;
    };

    // ============================================================
    // 连接
    // ============================================================
    struct VFXLink {
        uint32 id;
        uint32 outputNodeId;
        uint32 outputPinId;
        uint32 inputNodeId;
        uint32 inputPinId;
    };

    // ============================================================
    // 编译错误
    // ============================================================
    struct VFXCompileError {
        enum Severity { Info, Warning, Error };
        Severity severity = Error;
        uint32 nodeId = 0;
        uint32 pinId = 0;
        std::string message;
    };

    // ============================================================
    // 上下文类型（五大生命周期容器）
    // ============================================================
    enum class ContextType : uint8 {
        System,       ///< 全局变量（LOD、时间缩放）
        Spawn,        ///< 生成阶段：何时/产生多少粒子
        Initialize,   ///< 初始化阶段：粒子初始状态
        Update,       ///< 更新阶段：每帧物理/数学模拟
        Output,       ///< 输出阶段：渲染表现
        Count
    };

    inline const char* ContextTypeName(ContextType ct) {
        switch (ct) {
            case ContextType::System:     return "System";
            case ContextType::Spawn:      return "Spawn";
            case ContextType::Initialize: return "Initialize";
            case ContextType::Update:     return "Update";
            case ContextType::Output:     return "Output";
            default:                      return "Unknown";
        }
    }

    inline const char* ContextTypeDisplayName(ContextType ct) {
        switch (ct) {
            case ContextType::System:     return "System (系统)";
            case ContextType::Spawn:      return "Spawn (生成)";
            case ContextType::Initialize: return "Initialize (初始化)";
            case ContextType::Update:     return "Update (更新)";
            case ContextType::Output:     return "Output (输出)";
            default:                      return "Unknown";
        }
    }

    // ============================================================
    // 块类别
    // ============================================================
    enum class BlockCategory : uint8 {
        Spawn_Burst,        ///< 单次爆发
        Spawn_Rate,         ///< 持续发射
        Spawn_Distance,     ///< 距离触发

        Init_Life,          ///< 生命周期
        Init_Position,      ///< 初始位置
        Init_Velocity,      ///< 初始速度
        Init_Color,         ///< 初始颜色
        Init_Size,          ///< 初始大小
        Init_Rotation,      ///< 初始旋转

        Update_Gravity,     ///< 重力
        Update_Drag,        ///< 空气阻力
        Update_Turbulence,  ///< 湍流
        Update_Vortex,      ///< 涡旋
        Update_VectorField, ///< 矢量场
        Update_Noise,       ///< 噪声
        Update_Color,       ///< 颜色随生命周期变化
        Update_Size,        ///< 大小随生命周期变化
        Update_Collision,   ///< 碰撞

        Output_Quad,        ///< 面片 (Billboard)
        Output_Mesh,        ///< 网格
        Output_Trail,       ///< 拖尾
        Output_Light,       ///< 光照注入

        Utility_Constant,   ///< 常量
        Utility_Random,     ///< 随机
        Utility_Curve,      ///< 曲线采样
        Utility_SampleTex,  ///< 纹理采样
        Utility_Math,       ///< 数学运算
        Utility_Count
    };

    inline const char* BlockCategoryName(BlockCategory bc) {
        switch (bc) {
            case BlockCategory::Spawn_Burst:       return "Spawn Burst";
            case BlockCategory::Spawn_Rate:        return "Spawn Rate";
            case BlockCategory::Spawn_Distance:    return "Spawn Distance";
            case BlockCategory::Init_Life:         return "Init Life";
            case BlockCategory::Init_Position:     return "Init Position";
            case BlockCategory::Init_Velocity:     return "Init Velocity";
            case BlockCategory::Init_Color:        return "Init Color";
            case BlockCategory::Init_Size:         return "Init Size";
            case BlockCategory::Init_Rotation:     return "Init Rotation";
            case BlockCategory::Update_Gravity:    return "Gravity";
            case BlockCategory::Update_Drag:       return "Drag";
            case BlockCategory::Update_Turbulence: return "Turbulence";
            case BlockCategory::Update_Vortex:     return "Vortex";
            case BlockCategory::Update_VectorField:return "Vector Field";
            case BlockCategory::Update_Noise:      return "Noise";
            case BlockCategory::Update_Color:      return "Color Over Life";
            case BlockCategory::Update_Size:       return "Size Over Life";
            case BlockCategory::Update_Collision:  return "Collision";
            case BlockCategory::Output_Quad:       return "Quad Billboard";
            case BlockCategory::Output_Mesh:       return "Mesh";
            case BlockCategory::Output_Trail:      return "Trail Ribbon";
            case BlockCategory::Output_Light:      return "Light";
            case BlockCategory::Utility_Constant:  return "Constant";
            case BlockCategory::Utility_Random:    return "Random";
            case BlockCategory::Utility_Curve:     return "Curve Sample";
            case BlockCategory::Utility_SampleTex: return "Sample Texture";
            case BlockCategory::Utility_Math:      return "Math";
            default: return "Unknown";
        }
    }

    inline const char* BlockCategoryGroupName(BlockCategory bc) {
        if (bc <= BlockCategory::Spawn_Distance)    return "Spawn";
        if (bc <= BlockCategory::Init_Rotation)     return "Initialize";
        if (bc <= BlockCategory::Update_Collision)  return "Update";
        if (bc <= BlockCategory::Output_Light)      return "Output";
        return "Utility";
    }

    inline ContextType BlockCategoryToContext(BlockCategory bc) {
        if (bc <= BlockCategory::Spawn_Distance)    return ContextType::Spawn;
        if (bc <= BlockCategory::Init_Rotation)     return ContextType::Initialize;
        if (bc <= BlockCategory::Update_Collision)  return ContextType::Update;
        if (bc <= BlockCategory::Output_Light)      return ContextType::Output;
        return ContextType::System;
    }

    // ============================================================
    // VFX 块基类 (Block)
    // ============================================================
    class VFXBlock {
    public:
        VFXBlock(uint32 id, const std::string& name, BlockCategory category)
            : m_Id(id), m_Name(name), m_Category(category) {}
        virtual ~VFXBlock() = default;

        uint32              GetId() const { return m_Id; }
        void                SetId(uint32 id) { m_Id = id; }
        const std::string&  GetName() const { return m_Name; }
        void                SetName(const std::string& n) { m_Name = n; }
        BlockCategory       GetCategory() const { return m_Category; }

        const std::vector<VFXPin>& GetInputPins() const { return m_InputPins; }
        const std::vector<VFXPin>& GetOutputPins() const { return m_OutputPins; }
        std::vector<VFXPin>&       GetInputPins() { return m_InputPins; }
        std::vector<VFXPin>&       GetOutputPins() { return m_OutputPins; }

        void SetPosition(float x, float y) { m_PosX = x; m_PosY = y; }
        float GetPosX() const { return m_PosX; }
        float GetPosY() const { return m_PosY; }
        void SetSize(float w, float h) { m_SizeX = w; m_SizeY = h; }
        float GetSizeX() const { return m_SizeX; }
        float GetSizeY() const { return m_SizeY; }
        bool  IsCollapsed() const { return m_Collapsed; }
        void  SetCollapsed(bool c) { m_Collapsed = c; }

        /// 启用/禁用块（决定是否参与代码生成）
        bool IsEnabled() const { return m_Enabled; }
        void SetEnabled(bool e) { m_Enabled = e; }

        /// 生成 GPU Compute Shader 代码
        virtual std::string GenerateHLSL(const std::unordered_map<uint32, std::string>& pinOutputs) const = 0;

        /// 生成属性声明（CBuffer）
        virtual std::string GeneratePropertyDecl() const { return ""; }

        virtual std::unique_ptr<VFXBlock> Clone(uint32 newId) const = 0;

        /// 内联编辑控件（属性面板中使用）
        virtual void DrawInlineContent() {}

    protected:
        uint32      m_Id;
        std::string m_Name;
        BlockCategory m_Category;
        std::vector<VFXPin> m_InputPins;
        std::vector<VFXPin> m_OutputPins;
        float m_PosX = 100.0f, m_PosY = 100.0f;
        float m_SizeX = 160.0f, m_SizeY = 80.0f;
        bool  m_Collapsed = false;
        bool  m_Enabled = true;

        VFXPin& AddInputPin(const std::string& name, VFXPinType type) {
            m_InputPins.push_back({ nextPinId(), name, PinDirection::Input, type, m_Id });
            return m_InputPins.back();
        }
        VFXPin& AddOutputPin(const std::string& name, VFXPinType type) {
            m_OutputPins.push_back({ nextPinId(), name, PinDirection::Output, type, m_Id });
            return m_OutputPins.back();
        }

    private:
        static uint32 nextPinId() {
            static uint32 s_PinIdCounter = 1;
            return s_PinIdCounter++;
        }
    };

    // ============================================================
    // 上下文容器（Context）
    // ============================================================
    struct VFXContext {
        ContextType type;
        std::string name;
        float posX = 0, posY = 0;
        float sizeX = 400, sizeY = 300;
        bool collapsed = false;

        /// 此上下文包含的块列表（有序）
        std::vector<uint32> blockIds;

        /// 编辑器可见性
        bool visible = true;
    };

    // ============================================================
    // 黑板参数
    // ============================================================
    struct VFXBlackboardProperty {
        std::string name;
        VFXPinType  type = VFXPinType::Float;
        union { float f[4] = {0,0,0,0}; bool b; };
        std::string tooltip;
        std::string texturePath;  // 用于 Texture2D/Texture3D
    };

    // ============================================================
    // 向后兼容类型（OpenGLComputeParticles 等旧代码使用）
    // ============================================================

    /// @deprecated 使用 VFXGraph Block 系统代替
    enum class EmitterType : uint8 {
        Burst,
        Continuous,
        Distance,
        Count
    };

    /// @deprecated 旧 EmitterConfig — 请使用 VFXGraph Block 系统代替
    struct EmitterConfig {
        EmitterType type = EmitterType::Continuous;
        uint32 maxParticles = kDefaultParticleCapacity;
        float  emitRate = 100.0f;
        float  lifetime = 2.0f;
        float  startSpeed = 5.0f;
        float  startSize = 1.0f;
        float  startColor[4] = {1,1,1,1};
        float  gravityModifier = 1.0f;
        bool   loop = true;
        bool   prewarm = false;
        float  lifetimeRandom = 0.0f;
        float  speedRandom = 0.5f;
        float  sizeRandom = 0.2f;
    };

    /// @deprecated 旧 ForceField — 请使用 VFXGraph Block 系统代替
    struct ForceField {
        float gravity = -9.8f;
        float drag = 0.0f;
        float turbulence = 0.0f;
        float noiseStrength = 0.0f;
        float wind[3] = {0,0,0};
        std::string vectorFieldPath;
        float vectorFieldStength = 0.0f;
    };

    /// @deprecated 旧 RenderConfig — 请使用 VFXGraph Block 系统代替
    struct RenderConfig {
        enum class RenderMode : uint8 {
            Billboard, Mesh, Trail, Stripe, Count
        };
        RenderMode mode = RenderMode::Billboard;
        std::string texturePath;
        bool depthWrite = false;
        bool depthTest = true;
        bool softParticles = true;
        bool castShadows = false;
        bool receiveShadows = false;
        enum class SortMode : uint8 { None, Distance, Age };
        SortMode sortMode = SortMode::Distance;
    };

    // ============================================================
    // VFX 图主类
    // ============================================================
    class VFXGraph {
    public:
        VFXGraph();
        ~VFXGraph() = default;

        // ── 块操作 ──
        uint32 AddBlock(std::unique_ptr<VFXBlock> block);
        bool   RemoveBlock(uint32 blockId);
        VFXBlock* GetBlock(uint32 blockId);
        const VFXBlock* GetBlock(uint32 blockId) const;
        const std::unordered_map<uint32, std::unique_ptr<VFXBlock>>& GetBlocks() const { return m_Blocks; }

        // ── 连接操作 ──
        uint32 AddLink(uint32 outputBlockId, uint32 outputPinId, uint32 inputBlockId, uint32 inputPinId);
        bool   RemoveLink(uint32 linkId);
        bool   RemoveLinkByPins(uint32 outputBlockId, uint32 outputPinId, uint32 inputBlockId, uint32 inputPinId);
        void   RemoveLinksConnectedToPin(uint32 pinId);
        const std::vector<VFXLink>& GetLinks() const { return m_Links; }
        bool CanConnectPins(const VFXPin& outPin, const VFXPin& inPin, std::string& outReason) const;
        uint32 ReplaceLink(uint32 outputBlockId, uint32 outputPinId, uint32 inputBlockId, uint32 inputPinId);

        // ── 上下文管理 ──
        VFXContext& GetOrCreateContext(ContextType type);
        VFXContext& CreateNewContext(ContextType type);
        bool RemoveContext(ContextType type);
        void RemoveBlockFromAllContexts(uint32 blockId);
        VFXContext* GetContext(ContextType type);
        const VFXContext* GetContext(ContextType type) const;
        std::vector<VFXContext>& GetContexts() { return m_Contexts; }
        const std::vector<VFXContext>& GetContexts() const { return m_Contexts; }

        // ── 拓扑排序 ──
        std::vector<uint32> TopologicalSort(std::vector<VFXCompileError>* outErrors = nullptr) const;

        // ── 脏标记 ──
        void MarkDirty(bool dirty = true) { m_IsDirty = dirty; }
        bool IsDirty() const { return m_IsDirty; }

        // ── 黑板 ──
        std::vector<VFXBlackboardProperty>& GetProperties() { return m_Properties; }
        const std::vector<VFXBlackboardProperty>& GetProperties() const { return m_Properties; }
        void AddProperty(const VFXBlackboardProperty& prop) { m_Properties.push_back(prop); MarkDirty(); }
        void RemoveProperty(int index);

        // ── 序列化 ──
        std::string ToJSON() const;
        bool FromJSON(const std::string& json);
        bool SaveToFile(const std::string& path) const;
        bool LoadFromFile(const std::string& path);

        // ── 编译校验 ──
        std::vector<VFXCompileError> Validate() const;

        // ── 清除 ──
        void Clear();

        /// 创建默认模板（空的生命周期 Context + 默认 Spawn Rate Block）
        void CreateDefaultTemplate();

        /// 根据 BlockCategory 创建 Block 实例（用于反序列化）
        std::unique_ptr<VFXBlock> CreateBlockByCategory(BlockCategory category, uint32 id) const;

        // ── 运行时参数 ──
        uint32 GetParticleCapacity() const { return m_ParticleCapacity; }
        void SetParticleCapacity(uint32 cap) { m_ParticleCapacity = cap; MarkDirty(); }

        float GetDuration() const { return m_Duration; }
        void SetDuration(float d) { m_Duration = d; MarkDirty(); }

        bool IsLooping() const { return m_Looping; }
        void SetLooping(bool l) { m_Looping = l; MarkDirty(); }

        bool IsPrewarmed() const { return m_Prewarm; }
        void SetPrewarmed(bool p) { m_Prewarm = p; MarkDirty(); }

    private:
        bool HasPathTo(uint32 fromBlockId, uint32 toBlockId, std::unordered_set<uint32>& visited) const;

        std::unordered_map<uint32, std::unique_ptr<VFXBlock>> m_Blocks;
        std::vector<VFXLink> m_Links;
        std::vector<VFXContext> m_Contexts;
        std::vector<VFXBlackboardProperty> m_Properties;
        uint32 m_NextBlockId = 1;
        uint32 m_NextLinkId = 1;
        bool m_IsDirty = false;

        // 运行时元数据
        uint32 m_ParticleCapacity = kDefaultParticleCapacity;
        float  m_Duration = 5.0f;
        bool   m_Looping = true;
        bool   m_Prewarm = false;
    };

    // ============================================================
    // VFX 运行时实例
    // ============================================================
    struct ParticleData {
        float position[3];
        float velocity[3];
        float color[4];
        float size;
        float lifetime;
        float age;
        uint32 alive;
        float pad[2];
    };

    class VFXInstance {
    public:
        VFXInstance();
        ~VFXInstance();

        void SetGraph(std::shared_ptr<VFXGraph> graph) { m_Graph = graph; }
        std::shared_ptr<VFXGraph> GetGraph() const { return m_Graph; }

        void Play()  { m_Playing = true; }
        void Stop()  { m_Playing = false; Clear(); }
        void Pause() { m_Paused = !m_Paused; }

        bool IsPlaying() const { return m_Playing; }
        bool IsPaused() const { return m_Paused; }

        void OnUpdate(float dt);

        const ParticleData* GetParticleBuffer() const { return m_Particles.data(); }
        uint32 GetParticleCount() const { return m_AliveCount; }
        uint32 GetMaxCapacity() const { return m_MaxCapacity; }

        void Clear();

    private:
        void Emit(uint32 count);
        void UpdateParticles(float dt);

        std::shared_ptr<VFXGraph> m_Graph;
        std::vector<ParticleData> m_Particles;
        uint32 m_AliveCount = 0;
        uint32 m_MaxCapacity = kDefaultParticleCapacity;
        float m_EmitAccum = 0.0f;
        float m_TotalTime = 0.0f;
        uint32 m_CurrentSeed = 0;
        bool m_Playing = false;
        bool m_Paused = false;
    };

    // ============================================================
    // VFX 管理器
    // ============================================================
    class VFXManager {
    public:
        static VFXManager& Get();

        uint32 Spawn(std::shared_ptr<VFXGraph> graph);
        void   Despawn(uint32 id);
        VFXInstance* Get(uint32 id);

        void OnUpdate(float dt);
        void Clear();

        const std::unordered_map<uint32, std::unique_ptr<VFXInstance>>& GetAll() const { return m_Instances; }

    private:
        VFXManager() = default;
        std::unordered_map<uint32, std::unique_ptr<VFXInstance>> m_Instances;
        uint32 m_NextId = 1;
    };

}} // namespace Engine::VFX