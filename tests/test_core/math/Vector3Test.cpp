/**
 * @file Vector3Test.cpp
 * @brief GLM 数学库测试
 *
 * 引擎使用 GLM 作为数学库，本测试验证 GLM 基本操作正确性。
 */
#include <gtest/gtest.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

using namespace glm;

TEST(Vector3Test, DefaultConstructorIsZero) {
    vec3 v; // GLM 默认初始化为 (0,0,0)
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(Vector3Test, ParameterizedConstructor) {
    vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(Vector3Test, CrossProduct) {
    vec3 a(1, 0, 0);
    vec3 b(0, 1, 0);
    vec3 c = cross(a, b);
    EXPECT_NEAR(c.x, 0.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.0f, 1e-6f);
    EXPECT_NEAR(c.z, 1.0f, 1e-6f);
}

TEST(Vector3Test, DotProduct) {
    vec3 a(1, 2, 3);
    vec3 b(4, 5, 6);
    float d = dot(a, b);
    EXPECT_FLOAT_EQ(d, 32.0f); // 1*4 + 2*5 + 3*6 = 32
}

TEST(Vector3Test, Length) {
    vec3 v(3, 4, 0);
    EXPECT_FLOAT_EQ(length(v), 5.0f);
}

TEST(Vector3Test, Normalize) {
    vec3 v(0, 5, 0);
    vec3 n = normalize(v);
    EXPECT_FLOAT_EQ(n.x, 0.0f);
    EXPECT_FLOAT_EQ(n.y, 1.0f);
    EXPECT_FLOAT_EQ(n.z, 0.0f);
}

TEST(Vector3Test, Addition) {
    vec3 a(1, 2, 3);
    vec3 b(4, 5, 6);
    vec3 c = a + b;
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 7.0f);
    EXPECT_FLOAT_EQ(c.z, 9.0f);
}

TEST(Vector3Test, ScalarMultiplication) {
    vec3 v(1, 2, 3);
    vec3 r = v * 2.0f;
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 4.0f);
    EXPECT_FLOAT_EQ(r.z, 6.0f);
}

TEST(Vector3Test, Equality) {
    vec3 a(1, 2, 3);
    vec3 b(1, 2, 3);
    vec3 c(1, 2, 4);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
}

TEST(Matrix4Test, IdentityMatrix) {
    mat4 m = mat4(1.0f); // GLM identity
    EXPECT_FLOAT_EQ(m[0][0], 1.0f);
    EXPECT_FLOAT_EQ(m[1][1], 1.0f);
    EXPECT_FLOAT_EQ(m[2][2], 1.0f);
    EXPECT_FLOAT_EQ(m[3][3], 1.0f);
}

TEST(Matrix4Test, Translation) {
    mat4 t = translate(mat4(1.0f), vec3(10, 20, 30));
    vec4 p(1, 2, 3, 1);
    vec4 tp = t * p;
    EXPECT_FLOAT_EQ(tp.x, 11.0f);
    EXPECT_FLOAT_EQ(tp.y, 22.0f);
    EXPECT_FLOAT_EQ(tp.z, 33.0f);
    EXPECT_FLOAT_EQ(tp.w, 1.0f);
}

TEST(Matrix4Test, RotateAndTranslate) {
    mat4 m = rotate(mat4(1.0f), radians(90.0f), vec3(0, 1, 0));
    vec4 p(1, 0, 0, 1);
    vec4 rp = m * p;
    // 绕 Y 轴旋转 90°: (1,0,0) → (0,0,-1)
    EXPECT_NEAR(rp.x, 0.0f, 1e-5f);
    EXPECT_NEAR(rp.z, -1.0f, 1e-5f);
}