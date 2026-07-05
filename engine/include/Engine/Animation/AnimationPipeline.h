#pragma once

/**
 * @file AnimationPipeline.h
 * @brief 动画管线 — 6 阶段标准化动画处理流程
 *
 * ════════════════════════════════════════════
 * 管线阶段
 * ════════════════════════════════════════════
 *
 *   Stage 1: EXTRACT  (片段解压与姿势提取)
 *     从 AnimationLocalTimeline / CompressedTrack 中
 *     提取指定时间点的骨骼局部姿势 → PoseLocalData
 *
 *   Stage 2: BLEND    (姿势混合)
 *     对多个 PoseLocalData 做 LERP / SLERP / 遮罩混合 / 加法混合
 *     → 输出单个 PoseLocalData
 *
 *   Stage 3: GLOBALIZE (全局姿势生成)
 *     将局部姿势矩阵通过骨骼层级累乘为全局姿势矩阵
 *     Skeleton::UpdateWorldPoses()
 *
 *   Stage 4: POSTPROCESS (后期处理)
 *     IK 求解、脚部锁定、程序化修正等
 *
 *   Stage 5: POSTPROCESS2 (后期处理 2)
 *     叠加层：呼吸、抖动、物理跟随等
 *
 *   Stage 6: PALETTE (矩阵调色板生成)
 *     生成最终蒙皮矩阵数组供 GPU 使用
 *     skinningMatrix[i] = currentPoseMatrix[i] * inverseBindMatrix[i]
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   AnimationPipeline pipeline(skeleton);
 *
 *   // 配置各阶段
 *   pipeline.SetClip("walk", &walkClip);
 *   pipeline.SetBlendSettings({walkClip, runClip, 0.5f});
 *   pipeline.SetPostProcess(IKHandler);
 *
 *   // 执行完整管线
 *   pipeline.Execute(dt);
 *
 *   // 获取结果
 *   const auto& matrices = pipeline.GetMatrixPalette();
 *   // 上传蒙皮矩阵到 GPU（通过渲染器接口）
 */

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationLocalTimeline.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/BlendMask.h"
#include "Engine/Animation/IK.h"
#include <vector>
#include <memory>
#include <functional>

// 前向声明
namespace Engine {
    class ConstraintSolver;
}

namespace Engine {

    // ============================================================
    // 管线段落枚举
    // ============================================================
    enum class PipelineStage : uint8 {
        Extract     = 0,    ///< 片段解压与姿势提取
        Blend       = 1,    ///< 姿势混合
        Globalize   = 2,    ///< 全局姿势生成
        PostProcess = 3,    ///< 后期处理
        PostProcess2= 4,    ///< 后期处理 2
        Palette     = 5,    ///< 矩阵调色板生成
        COUNT               ///< 阶段总数
    };

    // ============================================================
    // 片段解压结果 — 骨骼局部姿势的快照
    // ============================================================
    struct PoseLocalData {
        std::vector<Vec3> translations;  ///< 每骨骼的局部平移
        std::vector<Quat> rotations;     ///< 每骨骼的局部旋转（四元数）
        std::vector<Vec3> scales;        ///< 每骨骼的局部缩放

        size_t GetBoneCount() const noexcept { return translations.size(); }

        bool IsValid() const noexcept {
            return translations.size() == rotations.size()
                && rotations.size() == scales.size();
        }

        void Resize(size_t count) {
            translations.resize(count, Vec3(0, 0, 0));
            rotations.resize(count, Quat::Identity());
            scales.resize(count, Vec3(1, 1, 1));
        }
    };

    // ============================================================
    // 混合配置
    // ============================================================
    struct BlendInput {
        const AnimationLocalTimeline* clip       = nullptr;  ///< 源动画片段
        float32                       weight     = 1.0f;     ///< 混合权重
        const BlendMask*              mask       = nullptr;  ///< 可选遮罩
        bool                          additive   = false;    ///< 是否为加法混合

        BlendInput() = default;
        BlendInput(const AnimationLocalTimeline* c, float32 w = 1.0f)
            : clip(c), weight(w) {}
        BlendInput(const AnimationLocalTimeline* c, const BlendMask& m, float32 w = 1.0f)
            : clip(c), weight(w), mask(&m) {}
    };

