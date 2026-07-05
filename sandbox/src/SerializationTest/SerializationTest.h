#pragma once

/**
 * @file SerializationTest.h
 * @brief 场景序列化/反序列化完整测试套件
 *
 * 测试覆盖：
 *   - 场景属性 (环境光、重力、雾等) 的保存与恢复
 *   - Transform (位置/旋转/缩放) 的精确往返
 *   - SpriteComponent 全部字段的 JSON 输出与读回
 *   - PhysicsComponent 标记字段的保存
 *   - GameObject 父子层级的递归序列化
 *   - 多对象场景的整体保存/加载
 *   - 空场景、非活跃对象等边缘情况
 *   - 资源路径引用的完整性 (纹理路径字符串)
 *   - 注册式组件工厂的正确性 (类型映射)
 *
 * 限制：
 *   - 不依赖 OpenGL 上下文，因此不实际加载纹理
 *   - SpriteComponent 的纹理路径作为字符串验证
 *
 * 使用方法：
 *   SerializationTest::RunAll();
 */

#include <Engine/Core/RHI/MathTypes.h>
#include <Engine/Types.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <string>

namespace Engine {

    class SerializationTest {
    public:
        /** 运行全部测试，返回是否全部通过 */
        static bool RunAll();

        /** 获取通过的测试数 */
        static int GetPassed() { return s_Passed; }
        /** 获取失败的测试数 */
        static int GetFailed() { return s_Failed; }

    private:
        // ── 测试用例 ──
        static bool TestSceneProperties();
        static bool TestEmptyScene();
        static bool TestTransformRoundTrip();
        static bool TestSingleObjectWithSprite();
        static bool TestParentChildHierarchy();
        static bool TestMultipleRootObjects();
        static bool TestInactiveObject();
        static bool TestComponentFactoryRegistration();
        static bool TestResourcePathPreservation();
        static bool TestJsonFormatStructure();
        static bool TestSceneSaveLoadFile();

        // ── 辅助 ──
        static bool Vec3Equal(const Vec3& a, const Vec3& b, float eps = 0.001f);
        static bool Vec4Equal(const Vec4& a, const Vec4& b, float eps = 0.001f);

        static int s_Passed;
        static int s_Failed;
    };

#define TEST_SECTION(name) \
    std::cout << "\n[" << name << "]" << std::endl;

#define TEST_CASE(name, expr) \
    do { \
        std::cout << "  " << name << "... "; \
        if (expr) { \
            std::cout << "PASSED" << std::endl; \
            SerializationTest::s_Passed++; \
        } else { \
            std::cout << "FAILED" << std::endl; \
            SerializationTest::s_Failed++; \
        } \
    } while(0)

} // namespace Engine
