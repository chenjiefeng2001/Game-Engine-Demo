#pragma once

/**
 * @file DependencyGraphPanel.h
 * @brief 依赖关系图面板 — 可视化资产依赖关系 + 管道构建控制
 *
 * 功能：
 *   - 按 GUID/路径/类型浏览资产
 *   - 查看选中资产的上下游依赖链
 *   - 检测并显示循环依赖
 *   - 运行资产调节管道（单资产 / 批量）
 *   - 查看构建报告
 */

#include "Engine/Types.h"
#include "Engine/Core/Resources/AssetDatabase.h"
#include "Engine/Core/Resources/AssetPipeline.h"
#include <string>
#include <vector>
#include <memory>

namespace Engine {

    class DependencyGraphPanel {
    public:
        DependencyGraphPanel() = default;
        ~DependencyGraphPanel() = default;

        DependencyGraphPanel(const DependencyGraphPanel&) = delete;
        DependencyGraphPanel& operator=(const DependencyGraphPanel&) = delete;

        // ── 配置 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

        /** 设置数据库引用 */
        void SetDatabase(AssetDatabase* db) { m_Database = db; }

        /** 设置管道引用 */
        void SetPipeline(AssetPipeline* pipeline) { m_Pipeline = pipeline; }

        /** 设置输出目录 */
        void SetOutputDir(const std::string& dir) { m_OutputDir = dir; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用 */
        void OnImGui();

    private:
        // ── 内部绘制函数 ──
        void DrawAssetSelector();       // 资产选择器（左侧列表）
        void DrawDependencyView();      // 依赖详情（右侧主区域）
        void DrawDependencyChain();     // 上下游依赖链展示
        void DrawCycleWarnings();       // 循环依赖警告
        void DrawPipelineControls();    // 管道运行控制按钮
        void DrawBuildReport();         // 构建报告展示

        // ── 数据 ──
        bool m_Visible = false;
        AssetDatabase* m_Database = nullptr;
        AssetPipeline* m_Pipeline = nullptr;
        std::string m_OutputDir = "assets/build/";

        // 选择状态
        std::string m_SelectedGuid;
        std::string m_FilterText;

        // 缓存
        std::vector<CycleInfo> m_CachedCycles;
        bool m_CyclesChecked = false;

        // 构建报告
        BuildReport m_LastReport;
        bool m_HasReport = false;

        // 资产列表滚动
        int m_AssetListScrollTo = -1;
    };

} // namespace Engine
