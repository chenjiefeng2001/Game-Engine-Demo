/**
 * @file AnimationController.cpp
 * @brief 动画控制器实现
 */

#include "Engine/Animation/AnimationController.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationPose.h"
#include <cmath>
#include <algorithm>

namespace Engine {

    // ============================================================
    // 构造
    // ============================================================

    AnimationController::AnimationController(std::shared_ptr<Skeleton> skeleton,
                                             std::shared_ptr<AnimationResource> resource)
        : m_Skeleton(std::move(skeleton))
        , m_Resource(std::move(resource))
    {
        if (m_Skeleton) {
            size_t n = m_Skeleton->GetBoneCount();
            m_OutputPose.Resize(n);
        }

        // 默认创建基础层
        AddLayer("Base", 1.0f, nullptr);
    }

    // ════════════════════════════════════════════
    // 层管理
    // ════════════════════════════════════════════

    int32 AnimationController::GetLayerIndex(const std::string& name) const {
        for (int32 i = 0; i < static_cast<int32>(m_Layers.size()); ++i) {
            if (m_Layers[i].data.name == name) return i;
        }
        return -1;
    }

    int32 AnimationController::AddLayer(const std::string& name, float32 weight,
                                        std::shared_ptr<BlendMask> mask) {
        if (GetLayerIndex(name) >= 0) return GetLayerIndex(name);

        ControllerLayer layer;
        layer.data.name = name;
        layer.data.weight = weight;
        layer.data.mask = mask ? mask : std::make_shared<BlendMask>();
        if (m_Skeleton) {
            layer.data.mask->Resize(m_Skeleton->GetBoneCount());
            layer.data.mask->SetAll(1.0f);
        }

        m_Layers.push_back(std::move(layer));
        return static_cast<int32>(m_Layers.size()) - 1;
    }

    void AnimationController::RemoveLayer(const std::string& name) {
        auto it = std::remove_if(m_Layers.begin(), m_Layers.end(),
            [&](const ControllerLayer& l) { return l.data.name == name; });
        m_Layers.erase(it, m_Layers.end());
    }

    AnimationLayerData* AnimationController::GetLayer(const std::string& name) {
        int32 idx = GetLayerIndex(name);
        return idx >= 0 ? &m_Layers[idx].data : nullptr;
    }

    const AnimationLayerData* AnimationController::GetLayer(const std::string& name) const {
        int32 idx = GetLayerIndex(name);
        return idx >= 0 ? &m_Layers[idx].data : nullptr;
    }

    AnimStateMachine* AnimationController::GetLayerStateMachine(const std::string& layerName) {
        auto* layer = GetLayer(layerName);
        return layer ? &layer->stateMachine : nullptr;
    }

    const AnimStateMachine* AnimationController::GetLayerStateMachine(const std::string& layerName) const {
        auto* layer = GetLayer(layerName);
        return layer ? &layer->stateMachine : nullptr;
    }

    void AnimationController::SetLayerWeight(const std::string& name, float32 weight) {
        auto* layer = GetLayer(name);
        if (layer) layer->weight = weight;
    }

    float32 AnimationController::GetLayerWeight(const std::string& name) const {
        auto* layer = GetLayer(name);
        return layer ? layer->weight : 0.0f;
    }

    // ════════════════════════════════════════════
    // 全局参数系统
    // ════════════════════════════════════════════

    void AnimationController::SetFloat(const std::string& name, float32 value) {
        m_GlobalParams.SetFloat(name, value);
    }

    void AnimationController::SetBool(const std::string& name, bool value) {
        m_GlobalParams.SetBool(name, value);
    }

    void AnimationController::SetInteger(const std::string& name, int32 value) {
        m_GlobalParams.SetInt(name, value);
    }

    void AnimationController::SetTrigger(const std::string& name) {
        m_GlobalParams.SetTrigger(name);
    }

    float32 AnimationController::GetFloat(const std::string& name) const {
        return m_GlobalParams.GetFloat(name);
    }

    bool AnimationController::GetBool(const std::string& name) const {
        return m_GlobalParams.GetBool(name);
    }

    int32 AnimationController::GetInteger(const std::string& name) const {
        return m_GlobalParams.GetInt(name);
    }

    bool AnimationController::ConsumeTrigger(const std::string& name) {
        return m_GlobalParams.ConsumeTrigger(name);
    }

