// ═══════════════════════════════════════════════════════════════════════════
// HRC-3 P1.5 / GP1-DX：production presentation target 的尺寸策略
// ═══════════════════════════════════════════════════════════════════════════
//
// 这是一个**纯逻辑**单元：没有 GL、没有分配、没有线程假设，可以穷举。
//
// 为什么要单独抽出来：FBO 重建需要 current GL context，实测在本地环境里不可靠
// （跨线程 GL name 析构会污染后续 GLFW 初始化，见 EngineHost.cpp 的 P1.1 记录）。
// 但真正容易写错的恰恰是**决定"要不要重建"的那层判断**，而它恰恰是可测的：
//
//   · 去重 —— 拖拽窗口时 SizeChanged 每帧都来，重复同尺寸不应触发 GL 重建。
//     旧代码用编译期常量 800×600 比对（FBO 永远等于常量），这条路径根本没被走过。
//   · 零尺寸 —— 最小化时宿主会报 0×0。若照单重建，FBO 变成 0×0 且 attachments
//     被删除，之后每帧都会失败；正确做法是**保留上一个有效尺寸**，不碰 GL 资源。
//   · 恢复 —— 最小化后窗口还原，必须能重新 Resize 回有效尺寸，而不是卡在 0。
//   · 越界 —— 一个失控的尺寸（IME 弹窗、拖出屏幕）会让 FBO 分配几 GB。
//     上界必须在这里挡，而不是等 OpenGLFramebuffer 自己失败。
//
// 比例（aspect）也由本单元给出，并保证它和尺寸在**同一次** Request 中产出 ——
// 需求要求"相机投影与渲染目标比例一致（同一次事务）"，把两者拆成两次更新
// 就会有一帧的错配。

#ifndef EDITOR_BRIDGE_PRESENTATION_TARGET_POLICY_H
#define EDITOR_BRIDGE_PRESENTATION_TARGET_POLICY_H

#include <cstdint>

namespace editor_bridge {

class PresentationTargetPolicy {
public:
    enum class Decision {
        /// 尺寸非法（<=0 或越界）。**保留上一个有效尺寸**，不触碰任何 GL 资源。
        Rejected = 0,
        /// 与当前有效尺寸相同。去重：不触发重建。
        Unchanged = 1,
        /// 需要重建渲染目标。
        Resize = 2,
    };

    struct Result {
        Decision decision = Decision::Rejected;
        /// 决策后的**有效**尺寸。Rejected 时是上一个有效值（即"什么都不做"）。
        int32_t width = 0;
        int32_t height = 0;
        /// 宽高比 w/h。Rejected 时保持上一有效值，不被非法输入污染。
        float aspect = 0.0f;
    };

    /// 单边上限。GL 的 MAX_TEXTURE_SIZE 通常是 16384；再大也没有硬件能出图，
    /// 而分配尝试本身可能先让进程 OOM。
    static constexpr int32_t kMaxDimension = 16384;

    /// bootstrap 用的默认尺寸（沿用既有编译期常量，仅作为**初始值**）。
    static constexpr int32_t kDefaultWidth = 800;
    static constexpr int32_t kDefaultHeight = 600;

    PresentationTargetPolicy()
        : m_Width(0), m_Height(0), m_Initialized(false) {}

    PresentationTargetPolicy(int32_t width, int32_t height) {
        m_Initialized = false;
        Request(width, height);
    }

    /// 并入宿主报告的视口尺寸。
    ///
    /// 对同一尺寸的重复调用返回 Unchanged（去重）——这是拖拽窗口时唯一正确的行为，
    /// 旧代码从未真正走过这条路径，因为目标尺寸恒等于编译期常量。
    Result Request(int32_t width, int32_t height) {
        if (!IsAcceptable(width, height)) {
            // 保留上一个有效值。首次就非法时有效值仍是"未初始化"。
            Result r;
            r.decision = Decision::Rejected;
            r.width = m_Width;
            r.height = m_Height;
            r.aspect = AspectOf(m_Width, m_Height);
            return r;
        }
        if (m_Initialized && width == m_Width && height == m_Height) {
            Result r;
            r.decision = Decision::Unchanged;
            r.width = m_Width;
            r.height = m_Height;
            r.aspect = AspectOf(m_Width, m_Height);
            return r;
        }
        m_Width = width;
        m_Height = height;
        m_Initialized = true;
        Result r;
        r.decision = Decision::Resize;
        r.width = width;
        r.height = height;
        r.aspect = AspectOf(width, height);
        return r;
    }

    bool IsInitialized() const { return m_Initialized; }
    int32_t Width() const { return m_Width; }
    int32_t Height() const { return m_Height; }
    float Aspect() const { return AspectOf(m_Width, m_Height); }

    /// 该策略下 Fetch 所需的字节数：header + w*h*4。
    /// Fetch 路径必须用这个**运行时**值做容量校验，而不是编译期的 800×600 上界，
    /// 否则一旦目标真的被 resize，所有非默认尺寸的帧都会被误判为"装不下"。
    static int64_t RequiredPacketBytes(int32_t width, int32_t height,
                                       int32_t headerBytes) {
        if (width <= 0 || height <= 0) return 0;
        return static_cast<int64_t>(headerBytes) +
               static_cast<int64_t>(width) * static_cast<int64_t>(height) * 4;
    }

private:
    static bool IsAcceptable(int32_t width, int32_t height) {
        if (width <= 0 || height <= 0) return false;
        if (width > kMaxDimension || height > kMaxDimension) return false;
        return true;
    }

    static float AspectOf(int32_t width, int32_t height) {
        if (width <= 0 || height <= 0) return 0.0f;
        return static_cast<float>(width) / static_cast<float>(height);
    }

    int32_t m_Width;
    int32_t m_Height;
    bool m_Initialized;
};

} // namespace editor_bridge

#endif // EDITOR_BRIDGE_PRESENTATION_TARGET_POLICY_H
