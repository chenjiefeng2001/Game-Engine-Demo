#pragma once

/**
 * @file SIMDBasic.h
 * @brief SIMD 基础包装 — SSE4.1+/AVX + 跨平台标量 fallback
 *
 * 设计目标：
 *   1. 一套代码、多指令集编译（SSE4.1 → AVX2 → ARM NEON → 标量 fallback）
 *   2. 封装 __m128/__m256 为一等公民类型，消除手动 intrinsics 的样板代码
 *   3. 核心算法（点积、叉积、归一化、矩阵乘法、AABB 相交）
 *   4. SoA ↔ AoS 批量转换辅助
 *
 * 指令集选择（编译时自动检测）：
 *   - __AVX2__    → 使用 AVX2（8-wide float）
 *   - __SSE4_1__  → 使用 SSE4.1（4-wide float），MSVC 默认开启
 *   - __ARM_NEON  → ARM NEON
 *   - 其他        → 自动回退到标量实现
 *
 * 使用示例：
 * @code
 *   #include "SIMDBasic.h"
 *
 *   SIMD::Vec4 a(1.0f), b(2.0f, 3.0f, 4.0f, 5.0f);
 *   SIMD::Vec4 c = SIMD::Add(a, b);          // {3, 4, 5, 6}
 *   SIMD::Vec4 d = SIMD::Mul(a, b);          // {2, 3, 4, 5}
 *   float dp = SIMD::Dot(a, b);              // 2+3+4+5 = 14
 *   SIMD::Vec4 n = SIMD::Normalize(SIMD::Vec4(3, 0, 0, 0));
 *
 *   // AABB 相交
 *   SIMD::AABB box1{0,0,0, 1,1,1}, box2{0.5f,0.5f,0.5f, 2,2,2};
 *   bool hit = SIMD::AABBIntersect(box1, box2);  // true
 * @endcode
 */

#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>

// ═══════════════════════════════════════════════════════════
// 指令集检测
// ═══════════════════════════════════════════════════════════

#if defined(__AVX2__) || defined(__FMA__)
    #define SIMD_AVX2 1
    #include <immintrin.h>  // AVX2 + FMA
#elif defined(__SSE4_1__) || defined(_MSC_VER)
    #define SIMD_SSE41 1
    #include <smmintrin.h>  // SSE4.1
    #include <xmmintrin.h>  // SSE
#elif defined(__ARM_NEON)
    #define SIMD_NEON 1
    #include <arm_neon.h>
#else
    #define SIMD_SCALAR 1
#endif

namespace Engine {
namespace SIMD {

    // ============================================================
    // Vec4 — 4 分量浮点向量
    // ============================================================
    /**
     * @brief 4 元素 SIMD 向量（可用于位置、颜色、四元数）
     */
    struct Vec4 {
        // ── 内部存储（union 保证 x/y/z/w 在所有路径上可用） ──
        union {
        #ifdef SIMD_AVX2
            __m256 m256;
        #endif
        #ifdef SIMD_SSE41
            __m128 m128;
        #endif
        #ifdef SIMD_NEON
            float32x4_t v;
        #endif
            struct { float x, y, z, w; };
        };

        // ── 构造 ──
        Vec4() { SetZero(); }

    #ifdef SIMD_SSE41
        Vec4(__m128 v) noexcept : m128(v) {}
    #elif defined(SIMD_AVX2)
        Vec4(__m256 v) noexcept : m256(v) {}
    #endif

        Vec4(float x, float y, float z, float w = 1.0f) noexcept {
        #ifdef SIMD_SSE41
            m128 = _mm_set_ps(w, z, y, x);
        #elif defined(SIMD_AVX2)
            m256 = _mm256_set_ps(0, 0, 0, w, z, y, x);
        #elif defined(SIMD_NEON)
            float f[4] = {x, y, z, w};
            v = vld1q_f32(f);
        #else
            this->x = x; this->y = y; this->z = z; this->w = w;
        #endif
        }

        explicit Vec4(float s) noexcept : Vec4(s, s, s, s) {}

        // ── 零向量 ──
        void SetZero() noexcept {
        #ifdef SIMD_SSE41
            m128 = _mm_setzero_ps();
        #elif defined(SIMD_AVX2)
            m256 = _mm256_setzero_ps();
        #elif defined(SIMD_NEON)
            v = vdupq_n_f32(0.0f);
        #else
            x = y = z = w = 0.0f;
        #endif
        }

