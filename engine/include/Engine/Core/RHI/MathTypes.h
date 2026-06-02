#pragma once

/**
 * @file MathTypes.h
 * @brief RHI 纯数据数学类型
 *
 * RHI 原则：
 *   - 此头文件不包含任何第三方数学库（glm / DirectXMath 等）
 *   - 所有类型都是 POD 结构体，内存布局与 GPU 着色器一致
 *   - 数学运算在 .cpp 实现文件中使用具体数学库完成
 *   - 核心引擎接口只依赖此文件，不依赖 glm
 */

#include "Engine/Types.h"
#include <cstring>   // memcpy, memset

namespace Engine {

// ──────────────────────────────────────────
// 2D 向量
// ──────────────────────────────────────────
#pragma pack(push, 1)
struct Vec2 {
    float32 x, y;

    Vec2() : x(0.0f), y(0.0f) {}
    Vec2(float32 x_, float32 y_) : x(x_), y(y_) {}

    float32& operator[](int32 i)       { return (&x)[i]; }
    float32  operator[](int32 i) const { return (&x)[i]; }

    bool operator==(const Vec2& o) const { return x == o.x && y == o.y; }
    bool operator!=(const Vec2& o) const { return !(*this == o); }
};

// ──────────────────────────────────────────
// 3D 向量
// ──────────────────────────────────────────
struct Vec3 {
    float32 x, y, z;

    Vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    Vec3(float32 x_, float32 y_, float32 z_) : x(x_), y(y_), z(z_) {}

    float32& operator[](int32 i)       { return (&x)[i]; }
    float32  operator[](int32 i) const { return (&x)[i]; }

    bool operator==(const Vec3& o) const { return x == o.x && y == o.y && z == o.z; }
    bool operator!=(const Vec3& o) const { return !(*this == o); }
};

// ──────────────────────────────────────────
// 4D 向量
// ──────────────────────────────────────────
struct Vec4 {
    float32 x, y, z, w;

    Vec4() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Vec4(float32 x_, float32 y_, float32 z_, float32 w_)
        : x(x_), y(y_), z(z_), w(w_) {}

    float32& operator[](int32 i)       { return (&x)[i]; }
    float32  operator[](int32 i) const { return (&x)[i]; }

    bool operator==(const Vec4& o) const { return x == o.x && y == o.y && z == o.z && w == o.w; }
    bool operator!=(const Vec4& o) const { return !(*this == o); }
};
#pragma pack(pop)

// ──────────────────────────────────────────
// 四元数
// ──────────────────────────────────────────
struct Quat {
    float32 x, y, z, w;

    Quat() : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}
    Quat(float32 x_, float32 y_, float32 z_, float32 w_)
        : x(x_), y(y_), z(z_), w(w_) {}

    float32& operator[](int32 i)       { return (&x)[i]; }
    float32  operator[](int32 i) const { return (&x)[i]; }

    bool operator==(const Quat& o) const {
        return x == o.x && y == o.y && z == o.z && w == o.w;
    }
    bool operator!=(const Quat& o) const { return !(*this == o); }

    /** 单位四元数（无旋转） */
    static Quat Identity() { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }
};

// ──────────────────────────────────────────
// 4×4 矩阵（列主序，与 OpenGL / glm 内存布局一致）
//
// 内存布局: data[col * 4 + row]
//    data[ 0]  data[ 4]  data[ 8]  data[12]    ← col 0
//    data[ 1]  data[ 5]  data[ 9]  data[13]    ← col 1
//    data[ 2]  data[ 6]  data[10]  data[14]    ← col 2
//    data[ 3]  data[ 7]  data[11]  data[15]    ← col 3
// ──────────────────────────────────────────
struct Mat4 {
    float32 data[16];

    Mat4() { Identity(); }

    // ---- 构造 ----
    Mat4& Identity() {
        std::memset(data, 0, sizeof(data));
        data[0] = data[5] = data[10] = data[15] = 1.0f;
        return *this;
    }

    static Mat4 Zero() {
        Mat4 m;
        std::memset(m.data, 0, sizeof(m.data));
        return m;
    }

    // ---- 访问器 ----
    const float32* Data() const { return data; }
          float32* Data()       { return data; }

    // 元素访问: (row, col)，row 和 col 范围 0-3
    float32& operator()(int32 row, int32 col)       { return data[col * 4 + row]; }
    float32  operator()(int32 row, int32 col) const { return data[col * 4 + row]; }

    // ---- 相等 ----
    bool operator==(const Mat4& o) const {
        return std::memcmp(data, o.data, sizeof(data)) == 0;
    }
    bool operator!=(const Mat4& o) const { return !(*this == o); }
};


// ============================================================
// RHI 内联工具函数（纯数据操作，不依赖第三方库）
// ============================================================

/** 4×4 矩阵乘法: out = a * b */
inline void Mat4Multiply(const Mat4& a, const Mat4& b, Mat4& out) {
    for (int32 col = 0; col < 4; ++col) {
        for (int32 row = 0; row < 4; ++row) {
            out.data[col * 4 + row] =
                a.data[0 * 4 + row] * b.data[col * 4 + 0] +
                a.data[1 * 4 + row] * b.data[col * 4 + 1] +
                a.data[2 * 4 + row] * b.data[col * 4 + 2] +
                a.data[3 * 4 + row] * b.data[col * 4 + 3];
        }
    }
}

/** 4×4 矩阵乘向量: out = m * v */
inline Vec4 Mat4MultiplyVec4(const Mat4& m, const Vec4& v) {
    Vec4 result;
    for (int32 row = 0; row < 4; ++row) {
        result[row] =
            m.data[0 * 4 + row] * v[0] +
            m.data[1 * 4 + row] * v[1] +
            m.data[2 * 4 + row] * v[2] +
            m.data[3 * 4 + row] * v[3];
    }
    return result;
}

/** 矩阵转置 */
inline void Mat4Transpose(const Mat4& in, Mat4& out) {
    for (int32 col = 0; col < 4; ++col)
        for (int32 row = 0; row < 4; ++row)
            out.data[col * 4 + row] = in.data[row * 4 + col];
}

/** 复制 float[16] 到 Mat4 */
inline void Mat4Copy(const float32* src, Mat4& out) {
    std::memcpy(out.data, src, sizeof(float32) * 16);
}

/** 从 Mat4 复制到 float[16] */
inline void Mat4CopyTo(const Mat4& m, float32* dst) {
    std::memcpy(dst, m.data, sizeof(float32) * 16);
}

} // namespace Engine
