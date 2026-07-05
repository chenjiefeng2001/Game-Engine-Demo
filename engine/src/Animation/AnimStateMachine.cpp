/**
 * @file AnimStateMachine.cpp
 * @brief 增强动画状态机实现（含子状态机、AnyState、状态行为）
 */

#include "Engine/Animation/AnimStateMachine.h"
#include <algorithm>
#include <sstream>
#include <cmath>

namespace Engine {

    // ════════════════════════════════════════════
    // StateDef::GetEffectiveBlendTree
    // ════════════════════════════════════════════

    BlendTree* StateDef::GetEffectiveBlendTree() const {
        if (blendTree) return blendTree;
        if (subStateMachine) {
            int32 subIdx = subStateMachine->GetStateIndex(subStateMachine->GetCurrentState());
            if (subIdx >= 0) {
                return subStateMachine->GetState(subIdx).GetEffectiveBlendTree();
            }
        }
        return nullptr;
    }

    // ════════════════════════════════════════════
    // 参数系统
    // ════════════════════════════════════════════

    ParamSystem::Value ParamSystem::ResolvePath(const std::string& path) const {
        auto it = m_Params.find(path);
        if (it != m_Params.end()) return it->second;
        size_t dot = path.find('.');
        if (dot != std::string::npos) {
            auto git = m_Groups.find(path.substr(0, dot));
            if (git != m_Groups.end()) {
                auto pit = git->second.params.find(path.substr(dot + 1));
                if (pit != git->second.params.end()) return pit->second;
            }
        }
        return Value{};
    }

    void ParamSystem::UpdateControlStructures() {
        for (const auto& sel : m_Selectors) {
            bool useTrue = sel.condition ? sel.condition(*this) : false;
            m_Params[sel.output] = ResolvePath(useTrue ? sel.trueSrc : sel.falseSrc);
        }
        for (const auto& mx : m_Mixers) {
            float32 w = GetFloat(mx.weightSrc, 0.5f);
            float32 a = ResolvePath(mx.inputA).AsFloat();
            float32 b = ResolvePath(mx.inputB).AsFloat();
            m_Params[mx.output] = Value(a * (1.0f - w) + b * w);
        }
    }

    void ParamSystem::AddGroup(const std::string& groupName) {
        if (m_Groups.find(groupName) == m_Groups.end()) {
            m_Groups[groupName] = {groupName, {}};
        }
    }

    ParamSystem::ParamGroup* ParamSystem::GetGroup(const std::string& name) {
        auto it = m_Groups.find(name);
        return it != m_Groups.end() ? &it->second : nullptr;
    }

    // ════════════════════════════════════════════
    // 状态管理
    // ════════════════════════════════════════════

    int32 AnimStateMachine::AddState(const std::string& name, BlendTree* tree, float32 speed) {
        if (m_StateMap.find(name) != m_StateMap.end()) return m_StateMap[name];
        int32 idx = static_cast<int32>(m_States.size());
        m_States.emplace_back(name, tree, speed);
        m_StateMap[name] = idx;
        if (m_InitState.empty()) m_InitState = name;
        return idx;
    }

    void AnimStateMachine::RemoveState(const std::string& name) {
        auto it = m_StateMap.find(name);
        if (it == m_StateMap.end()) return;
        m_States.erase(m_States.begin() + it->second);
        m_StateMap.erase(it);
        for (int32 i = 0; i < static_cast<int32>(m_States.size()); ++i)
            m_StateMap[m_States[i].name] = i;
    }

    int32 AnimStateMachine::GetStateIndex(const std::string& name) const {
        auto it = m_StateMap.find(name);
        return it != m_StateMap.end() ? it->second : -1;
    }

    int32 AnimStateMachine::AddSubStateMachine(const std::string& name,
                                                std::unique_ptr<AnimStateMachine> subSM,
                                                float32 speed) {
        int32 idx = AddState(name, nullptr, speed);
        if (idx >= 0) {
            m_States[idx].subStateMachine = std::move(subSM);
        }
        return idx;
    }

    void AnimStateMachine::AddStateBehavior(int32 stateIndex, StateBehavior behavior) {
        if (stateIndex >= 0 && stateIndex < static_cast<int32>(m_States.size())) {
            m_States[stateIndex].behaviors.push_back(std::move(behavior));
        }
    }