        static Vec4 Zero() noexcept { return Vec4(0.0f); }
    };

    // ══════════════════════════════════════════════════════
    // 基础运算
    // ══════════════════════════════════════════════════════

    inline Vec4 Add(Vec4 a, Vec4 b) noexcept {
    #ifdef SIMD_SSE41
        return Vec4(_mm_add_ps(a.m128, b.m128));
    #elif defined(SIMD_AVX2)
        return Vec4(_mm256_add_ps(a.m256, b.m256));
    #elif defined(SIMD_NEON)
        Vec4 r; r.v = vaddq_f32(a.v, b.v); return r;
    #else
        return Vec4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w);
    #endif
    }

    inline Vec4 Sub(Vec4 a, Vec4 b) noexcept {
    #ifdef SIMD_SSE41
        return Vec4(_mm_sub_ps(a.m128, b.m128));
    #elif defined(SIMD_AVX2)
        return Vec4(_mm256_sub_ps(a.m256, b.m256));
    #elif defined(SIMD_NEON)
        Vec4 r; r.v = vsubq_f32(a.v, b.v); return r;
    #else
        return Vec4(a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w);
    #endif
    }

    inline Vec4 Mul(Vec4 a, Vec4 b) noexcept {
    #ifdef SIMD_SSE41
        return Vec4(_mm_mul_ps(a.m128, b.m128));
    #elif defined(SIMD_AVX2)
        return Vec4(_mm256_mul_ps(a.m256, b.m256));
    #elif defined(SIMD_NEON)
        Vec4 r; r.v = vmulq_f32(a.v, b.v); return r;
    #else
        return Vec4(a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w);
    #endif
    }

    inline Vec4 Scale(Vec4 a, float s) noexcept {
        return Mul(a, Vec4(s));
    }

    // ══════════════════════════════════════════════════════
    // 向量运算
    // ══════════════════════════════════════════════════════

    /** @brief 点积（splat 全通道） */
    inline float Dot(Vec4 a, Vec4 b) noexcept {
    #ifdef SIMD_SSE41
        __m128 m = _mm_mul_ps(a.m128, b.m128);
        m = _mm_hadd_ps(m, m);
        m = _mm_hadd_ps(m, m);
        return _mm_cvtss_f32(m);
    #elif defined(SIMD_AVX2)
        __m128 lo = _mm256_castps256_ps128(a.m256);
        __m128 hi = _mm256_extractf128_ps(a.m256, 1);
        __m128 m = _mm_add_ps(
            _mm_mul_ps(lo, _mm256_castps256_ps128(b.m256)),
            _mm_mul_ps(hi, _mm256_extractf128_ps(b.m256, 1)));
        m = _mm_hadd_ps(m, m);
        m = _mm_hadd_ps(m, m);
        return _mm_cvtss_f32(m);
    #elif defined(SIMD_NEON)
        float32x4_t m = vmulq_f32(a.v, b.v);
        float32x2_t lo = vget_low_f32(m);
        float32x2_t hi = vget_high_f32(m);
        float32x2_t sum = vadd_f32(lo, hi);
        sum = vpadd_f32(sum, sum);
        return vget_lane_f32(sum, 0);
    #else
        return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    #endif
    }

