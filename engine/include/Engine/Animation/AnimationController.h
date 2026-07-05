#pragma once

/**
 * @file AnimationController.h
 * @brief 动画控制器 — 统一的高层动画控制接口
 *
 * ════════════════════════════════════════════
 * 架构（参考 Unity Animator Controller / Unreal Animation Blueprint）
 * ════════════════════════════════════════════
 *
 *   AnimationController
 *   ├── 全局参数系统 (ParamSystem)
 *   ├── 层 (Layer)
 *   │   ├── Layer 0: 基础层（全身动画）
 *   │   │   └── AnimStateMachine + BlendTree
 *   │   ├── Layer 1: 上层（上半身叠加）
 *   │   │   └── AnimStateMachine + BlendTree + BlendMask
 *   │   └── ...
 *   ├── 动画插槽 (Slot)
 *   │   ├── Slot "Action": 一次性动作动画
 *   │   └── Slot "Additive": 叠加动画
 *   └── 高电平 API: Play / CrossFade / SetFloat / SetTrigger
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   // 创建控制器
 *   AnimationController controller(skeleton, resource);
 *
 *   // 添加层
 *   controller.AddLayer("Base", 1.0f);
 *   controller.AddLayer("UpperBody", 0.5f, upperBodyMask);
 *
 *   // 在基础层添加状态
 *   controller.AddState("Base", "Idle", idleTree);
 *   controller.AddState("Base", "Walk", walkTree);
 *   controller.AddTransition("Base", "Idle", "Walk", TransitionType::CrossFade, 0.2f,
 *       [](const ParamSystem& p) { return p.GetFloat("Speed") > 0.1f; });
 *
 *   // 设置全局参数
 *   controller.SetFloat("Speed", 0.0f);
 *   controller.SetTrigger("Jump");
 *
 *   // 播放动画（高电平 API）
 *   controller.CrossFade("Walk", 0.3f);
 *   controller.PlayInSlot("Action", "punch_clip", 0.1f);
 *
 *   // 每帧更新
 *   controller.Update(dt);
 *
 *   // 获取输出姿势
 *   const auto& pose = controller.GetOutputPose();
 *
 * ════════════════════════════════════════════
 * 与 AnimationInstance 的关系
 * ════════════════════════════════════════════
 *
 *   AnimationController 是更高层的抽象，它：
 *   - 包装了 AnimationLayerSystem 的层管理
 *   - 提供全局参数系统（自动同步到各层状态机）
 *   - 提供动画插槽系统（一次性/叠加动画）
 *   - 提供 Play / CrossFade 等便捷方法
 *   - 负责层的最终混合与输出
 *
 *   AnimationInstance 可以持有一个 AnimationController，
 *   在 Update() 中使用控制器替代手动管理 Pipeline。
 */

#include "Engine/Animation/AnimStateMachine.h"
#include "Engine/Animation/AnimationLayer.h"
#include "Engine/Animation/BlendMask.h"
#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationResource.h"
#include "Engine/Animation/AnimationPipeline.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>

namespace Engine {

    // ============================================================
    // 动画插槽 — 一次性/叠加动画
    // ============================================================
    /**
     * @brief 动画插槽用于播放一次性或叠加动画
     *
     * 插槽可以在现有动画之上播放一个临时动画片段，
     * 播放完毕后自动淡出。适合用于"出拳"、"受击"等一次性动作。
     *
     * 每个插槽有：
     *   - 名称（唯一标识）
     *   - 当前播放的片段
     *   - 权重（淡入/淡出控制）
     *   - BlendMask（可选，控制影响哪些骨骼）
     *   - 循环模式
     */
    struct AnimationSlot {
        std::string name;                        ///< 插槽名称
        std::string clipName;                    ///< 当前播放的片段名
        float32     weight       = 0.0f;         ///< 当前权重（0~1）
        float32     targetWeight = 0.0f;         ///< 目标权重
        float32     fadeSpeed    = 3.0f;         ///< 淡入淡出速度（每秒）
        bool        isPlaying    = false;        ///< 是否正在播放
        bool        isAdditive   = false;        ///< 是否为加法混合
        float32     localTime    = 0.0f;         ///< 播放时间
        AnimationLoopMode loop   = AnimationLoopMode::Once;  ///< 循环模式
        std::shared_ptr<BlendMask> mask;         ///< 可选骨骼遮罩

        /** 获取当前有效权重（用于混合） */
        float32 GetEffectiveWeight() const { return weight * (isPlaying ? 1.0f : 0.0f); }

        /** 重置插槽 */
        void Reset() {
            isPlaying = false;
            weight = 0.0f;
            targetWeight = 0.0f;
            localTime = 0.0f;
        }
    };

