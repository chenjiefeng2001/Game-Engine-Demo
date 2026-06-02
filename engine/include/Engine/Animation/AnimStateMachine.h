#pragma once

/**
 * @file AnimStateMachine.h
 * @brief 动画状态机 — 增强版：过渡矩阵、缓动曲线、控制参数
 *
 * ════════════════════════════════════════════
 * 架构
 * ════════════════════════════════════════════
 *
 *   [ParamSystem] → 命名参数 / 节点搜寻 / 控制结构
 *        │
 *   [AnimStateMachine]
 *        │
 *        ├── State ── BlendTree
 *        ├── Transition ── (type, curve, window, duration)
 *        └── TransitionMatrix (稀疏矩阵优化)
 *
 * ════════════════════════════════════════════
 * 增强过渡
 * ════════════════════════════════════════════
 *
 *   TransitionType:
 *     CrossFade   — 标准交叉淡入淡出
 *     Morph       — 逐骨骼形态混合
 *     Switch      — 立即切换
 *     InPlace     — 原地过渡
 *     Sync        — 同步过渡
 *
 *   EaseCurve:
 *     Linear, SmoothStep, Overshoot, Bounce, Elastic
 *
 *   TransitionWindow:
 *     earliestStart, latestStart
 *
 * ════════════════════════════════════════════
 * 稀疏过渡矩阵
 * ════════════════════════════════════════════
 *   只存储有效过渡（非零元素）
 *   O(1) 查找 from→to 过渡
 *
 * ════════════════════════════════════════════
 * 控制参数系统
 * ════════════════════════════════════════════
 *   ParamRef  — 统一参数引用
 *   Selector  — 条件选择参数源
 *   Mixer     — 参数混合
 */

#include "Engine/Animation/BlendTree.h"
#include "Engine/Animation/BlendMask.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cmath>

namespace Engine {

    // 前向声明
    class AnimStateMachine;

    // ════════════════════════════════════════════
    // AnyState 常量 — 表示"任意状态"的特殊名称
    // ════════════════════════════════════════════
    inline constexpr const char* kAnyStateName = "__ANY_STATE__";

    // ════════════════════════════════════════════
    // 状态行为 — 状态生命周期回调
    // ════════════════════════════════════════════
    struct StateBehavior {
        std::string name;

        /** 状态激活时每帧调用 */
        std::function<void(class StateDef& state, float32 dt)> onUpdate;

        /** 进入状态时调用一次 */
        std::function<void(class StateDef& state)> onEnter;

        /** 离开状态时调用一次 */
        std::function<void(class StateDef& state)> onExit;

        StateBehavior() = default;
        explicit StateBehavior(std::string n) : name(std::move(n)) {}
    };

    // ============================================================
    // 过渡类型
    // ============================================================
    enum class TransitionType : uint8 {
        CrossFade  = 0,
        Morph      = 1,
        Switch     = 2,
        InPlace    = 3,
        Sync       = 4,
    };

    // ============================================================
    // 缓动曲线类型
    // ============================================================
    enum class EaseCurveType : uint8 {
        Linear     = 0,
        SmoothStep = 1,
        Overshoot  = 2,
        Bounce     = 3,
        Elastic    = 4,
        Custom     = 5,
    };

    // ============================================================
    // 过渡窗口
    // ============================================================
    struct TransitionWindow {
        float32 earliestStart = 0.0f;
        float32 latestStart   = 0.0f;

        bool IsInWindow(float32 stateTime) const {
            if (stateTime < earliestStart) return false;
            if (latestStart > 0.0f && stateTime > latestStart) return false;
            return true;
        }
    };

