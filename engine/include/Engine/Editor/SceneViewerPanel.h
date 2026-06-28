#pragma once

/**
 * @file SceneViewerPanel.h
 * @brief 场景查看器面板 — 可视化多场景层级、空间关系与运行时资源监控
 *
 * 与 SceneManagerPanel 构成"配置→编辑→监控"的全链路闭环：
 *   - SceneManagerPanel: 增删场景、配置依赖、设置加载策略（"控制台"）
 *   - SceneViewerPanel:  监控运行时状态、可视化场景空间关系（"地图"）
 *
 * ═══════════════════════ 功能模块 ═══════════════════════
 *
 * 1. 场景层级树 (Enhanced Hierarchy)
 *    - 按类型分组显示 (Permanent / Environment / Gameplay / UI)
 *    - 状态图标：云朵(远程) / 锁(锁定) / 眼睛(可见性切换)
 *    - 右键操作：Save Alone / Extract to New Scene / Move to Top
 *
 * 2. 空间范围可视化 (Spatial Bounds)
 *    - AABB 包围盒显示（不同场景不同颜色）
 *    - 加载触发区可视化
 *    - 摄像机距离标签
 *
 * 3. Solo / Focus 模式
 *    - Solo Mode：只显示选中场景 + 主场景
 *    - View Focus：双击聚焦到场景包围盒中心
 *
 * 4. 运行时资源热力图
 *    - 每场景内存占用条
 *    - 顶点数 / DrawCall / 渲染器数量
 *    - 加载耗时分解
 *
 * ═══════════════════════ 使用方式 ═══════════════════════
 *
 * @code
 *   SceneViewerPanel viewer;
 *   viewer.OnImGui();  // 在 DockSpace 中渲染
 *   viewer.OnOverlay(dt, camera, viewProj); // 在 3D 视口绘制场景包围盒
 * @endcode
 */

#include "Engine/Types.h"
#include "Engine/Core/Scene/Scene.h"
#include "Engine/Core/Scene/SceneTypes.h"
#include "Engine/Core/Scene/SceneManager.h"
#include "Engine/Core/Level/Level.h"
#include "Engine/Core/RHI/MathTypes.h"
#include "Engine/Core/RHI/ISceneGraph.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <unordered_map>
#include <set>

namespace Engine {

    class EditorCamera;

    // ============================================================
    // 场景查看器条目（包装 Level 和实时数据）
    // ============================================================
    struct SceneViewerEntry {
        const Level* level = nullptr;
        std::string  displayName;

        // 实时监控数据
        float memoryUsageMB  = 0.0f;
        uint32 objectCount   = 0;
        uint32 drawCallCount = 0;
        uint32 vertexCount   = 0;

        // 空间数据
        AABB   bounds;
        Vec3   center;
        float  distanceToCamera = 0.0f;

        // 查看器状态
        bool solo     = false;  // Solo 模式激活
        bool visible  = true;   // 眼睛图标切换
        bool focused  = false;  // 是否被聚焦

        // 加载耗时分解（从 SceneLoadProfile 获取）
        double ioTimeMs          = 0.0;
        double deserializeTimeMs = 0.0;
        double initTimeMs        = 0.0;
        double totalTimeMs       = 0.0;
    };

    // ============================================================
    // 场景分组定义
    // ============================================================
    enum class SceneCategoryGroup : uint8 {
        Persistent,   ///< 常驻场景（全局系统）
        Environment,  ///< 环境场景
        Gameplay,     ///< 游戏逻辑场景
        UI,           ///< UI 场景
        Cinematic,    ///< 过场动画
        Debug,        ///< 调试场景
        COUNT
    };

    inline const char* SceneCategoryGroupName(SceneCategoryGroup g) {
        switch (g) {
            case SceneCategoryGroup::Persistent:  return "📌 Persistent";
            case SceneCategoryGroup::Environment: return "🌿 Environment";
            case SceneCategoryGroup::Gameplay:    return "🎮 Gameplay";
            case SceneCategoryGroup::UI:          return "🖥 UI";
            case SceneCategoryGroup::Cinematic:   return "🎬 Cinematic";
            case SceneCategoryGroup::Debug:       return "🐛 Debug";
            default:                              return "Unknown";
        }
    }