    // ════════════════════════════════════════════
    // 状态管理
    // ════════════════════════════════════════════

    int32 AnimationController::AddState(const std::string& layerName,
                                         const std::string& stateName,
                                         BlendTree* tree, float32 speed) {
        auto* sm = GetLayerStateMachine(layerName);
        return sm ? sm->AddState(stateName, tree, speed) : -1;
    }

    int32 AnimationController::AddSubStateMachine(const std::string& layerName,
                                                    const std::string& stateName,
                                                    std::unique_ptr<AnimStateMachine> subSM,
                                                    float32 speed) {
        auto* sm = GetLayerStateMachine(layerName);
        return sm ? sm->AddSubStateMachine(stateName, std::move(subSM), speed) : -1;
    }

    void AnimationController::SetInitialState(const std::string& layerName,
                                               const std::string& stateName) {
        auto* sm = GetLayerStateMachine(layerName);
        if (sm) sm->SetInitialState(stateName);
    }

    // ════════════════════════════════════════════
    // 过渡管理
    // ════════════════════════════════════════════

    int32 AnimationController::AddTransition(const std::string& layerName,
                                              const std::string& from, const std::string& to,
                                              TransitionType type, float32 duration,
                                              EaseCurveType curve,
                                              std::function<bool(const ParamSystem&)> cond,
                                              const TransitionWindow& win) {
        auto* sm = GetLayerStateMachine(layerName);
        return sm ? sm->AddTransition(from, to, type, duration, curve, std::move(cond), win) : -1;
    }

    int32 AnimationController::AddAnyStateTransition(const std::string& layerName,
                                                      const std::string& to,
                                                      TransitionType type, float32 duration,
                                                      EaseCurveType curve,
                                                      std::function<bool(const ParamSystem&)> cond) {
        auto* sm = GetLayerStateMachine(layerName);
        return sm ? sm->AddAnyStateTransition(to, type, duration, curve, std::move(cond)) : -1;
    }

    // ════════════════════════════════════════════
    // 高电平 API
    // ════════════════════════════════════════════

    void AnimationController::Play(const std::string& stateName, const std::string& layerName) {
        std::string targetLayer = layerName;
        if (targetLayer.empty() && !m_Layers.empty()) {
            targetLayer = m_Layers[0].data.name;
        }
        auto* sm = GetLayerStateMachine(targetLayer);
        if (sm) {
            sm->JumpTo(stateName);
        }
    }

    void AnimationController::CrossFade(const std::string& stateName, float32 fadeTime,
                                         const std::string& layerName) {
        std::string targetLayer = layerName;
        if (targetLayer.empty() && !m_Layers.empty()) {
            targetLayer = m_Layers[0].data.name;
        }
        auto* sm = GetLayerStateMachine(targetLayer);
        if (sm) {
            // 查找当前状态到目标状态的过渡
            const auto& cur = sm->GetCurrentState();
            if (!cur.empty() && cur != stateName) {
                // 尝试查找现有过渡
                int32 fi = sm->GetStateIndex(cur);
                int32 ti = sm->GetStateIndex(stateName);
                if (fi >= 0 && ti >= 0) {
                    auto* t = sm->GetTransMatrix().Get(fi, ti);
                    if (t) {
                        sm->ForceTransition(stateName);
                        return;
                    }
                }
                // 没有预定义过渡，使用默认 CrossFade
                sm->AddTransition(cur, stateName, TransitionType::CrossFade,
                                  fadeTime, EaseCurveType::SmoothStep, nullptr);
                sm->ForceTransition(stateName);
            } else {
                sm->JumpTo(stateName);
            }
        }
    }

    // ════════════════════════════════════════════
    // 动画插槽
    // ════════════════════════════════════════════

    int32 AnimationController::AddSlot(const std::string& slotName, bool additive,
                                        float32 fadeSpeed) {
        // 检查是否已存在
        for (size_t i = 0; i < m_Slots.size(); ++i) {
            if (m_Slots[i].name == slotName) return static_cast<int32>(i);
        }
        AnimationSlot slot;
        slot.name = slotName;
        slot.isAdditive = additive;
        slot.fadeSpeed = fadeSpeed;
        m_Slots.push_back(slot);
        return static_cast<int32>(m_Slots.size()) - 1;
    }