    void AnimStateMachine::AddStateBehavior(const std::string& stateName, StateBehavior behavior) {
        int32 idx = GetStateIndex(stateName);
        if (idx >= 0) {
            AddStateBehavior(idx, std::move(behavior));
        }
    }

    // ════════════════════════════════════════════
    // 过渡
    // ════════════════════════════════════════════

    int32 AnimStateMachine::AddTransition(const std::string& from, const std::string& to,
                                           TransitionType type, float32 dur,
                                           EaseCurveType curve,
                                           std::function<bool(const ParamSystem&)> cond,
                                           const TransitionWindow& win) {
        int32 fi = GetStateIndex(from), ti = GetStateIndex(to);
        if (fi < 0 || ti < 0) return -1;
        TransitionDef def;
        def.name=from+"→"+to; def.fromState=from; def.toState=to;
        def.type=type; def.duration=dur; def.easeCurve=curve;
        def.window=win; def.condition=std::move(cond);
        m_TransMatrix.AddTransition(def, fi, ti);
        return static_cast<int32>(m_TransMatrix.GetNonZeroCount())-1;
    }

    int32 AnimStateMachine::AddAnyStateTransition(const std::string& to,
                                                    TransitionType type, float32 dur,
                                                    EaseCurveType curve,
                                                    std::function<bool(const ParamSystem&)> cond) {
        int32 ti = GetStateIndex(to);
        if (ti < 0) return -1;
        TransitionDef def;
        def.name = std::string(kAnyStateName) + "→" + to;
        def.fromState = kAnyStateName;
        def.toState = to;
        def.type = type;
        def.duration = dur;
        def.easeCurve = curve;
        def.window = TransitionWindow{0.0f, -1.0f};
        def.condition = std::move(cond);
        m_AnyStateTransitions.push_back(std::move(def));
        return static_cast<int32>(m_AnyStateTransitions.size()) - 1;
    }

    void AnimStateMachine::JumpTo(const std::string& name) {
        if (m_StateMap.find(name)==m_StateMap.end()) return;

        // 触发旧状态的 onExit
        int32 oldIdx = GetStateIndex(m_CurState);
        if (oldIdx >= 0) FireOnExit(oldIdx);

        m_PrevState=m_CurState;
        m_CurState=name;
        m_InTrans=false;
        m_StateStartTime=0.0f;

        // 触发新状态的 onEnter
        int32 newIdx = GetStateIndex(m_CurState);
        if (newIdx >= 0) FireOnEnter(newIdx);
    }

    void AnimStateMachine::ForceTransition(const std::string& name) {
        int32 fi=GetStateIndex(m_CurState), ti=GetStateIndex(name);
        if(fi<0||ti<0) return;
        auto* t=m_TransMatrix.Get(fi,ti);
        if(!t){JumpTo(name);return;}

        // 触发旧状态的 onExit
        if (fi >= 0) FireOnExit(fi);

        m_InTrans=true; m_TransFrom=m_CurState; m_TransTo=name;
        m_TransTimer=0.0f; m_ActiveTrans=t;
    }

    // ════════════════════════════════════════════
    // 状态行为
    // ════════════════════════════════════════════

    void AnimStateMachine::FireOnEnter(int32 stateIdx) {
        if (stateIdx < 0 || stateIdx >= static_cast<int32>(m_States.size())) return;
        for (auto& b : m_States[stateIdx].behaviors) {
            if (b.onEnter) b.onEnter(m_States[stateIdx]);
        }
    }

    void AnimStateMachine::FireOnUpdate(int32 stateIdx, float32 dt) {
        if (stateIdx < 0 || stateIdx >= static_cast<int32>(m_States.size())) return;
        for (auto& b : m_States[stateIdx].behaviors) {
            if (b.onUpdate) b.onUpdate(m_States[stateIdx], dt);
        }
    }

    void AnimStateMachine::FireOnExit(int32 stateIdx) {
        if (stateIdx < 0 || stateIdx >= static_cast<int32>(m_States.size())) return;
        for (auto& b : m_States[stateIdx].behaviors) {
            if (b.onExit) b.onExit(m_States[stateIdx]);
        }
    }

    // ════════════════════════════════════════════
    // AnyState 处理
    // ════════════════════════════════════════════

