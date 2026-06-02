#pragma once

/**
 * @file AnimationInstance.h
 * @brief 动画实例 — 每个动画角色的运行时状态
 *
 * AnimationInstance 包含每个动画化实体的独立状态数据：
 *   1. 片段状态 (ClipState) — 每个活跃片段的播放进度、循环模式
 *   2. 混合规格 (BlendSpec) — 哪些片段参与混合、权重
 *   3. 分部骨骼关节权重 (PerBoneWeights) — BlendMask 控制每骨骼参与度
 *   4. 局部姿势 (LocalPose) — 当前帧在骨骼局部空间的变换
 *   5. 全局姿势 (GlobalPose) — 层级累乘后的世界空间变换
 *      + 矩阵调色板 (MatrixPalette) — 最终蒙皮矩阵供 GPU 使用
 *
 * 每个 AnimationInstance 引用一个 Shared AnimationResource，
 * 共享资源包含骨骼、蒙皮网格和动画片段数据。
 */

#include "Engine/Animation/AnimationResource.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/BlendMask.h"
#include "Engine/Animation/AnimationPipeline.h"
#include "Engine/Animation/ConstraintSolver.h"
#include <string>
#include <memory>
#include <vector>
#include <functional>

namespace Engine {

    // ============================================================
    // 片段状态 — 单个动画片段的播放状态
    // ============================================================
    struct ClipState {
        std::string clipName;           ///< 片段名称（在 AnimationResource 中查找）
        float32     localTime  = 0.0f;  ///< 当前播放时间
        float32     timeScale  = 1.0f;  ///< 时间缩放
        TimelineState state    = TimelineState::Stopped;  ///< 播放状态
        AnimationLoopMode loop = AnimationLoopMode::Once;  ///< 循环模式

        ClipState() = default;
        explicit ClipState(std::string name) : clipName(std::move(name)) {}
    };

    // ============================================================
    // 混合规格 — 定义如何混合多个片段
    // ============================================================
    struct BlendSpec {
        /// 单个混合层
        struct Layer {
            std::string clipName;           ///< 片段名称
            float32     weight    = 1.0f;   ///< 混合权重
            bool        additive  = false;  ///< 是否为加法混合
            std::shared_ptr<BlendMask> mask;  ///< 可选遮罩

            Layer() = default;
            Layer(std::string name, float32 w = 1.0f, bool add = false)
                : clipName(std::move(name)), weight(w), additive(add) {}
            Layer(std::string name, const BlendMask& m, float32 w = 1.0f, bool add = false)
                : clipName(std::move(name)), weight(w), additive(add)
                , mask(std::make_shared<BlendMask>(m)) {}
        };

        std::vector<Layer> layers;  ///< 混合层列表

        void Clear() { layers.clear(); }

        void AddLayer(const Layer& layer) { layers.push_back(layer); }
        void AddLayer(std::string clipName, float32 weight = 1.0f,
                      bool additive = false) {
            layers.emplace_back(std::move(clipName), weight, additive);
        }
        void AddLayer(std::string clipName, const BlendMask& mask,
                      float32 weight = 1.0f, bool additive = false) {
            layers.emplace_back(std::move(clipName), mask, weight, additive);
        }

        bool IsEmpty() const { return layers.empty(); }
        size_t GetLayerCount() const { return layers.size(); }
    };

    // ============================================================
    // 全局姿势数据
    // ============================================================
    struct GlobalPoseData {
        std::vector<Vec3> translations;  ///< 全局平移（世界空间）
        std::vector<Quat> rotations;     ///< 全局旋转（世界空间）
        std::vector<Vec3> scales;        ///< 全局缩放

        void Resize(size_t count) {
            translations.resize(count, Vec3(0, 0, 0));
            rotations.resize(count, Quat::Identity());
            scales.resize(count, Vec3(1, 1, 1));
        }
        size_t GetBoneCount() const noexcept { return translations.size(); }
        bool IsValid() const noexcept {
            return translations.size() == rotations.size()
                && rotations.size() == scales.size();
        }
    };

    // ============================================================
    // 动画实例
    // ============================================================
    class AnimationInstance {
    public:
        /**
         * @brief 创建动画实例
         * @param resource 共享资源（骨骼、网格、片段）
         */
        explicit AnimationInstance(std::shared_ptr<AnimationResource> resource);
        ~AnimationInstance() = default;

        // 禁止拷贝
        AnimationInstance(const AnimationInstance&) = delete;
        AnimationInstance& operator=(const AnimationInstance&) = delete;

        // ── 共享资源访问 ──
        std::shared_ptr<AnimationResource> GetResource() const noexcept { return m_Resource; }
        void SetResource(std::shared_ptr<AnimationResource> resource);

        // ── 1. 片段状态 ──