    // ============================================================
    // 控制器事件 — 命名回调
    // ============================================================
    struct ControllerEvent {
        std::string name;                        ///< 事件名称
        float32     triggerTime;                 ///< 触发时间点
        std::function<void()> callback;          ///< 回调函数
        bool        fired = false;               ///< 是否已触发
    };

    // ============================================================
    // 动画控制器
    // ============================================================
    class AnimationController {
    public:
        /**
         * @brief 构造动画控制器
         * @param skeleton 目标骨骼
         * @param resource 动画资源（片段、网格等）
         */
        AnimationController(std::shared_ptr<Skeleton> skeleton,
                            std::shared_ptr<AnimationResource> resource);
        ~AnimationController() = default;

        // 禁止拷贝
        AnimationController(const AnimationController&) = delete;
        AnimationController& operator=(const AnimationController&) = delete;
        AnimationController(AnimationController&&) = default;
        AnimationController& operator=(AnimationController&&) = default;

        // ════════════════════════════════════════════
        // 层管理
        // ════════════════════════════════════════════

        /**
         * @brief 添加动画层
         * @param name   层名称
         * @param weight 层权重
         * @param mask   可选骨骼遮罩
         * @return 层索引
         */
        int32 AddLayer(const std::string& name, float32 weight = 1.0f,
                       std::shared_ptr<BlendMask> mask = nullptr);

        /** 移除层 */
        void RemoveLayer(const std::string& name);

        /** 获取层 */
        AnimationLayerData* GetLayer(const std::string& name);
        const AnimationLayerData* GetLayer(const std::string& name) const;

        /** 获取层的状态机引用 */
        AnimStateMachine* GetLayerStateMachine(const std::string& layerName);
        const AnimStateMachine* GetLayerStateMachine(const std::string& layerName) const;

        /** 设置层权重 */
        void SetLayerWeight(const std::string& name, float32 weight);
        float32 GetLayerWeight(const std::string& name) const;

        /** 获取层数量 */
        size_t GetLayerCount() const noexcept { return m_Layers.size(); }

        // ════════════════════════════════════════════
        // 全局参数系统
        // ════════════════════════════════════════════

        void SetFloat(const std::string& name, float32 value);
        void SetBool(const std::string& name, bool value);
        void SetInteger(const std::string& name, int32 value);
        void SetTrigger(const std::string& name);

        float32 GetFloat(const std::string& name) const;
        bool    GetBool(const std::string& name) const;
        int32   GetInteger(const std::string& name) const;
        bool    ConsumeTrigger(const std::string& name);

        /** 获取全局参数系统引用 */
        ParamSystem& GetParams() noexcept { return m_GlobalParams; }
        const ParamSystem& GetParams() const noexcept { return m_GlobalParams; }

        // ════════════════════════════════════════════
        // 状态管理（便捷方法，操作第 0 层）
        // ════════════════════════════════════════════

        /**
         * @brief 在指定层添加状态
         * @param layerName 层名称
         * @param stateName 状态名称
         * @param tree      绑定的混合树
         * @param speed     速度倍率
         * @return 状态索引
         */
        int32 AddState(const std::string& layerName, const std::string& stateName,
                       BlendTree* tree = nullptr, float32 speed = 1.0f);

        /** 添加带子状态机的状态 */
        int32 AddSubStateMachine(const std::string& layerName, const std::string& stateName,
                                 std::unique_ptr<AnimStateMachine> subSM,
                                 float32 speed = 1.0f);

        /** 设置初始状态 */
        void SetInitialState(const std::string& layerName, const std::string& stateName);

        // ════════════════════════════════════════════
        // 过渡管理
        // ════════════════════════════════════════════

        int32 AddTransition(const std::string& layerName,
                            const std::string& from, const std::string& to,
                            TransitionType type = TransitionType::CrossFade,
                            float32 duration = 0.2f,
                            EaseCurveType curve = EaseCurveType::SmoothStep,
                            std::function<bool(const ParamSystem&)> cond = nullptr,
                            const TransitionWindow& win = TransitionWindow{});

        int32 AddAnyStateTransition(const std::string& layerName,
                                     const std::string& to,
                                     TransitionType type = TransitionType::CrossFade,
                                     float32 duration = 0.2f,
                                     EaseCurveType curve = EaseCurveType::SmoothStep,
                                     std::function<bool(const ParamSystem&)> cond = nullptr);

        // ════════════════════════════════════════════
        // 高电平 API
        // ════════════════════════════════════════════

        /**
         * @brief 直接跳转到指定状态
         * @param stateName 目标状态
         * @param layerName 层名称（空字符串表示第 0 层）
         */
        void Play(const std::string& stateName, const std::string& layerName = "");

        /**
         * @brief 带淡入淡出地过渡到指定状态
         * @param stateName 目标状态
         * @param fadeTime  淡入淡出时间
         * @param layerName 层名称（空字符串表示第 0 层）
         */
        void CrossFade(const std::string& stateName, float32 fadeTime = 0.3f,
                       const std::string& layerName = "");