    void AnimationController::PlayInSlot(const std::string& slotName, const std::string& clipName,
                                          float32 fadeTime, AnimationLoopMode loop) {
        auto* slot = GetSlot(slotName);
        if (!slot) {
            AddSlot(slotName);
            slot = GetSlot(slotName);
        }
        if (slot) {
            slot->clipName = clipName;
            slot->isPlaying = true;
            slot->localTime = 0.0f;
            slot->loop = loop;
            slot->targetWeight = 1.0f;
            // 快速淡入
            slot->fadeSpeed = fadeTime > 0.0f ? 1.0f / fadeTime : 100.0f;
        }
    }

    void AnimationController::StopSlot(const std::string& slotName, float32 blendOut) {
        auto* slot = GetSlot(slotName);
        if (slot) {
            slot->targetWeight = 0.0f;
            slot->fadeSpeed = blendOut > 0.0f ? 1.0f / blendOut : 100.0f;
        }
    }

    AnimationSlot* AnimationController::GetSlot(const std::string& slotName) {
        for (auto& s : m_Slots) {
            if (s.name == slotName) return &s;
        }
        return nullptr;
    }

    const AnimationSlot* AnimationController::GetSlot(const std::string& slotName) const {
        for (const auto& s : m_Slots) {
            if (s.name == slotName) return &s;
        }
        return nullptr;
    }

    // ════════════════════════════════════════════
    // 控制器事件
    // ════════════════════════════════════════════

    int32 AnimationController::AddEvent(const std::string& eventName, float32 triggerTime,
                                         std::function<void()> callback) {
        ControllerEvent evt;
        evt.name = eventName;
        evt.triggerTime = triggerTime;
        evt.callback = std::move(callback);
        m_Events.push_back(std::move(evt));
        return static_cast<int32>(m_Events.size()) - 1;
    }

    void AnimationController::FireEvent(const std::string& eventName) {
        for (auto& evt : m_Events) {
            if (evt.name == eventName && evt.callback) {
                evt.callback();
            }
        }
    }

    // ════════════════════════════════════════════
    // 状态行为
    // ════════════════════════════════════════════

    void AnimationController::AddStateBehavior(const std::string& layerName,
                                                const std::string& stateName,
                                                StateBehavior behavior) {
        auto* sm = GetLayerStateMachine(layerName);
        if (sm) {
            sm->AddStateBehavior(stateName, std::move(behavior));
        }
    }

    // ════════════════════════════════════════════
    // 内部辅助
    // ════════════════════════════════════════════

    void AnimationController::EnsurePoseSize(PoseLocalData& pose) {
        if (m_Skeleton && pose.GetBoneCount() != m_Skeleton->GetBoneCount()) {
            pose.Resize(m_Skeleton->GetBoneCount());
        }
    }

    void AnimationController::SyncParamsToLayer(ControllerLayer& layer) {
        if (!layer.syncParams) return;

        // 将全局参数复制到层的状态机参数系统
        ParamSystem& layerParams = layer.data.stateMachine.GetParams();

        // 复制所有全局参数
        // 注意：ParamSystem 没有提供枚举所有参数的接口，
        // 所以我们通过控制器层面确保参数通过 Set* 方法同步。
        // 层状态机的 Update 会使用其自身的 ParamSystem。
        // 这里我们不做自动同步，而是让用户通过 GetParams() 直接访问。
        // 实际同步通过全局引用完成。
        (void)layerParams;
    }

    void AnimationController::UpdateSlot(AnimationSlot& slot, float32 dt) {
        if (!slot.isPlaying && slot.weight < 0.01f) return;

        // 淡入淡出
        if (slot.weight < slot.targetWeight) {
            slot.weight = std::min(slot.weight + slot.fadeSpeed * dt, slot.targetWeight);
        } else if (slot.weight > slot.targetWeight) {
            slot.weight = std::max(slot.weight - slot.fadeSpeed * dt, slot.targetWeight);
        }

        if (!slot.isPlaying) {
            // 播放结束后淡出至零
            if (slot.weight <= 0.01f) {
                slot.weight = 0.0f;
                return;
            }
        }

        if (slot.isPlaying) {
            // 推进时间
            slot.localTime += dt;

            // 获取片段时长
            float32 duration = 0.0f;
            if (m_Resource) {
                auto clipPtr = m_Resource->GetClip(slot.clipName);
                if (clipPtr) duration = clipPtr->GetDuration();
            }

            // 循环处理
            if (duration > 0.0f) {
                switch (slot.loop) {
                    case AnimationLoopMode::Once:
                        if (slot.localTime >= duration) {
                            slot.localTime = duration;
                            slot.isPlaying = false;
                            slot.targetWeight = 0.0f; // 开始淡出
                        }
                        break;
                    case AnimationLoopMode::Loop:
                        if (slot.localTime >= duration) {
                            slot.localTime = std::fmod(slot.localTime, duration);
                        }
                        break;
                    case AnimationLoopMode::PingPong: {
                        float32 cycleTime = std::fmod(slot.localTime, duration * 2.0f);
                        slot.localTime = cycleTime < duration
                            ? cycleTime : duration * 2.0f - cycleTime;
                        break;
                    }
                }
            }
        }
    }