    // ============================================================
    // 缓动函数库
    // ============================================================
    struct EaseFunctions {
        static float32 Evaluate(EaseCurveType type, float32 t) {
            t = std::max(0.0f, std::min(1.0f, t));
            switch (type) {
                case EaseCurveType::Linear:     return t;
                case EaseCurveType::SmoothStep: return t * t * (3.0f - 2.0f * t);
                case EaseCurveType::Overshoot:  return t * t * (2.5f * t - 1.5f);
                case EaseCurveType::Bounce:     return BounceImpl(t);
                case EaseCurveType::Elastic:    return ElasticImpl(t);
                default: return t;
            }
        }
    private:
        static float32 BounceImpl(float32 t) {
            if (t < 1.0f/2.75f) return 7.5625f*t*t;
            if (t < 2.0f/2.75f) { t-=1.5f/2.75f; return 7.5625f*t*t+0.75f; }
            if (t < 2.5f/2.75f) { t-=2.25f/2.75f; return 7.5625f*t*t+0.9375f; }
            t-=2.625f/2.75f; return 7.5625f*t*t+0.984375f;
        }
        static float32 ElasticImpl(float32 t) {
            if (t==0||t==1) return t;
            return -std::pow(2.0f,10.0f*(t-1.0f))*std::sin((t-1.1f)*5.0f*3.14159f);
        }
    };

    // ============================================================
    // 参数系统
    // ============================================================
    class ParamSystem {
    public:
        struct Value {
            enum Type : uint8 { Float, Bool, Int, Trigger, Invalid };
            Type type = Invalid;
            float32 f = 0.0f;
            bool    b = false;
            int32   i = 0;

            Value() = default;
            explicit Value(float32 v) : type(Float), f(v) {}
            explicit Value(bool v)    : type(Bool),  b(v) {}
            explicit Value(int32 v)   : type(Int),   i(v) {}
            float32 AsFloat() const { return type==Float?f:(float32)i; }
            bool    AsBool()  const { return type==Bool?b:(f!=0||i!=0); }
            int32   AsInt()   const { return type==Int?i:(int32)f; }
        };

        void SetFloat(const std::string& name, float32 v) { m_Params[name]=Value(v); }
        void SetBool(const std::string& name, bool v)      { m_Params[name]=Value(v); }
        void SetInt(const std::string& name, int32 v)      { m_Params[name]=Value(v); }
        void SetTrigger(const std::string& name)            { m_Triggers[name]=true; }

        float32 GetFloat(const std::string& n, float32 d=0) const {
            auto it=m_Params.find(n); return it!=m_Params.end()?it->second.AsFloat():d;
        }
        bool GetBool(const std::string& n, bool d=false) const {
            auto it=m_Params.find(n); return it!=m_Params.end()?it->second.AsBool():d;
        }
        int32 GetInt(const std::string& n, int32 d=0) const {
            auto it=m_Params.find(n); return it!=m_Params.end()?it->second.AsInt():d;
        }
        bool ConsumeTrigger(const std::string& n) {
            auto it=m_Triggers.find(n);
            if(it!=m_Triggers.end()&&it->second){it->second=false;return true;}
            return false;
        }

        // ── 节点搜寻参数 ──
        Value GetByPath(const std::string& path) const { return ResolvePath(path); }
        void SetByPath(const std::string& path, const Value& val) { m_Params[path]=val; }

        // ── 控制结构 ──
        using ConditionFn = std::function<bool(const ParamSystem&)>;
        struct Selector {
            std::string output; ConditionFn condition;
            std::string trueSrc, falseSrc;
        };
        void AddSelector(const Selector& s) { m_Selectors.push_back(s); }

        struct Mixer {
            std::string output, inputA, inputB, weightSrc;
        };
        void AddMixer(const Mixer& m) { m_Mixers.push_back(m); }

        void UpdateControlStructures();

        // ── 参数组管理 ──
        struct ParamGroup {
            std::string name;
            std::unordered_map<std::string, Value> params;
        };
        void AddGroup(const std::string& groupName);
        ParamGroup* GetGroup(const std::string& name);

    private:
        std::unordered_map<std::string, Value> m_Params;
        std::unordered_map<std::string, bool>  m_Triggers;
        std::vector<Selector> m_Selectors;
        std::vector<Mixer>    m_Mixers;
        std::unordered_map<std::string, ParamGroup> m_Groups;
        Value ResolvePath(const std::string& path) const;
    };

