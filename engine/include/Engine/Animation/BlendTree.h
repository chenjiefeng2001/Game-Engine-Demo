#pragma once

/**
 * @file BlendTree.h
 * @brief 混合操作树 — 分层级、可组合的动画混合系统
 *
 * ════════════════════════════════════════════
 * 混合树概念
 * ════════════════════════════════════════════
 *
 * 混合树将动画混合描述为树形结构：
 *
 *   叶节点   = 动画片段（ClipNode）或 浮点输入
 *   内部节点 = 混合操作（LERP, Additive, BlendSpace, 加权平均等）
 *   求值     = 从叶子到根递归计算 PoseLocalData
 *
 * ════════════════════════════════════════════
 * 节点类型
 * ════════════════════════════════════════════
 *
 *   ClipNode      — 叶节点：引用一个动画片段
 *   LERPNode      — 二元节点：在两个子节点之间线性/球面插值
 *   AdditiveNode  — 二元节点：将子节点 B 叠加到子节点 A 上
 *   MaskedLERPNode— 带 BlendMask 的 LERP
 *   WeightedAvgNode— N 元节点：多个子节点的扁平加权平均
 *   BlendSpace1D  — 1D 混合空间（多子节点 + 参数驱动）
 *
 * ════════════════════════════════════════════
 * 使用示例
 * ════════════════════════════════════════════
 *
 *   BlendTree tree;
 *
 *   // 简单 LERP 混合
 *   auto* lerp = tree.AddLERP("walk", "run", 0.5f);
 *
 *   // 带遮罩的混合
 *   BlendMask upperMask;
 *   upperMask.SetBoneWeightRecursive(*skeleton, "Spine", 1.0f);
 *   auto* masked = tree.AddMaskedLERP("walk", "aim", upperMask, 1.0f);
 *
 *   // 加权平均（3 个片段等权重）
 *   float weights[] = {0.3f, 0.3f, 0.4f};
 *   auto* avg = tree.AddWeightedAvg({"idle", "walk", "run"}, weights, 3);
 *
 *   // 构建子树：上半身瞄准 + 下半身行走
 *   auto* upper = tree.AddMaskedLERP("walk", "aim", upperMask, 1.0f);
 *
 *   // 每帧求值
 *   tree.Evaluate(skeleton, poseEval, dt);
 *
 *   // 获取矩阵调色板
 *   const auto& matrices = skeleton.GetSkinningMatrices();
 */

#include "Engine/Animation/Skeleton.h"
#include "Engine/Animation/AnimationResource.h"
#include "Engine/Animation/AnimationBlend.h"
#include "Engine/Animation/AnimationPose.h"
#include "Engine/Animation/AnimationPipeline.h"
#include "Engine/Animation/BlendMask.h"
#include "Engine/Animation/BlendSpace1D.h"
#include <string>
#include <memory>
#include <vector>
#include <unordered_map>

namespace Engine {

    // ============================================================
    // 混合节点类型枚举
    // ============================================================
    enum class BlendNodeType : uint8 {
        Clip,           ///< 动画片段（叶节点）
        LERP,           ///< 二元 LERP/SLERP 混合
        Additive,       ///< 加法混合
        MaskedLERP,     ///< 带遮罩的 LERP
        WeightedAvg,    ///< N 元加权平均
        BlendSpace1D,   ///< 1D 混合空间
        Bilinear,       ///< 2D 双线性混合（4 个片段网格）
        TriLERP,        ///< 三角 LERP 混合（3 个片段）
        Fade,           ///< 淡入/淡出包装节点
    };

    // ============================================================
    // 混合节点基类
    // ============================================================
    class BlendNode {
    public:
        BlendNode() = default;
        virtual ~BlendNode() = default;

        BlendNodeType GetType() const noexcept { return m_Type; }

        /**
         * @brief 求值此节点
         * @param skeleton   骨骼（用于提取/写入姿势）
         * @param resource   动画资源（获取片段）
         * @param time       全局时间
         * @param out        输出局部姿势
         */
        virtual void Evaluate(Skeleton& skeleton,
                              const AnimationResource& resource,
                              float32 time,
                              PoseLocalData& out) const = 0;

    protected:
        BlendNode(BlendNodeType type) : m_Type(type) {}

        /** 从资源获取片段并提取姿势 */
        static void ExtractClipPose(const AnimationResource& resource,
                                     const std::string& clipName,
                                     float32 time,
                                     Skeleton& skeleton,
                                     PoseLocalData& out);

