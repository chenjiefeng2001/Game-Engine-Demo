/**
 * @file SkinningPixelRig.h
 * @brief GPU 蒙皮像素验证的后端无关核心：共享数据、共享断言工具、后端接口
 *
 * 为什么是这个形状
 * --------------
 * 这四个用例此前是纯 GL 的：自己建 GLFW 4.6 窗口、走 IGraphicsFactory /
 * IRenderContext、raw GL 建 FBO、glReadPixels。结果是**每一个 CI runner 都跳过
 * 它们** —— 引擎要求 GL 4.6，而 hosted runner 给不出（Windows 上连不带版本
 * 提示的默认上下文都建不出来，见 docs/Handoff-GLAD-Loader-Minimum-Context.md）。
 *
 * 但「一份代码两个驱动」在这里做不到，必须说清楚原因：
 * GL 后端的 IRHICommandList 光栅化路径**全是空桩**（SetVertexBuffer /
 * SetIndexBuffer / Draw / BeginRendering / Submit 全是 {}，CreateTexture 返回空
 * 纹理），GL 的渲染完全走另一套抽象 IGraphicsFactory / IRenderContext。
 * 换句话说 GL 有两条互不相通的路，而 Vulkan 那条是通的。
 *
 * 因此这里的「后端无关」指的是：**用例、顶点数据、骨骼矩阵、分带分析、
 * 断言全部只写一遍**，具体怎么把像素弄出来由后端各自实现。后端按能力挑选：
 * GL 4.6 可用就用 GL（此时第 4 个用例也真跑，因为它验的是产品 GL 绘制分支），
 * 否则退到 Vulkan（lavapipe）。
 *
 * 像素契约（两个后端都必须满足，否则共享断言没有意义）
 * --------------------------------------------------
 *   * RGBA8，紧密排列，行宽 == w * 4
 *   * 左下角原点（与 glReadPixels 一致）。Vulkan 回读是自上而下，因此 Vulkan
 *     后端负责翻行。分带断言只看列区间与整帧比较，本可对翻转免疫，但统一朝向
 *     免得以后有人写出依赖朝向的断言。
 *   * 未画到的区域必须是**我们清除的颜色**，不能是未初始化内存。GL 用私有 FBO
 *     而不是窗口 framebuffer 0，正是因为后者回读出未初始化内存，表现为「整帧
 *     到处都有亮像素」，与假阳性无法区分。
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace Engine {
namespace Test {

// ── 共享顶点布局 ────────────────────────────────────────────────────────────
// 与 assets/shaders/skinned_lit.vert 的 location 0..5 一一对应：
//   0 a_Position  1 a_Normal  2 a_TexCoord  3 a_Tangent
//   4 a_BoneIndices  5 a_BoneWeights
//
// 两个后端读的是同一块内存，差别只在「声明哪些 location」：GL 用全部 6 个
// （产品着色器要求如此），Vulkan 只声明用得到的 3 个（position / 骨骼索引 /
// 骨骼权重），位置在着色器里重新编号。
struct SkinVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
    float tangent[3];
    int   boneIndices[4];
    float boneWeights[4];
};

// 真实回读：像素 + 它们实际到达时的尺寸。
struct SkinCapture {
    std::vector<uint8_t> px;
    int w = 0;
    int h = 0;

    bool Valid() const { return w > 0 && h > 0 && !px.empty(); }

    // 驱动可能只交回比申报高度更少的完整行（实测：申报 64x64，实际 48 行）。
    // 因此可寻址的行数从缓冲区推导，而不是相信 h。
    int Rows() const
    {
        if (!Valid()) return 0;
        const size_t perRow = static_cast<size_t>(w) * 4;
        const int rows = static_cast<int>(px.size() / perRow);
        return rows < h ? rows : h;
    }
};

// 列分带，边界从真实宽度推导，从不写死。
struct SkinBands {
    int left;
    int centre;
    int right;
};

inline SkinBands MakeSkinBands(int w)
{
    SkinBands b;
    b.left   = 0;
    b.centre = w / 4;
    b.right  = (w * 3) / 4;
    return b;
}

inline void SkinMakeTranslate(float x, float y, float z, float* out)
{
    std::memset(out, 0, 16 * sizeof(float));
    out[0] = out[5] = out[10] = out[15] = 1.0f;
    out[12] = x; out[13] = y; out[14] = z;
}

// 亮像素阈值：任一通道 > 8。三通道之和 > 30（产品用例用的口径）等价于
// 「至少一个通道 > 10」，两者不完全相同，因此统一用本口径。
inline bool SkinPixelLit(const uint8_t* p)
{
    return p[0] > 8 || p[1] > 8 || p[2] > 8;
}

// 统计闭区间列范围内的亮像素数，用真实尺寸。无效回读返回 -1，与 0 区分开：
// 「没有亮像素」和「没有像素可看」不能混为一谈。
inline int SkinLitInColumns(const SkinCapture& cap, int c0, int c1)
{
    if (!cap.Valid()) return -1;
    const int rows = cap.Rows();
    if (rows <= 0) return -1;
    const int lo = (c0 < 0) ? 0 : c0;
    const int hi = (c1 >= cap.w) ? cap.w - 1 : c1;
    if (hi < lo) return 0;
    int n = 0;
    for (int y = 0; y < rows; ++y) {
        for (int x = lo; x <= hi; ++x) {
            const uint8_t* q = &cap.px[static_cast<size_t>((y * cap.w + x) * 4)];
            if (SkinPixelLit(q)) ++n;
        }
    }
    return n;
}

inline int SkinCountLit(const SkinCapture& cap)
{
    if (!cap.Valid()) return -1;
    int n = 0;
    for (size_t i = 0; i + 3 < cap.px.size(); i += 4) {
        if (SkinPixelLit(&cap.px[i])) ++n;
    }
    return n;
}

// 固定大小的中心方框（产品用例用 16..48），用于「姿势变化后中心仍被覆盖」。
inline int SkinCentreBoxLit(const SkinCapture& cap)
{
    if (!cap.Valid()) return -1;
    int n = 0;
    for (int y = 16; y < 48; ++y) {
        for (int x = 16; x < 48; ++x) {
            const size_t o = static_cast<size_t>(y * cap.w + x) * 4;
            if (SkinPixelLit(&cap.px[o])) ++n;
        }
    }
    return n;
}

// 两帧是否在任意字节上不同。
inline bool SkinAnyPixelDiffers(const SkinCapture& a, const SkinCapture& b)
{
    if (a.px.size() != b.px.size() || a.px.empty()) return false;
    for (size_t i = 0; i < a.px.size(); ++i) {
        if (a.px[i] != b.px[i]) return true;
    }
    return false;
}

// 左下角 span x span 区域在两帧之间是否逐字节相同（不含 alpha）。
// 该区域在两个姿势下都在四边形之外，因此必须完全一致 —— 这是把「整帧因为无关
// 原因变了」与「蒙皮生效」区分开的反假阳性检查。
inline bool SkinCornerByteStable(const SkinCapture& a, const SkinCapture& b, int span)
{
    if (!a.Valid() || !b.Valid()) return false;
    const int rows = a.Rows() < b.Rows() ? a.Rows() : b.Rows();
    if (rows <= 0 || a.w != b.w) return false;
    const int side = span < a.w ? span : a.w;
    for (int y = 0; y < side && y < rows; ++y) {
        for (int x = 0; x < side; ++x) {
            const size_t o = static_cast<size_t>((y * a.w + x) * 4);
            if (a.px[o] != b.px[o] || a.px[o + 1] != b.px[o + 1] || a.px[o + 2] != b.px[o + 2]) {
                return false;
            }
        }
    }
    return true;
}

// 确定性的四边形：每个顶点完全绑定到 0 号骨骼。坐标沿用产品用例的那一组，
// 因为骨骼平移 +0.8 的落点（右带）就是按这组坐标标定出来的。
inline std::vector<SkinVertex> SkinBuildQuadVertices()
{
    const float quad[4][2] = { { -0.45f, -0.45f }, { 0.45f, -0.45f },
                               { 0.45f,  0.45f }, { -0.45f,  0.45f } };
    std::vector<SkinVertex> verts;
    verts.reserve(4);
    for (int i = 0; i < 4; ++i) {
        SkinVertex v{};
        v.position[0] = quad[i][0];
        v.position[1] = quad[i][1];
        v.position[2] = 0.0f;
        v.normal[0] = 0.0f;
        v.normal[1] = 0.0f;
        v.normal[2] = 1.0f;
        v.texCoord[0] = static_cast<float>(i);
        v.texCoord[1] = 0.0f;
        v.tangent[0] = 1.0f;
        v.boneIndices[0] = 0; v.boneIndices[1] = 0; v.boneIndices[2] = 0; v.boneIndices[3] = 0;
        v.boneWeights[0] = 1.0f; v.boneWeights[1] = 0.0f;
        v.boneWeights[2] = 0.0f; v.boneWeights[3] = 0.0f;
        verts.push_back(v);
    }
    return verts;
}

inline const uint32_t* SkinQuadIndices(uint32_t* outCount)
{
    static const uint32_t idx[6] = { 0, 1, 2, 2, 3, 0 };
    if (outCount) *outCount = 6;
    return idx;
}

// ── 后端接口 ────────────────────────────────────────────────────────────────
/**
 * 每个后端负责「把像素弄出来」以及「证明用的确实是蒙皮着色器」，
 * 断言本身不在这里，而在 SkinningPixelCases.cpp 里只写一遍。
 */