    // ============================================================
    // 稀疏过渡矩阵
    // ============================================================
    struct TransitionDef {
        std::string             name;
        std::string             fromState, toState;
        TransitionType          type      = TransitionType::CrossFade;
        float32                 duration  = 0.2f;
        EaseCurveType           easeCurve = EaseCurveType::SmoothStep;
        TransitionWindow        window;
        std::function<bool(const ParamSystem&)> condition;
    };

    class SparseTransitionMatrix {
    public:
        void AddTransition(const TransitionDef& t, int32 fi, int32 ti) { m_Entries.push_back({fi,ti,t}); }
        const TransitionDef* Get(int32 fi, int32 ti) const {
            for(auto& e:m_Entries) if(e.fromIdx==fi&&e.toIdx==ti) return &e.data;
            return nullptr;
        }
        std::vector<const TransitionDef*> GetFrom(int32 fi) const {
            std::vector<const TransitionDef*> r;
            for(auto& e:m_Entries) if(e.fromIdx==fi) r.push_back(&e.data);
            return r;
        }
        void Clear() { m_Entries.clear(); }
        size_t GetNonZeroCount() const { return m_Entries.size(); }
        size_t GetMemoryBytes() const { return m_Entries.size()*(sizeof(int32)*2+sizeof(TransitionDef)); }
    private:
        struct Entry { int32 fromIdx,toIdx; TransitionDef data; };
        std::vector<Entry> m_Entries;
    };

    // ============================================================
    // 状态定义（增强版）
    // ============================================================
    struct StateDef {
        std::string name;                    ///< 状态名称
        BlendTree*  blendTree       = nullptr;   ///< 绑定的混合树（非拥有）
        float32     speedMultiplier = 1.0f;  ///< 播放速度倍率

        // ── 子状态机（可选） ──
        std::unique_ptr<AnimStateMachine> subStateMachine; ///< 嵌套子状态机

        // ── 自身拥有的 BlendTree（可选） ──
        std::unique_ptr<BlendTree> ownBlendTree;  ///< 状态拥有的混合树（替代非拥有指针）

        // ── 状态行为 ──
        std::vector<StateBehavior> behaviors;  ///< 状态生命周期行为

        StateDef() = default;
        StateDef(std::string n, BlendTree* t, float32 s)
            : name(std::move(n)), blendTree(t), speedMultiplier(s) {}

        /** 设置状态的 BlendTree（自动接管所有权） */
        void SetOwnBlendTree(std::unique_ptr<BlendTree> tree) {
            ownBlendTree = std::move(tree);
            blendTree = ownBlendTree.get();
        }

        /** 获取有效的 BlendTree 指针（递归查找子状态机，实现见 .cpp） */
        BlendTree* GetEffectiveBlendTree() const;
    };

    // ============================================================
    // 增强动画状态机
    // ============================================================
    class AnimStateMachine {
    public:
        AnimStateMachine() = default;
        ~AnimStateMachine() = default;
        AnimStateMachine(const AnimStateMachine&) = delete;
        AnimStateMachine& operator=(const AnimStateMachine&) = delete;
        AnimStateMachine(AnimStateMachine&&) = default;
        AnimStateMachine& operator=(AnimStateMachine&&) = default;

        // ── 状态管理 ──
        int32 AddState(const std::string& name, BlendTree* tree=nullptr, float32 speed=1.0f);
        void RemoveState(const std::string& name);
        void SetInitialState(const std::string& n) { m_InitState=n; }
        int32 GetStateIndex(const std::string& n) const;
        const std::string& GetCurrentState() const { return m_CurState; }
        StateDef& GetState(int32 index) { return m_States[index]; }
        const StateDef& GetState(int32 index) const { return m_States[index]; }
        size_t GetStateCount() const noexcept { return m_States.size(); }

        /** 添加带子状态机的状态 */
        int32 AddSubStateMachine(const std::string& name,
                                 std::unique_ptr<AnimStateMachine> subSM,
                                 float32 speed = 1.0f);

        /** 为状态添加行为 */
        void AddStateBehavior(int32 stateIndex, StateBehavior behavior);
        void AddStateBehavior(const std::string& stateName, StateBehavior behavior);