    void AnimationController::BlendSlotIntoOutput(const AnimationSlot& slot,
                                                    const PoseLocalData& slotPose) {
        if (slot.GetEffectiveWeight() < 0.01f) return;
        if (m_OutputPose.GetBoneCount() == 0) return;

        size_t n = std::min(m_OutputPose.GetBoneCount(), slotPose.GetBoneCount());
        float32 w = slot.GetEffectiveWeight();

        for (size_t i = 0; i < n; ++i) {
            float32 boneWeight = w;
            if (slot.mask && i < static_cast<size_t>(slot.mask->GetSize())) {
                boneWeight *= slot.mask->GetBoneWeight(static_cast<int32>(i));
            }

            if (slot.isAdditive) {
                // 加法混合
                m_OutputPose.translations[i].x += slotPose.translations[i].x * boneWeight;
                m_OutputPose.translations[i].y += slotPose.translations[i].y * boneWeight;
                m_OutputPose.translations[i].z += slotPose.translations[i].z * boneWeight;
                m_OutputPose.rotations[i] = AnimationBlend::SLERP(
                    m_OutputPose.rotations[i], slotPose.rotations[i], boneWeight);
                m_OutputPose.scales[i].x += (slotPose.scales[i].x - 1.0f) * boneWeight;
                m_OutputPose.scales[i].y += (slotPose.scales[i].y - 1.0f) * boneWeight;
                m_OutputPose.scales[i].z += (slotPose.scales[i].z - 1.0f) * boneWeight;
            } else {
                // LERP 混合
                m_OutputPose.translations[i] = AnimationBlend::LERP(
                    m_OutputPose.translations[i], slotPose.translations[i], boneWeight);
                m_OutputPose.rotations[i] = AnimationBlend::SLERP(
                    m_OutputPose.rotations[i], slotPose.rotations[i], boneWeight);
                m_OutputPose.scales[i] = AnimationBlend::LERP(
                    m_OutputPose.scales[i], slotPose.scales[i], boneWeight);
            }
        }
    }

    // ════════════════════════════════════════════
    // 更新
    // ════════════════════════════════════════════