class ISkinningPixelBackend {
public:
    virtual ~ISkinningPixelBackend() = default;

    /** 日志里用的后端名，例如 "OpenGL 4.6" / "Vulkan (llvmpipe)"。 */
    virtual const char* Name() const = 0;

    /** 共享 rig 路径：用后端自己的方式画一次并回读。失败返回空 SkinCapture。 */
    virtual SkinCapture DrawRig(float boneX) = 0;

    /**
     * 证明「用的确实是蒙皮着色器」，而不是某个会画出点东西的替代品。
     * 实现方式按后端而不同（GL 反射 uniform/attrib 位置；Vulkan 反射 SPIR-V 的
     * 描述符声明 + 真实 pipeline 句柄）。返回 false 时 detail 写入原因，
     * 调用方必须让用例**失败**而不是跳过。
     */
    virtual bool VerifySkinnedProgram(std::string* detail) = 0;

    /**
     * 产品绘制分支是否存在。它验的是 EditorDemo 生产路径调用的那个函数，
     * 目前只有 GL 有；没有时该用例显式跳过并说明理由。
     */
    virtual bool ProductPathAvailable() const { return false; }
    virtual SkinCapture DrawProductPath(float boneX) { (void)boneX; return SkinCapture{}; }
};

/** 尝试建立 GL 4.6 后端。失败时 why 写入实测原因，不抛异常。 */
std::unique_ptr<ISkinningPixelBackend> TryCreateGLSkinningBackend(std::string* why);

/**
 * 尝试建立 Vulkan 后端（headless；CI 上是 lavapipe）。失败时 why 写入实测原因。
 * 未编译 Vulkan 后端或 shaderc 时直接返回 nullptr 并说明原因 —— 这样
 * SelectSkinningPixelBackend 不必关心哪些后端存在。
 */
std::unique_ptr<ISkinningPixelBackend> TryCreateVulkanSkinningBackend(std::string* why);

/**
 * 按能力挑选后端：先 GL 4.6（产品路径也在那儿），再 Vulkan。
 * 两者都不成立时返回 nullptr，report 写入**两次尝试各自的实测原因** ——
 * 只报一个原因会让人以为是某一边坏了。
 */
std::unique_ptr<ISkinningPixelBackend> SelectSkinningPixelBackend(std::string* report);

} // namespace Test
} // namespace Engine