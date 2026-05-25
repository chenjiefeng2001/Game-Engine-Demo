#pragma once

/**
 * @file InspectorPanel.h
 * @brief 属性检视器面板 — 显示和编辑选中实体的组件属性
 *
 * 内置组件检视：
 *   - Transform（位置/旋转/缩放拖拽编辑）
 *   - Sprite（纹理/颜色/UV/排序）
 *   - Physics（刚体类型/形状/参数）
 *
 * 扩展方式（用于未来新增组件）：
 *   1. 通过 RegisterComponentDrawer() 注册自定义绘制函数
 *   2. 在 OnImGui() 中自动调用所有注册的绘制器
 *
 * 使用示例：
 * @code
 *   InspectorPanel inspector;
 *   inspector.SetTarget(selectedObject);
 *   inspector.OnImGui();
 *
 *   // 注册自定义组件绘制器
 *   inspector.RegisterComponentDrawer("AudioSource", [](GameObject* obj) {
 *       ImGui::Text("Audio Source Component");
 *   });
 * @endcode
 */

#include "Engine/Types.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace Engine {

    class GameObject;

    class InspectorPanel {
    public:
        InspectorPanel();
        ~InspectorPanel() = default;

        // 禁止拷贝
        InspectorPanel(const InspectorPanel&) = delete;
        InspectorPanel& operator=(const InspectorPanel&) = delete;

        // ── 目标管理 ──
        void SetTarget(GameObject* target) { m_Target = target; }
        GameObject* GetTarget() const { return m_Target; }

        // ── 渲染 ──
        /** 在 OnImGui() 中调用，绘制检视器面板 */
        void OnImGui();

        // ── 扩展接口 ──
        /**
         * @brief 组件绘制函数类型
         * @param obj 当前检视的目标对象
         */
        using ComponentDrawFn = std::function<void(GameObject* obj)>;

        /**
         * @brief 注册自定义组件绘制器
         * @param name    组件显示名称（将作为 ImGui 折叠标题）
         * @param drawFn  绘制回调
         */
        void RegisterComponentDrawer(const std::string& name, ComponentDrawFn drawFn);

        /**
         * @brief 移除自定义组件绘制器
         */
        void UnregisterComponentDrawer(const std::string& name);

        // ── 显示控制 ──
        void SetVisible(bool visible) { m_Visible = visible; }
        bool IsVisible() const { return m_Visible; }
        void ToggleVisibility() { m_Visible = !m_Visible; }

    private:
        // ── 内置组件绘制器 ──
        void DrawHeader(GameObject* obj);          
        void DrawTransform(GameObject* obj);        
        void DrawSprite(GameObject* obj);           
        void DrawPhysics(GameObject* obj);         

        GameObject* m_Target = nullptr;
        bool m_Visible = true;

        // 自定义组件绘制器注册表（name → drawFn）
        std::unordered_map<std::string, ComponentDrawFn> m_CustomDrawers;
    };

} // namespace Engine