    // ============================================================
    // 后期处理函数类型
    // ============================================================
    using PostProcessFn = std::function<void(Skeleton&, float32 dt)>;

    // ============================================================
    // 动画管线
    // ============================================================
    class AnimationPipeline {
    public:
        /**
         * @brief 构造动画管线
         * @param skeleton    目标骨骼（管线将修改此骨骼的姿势）
         * @param poseEval    姿势求值器
         */
        explicit AnimationPipeline(std::shared_ptr<Skeleton> skeleton,
                                   std::shared_ptr<AnimationPose> poseEval = nullptr);
        ~AnimationPipeline() = default;

        // 禁止拷贝
        AnimationPipeline(const AnimationPipeline&) = delete;
        AnimationPipeline& operator=(const AnimationPipeline&) = delete;

        // ── 骨骼 ──
        void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
        std::shared_ptr<Skeleton> GetSkeleton() const noexcept { return m_Skeleton; }

        // ════════════════════════════════════════════
        // Stage 1: EXTRACT — 片段解压与姿势提取
        // ════════════════════════════════════════════

        /**
         * @brief 设置主动画片段
         * @param clip  动画时间线
         * @param time  采样时间点（默认使用时间线的当前时间）
         */
        void SetPrimaryClip(const AnimationLocalTimeline* clip, float32 time = -1.0f);

        /** 获取提取后的局部姿势数据 */
        const PoseLocalData& GetExtractedPose() const noexcept { return m_ExtractedPose; }

        // ════════════════════════════════════════════
        // Stage 2: BLEND — 姿势混合
        // ════════════════════════════════════════════

        /** 清除混合输入列表 */
        void ClearBlendInputs();

        /**
         * @brief 添加一个混合输入
         * @param input 混合输入（片段 + 权重 + 可选遮罩）
         */
        void AddBlendInput(const BlendInput& input);

        /** 设置参考姿势（用于加法混合） */
        void SetReferencePose(const Skeleton* refPose) { m_ReferencePose = refPose; }

        /** 获取混合后的局部姿势数据 */
        const PoseLocalData& GetBlendedPose() const noexcept { return m_BlendedPose; }

        // ════════════════════════════════════════════
        // Stage 3: GLOBALIZE — 全局姿势生成
        // ════════════════════════════════════════════

        /** 全局姿势生成后是否自动计算蒙皮矩阵 */
        void SetAutoPalette(bool autoPalette) noexcept { m_AutoPalette = autoPalette; }
        bool GetAutoPalette() const noexcept { return m_AutoPalette; }

        // ════════════════════════════════════════════
        // Stage 4 & 5: POSTPROCESS — 后期处理
        // ════════════════════════════════════════════

        /**
         * @brief 注册后期处理函数（Stage 4）
         * @param fn 后期处理回调 void(Skeleton&, float32 dt)
         * @param order 执行顺序（小值先执行）
         */
        int32 AddPostProcess(PostProcessFn fn, int32 order = 0);

        /**
         * @brief 注册后期处理函数 2（Stage 5）
         * @param fn 后期处理回调
         * @param order 执行顺序
         */
        int32 AddPostProcess2(PostProcessFn fn, int32 order = 0);

        /** 清除所有后期处理函数 */
        void ClearPostProcess();

        /** 清除所有后期处理函数 2 */
        void ClearPostProcess2();

        // ════════════════════════════════════════════
        // Stage 6: PALETTE — 矩阵调色板生成
        // ════════════════════════════════════════════

        /**
         * @brief 获取蒙皮矩阵调色板
         * @return 蒙皮矩阵数组
         *
         * 每个矩阵 = bone.currentPoseMatrix * bone.inverseBindMatrix
         * 将顶点从 绑定姿势模型空间 变换到 当前姿势模型空间。
         */
        const std::vector<Mat4>& GetMatrixPalette() const noexcept {
            return m_MatrixPalette;
        }

        /** 获取矩阵调色板的 float* 指针（用于 glUniformMatrix4fv） */
        const float* GetMatrixPaletteData() const {
            return m_MatrixPalette.empty() ? nullptr : m_MatrixPalette[0].Data();
        }

        /** 获取矩阵数量 */
        uint32 GetMatrixCount() const noexcept {
            return static_cast<uint32>(m_MatrixPalette.size());
        }