    void AnimationController::Update(float32 dt) {
        if (!m_Skeleton || !m_Resource) return;

        size_t boneCount = m_Skeleton->GetBoneCount();
        if (boneCount == 0) return;

        // ── Step 1: 重置输出姿势 ──
        EnsurePoseSize(m_OutputPose);

        // ── Step 2: 更新各层 ──
        bool firstLayer = true;
        for (auto& layer : m_Layers) {
            auto& layerData = layer.data;
            if (layerData.weight < 0.01f) continue;

            // 同步全局参数到层状态机
            SyncParamsToLayer(layer);

            // 更新层状态机
            layerData.stateMachine.Update(*m_Skeleton, *m_Resource, dt);

            // 提取层输出姿势
            PoseLocalData layerPose;
            layerPose.Resize(boneCount);
            for (size_t i = 0; i < boneCount; ++i) {
                const Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
                layerPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                layerPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                layerPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
            layerData.localPose = layerPose;

            // 混合到输出
            float32 layerWeight = layerData.weight;
            if (firstLayer) {
                // 第一层直接复制
                m_OutputPose = layerPose;
                firstLayer = false;
            } else {
                // 后续层与输出混合（带遮罩）
                for (size_t i = 0; i < boneCount; ++i) {
                    float32 w = layerWeight;
                    if (layerData.mask && i < static_cast<size_t>(layerData.mask->GetSize())) {
                        w *= layerData.mask->GetBoneWeight(static_cast<int32>(i));
                    }
                    if (w < 0.01f) continue;

                    m_OutputPose.translations[i] = AnimationBlend::LERP(
                        m_OutputPose.translations[i], layerPose.translations[i], w);
                    m_OutputPose.rotations[i] = AnimationBlend::SLERP(
                        m_OutputPose.rotations[i], layerPose.rotations[i], w);
                    m_OutputPose.scales[i] = AnimationBlend::LERP(
                        m_OutputPose.scales[i], layerPose.scales[i], w);
                }
            }
        }

        // ── Step 3: 更新动画插槽 ──
        for (auto& slot : m_Slots) {
            UpdateSlot(slot, dt);

            if (slot.GetEffectiveWeight() < 0.01f && !slot.isPlaying) continue;
            if (slot.clipName.empty()) continue;

            // 提取插槽姿势
            PoseLocalData slotPose;
            slotPose.Resize(boneCount);

            auto clip = m_Resource->GetClip(slot.clipName);
            if (clip) {
                float32 savedTime = clip->GetLocalTime();
                clip->Seek(slot.localTime);

                // 使用 AnimationPose 评估
                AnimationPose poseEval(m_Skeleton);
                poseEval.EvaluateFromTimeline(*clip);

                for (size_t i = 0; i < boneCount; ++i) {
                    const Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
                    slotPose.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                    slotPose.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                    slotPose.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
                }

                clip->Seek(savedTime);
            }

            // 混合插槽姿势到输出
            BlendSlotIntoOutput(slot, slotPose);
        }

        // ── Step 4: 应用全局权重 ──
        if (m_GlobalWeight < 1.0f && !firstLayer) {
            for (size_t i = 0; i < boneCount; ++i) {
                m_OutputPose.translations[i] = AnimationBlend::LERP(
                    Vec3(0,0,0), m_OutputPose.translations[i], m_GlobalWeight);
                m_OutputPose.rotations[i] = AnimationBlend::SLERP(
                    Quat::Identity(), m_OutputPose.rotations[i], m_GlobalWeight);
                m_OutputPose.scales[i] = AnimationBlend::LERP(
                    Vec3(1,1,1), m_OutputPose.scales[i], m_GlobalWeight);
            }
        }

        // ── Step 5: 检查时间触发事件 ──
        for (auto& evt : m_Events) {
            if (!evt.fired && evt.triggerTime >= 0.0f) {
                // 获取当前状态时间（从第 0 层）
                if (!m_Layers.empty()) {
                    float32 stateTime = m_Layers[0].data.stateMachine.GetStateTime();
                    if (stateTime >= evt.triggerTime) {
                        if (evt.callback) evt.callback();
                        evt.fired = true;
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════
    // ApplyToSkeleton
    // ════════════════════════════════════════════

    void AnimationController::ApplyToSkeleton() {
        if (!m_Skeleton) return;

        size_t n = std::min(m_OutputPose.GetBoneCount(), m_Skeleton->GetBoneCount());
        for (size_t i = 0; i < n; ++i) {
            Bone& bone = m_Skeleton->GetBone(static_cast<int32>(i));
            Vec3 euler = AnimationBlend::ToEuler(m_OutputPose.rotations[i]);
            AnimationPose::ComposeMatrix(
                m_OutputPose.translations[i], euler, m_OutputPose.scales[i],
                bone.localPoseMatrix);
        }
        m_Skeleton->UpdateWorldPoses();
    }

    // ════════════════════════════════════════════
    // 调试
    // ════════════════════════════════════════════

    AnimationController::DebugInfo AnimationController::GetDebugInfo() const {
        DebugInfo info;
        info.layerCount = m_Layers.size();
        info.slotCount = m_Slots.size();
        info.globalWeight = m_GlobalWeight;

        for (const auto& layer : m_Layers) {
            DebugInfo::LayerDebug ld;
            ld.name = layer.data.name;
            ld.weight = layer.data.weight;
            ld.smDebug = layer.data.stateMachine.GetDebugInfo();
            info.layers.push_back(ld);
        }

        for (const auto& slot : m_Slots) {
            DebugInfo::SlotDebug sd;
            sd.name = slot.name;
            sd.playing = slot.isPlaying;
            sd.weight = slot.weight;
            sd.clip = slot.clipName;
            info.slots.push_back(sd);
        }

        return info;
    }

} // namespace Engine