    // ============================================================
    // 场景查看器面板
    // ============================================================
    class SceneViewerPanel {
    public:
        SceneViewerPanel();
        ~SceneViewerPanel() = default;

        SceneViewerPanel(const SceneViewerPanel&) = delete;
        SceneViewerPanel& operator=(const SceneViewerPanel&) = delete;

        // ── 面板主渲染 ──
        void OnImGui();

        /**
         * @brief 注入当前编辑场景（当不使用 Level/SceneManager 系统时）
         * @param scene 编辑中的场景指针，为 nullptr 时清除缓存
         *
         * 用于 EditorDemo 等直接通过 SetEditorScene() 管理场景但不注册到
         * Level 系统的场景。注入后 OnOverlay() 可以正常绘制场景包围盒和标签。
         */
        void SetEditorScene(Scene* scene);

        // ── 3D 视口叠加层（在 ViewportPanel 渲染后调用） ──
        /**
         * @brief 在场景视口中绘制场景包围盒和标签
         * @param dt        Delta time
         * @param camera    编辑器相机（用于距离计算和标签朝向）
         * @param drawList  ImDrawList 或渲染上下文
         */
        void OnOverlay(float32 dt, const EditorCamera* camera);

        // ── 可见性 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        // ── 联动接口（与 SceneManagerPanel 通讯） ──
        /** 高亮显示指定场景组包含的所有场景 */
        void HighlightGroup(const std::string& groupName);

        /** 聚焦到指定场景 */
        void FocusOnScene(const std::string& sceneName);

        /** 清除所有高亮 */
        void ClearHighlights();

        // ── 回调 ──
        using SceneSelectCallback = std::function<void(const std::string& sceneName)>;
        void SetSceneSelectCallback(SceneSelectCallback cb) { m_OnSceneSelect = std::move(cb); }

        using FocusRequestCallback = std::function<void(const Vec3& center, float radius)>;
        void SetFocusRequestCallback(FocusRequestCallback cb) { m_OnFocusRequest = std::move(cb); }

    private:
        // ── 内部绘制函数 ──
        void DrawToolbar();
        void DrawHierarchyTree();
        void DrawCategoryGroup(SceneCategoryGroup category,
                               const std::vector<SceneViewerEntry>& entries);
        void DrawSceneEntry(const SceneViewerEntry& entry);

        // ── 运行时热力图 ──
        void DrawHeatmapPanel();

        // ── 数据刷新 ──
        void RefreshSceneData();

        // ── 查找场景类别 ──
        SceneCategoryGroup CategorizeScene(const std::string& name) const;

        // ── 数据 ──
        bool m_Visible = true;

        // 场景条目缓存（按类别分组）
        std::unordered_map<SceneCategoryGroup, std::vector<SceneViewerEntry>> m_SceneEntries;
        float m_RefreshTimer = 0.0f;

        // Solo 模式
        bool  m_SoloModeActive = false;
        std::string m_SoloSceneName;

        // 高亮
        std::set<std::string> m_HighlightedScenes;

        // 搜索
        char m_SearchBuffer[256] = {};

        // 显示开关
        bool m_ShowHeatmap     = true;
        bool m_ShowBounds      = true;
        bool m_ShowDistances   = true;
        bool m_ShowLoadingVolumes = false;

        // 空间可视化颜色
        float m_BoundsOpacity = 0.4f;

        // 回调
        SceneSelectCallback   m_OnSceneSelect;
        FocusRequestCallback  m_OnFocusRequest;

        // 视口叠加缓存
        struct OverlayCache {
            Vec3   sceneCenter;
            AABB   sceneBounds;
            Vec4   color;
            std::string name;
            float  distance;
        };
        std::vector<OverlayCache> m_OverlayCache;

        // Solo 颜色分配
        uint32 m_NextColorIndex = 0;
        static const Vec4 k_SceneColors[8];
    };

} // namespace Engine