        /** 将姿势写入骨骼 localPoseMatrix 并读回 PoseLocalData */
        static void WritePoseToSkeleton(Skeleton& skeleton,
                                         const PoseLocalData& pose) {
            for (size_t i = 0; i < std::min(pose.GetBoneCount(), skeleton.GetBoneCount()); ++i) {
                Vec3 euler = AnimationBlend::ToEuler(pose.rotations[i]);
                AnimationPose::ComposeMatrix(
                    pose.translations[i], euler, pose.scales[i],
                    skeleton.GetBone(static_cast<int32>(i)).localPoseMatrix);
            }
        }

        static void ReadPoseFromSkeleton(const Skeleton& skeleton,
                                          PoseLocalData& out) {
            size_t n = std::min(out.GetBoneCount(), skeleton.GetBoneCount());
            for (size_t i = 0; i < n; ++i) {
                const Bone& bone = skeleton.GetBone(static_cast<int32>(i));
                out.translations[i] = AnimationBlend::DecomposeTranslation(bone.localPoseMatrix);
                out.rotations[i]    = AnimationBlend::DecomposeRotation(bone.localPoseMatrix);
                out.scales[i]       = AnimationBlend::DecomposeScale(bone.localPoseMatrix);
            }
        }

    private:
        BlendNodeType m_Type;
    };

    // ════════════════════════════════════════════
    // 叶节点：动画片段
    // ════════════════════════════════════════════

    class ClipBlendNode : public BlendNode {
    public:
        explicit ClipBlendNode(std::string clipName)
            : BlendNode(BlendNodeType::Clip), m_ClipName(std::move(clipName)) {}

        const std::string& GetClipName() const { return m_ClipName; }
        void SetClipName(const std::string& name) { m_ClipName = name; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::string m_ClipName;
    };

    // ════════════════════════════════════════════
    // 二元节点：LERP 混合
    // ════════════════════════════════════════════

    class LERPBlendNode : public BlendNode {
    public:
        LERPBlendNode(std::unique_ptr<BlendNode> childA,
                      std::unique_ptr<BlendNode> childB,
                      float32 blendWeight = 0.5f)
            : BlendNode(BlendNodeType::LERP)
            , m_ChildA(std::move(childA))
            , m_ChildB(std::move(childB))
            , m_BlendWeight(blendWeight) {}

        void SetBlendWeight(float32 w) noexcept { m_BlendWeight = w; }
        float32 GetBlendWeight() const noexcept { return m_BlendWeight; }

        BlendNode* GetChildA() const { return m_ChildA.get(); }
        BlendNode* GetChildB() const { return m_ChildB.get(); }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::unique_ptr<BlendNode> m_ChildA;
        std::unique_ptr<BlendNode> m_ChildB;
        float32 m_BlendWeight;
    };

    // ════════════════════════════════════════════
    // 二元节点：加法混合
    // ════════════════════════════════════════════

    class AdditiveBlendNode : public BlendNode {
    public:
        AdditiveBlendNode(std::unique_ptr<BlendNode> base,
                          std::unique_ptr<BlendNode> additive,
                          float32 weight = 1.0f)
            : BlendNode(BlendNodeType::Additive)
            , m_Base(std::move(base))
            , m_Additive(std::move(additive))
            , m_Weight(weight) {}

        void SetWeight(float32 w) noexcept { m_Weight = w; }
        float32 GetWeight() const noexcept { return m_Weight; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::unique_ptr<BlendNode> m_Base;
        std::unique_ptr<BlendNode> m_Additive;
        float32 m_Weight;
    };

    // ════════════════════════════════════════════
    // 二元节点：带遮罩的 LERP
    // ════════════════════════════════════════════

    class MaskedLERPBlendNode : public BlendNode {
    public:
        MaskedLERPBlendNode(std::unique_ptr<BlendNode> childA,
                            std::unique_ptr<BlendNode> childB,
                            const BlendMask& mask,
                            float32 blendWeight = 1.0f)
            : BlendNode(BlendNodeType::MaskedLERP)
            , m_ChildA(std::move(childA))
            , m_ChildB(std::move(childB))
            , m_Mask(std::make_shared<BlendMask>(mask))
            , m_BlendWeight(blendWeight) {}

        void SetBlendWeight(float32 w) noexcept { m_BlendWeight = w; }
        float32 GetBlendWeight() const noexcept { return m_BlendWeight; }