    /** @brief 叉积（仅前 3 分量；w 不变） */
    inline Vec4 Cross(Vec4 a, Vec4 b) noexcept {
    #ifdef SIMD_SSE41
        __m128 yzx_a = _mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 0, 2, 1));
        __m128 zxy_a = _mm_shuffle_ps(a.m128, a.m128, _MM_SHUFFLE(3, 1, 0, 2));
        __m128 yzx_b = _mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 0, 2, 1));
        __m128 zxy_b = _mm_shuffle_ps(b.m128, b.m128, _MM_SHUFFLE(3, 1, 0, 2));
        return Vec4(_mm_sub_ps(
            _mm_mul_ps(yzx_a, zxy_b),
            _mm_mul_ps(zxy_a, yzx_b)));
    #else
        return Vec4(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
            0.0f);
    #endif
    }

    /** @brief 归一化（返回单位向量，零向量 → 零向量） */
    inline Vec4 Normalize(Vec4 v) noexcept {
        float lenSq = Dot(v, v);
        if (lenSq < 1e-12f) return Vec4::Zero();
        float invLen = 1.0f / std::sqrt(lenSq);
        return Scale(v, invLen);
    }

    /** @brief 线性插值 */
    inline Vec4 Lerp(Vec4 a, Vec4 b, float t) noexcept {
    #ifdef SIMD_SSE41
        __m128 ta = _mm_set1_ps(1.0f - t);
        __m128 tb = _mm_set1_ps(t);
        return Vec4(_mm_add_ps(
            _mm_mul_ps(a.m128, ta),
            _mm_mul_ps(b.m128, tb)));
    #else
        float inv = 1.0f - t;
        return Vec4(
            inv * a.x + t * b.x,
            inv * a.y + t * b.y,
            inv * a.z + t * b.z,
            inv * a.w + t * b.w);
    #endif
    }

    // ============================================================
    // Mat4 — 4x4 列主序矩阵（与 GLM 兼容）
    // ============================================================
    /**
     * @brief 4x4 矩阵，4 列各为 Vec4（列主序，与 GLM 一致）
     */
    struct Mat4 {
        Vec4 col[4];

        Mat4() {
            col[0] = Vec4(1,0,0,0);
            col[1] = Vec4(0,1,0,0);
            col[2] = Vec4(0,0,1,0);
            col[3] = Vec4(0,0,0,1);
        }
    };

    /** @brief 矩阵 × 向量（SIMD 优化：4 个广播乘法并行） */
    inline Vec4 MulMatVec(const Mat4& m, Vec4 v) noexcept {
    #ifdef SIMD_SSE41
        __m128 x = _mm_shuffle_ps(v.m128, v.m128, _MM_SHUFFLE(0, 0, 0, 0));
        __m128 y = _mm_shuffle_ps(v.m128, v.m128, _MM_SHUFFLE(1, 1, 1, 1));
        __m128 z = _mm_shuffle_ps(v.m128, v.m128, _MM_SHUFFLE(2, 2, 2, 2));
        __m128 w = _mm_shuffle_ps(v.m128, v.m128, _MM_SHUFFLE(3, 3, 3, 3));

        return Vec4(_mm_add_ps(
            _mm_add_ps(_mm_mul_ps(m.col[0].m128, x),
                       _mm_mul_ps(m.col[1].m128, y)),
            _mm_add_ps(_mm_mul_ps(m.col[2].m128, z),
                       _mm_mul_ps(m.col[3].m128, w))));
    #else
        Vec4 r;
        r.x = m.col[0].x * v.x + m.col[1].x * v.y + m.col[2].x * v.z + m.col[3].x * v.w;
        r.y = m.col[0].y * v.x + m.col[1].y * v.y + m.col[2].y * v.z + m.col[3].y * v.w;
        r.z = m.col[0].z * v.x + m.col[1].z * v.y + m.col[2].z * v.z + m.col[3].z * v.w;
        r.w = m.col[0].w * v.x + m.col[1].w * v.y + m.col[2].w * v.z + m.col[3].w * v.w;
        return r;
    #endif
    }

    // ============================================================
    // AABB — 轴对齐包围盒
    // ============================================================
    struct AABB {
        Vec4 min;
        Vec4 max;

        AABB() : min(0,0,0,0), max(0,0,0,0) {}
        AABB(float minX, float minY, float minZ, float maxX, float maxY, float maxZ)
            : min(minX, minY, minZ, 0), max(maxX, maxY, maxZ, 0) {}
    };

    /**
     * @brief AABB 相交测试（SIMD 优化，4 通道并行比较）
     *
     * 算法：2 个 AABB 相交 ⇔ 所有 3 轴都有重叠
     */
    inline bool AABBIntersect(const AABB& a, const AABB& b) noexcept {
    #ifdef SIMD_SSE41
        // min(a) < max(b) && min(b) < max(a) 对所有轴
        __m128 le1 = _mm_cmple_ps(a.min.m128, b.max.m128);  // a.min <= b.max
        __m128 le2 = _mm_cmple_ps(b.min.m128, a.max.m128);  // b.min <= a.max
        __m128 allLe = _mm_and_ps(le1, le2);
        int mask = _mm_movemask_ps(allLe);
        return (mask & 7) == 7;  // 仅检查 x/y/z（位 0,1,2），忽略 w
    #else
        return (a.min.x <= b.max.x && b.min.x <= a.max.x) &&
               (a.min.y <= b.max.y && b.min.y <= a.max.y) &&
               (a.min.z <= b.max.z && b.min.z <= a.max.z);
    #endif
    }

    /**
     * @brief 批量 AABB 相交（一次处理 4 个 AABB 对）
     *
     * 返回 4 位掩码（bit 0 = AABB pair 0 intersects, ...）。
     */
    inline int AABBIntersect4x(
        const AABB& a0, const AABB& b0,
        const AABB& a1, const AABB& b1,
        const AABB& a2, const AABB& b2,
        const AABB& a3, const AABB& b3) noexcept
    {
        // 标量 fallback
        int mask = 0;
        if (AABBIntersect(a0, b0)) mask |= 1;
        if (AABBIntersect(a1, b1)) mask |= 2;
        if (AABBIntersect(a2, b2)) mask |= 4;
        if (AABBIntersect(a3, b3)) mask |= 8;
        return mask;
    }

    // ============================================================
    // Frustum — 视锥体裁剪
    // ============================================================
    /**
     * @brief 平面方程：Normal · X + D = 0（Vec4 = {Nx, Ny, Nz, D}）
     */
    struct Plane {
        Vec4 normalD;  // (nx, ny, nz, d)
    };

    /**
     * @brief 视锥体（6 平面）
     *
     * 布局：
     *   [0] = Left,   [1] = Right
     *   [2] = Bottom, [3] = Top
     *   [4] = Near,   [5] = Far
     */
    struct Frustum {
        Plane planes[6];
    };

    /**
     * @brief 点-平面距离（带符号）
     *
     * @param p 平面
     * @param point 带 W=1 的齐次点
     * @return 有符号距离（>0 = 在正半空间，<0 = 在负半空间）
     */
    inline float SignedDistance(const Plane& p, Vec4 point) noexcept {
        // dot(normal, point) + d
        return Dot(p.normalD, point) + p.normalD.w;
    }

    /**
     * @brief AABB vs Frustum — 完整视锥体裁剪测试
     *
     * 返回枚举：
     *   -1 = 完全在视锥外（可剔除）
     *    1 = 完全在视锥内
     *    0 = 相交
     */
    inline int FrustumCull(const Frustum& frustum, const AABB& box) noexcept {
        int result = 1;  // 假设完全在内

        Vec4 boxMin = box.min;
        Vec4 boxMax = box.max;
        Vec4 boxMinW1 = Vec4(boxMin.x, boxMin.y, boxMin.z, 1.0f);
        Vec4 boxMaxW1 = Vec4(boxMax.x, boxMax.y, boxMax.z, 1.0f);

        for (int i = 0; i < 6; ++i) {
            const Plane& p = frustum.planes[i];

            // 选 AABB 的 P 顶点和 N 顶点（基于平面法线符号）
            // P 顶点 = 最大法线分量对齐的点 → 距离最大
            // N 顶点 = 最小法线分量对齐的点 → 距离最小
            Vec4 N = boxMin, P = boxMax;
        #ifdef SIMD_SSE41
            __m128 cmp = _mm_cmpge_ps(p.normalD.m128, _mm_setzero_ps());
            // 用 blend 选择 min/max
            // 简化：标量分支（SIMD 版可用 _mm_blendv_ps）
            if (p.normalD.x >= 0) { N.x = boxMin.x; P.x = boxMax.x; }
            else                    { N.x = boxMax.x; P.x = boxMin.x; }
            if (p.normalD.y >= 0) { N.y = boxMin.y; P.y = boxMax.y; }
            else                    { N.y = boxMax.y; P.y = boxMin.y; }
            if (p.normalD.z >= 0) { N.z = boxMin.z; P.z = boxMax.z; }
            else                    { N.z = boxMax.z; P.z = boxMin.z; }
        #else
            if (p.normalD.x >= 0) { N.x = boxMin.x; P.x = boxMax.x; }
            else                    { N.x = boxMax.x; P.x = boxMin.x; }
            if (p.normalD.y >= 0) { N.y = boxMin.y; P.y = boxMax.y; }
            else                    { N.y = boxMax.y; P.y = boxMin.y; }
            if (p.normalD.z >= 0) { N.z = boxMin.z; P.z = boxMax.z; }
            else                    { N.z = boxMax.z; P.z = boxMin.z; }
        #endif

            // 距离最小顶点（N 顶点）在平面外？ → AABB 完全在视锥外
            float d = SignedDistance(p, N);
            if (d > 0.0f) return -1;  // 完全在外

            // 距离最大顶点（P 顶点）在平面外？ → 相交（不能确定完全在内）
            float d2 = SignedDistance(p, P);
            if (d2 > 0.0f) result = 0;
        }

        return result;  // 1 = 完全在内，0 = 相交
    }

    // ============================================================
    // 工具：AoS ↔ SoA 批量转换
    // ============================================================
    /**
     * @brief 将 4 个 AoS Vec4 转换为 SoA 布局
     *
     * 输入：4 个 Vec4 a, b, c, d
     * 布局：[a.x, b.x, c.x, d.x] [a.y, b.y, c.y, d.y] ...
     */
    inline void Transpose4x4(Vec4 a, Vec4 b, Vec4 c, Vec4 d,
                             Vec4& outX, Vec4& outY, Vec4& outZ, Vec4& outW) noexcept
    {
    #ifdef SIMD_SSE41
        __m128 tmp0 = _mm_shuffle_ps(a.m128, b.m128, _MM_SHUFFLE(1,0,1,0));
        __m128 tmp1 = _mm_shuffle_ps(a.m128, b.m128, _MM_SHUFFLE(3,2,3,2));
        __m128 tmp2 = _mm_shuffle_ps(c.m128, d.m128, _MM_SHUFFLE(1,0,1,0));
        __m128 tmp3 = _mm_shuffle_ps(c.m128, d.m128, _MM_SHUFFLE(3,2,3,2));
        outX = Vec4(_mm_shuffle_ps(tmp0, tmp2, _MM_SHUFFLE(2,0,2,0)));
        outY = Vec4(_mm_shuffle_ps(tmp0, tmp2, _MM_SHUFFLE(3,1,3,1)));
        outZ = Vec4(_mm_shuffle_ps(tmp1, tmp3, _MM_SHUFFLE(2,0,2,0)));
        outW = Vec4(_mm_shuffle_ps(tmp1, tmp3, _MM_SHUFFLE(3,1,3,1)));
    #else
        outX = Vec4(a.x, b.x, c.x, d.x);
        outY = Vec4(a.y, b.y, c.y, d.y);
        outZ = Vec4(a.z, b.z, c.z, d.z);
        outW = Vec4(a.w, b.w, c.w, d.w);
    #endif
    }

    // ============================================================
    // 纹理/颜色操作
    // ============================================================
    /** @brief RGBA32 打包颜色：4 通道 float → 32bit（0xAABBGGRR） */
    inline uint32_t PackRGBA32(Vec4 color) noexcept {
        uint8_t r = static_cast<uint8_t>(std::clamp(color.x, 0.0f, 1.0f) * 255.0f + 0.5f);
        uint8_t g = static_cast<uint8_t>(std::clamp(color.y, 0.0f, 1.0f) * 255.0f + 0.5f);
        uint8_t b = static_cast<uint8_t>(std::clamp(color.z, 0.0f, 1.0f) * 255.0f + 0.5f);
        uint8_t a = static_cast<uint8_t>(std::clamp(color.w, 0.0f, 1.0f) * 255.0f + 0.5f);
        return (uint32_t(a) << 24) | (uint32_t(b) << 16) | (uint32_t(g) << 8) | uint32_t(r);
    }

    /** @brief 解包 RGBA32 → Vec4 */
    inline Vec4 UnpackRGBA32(uint32_t packed) noexcept {
        float r = ((packed >> 0)  & 0xFF) / 255.0f;
        float g = ((packed >> 8)  & 0xFF) / 255.0f;
        float b = ((packed >> 16) & 0xFF) / 255.0f;
        float a = ((packed >> 24) & 0xFF) / 255.0f;
        return Vec4(r, g, b, a);
    }

    // ============================================================
    // 工具：含 3 分量 GLM 兼容辅助
    // ============================================================
    /** 从 GLM vec3 + w 构造 Vec4 */
    inline Vec4 FromGLM(const float* glmVec, float w = 1.0f) noexcept {
        return Vec4(glmVec[0], glmVec[1], glmVec[2], w);
    }

    /** 提取 Vec4 的前 3 分量到 GLM vec3 兼容缓冲 */
    inline void ToGLM(Vec4 v, float* out3) noexcept {
        out3[0] = v.x; out3[1] = v.y; out3[2] = v.z;
    }

} // namespace SIMD
} // namespace Engine