        // ════════════════════════════════════════════
        // 约束系统（可选）
        // ════════════════════════════════════════════

        /**
         * @brief 设置约束求解器
         * @param solver 指向 ConstraintSolver 的指针（可空，不拥有所有权）
         *
         * 设置后，管线将在 Blend 阶段之后、Globalize 阶段之前
         * 自动求值约束，修改 BlendedPose 后再进行全局化。
         * 传入 nullptr 可禁用约束支持。
         */
        void SetConstraintSolver(ConstraintSolver* solver) noexcept { m_ConstraintSolver = solver; }
        ConstraintSolver* GetConstraintSolver() const noexcept { return m_ConstraintSolver; }

        // ════════════════════════════════════════════
        // 执行管线
        // ════════════════════════════════════════════

        /**
         * @brief 执行完整动画管线
         * @param dt 帧时间步长
         *
         * 依次执行所有 6 个阶段。
         * 可通过 SetStageEnabled 控制哪些阶段启用。
         */
        void Execute(float32 dt);

        /**
         * @brief 执行指定阶段
         * @param stage 阶段枚举
         * @param dt    帧时间步长
         */
        void ExecuteStage(PipelineStage stage, float32 dt);

        // ── 阶段控制 ──

        /** 启用/禁用指定阶段 */
        void SetStageEnabled(PipelineStage stage, bool enabled) {
            m_StageEnabled[static_cast<size_t>(stage)] = enabled;
        }

        /** 检查阶段是否启用 */
        bool IsStageEnabled(PipelineStage stage) const {
            return m_StageEnabled[static_cast<size_t>(stage)];
        }

        // ── 调试 ──

        /** 获取每阶段耗时（毫秒） */
        const float32* GetStageTimings() const noexcept { return m_StageTimings; }

    private:
        // ── 各阶段实现 ──

        /** Stage 1: 从时间线提取姿势到 PoseLocalData */
        void StageExtract(float32 dt);

        /** Stage 2: 混合多个 PoseLocalData */
        void StageBlend(float32 dt);

        /** Stage 3: 将局部姿势写入 Skeleton 并更新全局姿势 */
        void StageGlobalize(float32 dt);

        /** Stage 4: 后期处理 */
        void StagePostProcess(float32 dt);

        /** Stage 5: 后期处理 2 */
        void StagePostProcess2(float32 dt);

        /** Stage 6: 生成矩阵调色板 */
        void StagePalette(float32 dt);

        // ── 内部：PoseLocalData ↔ Skeleton ──
        void PoseToSkeleton(const PoseLocalData& pose, Skeleton& skeleton) const;
        void SkeletonToPose(const Skeleton& skeleton, PoseLocalData& pose) const;

        // ── 内部：混合两个 PoseLocalData ──
        static void BlendPose(PoseLocalData& out,
                              const PoseLocalData& a, const PoseLocalData& b,
                              float32 t, const BlendMask* mask = nullptr);

        // ── 数据 ──
        std::shared_ptr<Skeleton>      m_Skeleton;
        std::shared_ptr<AnimationPose> m_PoseEval;

        // Stage 1: Extract
        const AnimationLocalTimeline*  m_PrimaryClip  = nullptr;
        float32                        m_ExtractTime  = -1.0f;
        PoseLocalData                  m_ExtractedPose;

        // Stage 2: Blend
        std::vector<BlendInput>        m_BlendInputs;
        const Skeleton*                m_ReferencePose = nullptr;
        PoseLocalData                  m_BlendedPose;

        // Stage 3: Globalize
        bool                           m_AutoPalette = true;

        // Stage 4 & 5: PostProcess
        struct PostProcessEntry {
            PostProcessFn fn;
            int32 order;
        };
        std::vector<PostProcessEntry>  m_PostProcessFns;
        std::vector<PostProcessEntry>  m_PostProcessFns2;

        // Stage 6: Palette
        std::vector<Mat4>              m_MatrixPalette;

        // 阶段控制
        bool m_StageEnabled[6] = {true, true, true, true, true, true};
        float32 m_StageTimings[6] = {0, 0, 0, 0, 0, 0};

        // 约束求解器（非拥有指针）
        ConstraintSolver* m_ConstraintSolver = nullptr;
    };

} // namespace Engine