        std::shared_ptr<BlendMask> GetMask() const { return m_Mask; }
        void SetMask(const BlendMask& mask) { *m_Mask = mask; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::unique_ptr<BlendNode> m_ChildA;
        std::unique_ptr<BlendNode> m_ChildB;
        std::shared_ptr<BlendMask> m_Mask;
        float32 m_BlendWeight;
    };

    // ════════════════════════════════════════════
    // N 元节点：扁平加权平均
    // ════════════════════════════════════════════

    struct WeightedAvgInput {
        std::string clipName;    ///< 片段名称
        float32     weight;      ///< 权重（无需归一化）
        bool        additive;    ///< 是否为加法层

        WeightedAvgInput() = default;
        WeightedAvgInput(std::string name, float32 w, bool add = false)
            : clipName(std::move(name)), weight(w), additive(add) {}
    };

    class WeightedAvgBlendNode : public BlendNode {
    public:
        WeightedAvgBlendNode()
            : BlendNode(BlendNodeType::WeightedAvg) {}

        void AddInput(const WeightedAvgInput& input) { m_Inputs.push_back(input); }
        void AddInput(const std::string& clipName, float32 weight = 1.0f,
                      bool additive = false) {
            m_Inputs.emplace_back(clipName, weight, additive);
        }

        void Clear() { m_Inputs.clear(); }

        size_t GetInputCount() const { return m_Inputs.size(); }
        const WeightedAvgInput& GetInput(int32 i) const { return m_Inputs[i]; }
        WeightedAvgInput& GetInput(int32 i) { return m_Inputs[i]; }

        /**
         * @brief 设置全局混合遮罩（应用于所有输入）
         */
        void SetGlobalMask(std::shared_ptr<BlendMask> mask) { m_GlobalMask = mask; }
        std::shared_ptr<BlendMask> GetGlobalMask() const { return m_GlobalMask; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::vector<WeightedAvgInput> m_Inputs;
        std::shared_ptr<BlendMask> m_GlobalMask;
    };

    // ════════════════════════════════════════════
    // N 元节点：2D 双线性混合（Bilinear）
    // ════════════════════════════════════════════

    /**
     * @brief 2D 双线性混合节点
     *
     * 在 2×2 网格的四个片段之间做双线性 LERP：
     *
     *   clipYX[0][1] ───── clipYX[1][1]
     *       │                   │
     *       │     P(tx,ty)      │
     *       │                   │
     *   clipYX[0][0] ───── clipYX[1][0]
     *
     *   top = LERP(C00, C10, tx)
     *   bot = LERP(C01, C11, tx)
     *  result = LERP(top, bot, ty)
     */
    class BilinearBlendNode : public BlendNode {
    public:
        BilinearBlendNode()
            : BlendNode(BlendNodeType::Bilinear) {}

        /** 设置四个角落的片段名称 */
        void SetClips(const std::string& c00, const std::string& c10,
                      const std::string& c01, const std::string& c11) {
            m_Clips[0] = c00; m_Clips[1] = c10;
            m_Clips[2] = c01; m_Clips[3] = c11;
        }

        /** 设置 2D 参数 (tx, ty)，范围 [0,1] */
        void SetParameter(float32 tx, float32 ty) noexcept { m_TX = tx; m_TY = ty; }
        float32 GetTX() const noexcept { return m_TX; }
        float32 GetTY() const noexcept { return m_TY; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::string m_Clips[4];  ///< [c00, c10, c01, c11]
        float32 m_TX = 0.0f;     ///< X 参数 [0,1]
        float32 m_TY = 0.0f;     ///< Y 参数 [0,1]
    };

    // ════════════════════════════════════════════
    // 三元节点：三角 LERP 混合（TriLERP）
    // ════════════════════════════════════════════

    /**
     * @brief 三角 LERP 混合节点
     *
     * 在三个片段之间使用重心坐标做 LERP/SLERP 混合：
     *
     *        C0
     *       /  \
     *      /    \
     *     /  P   \
     *    /   ↑    \
     *   C1────────C2
     *
     *   P = w0*C0 + w1*C1 + w2*C2  (w0+w1+w2 = 1)
     *
     * 使用重心坐标作为权重，逐骨骼做 T:LERP / R:SLERP / S:LERP。
     */
    class TriLERPBlendNode : public BlendNode {
    public:
        TriLERPBlendNode()
            : BlendNode(BlendNodeType::TriLERP) {}

        /** 设置三个顶点的片段名称 */
        void SetClips(const std::string& c0, const std::string& c1,
                      const std::string& c2) {
            m_Clips[0] = c0; m_Clips[1] = c1; m_Clips[2] = c2;
        }