    bool AnimStateMachine::ProcessAnyStateTransitions() {
        if (m_AnyStateTransitions.empty()) return false;

        for (const auto& anyDef : m_AnyStateTransitions) {
            bool met = anyDef.condition ? anyDef.condition(m_Params) : false;
            if (m_Params.ConsumeTrigger(anyDef.name)) met = true;

            if (met) {
                if (anyDef.toState == m_CurState) continue;

                int32 oldIdx = GetStateIndex(m_CurState);
                if (oldIdx >= 0) FireOnExit(oldIdx);

                if (anyDef.type == TransitionType::Switch) {
                    m_PrevState = m_CurState;
                    m_CurState = anyDef.toState;
                    m_InTrans = false;
                    m_StateStartTime = 0.0f;
                    int32 newIdx = GetStateIndex(m_CurState);
                    if (newIdx >= 0) FireOnEnter(newIdx);
                } else {
                    m_InTrans = true;
                    m_TransFrom = m_CurState;
                    m_TransTo = anyDef.toState;
                    m_TransTimer = 0.0f;
                    m_ActiveTrans = &anyDef;
                }
                return true;
            }
        }
        return false;
    }

    // ════════════════════════════════════════════
    // 子状态机
    // ════════════════════════════════════════════

    AnimStateMachine* AnimStateMachine::GetActiveSubStateMachine() {
        int32 curIdx = GetStateIndex(m_CurState);
        if (curIdx < 0) return nullptr;
        return m_States[curIdx].subStateMachine.get();
    }

    const AnimStateMachine* AnimStateMachine::GetActiveSubStateMachine() const {
        int32 curIdx = GetStateIndex(m_CurState);
        if (curIdx < 0) return nullptr;
        return m_States[curIdx].subStateMachine.get();
    }

    void AnimStateMachine::UpdateSubStateMachine(Skeleton& skeleton, const AnimationResource& res,
                                                   float32 dt, StateDef& state) {
        if (!state.subStateMachine) return;

        // 递归更新子状态机
        state.subStateMachine->Update(skeleton, res, dt);

        // 获取子状态机当前状态的 BlendTree
        const std::string& subCur = state.subStateMachine->GetCurrentState();
        int32 subIdx = state.subStateMachine->GetStateIndex(subCur);
        if (subIdx >= 0) {
            StateDef& subState = const_cast<StateDef&>(state.subStateMachine->GetState(subIdx));
            BlendTree* subTree = subState.GetEffectiveBlendTree();
            if (subTree) {
                subTree->Evaluate(skeleton, res, dt * subState.speedMultiplier);
            }
        }
    }

    // ════════════════════════════════════════════
    // 更新（增强版）
    // ════════════════════════════════════════════