        // ════════════════════════════════════════════
        // 动画插槽
        // ════════════════════════════════════════════

        /**
         * @brief 添加动画插槽
         * @param slotName  插槽名称
         * @param additive  是否为加法混合
         * @param fadeSpeed 淡入淡出速度
         * @return 插槽索引
         */
        int32 AddSlot(const std::string& slotName, bool additive = false,
                      float32 fadeSpeed = 3.0f);

        /**
         * @brief 在插槽中播放动画
         * @param slotName  插槽名称
         * @param clipName  片段名称
         * @param fadeTime  淡入时间
         * @param loop      循环模式
         */
        void PlayInSlot(const std::string& slotName, const std::string& clipName,
                        float32 fadeTime = 0.1f,
                        AnimationLoopMode loop = AnimationLoopMode::Once);

        /** 停止插槽动画 */
        void StopSlot(const std::string& slotName, float32 blendOut = 0.1f);

        /** 获取插槽 */
        AnimationSlot* GetSlot(const std::string& slotName);
        const AnimationSlot* GetSlot(const std::string& slotName) const;

        // ════════════════════════════════════════════
        // 控制器事件
        // ════════════════════════════════════════════

        /** 添加控制器事件（在指定时间触发回调） */
        int32 AddEvent(const std::string& eventName, float32 triggerTime,
                       std::function<void()> callback);

        /** 触发命名事件（立即触发所有同名事件） */
        void FireEvent(const std::string& eventName);

        // ════════════════════════════════════════════
        // 状态行为
        // ════════════════════════════════════════════

        /** 为指定层的指定状态添加行为 */
        void AddStateBehavior(const std::string& layerName, const std::string& stateName,
                              StateBehavior behavior);

        // ════════════════════════════════════════════
        // 更新与输出
        // ════════════════════════════════════════════

        /**
         * @brief 更新控制器
         * @param dt 帧时间步长
         *
         * 更新流程：
         *   1. 同步全局参数到各层
         *   2. 更新各层的状态机
         *   3. 更新动画插槽
         *   4. 按权重+遮罩混合各层输出
         *   5. 叠加插槽动画
         *   6. 生成最终输出姿势
         */
        void Update(float32 dt);

        /** 获取输出姿势 */
        const PoseLocalData& GetOutputPose() const noexcept { return m_OutputPose; }

        /** 将输出姿势写入骨骼并更新世界矩阵 */
        void ApplyToSkeleton();

        /** 获取骨骼引用 */
        Skeleton& GetSkeleton() { return *m_Skeleton; }
        const Skeleton& GetSkeleton() const { return *m_Skeleton; }

        // ════════════════════════════════════════════
        // 调试
        // ════════════════════════════════════════════

        struct DebugInfo {
            size_t layerCount;
            size_t slotCount;
            float32 globalWeight;
            struct LayerDebug {
                std::string name;
                float32 weight;
                AnimStateMachine::DebugInfo smDebug;
            };
            std::vector<LayerDebug> layers;
            struct SlotDebug {
                std::string name;
                bool playing;
                float32 weight;
                std::string clip;
            };
            std::vector<SlotDebug> slots;
        };
        DebugInfo GetDebugInfo() const;

    private:
        // ── 骨骼与资源 ──
        std::shared_ptr<Skeleton>           m_Skeleton;
        std::shared_ptr<AnimationResource>  m_Resource;

        // ── 层系统（扩展自 AnimationLayerData） ──
        struct ControllerLayer {
            AnimationLayerData data;
            // 控制器扩展数据
            bool syncParams = true;   ///< 是否自动同步全局参数到此层
        };
        std::vector<ControllerLayer> m_Layers;

        // ── 全局参数 ──
        ParamSystem m_GlobalParams;

        // ── 动画插槽 ──
        std::vector<AnimationSlot> m_Slots;

        // ── 控制器事件 ──
        std::vector<ControllerEvent> m_Events;

        // ── 输出 ──
        PoseLocalData m_OutputPose;
        float32       m_GlobalWeight = 1.0f;

        // ── 内部辅助 ──

        /** 同步全局参数到指定层的状态机 */
        void SyncParamsToLayer(ControllerLayer& layer);

        /** 更新单个插槽 */
        void UpdateSlot(AnimationSlot& slot, float32 dt);

        /** 混合插槽姿势到输出 */
        void BlendSlotIntoOutput(const AnimationSlot& slot,
                                 const PoseLocalData& slotPose);

        /** 获取层索引 */
        int32 GetLayerIndex(const std::string& name) const;

        /** 确保 PoseLocalData 匹配骨骼数量 */
        void EnsurePoseSize(PoseLocalData& pose);
    };

} // namespace Engine