        /** 设置重心坐标权重 (w0, w1, w2)，需和为 1.0 */
        void SetWeights(float32 w0, float32 w1, float32 w2) noexcept {
            m_Weights[0] = w0; m_Weights[1] = w1; m_Weights[2] = w2;
        }
        float32 GetWeight(int32 i) const { return m_Weights[i]; }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::string m_Clips[3];     ///< 三个顶点片段
        float32 m_Weights[3] = {1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f};
    };

    // ════════════════════════════════════════════
    // 一元节点：淡入/淡出（Fade）
    // ════════════════════════════════════════════

    /**
     * @brief 淡入/淡出包装节点
     *
     * 包裹任意子节点，为其添加淡入/淡出过渡效果：
     *
     *   FadeIn(duration)  — 从权重 0 在 duration 秒内渐变到 1
     *   FadeOut(duration) — 从权重 1 在 duration 秒内渐变到 0
     *   CrossFade(targetNode, duration) — 从当前节点交叉淡入到目标
     *
     * 淡入/淡出曲线使用平滑步进 (smoothstep) 使过渡更自然。
     */
    class FadeBlendNode : public BlendNode {
    public:
        enum class FadeState : uint8 {
            None,       ///< 无淡变
            FadingIn,   ///< 淡入中
            FadingOut,  ///< 淡出中
        };

        explicit FadeBlendNode(std::unique_ptr<BlendNode> child)
            : BlendNode(BlendNodeType::Fade)
            , m_Child(std::move(child)) {}

        /** 开始淡入 */
        void FadeIn(float32 duration);

        /** 开始淡出 */
        void FadeOut(float32 duration);

        /**
         * @brief 交叉淡入到新节点
         * @param newChild 新子节点
         * @param duration 过渡时长
         */
        void CrossFade(std::unique_ptr<BlendNode> newChild, float32 duration);

        /** 更新淡变状态（每帧调用） */
        void UpdateFade(float32 dt);

        /** 获取当前淡变进度 [0, 1] */
        float32 GetFadeProgress() const noexcept { return m_FadeProgress; }

        /** 获取当前淡变状态 */
        FadeState GetFadeState() const noexcept { return m_FadeState; }

        /** 设置淡变曲线类型 */
        void UseSmoothStep(bool smooth) noexcept { m_UseSmoothStep = smooth; }

        BlendNode* GetChild() const { return m_Child.get(); }
        void SetChild(std::unique_ptr<BlendNode> child) { m_Child = std::move(child); }

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time, PoseLocalData& out) const override;

    private:
        std::unique_ptr<BlendNode> m_Child;
        std::unique_ptr<BlendNode> m_TargetChild;  ///< 交叉淡入目标

        FadeState m_FadeState   = FadeState::None;
        float32   m_FadeProgress = 1.0f;  ///< 1.0 = 完全可见
        float32   m_FadeDuration = 0.0f;
        float32   m_FadeTimer    = 0.0f;
        bool      m_UseSmoothStep = true;
    };
    // ════════════════════════════════════════════

    // ============================================================
    // 自定义节点类型注册（支持用户扩展）
    // ============================================================

    /**
     * @brief 自定义节点工厂函数
     * 用户可以通过此函数创建任意 BlendNode 子类
     */
    using CustomNodeFactory = std::function<std::unique_ptr<BlendNode>()>;

    /**
     * @brief 自定义节点描述
     */
    struct CustomNodeDesc {
        std::string typeName;        ///< 节点类型名称（如 "MyCustomBlend"）
        CustomNodeFactory factory;   ///< 工厂函数
        std::string description;     ///< 描述信息
    };

    // ============================================================
    // 混合树 — 根容器
    // ============================================================
    class BlendTree {
    public:
        BlendTree() = default;
        ~BlendTree() = default;

        // 禁止拷贝
        BlendTree(const BlendTree&) = delete;
        BlendTree& operator=(const BlendTree&) = delete;

        // ── 节点工厂方法 ──
        // ... (existing methods)
        /** 创建片段节点（叶节点） */
        ClipBlendNode* AddClip(const std::string& clipName);

        /** 创建 LERP 混合节点 */
        LERPBlendNode* AddLERP(std::unique_ptr<BlendNode> childA,
                               std::unique_ptr<BlendNode> childB,
                               float32 blendWeight = 0.5f);

        /** 快速创建 LERP: 两个片段直接混合 */
        LERPBlendNode* AddLERP(const std::string& clipA,
                               const std::string& clipB,
                               float32 blendWeight = 0.5f);