        // ── 增强过渡 ──
        int32 AddTransition(const std::string& from, const std::string& to,
                            TransitionType type=TransitionType::CrossFade,
                            float32 duration=0.2f,
                            EaseCurveType curve=EaseCurveType::SmoothStep,
                            std::function<bool(const ParamSystem&)> cond=nullptr,
                            const TransitionWindow& win=TransitionWindow{});

        /**
         * @brief 添加 AnyState 过渡（从任意状态到目标状态的过渡）
         * @param to 目标状态名称
         * @return 过渡索引，失败返回 -1
         *
         * AnyState 过渡会在每帧检查条件，一旦满足就强制跳转到目标状态。
         * 适合用于"受伤"、"死亡"等可以从任何状态进入的中断式过渡。
         */
        int32 AddAnyStateTransition(const std::string& to,
                                    TransitionType type=TransitionType::CrossFade,
                                    float32 duration=0.2f,
                                    EaseCurveType curve=EaseCurveType::SmoothStep,
                                    std::function<bool(const ParamSystem&)> cond=nullptr);

        const SparseTransitionMatrix& GetTransMatrix() const { return m_TransMatrix; }
        const std::vector<TransitionDef>& GetAnyStateTransitions() const { return m_AnyStateTransitions; }

        // ── 参数系统 ──
        ParamSystem& GetParams() { return m_Params; }
        const ParamSystem& GetParams() const { return m_Params; }

        // ── 更新 ──
        void Update(Skeleton& skel, const AnimationResource& res, float32 dt);
        void JumpTo(const std::string& name);
        void ForceTransition(const std::string& name);

        // ── 状态查询 ──
        bool IsInTransition() const noexcept { return m_InTrans; }
        float32 GetStateTime() const noexcept { return m_StateStartTime; }
        float32 GetTransitionProgress() const noexcept {
            return m_InTrans && m_ActiveTrans && m_ActiveTrans->duration > 0.0f
                ? m_TransTimer / m_ActiveTrans->duration : 0.0f;
        }
        const std::string& GetPreviousState() const noexcept { return m_PrevState; }
        const std::string& GetTransitionTarget() const noexcept { return m_TransTo; }

        // ── 子状态机查询 ──
        AnimStateMachine* GetActiveSubStateMachine();
        const AnimStateMachine* GetActiveSubStateMachine() const;

        // ── 调试 ──
        struct DebugInfo {
            std::string cur, transTo;
            float32 progress=0, stateTime=0;
            int32 transCount=0;
            TransitionType activeType=TransitionType::CrossFade;
            EaseCurveType activeCurve=EaseCurveType::SmoothStep;
        };
        DebugInfo GetDebugInfo() const;

        // ── 序列化 ──
        std::string Serialize() const;
        bool Deserialize(const std::string& j);
        using LiveCallback = std::function<void(AnimStateMachine&)>;
        void SetLiveCallback(LiveCallback cb) { m_LiveCb=std::move(cb); }
        void Reload(const std::string& json);

    private:
        std::vector<StateDef> m_States;
        std::unordered_map<std::string,int32> m_StateMap;
        std::string m_InitState, m_CurState, m_PrevState;
        float32 m_StateStartTime=0;

        SparseTransitionMatrix m_TransMatrix;
        std::vector<TransitionDef> m_AnyStateTransitions;  ///< AnyState 过渡列表
        bool m_InTrans=false;
        std::string m_TransFrom, m_TransTo;
        float32 m_TransTimer=0;
        const TransitionDef* m_ActiveTrans=nullptr;

        ParamSystem m_Params;
        LiveCallback m_LiveCb;

        // ── 内部辅助 ──
        /** 触发状态进入行为 */
        void FireOnEnter(int32 stateIdx);
        /** 触发状态更新行为 */
        void FireOnUpdate(int32 stateIdx, float32 dt);
        /** 触发状态退出行为 */
        void FireOnExit(int32 stateIdx);
        /** 检查并处理 AnyState 过渡 */
        bool ProcessAnyStateTransitions();
        /** 递归更新子状态机 */
        void UpdateSubStateMachine(Skeleton& skel, const AnimationResource& res,
                                    float32 dt, StateDef& state);
    };

} // namespace Engine