        /**
         * @brief 设置当前播放的片段（快捷方式，自动创建 ClipState）
         * @param clipName 片段名称
         * @param loop     循环模式
         */
        void PlayClip(const std::string& clipName, AnimationLoopMode loop = AnimationLoopMode::Loop);

        /** 获取片段状态列表 */
        std::vector<ClipState>& GetClipStates() noexcept { return m_ClipStates; }
        const std::vector<ClipState>& GetClipStates() const noexcept { return m_ClipStates; }

        /** 按名称查找片段状态 */
        ClipState* FindClipState(const std::string& name);
        const ClipState* FindClipState(const std::string& name) const;

        // ── 2. 混合规格 ──

        BlendSpec& GetBlendSpec() noexcept { return m_BlendSpec; }
        const BlendSpec& GetBlendSpec() const noexcept { return m_BlendSpec; }

        // ── 3. 分部骨骼关节权重 ──

        std::shared_ptr<BlendMask> GetPerBoneWeights() noexcept { return m_PerBoneWeights; }
        const std::shared_ptr<BlendMask> GetPerBoneWeights() const noexcept { return m_PerBoneWeights; }

        /** 确保 PerBoneWeights 已初始化并匹配骨骼数量 */
        void EnsurePerBoneWeights();

        // ── 4. 局部姿势 ──

        PoseLocalData& GetLocalPose() noexcept { return m_LocalPose; }
        const PoseLocalData& GetLocalPose() const noexcept { return m_LocalPose; }

        // ── 5. 全局姿势 + 矩阵调色板 ──

        GlobalPoseData& GetGlobalPose() noexcept { return m_GlobalPose; }
        const GlobalPoseData& GetGlobalPose() const noexcept { return m_GlobalPose; }

        /** 获取蒙皮矩阵调色板 */
        const std::vector<Mat4>& GetMatrixPalette() const noexcept { return m_MatrixPalette; }
        const float* GetMatrixPaletteData() const {
            return m_MatrixPalette.empty() ? nullptr : m_MatrixPalette[0].Data();
        }
        uint32 GetMatrixCount() const noexcept {
            return static_cast<uint32>(m_MatrixPalette.size());
        }

        // ── 更新 ──

        /**
         * @brief 更新时间线（推进片段时间、自动循环）
         * @param dt 时间步长
         */
        void UpdateTimelines(float32 dt);

        /**
         * @brief 执行完整的动画更新流程
         * @param dt 时间步长
         *
         * 流程：
         *   1. UpdateTimelines(dt) — 推进片段时间
         *   2. ExtractPoses() — 从片段解压局部姿势
         *   3. BlendPoses() — 混合多个片段
         *   4. ApplyConstraints() — 应用动画约束（新增）
         *   5. GenerateGlobalPose() — 计算全局姿势
         *   6. GenerateMatrixPalette() — 生成蒙皮矩阵
         */
        void Update(float32 dt);

        // ── 管线各步（可单独调用） ──
        void ExtractPoses();
        void BlendPoses();
        void ApplyConstraints();
        void GenerateGlobalPose();
        void GenerateMatrixPalette();

        // ── 重定目标 ──
        void SetRetargetSource(std::shared_ptr<AnimationInstance> source);
        std::shared_ptr<AnimationInstance> GetRetargetSource() const { return m_RetargetSource; }

        // ── 约束系统 ──

        /** 获取约束求解器 */
        ConstraintSolver& GetConstraintSolver() noexcept { return m_ConstraintSolver; }
        const ConstraintSolver& GetConstraintSolver() const noexcept { return m_ConstraintSolver; }

        /**
         * @brief 设置是否启用约束求值
         * @param enabled 启用状态
         */
        void SetConstraintsEnabled(bool enabled) noexcept { m_ConstraintsEnabled = enabled; }
        bool GetConstraintsEnabled() const noexcept { return m_ConstraintsEnabled; }

    private:
        // 共享资源
        std::shared_ptr<AnimationResource> m_Resource;

        // 1. 片段状态
        std::vector<ClipState> m_ClipStates;

        // 2. 混合规格
        BlendSpec m_BlendSpec;

        // 3. 分部骨骼关节权重
        std::shared_ptr<BlendMask> m_PerBoneWeights;

        // 4. 局部姿势
        PoseLocalData m_LocalPose;

        // 5. 全局姿势 + 矩阵调色板
        GlobalPoseData m_GlobalPose;
        std::vector<Mat4> m_MatrixPalette;

        // 姿势求值器
        std::unique_ptr<AnimationPose> m_PoseEval;

        // 重定目标源
        std::shared_ptr<AnimationInstance> m_RetargetSource;

        // 约束系统
        ConstraintSolver m_ConstraintSolver;   ///< 约束求解器
        bool             m_ConstraintsEnabled = false;  ///< 是否启用约束求值
    };

} // namespace Engine