        /** 创建加法混合节点 */
        AdditiveBlendNode* AddAdditive(std::unique_ptr<BlendNode> base,
                                       std::unique_ptr<BlendNode> additive,
                                       float32 weight = 1.0f);

        /** 创建遮罩 LERP 节点 */
        MaskedLERPBlendNode* AddMaskedLERP(const std::string& clipA,
                                            const std::string& clipB,
                                            const BlendMask& mask,
                                            float32 blendWeight = 1.0f);

        /** 创建加权平均节点 */
        WeightedAvgBlendNode* AddWeightedAvg();

        /** 创建 2D 双线性混合节点 */
        BilinearBlendNode* AddBilinear();

        /** 创建三角 LERP 混合节点 */
        TriLERPBlendNode* AddTriLERP();

        /** 创建淡入/淡出包装节点 */
        FadeBlendNode* AddFade(std::unique_ptr<BlendNode> child);

        // ── 自定义节点注册 ──

        /**
         * @brief 注册自定义节点类型
         * @param typeName  节点类型名称
         * @param factory   工厂函数
         * @param desc      描述信息
         *
         * 注册后可通过 CreateCustomNode 创建。
         */
        static void RegisterCustomNode(const std::string& typeName,
                                        CustomNodeFactory factory,
                                        const std::string& desc = "");

        /**
         * @brief 创建已注册的自定义节点
         * @param typeName 节点类型名称
         * @return 新节点，如果类型未注册则返回 nullptr
         */
        static std::unique_ptr<BlendNode> CreateCustomNode(const std::string& typeName);

        /** 获取所有注册的自定义节点类型 */
        static std::vector<CustomNodeDesc> GetRegisteredCustomNodes();

        // ── 自定义树语法（DSL 辅助） ──

        /**
         * @brief 从简单描述字符串构建混合树
         *
         * 语法示例：
         *   "lerp(walk, run, 0.5)"                    → LERP Blend
         *   "additive(walk, aim, 0.3)"                → Additive Blend
         *   "masked(walk, aim, upperMask, 1.0)"       → Masked LERP
         *   "clip(walk)"                               → Clip Node
         *   "avg(idle=0.2, walk=0.5, run=0.3)"        → Weighted Avg
         *   "fade(clip(walk), 0.5)"                    → Fade Node
         *
         * @param syntax 描述字符串
         * @return 解析后的节点（所有权归调用者）
         */
        std::unique_ptr<BlendNode> BuildFromSyntax(const std::string& syntax);

        // ── 根节点 ──

        void SetRoot(std::unique_ptr<BlendNode> root) { m_Root = std::move(root); }
        BlendNode* GetRoot() const { return m_Root.get(); }
        bool HasRoot() const { return m_Root != nullptr; }

        // ── 求值 ──

        void Evaluate(Skeleton& skeleton, const AnimationResource& resource,
                      float32 time);

        void EvaluateToPose(PoseLocalData& out, Skeleton& skeleton,
                            const AnimationResource& resource, float32 time);

        // ── 序列化（现场更新工具接口） ──

        /**
         * @brief 将混合树序列化为 JSON 字符串
         * @return JSON 字符串
         */
        std::string Serialize() const;

        /**
         * @brief 从 JSON 字符串反序列化
         * @param json JSON 字符串
         * @return 是否成功
         */
        bool Deserialize(const std::string& json);

        /** 触发现场更新（从源码重新加载） */
        void ReloadFromSource(const std::string& source);

        /** 注册现场更新回调 */
        using LiveEditCallback = std::function<void(BlendTree&)>;
        void SetLiveEditCallback(LiveEditCallback cb) { m_LiveEditCallback = std::move(cb); }

        // ── 工具 ──

        struct Stats {
            int32 totalNodes  = 0;
            int32 clipNodes   = 0;
            int32 blendNodes  = 0;
            int32 customNodes = 0;
        };
        Stats GetStats() const;

        /** 清空所有节点 */
        void Clear();

    private:
        std::unique_ptr<BlendNode> m_Root;
        std::vector<std::unique_ptr<BlendNode>> m_OwnedNodes;

        // 自定义节点类型注册表
        static std::unordered_map<std::string, CustomNodeDesc>& GetCustomRegistry();

        // 现场更新回调
        LiveEditCallback m_LiveEditCallback;
        std::string m_SourceSyntax;  ///< 保存源码用于现场更新
    };

} // namespace Engine
