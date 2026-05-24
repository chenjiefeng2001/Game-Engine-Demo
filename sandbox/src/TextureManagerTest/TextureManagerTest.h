#pragma once

#include <Engine/Core/IGraphicsFactory.h>
#include <Engine/Core/RenderResources/Texture.h>
#include <Engine/Core/RenderResources/TextureManager.h>
#include <memory>
#include <string>

namespace Engine {

    /**
     * @brief TextureManager 功能验证测试
     *
     * 测试项：
     *   1. Load — 首次加载返回有效纹理
     *   2. Load — 相同路径第二次返回同一指针（缓存命中）
     *   3. Get  — 按路径查询已缓存的纹理
     *   4. Has  — 判断路径是否已缓存
     *   5. Count— 缓存数量正确
     *   6. Reload — 强制重新加载
     *   7. Remove — 移除指定纹理
     *   8. Clear — 清空所有缓存
     */
    class TextureManagerTest {
    public:
        explicit TextureManagerTest(IGraphicsFactory& factory);
        ~TextureManagerTest() = default;

        /**
         * @brief 运行所有测试用例
         * @return true 全部通过，false 有失败
         */
        bool Run();

    private:
        // ── 测试用例 ──
        bool TestLoadOnce();          // 加载纹理，验证非空
        bool TestLoadCache();         // 相同路径两次加载，验证指针相同
        bool TestGetTexture();        // Get() 返回正确的纹理
        bool TestHasTexture();        // Has() 正确判断存在/不存在
        bool TestCount();             // Count() 返回正确数量
        bool TestReload();            // Reload() 强制重新加载
        bool TestRemove();            // Remove() 移除后不可查询
        bool TestClear();             // Clear() 清空所有

        // ── 辅助 ──
        void PrintResult(const char* testName, bool passed);
        void PrintSummary();

        IGraphicsFactory& m_Factory;
        TextureManager m_TextureManager;

        // 统计
        int m_Passed  = 0;
        int m_Failed  = 0;

        // 测试用纹理路径
        static constexpr const char* kTexturePath = "assets/textures/test.png";
        static constexpr const char* kNonExistentPath = "assets/textures/non_existent.png";
    };

} // namespace Engine