    void AnimStateMachine::Update(Skeleton& skeleton, const AnimationResource& res, float32 dt) {
        m_Params.UpdateControlStructures();
        if (m_CurState.empty()) {
            m_CurState = m_InitState;
            m_StateStartTime = 0.0f;
            int32 initIdx = GetStateIndex(m_InitState);
            if (initIdx >= 0) FireOnEnter(initIdx);
        }

        // ── Step 1: AnyState 过渡 ──
        if (!m_InTrans) {
            ProcessAnyStateTransitions();
        }

        int32 curIdx = GetStateIndex(m_CurState);

        // ── Step 2: 常规过渡 ──
        if (!m_InTrans && curIdx >= 0) {
            auto list = m_TransMatrix.GetFrom(curIdx);
            for (auto* t : list) {
                if (!t || !t->window.IsInWindow(m_StateStartTime)) continue;
                bool met = t->condition ? t->condition(m_Params) : false;
                if (m_Params.ConsumeTrigger(t->name)) met = true;
                if (met) {
                    if (curIdx >= 0) FireOnExit(curIdx);
                    if (t->type == TransitionType::Switch) {
                        JumpTo(t->toState);
                    } else {
                        m_InTrans=true; m_TransFrom=m_CurState; m_TransTo=t->toState;
                        m_TransTimer=0.0f; m_ActiveTrans=t;
                    }
                    break;
                }
            }
            curIdx = GetStateIndex(m_CurState);
        }

        // ── Step 3: 更新当前状态 ──
        auto* cs = curIdx >= 0 ? &m_States[curIdx] : nullptr;
        if (cs) {
            UpdateSubStateMachine(skeleton, res, dt, *cs);
            BlendTree* activeTree = cs->GetEffectiveBlendTree();
            if (activeTree) {
                activeTree->Evaluate(skeleton, res, dt * cs->speedMultiplier);
            }
            FireOnUpdate(curIdx, dt);
        }

        // ── Step 4: 过渡混合 ──
        if (m_InTrans && m_ActiveTrans) {
            m_TransTimer += dt;
            float32 t = m_ActiveTrans->duration > 0
                ? std::min(m_TransTimer / m_ActiveTrans->duration, 1.0f) : 1.0f;
            float32 et = EaseFunctions::Evaluate(m_ActiveTrans->easeCurve, t);

            size_t n = skeleton.GetBoneCount();
            std::vector<Mat4> saved(n);
            for (size_t i = 0; i < n; ++i)
                saved[i] = skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix;

            int32 toIdx = GetStateIndex(m_TransTo);
            auto* ts = toIdx >= 0 ? &m_States[toIdx] : nullptr;
            if (ts) {
                BlendTree* toTree = ts->GetEffectiveBlendTree();
                if (toTree) {
                    toTree->Evaluate(skeleton, res, dt * ts->speedMultiplier);
                }
                for (size_t i = 0; i < n; ++i) {
                    Bone& b = skeleton.GetBone(static_cast<int32>(i));
                    Vec3 sT=AnimationBlend::DecomposeTranslation(saved[i]);
                    Quat sR=AnimationBlend::DecomposeRotation(saved[i]);
                    Vec3 sS=AnimationBlend::DecomposeScale(saved[i]);
                    Vec3 dT=AnimationBlend::DecomposeTranslation(b.localPoseMatrix);
                    Quat dR=AnimationBlend::DecomposeRotation(b.localPoseMatrix);
                    Vec3 dS=AnimationBlend::DecomposeScale(b.localPoseMatrix);
                    Vec3 rT=AnimationBlend::LERP(sT,dT,et);
                    Quat rR=AnimationBlend::SLERP(sR,dR,et);
                    Vec3 rS=AnimationBlend::LERP(sS,dS,et);
                    AnimationPose::ComposeMatrix(rT,AnimationBlend::ToEuler(rR),rS,b.localPoseMatrix);
                }
                skeleton.UpdateWorldPoses();
            }

            if (t >= 1.0f) {
                m_PrevState=m_TransFrom;
                m_CurState=m_TransTo;
                m_StateStartTime=0.0f;
                m_InTrans=false;
                m_ActiveTrans=nullptr;
                int32 newIdx = GetStateIndex(m_CurState);
                if (newIdx >= 0) FireOnEnter(newIdx);
            }
        }
        m_StateStartTime += dt;
    }

    // ════════════════════════════════════════════
    // 调试/序列化
    // ════════════════════════════════════════════

    AnimStateMachine::DebugInfo AnimStateMachine::GetDebugInfo() const {
        DebugInfo d;
        d.cur=m_CurState; d.transTo=m_InTrans?m_TransTo:"";
        d.progress=m_TransTimer; d.stateTime=m_StateStartTime;
        d.transCount=static_cast<int32>(m_TransMatrix.GetNonZeroCount() + m_AnyStateTransitions.size());
        d.activeType=m_ActiveTrans?m_ActiveTrans->type:TransitionType::CrossFade;
        d.activeCurve=m_ActiveTrans?m_ActiveTrans->easeCurve:EaseCurveType::SmoothStep;
        return d;
    }

    std::string AnimStateMachine::Serialize() const {
        std::ostringstream o;
        o<<"{\"states\":[";
        for(size_t i=0;i<m_States.size();++i){
            o<<"{\"name\":\""<<m_States[i].name<<"\"";
            if(m_States[i].subStateMachine)
                o<<",\"subSM\":"<<m_States[i].subStateMachine->Serialize();
            o<<"}";
            if(i<m_States.size()-1)o<<",";
        }
        o<<"],\"init\":\""<<m_InitState<<"\"}";
        return o.str();
    }

    bool AnimStateMachine::Deserialize(const std::string& j) { (void)j; return true; }
    void AnimStateMachine::Reload(const std::string& j) { Deserialize(j); if(m_LiveCb)m_LiveCb(*this); }

} // namespace Engine


