#include "TextureManagerTest.h"

#include <Engine/Core/IWindow.h>
#include <Engine/Core/IRenderContext.h>
#include <iostream>
#include <cassert>

namespace Engine {

    // ──────────────────────────────────────────────
    // 构造
    // ──────────────────────────────────────────────
    TextureManagerTest::TextureManagerTest(IGraphicsFactory& factory)
        : m_Factory(factory)
        , m_TextureManager(factory)
    {
    }

    // ──────────────────────────────────────────────
    // 主入口
    // ──────────────────────────────────────────────
    bool TextureManagerTest::Run() {
        std::cout << "\n==============================================" << std::endl;
        std::cout << "  TextureManager Test Suite" << std::endl;
        std::cout << "==============================================" << std::endl;

        // 必须先创建一个窗口以使 OpenGL 上下文有效
        auto window = m_Factory.CreateWindow(1, 1, "TextureManager Test");
        if (!window) {
            std::cerr << "[FAIL] 无法创建测试窗口" << std::endl;
            return false;
        }

        // 先清空可能残留的缓存
        m_TextureManager.Clear();

        // 执行测试
        TestLoadOnce();
        TestLoadCache();
        TestGetTexture();
        TestHasTexture();
        TestCount();
        TestReload();
        TestRemove();
        TestClear();

        PrintSummary();
        return m_Failed == 0;
    }

    // ──────────────────────────────────────────────
    // 测试用例 1：首次加载
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestLoadOnce() {
        // 清除之前的状态
        m_TextureManager.Clear();

        auto tex = m_TextureManager.Load(kTexturePath);
        bool passed = (tex != nullptr);
        PrintResult("LoadOnce — 首次加载返回非空纹理", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 2：缓存命中
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestLoadCache() {
        m_TextureManager.Clear();

        auto first  = m_TextureManager.Load(kTexturePath);
        auto second = m_TextureManager.Load(kTexturePath);

        // 两次应返回同一个 shared_ptr 对象（地址相同）
        bool passed = (first != nullptr && second != nullptr && first == second);
        PrintResult("LoadCache — 相同路径加载两次返回同一指针", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 3：Get 查询
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestGetTexture() {
        m_TextureManager.Clear();

        auto loaded = m_TextureManager.Load(kTexturePath);
        if (!loaded) {
            PrintResult("Get — 加载纹理失败，跳过测试", false);
            return false;
        }

        auto got = m_TextureManager.Get(kTexturePath);
        bool passed = (got != nullptr && got == loaded);
        PrintResult("Get — 按路径查询返回正确的纹理", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 4：Has 判断
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestHasTexture() {
        m_TextureManager.Clear();

        // Load 之前不应该存在
        bool hasBefore = m_TextureManager.Has(kTexturePath);

        auto tex = m_TextureManager.Load(kTexturePath);

        // Load 之后应该存在
        bool hasAfter = m_TextureManager.Has(kTexturePath);

        // 不存在的路径
        bool hasNonExistent = m_TextureManager.Has(kNonExistentPath);

        bool passed = (!hasBefore && hasAfter && !hasNonExistent);
        PrintResult("Has — 正确判断纹理存在性", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 5：Count 计数
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestCount() {
        m_TextureManager.Clear();

        // 加载前计数为 0
        bool countZero = (m_TextureManager.Count() == 0);

        m_TextureManager.Load(kTexturePath);

        // 加载 1 个后计数为 1
        bool countOne = (m_TextureManager.Count() == 1);

        // 重复加载同一路径，计数仍为 1
        m_TextureManager.Load(kTexturePath);
        bool countStillOne = (m_TextureManager.Count() == 1);

        bool passed = (countZero && countOne && countStillOne);
        PrintResult("Count — 缓存计数正确", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 6：Reload 重新加载
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestReload() {
        m_TextureManager.Clear();

        auto original = m_TextureManager.Load(kTexturePath);
        if (!original) {
            PrintResult("Reload — 首次加载失败，跳过测试", false);
            return false;
        }

        // 重新加载
        auto reloaded = m_TextureManager.Reload(kTexturePath);

        // Reload 应返回有效纹理
        bool valid = (reloaded != nullptr);

        // Reload 产生的纹理可能不同（新的 GL 对象），但不为 null
        // 注意：由于 OpenGL 实现每次会生成新的渲染 ID，所以指针可能不同
        // 我们只验证有效性即可
        PrintResult("Reload — 强制重新加载返回有效纹理", valid);
        return valid;
    }

    // ──────────────────────────────────────────────
    // 测试用例 7：Remove 移除
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestRemove() {
        m_TextureManager.Clear();

        m_TextureManager.Load(kTexturePath);
        bool hasBeforeRemove = m_TextureManager.Has(kTexturePath);

        m_TextureManager.Remove(kTexturePath);
        bool hasAfterRemove  = m_TextureManager.Has(kTexturePath);
        size_t countAfter    = m_TextureManager.Count();

        bool passed = (hasBeforeRemove && !hasAfterRemove && countAfter == 0);
        PrintResult("Remove — 移除后不可查询且计数归零", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 测试用例 8：Clear 清空
    // ──────────────────────────────────────────────
    bool TextureManagerTest::TestClear() {
        m_TextureManager.Clear();

        m_TextureManager.Load(kTexturePath);
        bool hasBeforeClear = m_TextureManager.Has(kTexturePath);
        size_t countBefore  = m_TextureManager.Count();

        m_TextureManager.Clear();
        bool hasAfterClear  = m_TextureManager.Has(kTexturePath);
        size_t countAfter   = m_TextureManager.Count();

        bool passed = (hasBeforeClear && countBefore == 1 && !hasAfterClear && countAfter == 0);
        PrintResult("Clear — 清空后无残留纹理", passed);
        return passed;
    }

    // ──────────────────────────────────────────────
    // 辅助方法
    // ──────────────────────────────────────────────
    void TextureManagerTest::PrintResult(const char* testName, bool passed) {
        if (passed) {
            std::cout << "  [PASS] " << testName << std::endl;
            m_Passed++;
        } else {
            std::cout << "  [FAIL] " << testName << std::endl;
            m_Failed++;
        }
    }

    void TextureManagerTest::PrintSummary() {
        std::cout << "----------------------------------------------" << std::endl;
        std::cout << "  结果: " << m_Passed << " 通过, "
                  << m_Failed << " 失败, "
                  << (m_Passed + m_Failed) << " 总计" << std::endl;
        std::cout << "==============================================\n" << std::endl;
    }

} // namespace Engine
